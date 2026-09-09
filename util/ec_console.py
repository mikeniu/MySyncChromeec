#!/usr/bin/env python3
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Host console utility for HDLC multiplexed EC UART.

Multiplexes interactive shell and logging over HDLC framing:
  - HDLC_ADDR_LOG (1): Dedicated EC log channel
  - HDLC_ADDR_SHELL (2): Interactive Zephyr shell channel
  - HDLC_ADDR_RPC (0x52): Reserved Pigweed RPC channel

Provides a PTY interface for terminal interaction and a single-command
execution mode (-c / --command). Automatically detects and falls back to
raw UART mode when communicating with non-HDLC DUTs or RO firmware.
"""

import argparse
import enum
import fcntl
import os
import pty
import re
import select
import subprocess
import sys
import termios
import threading
import time
import tty
from typing import Callable, Optional
import zlib


HDLC_FLAG = 0x7E
HDLC_ESCAPE = 0x7D
HDLC_ESCAPE_MASK = 0x20

HDLC_ADDR_LOG = 1
HDLC_ADDR_SHELL = 2
HDLC_ADDR_RPC = 0x52

_ANSI_RE = re.compile(r"\x1b\[[0-9;]*[a-zA-Z]")
_RESET_INDICATORS = (
    "Booting Zephyr",
    "*** Booting",
    "Image: RO",
    "Jumping to image RO",
    "Rebooting",
    "Reset cause:",
    "ec:~>",
    "ec:~$",
    "ec:~#",
)


def strip_ansi(text: str) -> str:
    """Strips ANSI escape codes from string."""
    return _ANSI_RE.sub("", text)


def encode_address(address: int) -> bytes:
    """Encodes an HDLC address as a one-terminated LSB varint."""
    result = bytearray()
    while True:
        result.append((address & 0x7F) << 1)
        address >>= 7
        if address == 0:
            break
    result[-1] |= 0x1
    return bytes(result)


def decode_address(data: bytes) -> tuple[int, int]:
    """Decodes an HDLC address from data, returning (address, length)."""
    result = 0
    length = 0
    while length < len(data):
        byte = data[length]
        result |= (byte >> 1) << (length * 7)
        length += 1
        if byte & 0x1 == 0x1:
            return result, length
    return -1, 0


_UI_CONTROL_BYTE = 0x03
_KNOWN_FIRST_ADDR_BYTES = frozenset(
    encode_address(addr)[0]
    for addr in (HDLC_ADDR_LOG, HDLC_ADDR_SHELL, HDLC_ADDR_RPC)
)


def encode_ui_frame(address: int, payload: bytes) -> bytes:
    """Encodes an Unnumbered Information (UI) HDLC frame."""
    frame = encode_address(address) + bytes([_UI_CONTROL_BYTE]) + payload
    fcs = zlib.crc32(frame).to_bytes(4, "little")
    raw = frame + fcs

    escaped = bytearray()
    for b in raw:
        if b == HDLC_ESCAPE:
            escaped.extend(b"\x7d\x5d")
        elif b == HDLC_FLAG:
            escaped.extend(b"\x7d\x5e")
        else:
            escaped.append(b)

    return bytes([HDLC_FLAG]) + bytes(escaped) + bytes([HDLC_FLAG])


class DecoderState(enum.Enum):
    """Internal state machine states for decoding HDLC frames."""

    INTERFRAME = 0
    FRAME = 1
    FRAME_ESCAPE = 2


class HdlcDecoder:
    """Decodes HDLC frames from a stream of bytes with raw byte passthrough."""

    def __init__(
        self,
        callback: Callable[[int, bytes], None],
        raw_callback: Optional[Callable[[bytes], None]] = None,
        max_frame_size: int = 2048,
    ):
        self._callback = callback
        self._raw_callback = raw_callback
        self._max_frame_size = max_frame_size
        self._state = DecoderState.INTERFRAME
        self._decoded_data = bytearray()
        self._raw_buf = bytearray()
        self.hdlc_detected = False

    def _flush_raw_buf(self):
        """Flushes buffered interframe raw bytes to the raw callback."""
        if self._raw_buf and self._raw_callback:
            data = bytes(self._raw_buf)
            self._raw_buf.clear()
            self._raw_callback(data)

    def _is_hdlc_header(self) -> bool:
        """Returns True if buffered bytes match known HDLC channel headers."""
        if not self._decoded_data:
            return False
        if self._decoded_data[0] not in _KNOWN_FIRST_ADDR_BYTES:
            return False
        if (
            len(self._decoded_data) >= 2
            and self._decoded_data[1] != _UI_CONTROL_BYTE
        ):
            return False
        return True

    def process_byte(self, byte: int):
        """Feeds a single byte into the HDLC frame decoder state machine."""
        if self._state == DecoderState.INTERFRAME:
            if byte == HDLC_FLAG:
                self._flush_raw_buf()
                self._state = DecoderState.FRAME
                self._decoded_data.clear()
            else:
                # Raw byte received outside HDLC frame (e.g. from RO firmware)
                if self._raw_callback:
                    self._raw_buf.append(byte)
        elif self._state == DecoderState.FRAME:
            if byte == HDLC_FLAG:
                if len(self._decoded_data) >= 6:  # 1B addr + 1B ctrl + 4B CRC
                    if self._validate_and_dispatch():
                        self.hdlc_detected = True
                        self._state = DecoderState.INTERFRAME
                    elif self._raw_callback:
                        if not self._is_hdlc_header():
                            self._raw_callback(
                                bytes([HDLC_FLAG])
                                + bytes(self._decoded_data)
                                + bytes([HDLC_FLAG])
                            )
                        self._state = DecoderState.INTERFRAME
                elif self._raw_callback and self._decoded_data:
                    if not self._is_hdlc_header():
                        self._raw_callback(
                            bytes([HDLC_FLAG])
                            + bytes(self._decoded_data)
                            + bytes([HDLC_FLAG])
                        )
                    self._state = DecoderState.INTERFRAME
                self._decoded_data.clear()
            elif byte == HDLC_ESCAPE:
                self._state = DecoderState.FRAME_ESCAPE
            else:
                self._decoded_data.append(byte)
                if len(self._decoded_data) > self._max_frame_size:
                    self._decoded_data.clear()
                    self._state = DecoderState.INTERFRAME
                    return

                # In raw serial, prompt or text may contain "~" (0x7E).
                # Check if header matches known HDLC channels. If not, abort raw.
                if not self._is_hdlc_header():
                    self._state = DecoderState.INTERFRAME
                    if self._raw_callback:
                        self._raw_callback(
                            bytes([HDLC_FLAG]) + bytes(self._decoded_data)
                        )
                    self._decoded_data.clear()
        elif self._state == DecoderState.FRAME_ESCAPE:
            if byte in (0x5D, 0x5E):
                self._state = DecoderState.FRAME
                self._decoded_data.append(byte ^ HDLC_ESCAPE_MASK)
            elif byte == HDLC_FLAG:
                # HDLC flag abort
                self._decoded_data.clear()
                self._state = DecoderState.FRAME
            else:
                # Invalid escape sequence
                self._state = DecoderState.INTERFRAME
                if self._raw_callback:
                    self._raw_callback(
                        bytes([HDLC_FLAG])
                        + bytes(self._decoded_data)
                        + bytes([byte])
                    )
                self._decoded_data.clear()

    def flush_pending_raw(self):
        """Flushes any buffered bytes waiting for frame completion in raw mode."""
        self._flush_raw_buf()
        if self._state == DecoderState.FRAME:
            if self._decoded_data and self._is_hdlc_header():
                return
            if self.hdlc_detected and not self._decoded_data:
                self._state = DecoderState.INTERFRAME
                return
            if self._raw_callback and self._decoded_data:
                self._raw_callback(
                    bytes([HDLC_FLAG]) + bytes(self._decoded_data)
                )
            elif self._raw_callback and not self.hdlc_detected:
                self._raw_callback(bytes([HDLC_FLAG]))
            self._decoded_data.clear()
            self._state = DecoderState.INTERFRAME

    def _validate_and_dispatch(self) -> bool:
        frame_bytes = bytes(self._decoded_data)
        payload_with_header = frame_bytes[:-4]
        expected_crc = int.from_bytes(frame_bytes[-4:], "little")

        if zlib.crc32(payload_with_header) != expected_crc:
            return False

        address, addr_len = decode_address(payload_with_header)
        if addr_len <= 0 or addr_len >= len(payload_with_header):
            return False

        # Skip control byte (1 byte)
        payload = payload_with_header[addr_len + 1 :]
        self._callback(address, payload)
        return True

    def process_bytes(self, data: bytes):
        """Feeds a byte array into the HDLC frame decoder state machine."""
        for b in data:
            self.process_byte(b)
        self._flush_raw_buf()


class EcConsole:
    """Main EC console session manager."""

    def __init__(
        self,
        device_path: str,
        baudrate: int = 115200,
        pty_symlink: Optional[str] = None,
        mode: str = "auto",
    ):
        self.device_path = device_path
        self.baudrate = baudrate
        self.pty_symlink = pty_symlink
        self.mode = mode

        self.uart_fd = -1
        self.pty_master_fd = -1
        self.pty_slave_fd = -1
        self.running = False

        self.decoder = HdlcDecoder(
            callback=self._on_frame_received,
            raw_callback=self._on_raw_data_received,
        )
        self._raw_str_buf = ""

    def _open_uart(self):
        self.uart_fd = os.open(
            self.device_path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK
        )
        attrs = termios.tcgetattr(self.uart_fd)
        attrs[0] = 0
        attrs[1] = 0
        attrs[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
        attrs[3] = 0
        attrs[4] = termios.B115200
        attrs[5] = termios.B115200
        termios.tcsetattr(self.uart_fd, termios.TCSANOW, attrs)

    def _setup_pty(self):
        if not self.pty_symlink:
            return

        self.pty_master_fd, self.pty_slave_fd = pty.openpty()
        flags = fcntl.fcntl(self.pty_master_fd, fcntl.F_GETFL)
        fcntl.fcntl(self.pty_master_fd, fcntl.F_SETFL, flags | os.O_NONBLOCK)

        pts_name = os.ttyname(self.pty_slave_fd)
        if os.path.islink(self.pty_symlink) or os.path.exists(self.pty_symlink):
            os.unlink(self.pty_symlink)
        os.symlink(pts_name, self.pty_symlink)
        print(
            f"[HDLC] PTY forwarder created: {self.pty_symlink} -> {pts_name}",
            file=sys.stderr,
        )

    def _write_shell_output(self, payload: bytes):
        """Routes shell output to PTY master or stdout."""
        if self.pty_master_fd >= 0:
            os.write(self.pty_master_fd, payload)
        else:
            sys.stdout.buffer.write(payload)
            sys.stdout.buffer.flush()

    def _check_raw_mode_indicators(self, text: str):
        """Checks if received text indicates raw UART mode or reset."""
        if self.mode == "auto" and self.decoder.hdlc_detected:
            if any(ind in text for ind in _RESET_INDICATORS):
                self.decoder.hdlc_detected = False

    def _on_raw_data_received(self, data: bytes):
        """Handles raw bytes received outside HDLC frames."""
        self._raw_str_buf += data.decode(errors="replace")
        self._check_raw_mode_indicators(self._raw_str_buf)

        if "\n" in self._raw_str_buf:
            lines = self._raw_str_buf.split("\n")
            for line in lines[:-1]:
                line = line.rstrip("\r")
                self._check_raw_mode_indicators(line)
                if not self.decoder.hdlc_detected:
                    self._write_shell_output((line + "\r\n").encode())
            self._raw_str_buf = lines[-1]

        stripped = self._raw_str_buf.lstrip("\r\n")
        if stripped and not stripped.startswith("["):
            if self.mode == "auto" and self.decoder.hdlc_detected:
                if (
                    any(ind in stripped for ind in _RESET_INDICATORS)
                    or stripped.endswith("> ")
                    or stripped.endswith(">")
                ):
                    self.decoder.hdlc_detected = False
            if not self.decoder.hdlc_detected:
                self._write_shell_output(self._raw_str_buf.encode())
                self._raw_str_buf = ""

    def _flush_raw_buffer_idle(self):
        """Flushes buffered raw shell text when the UART stream goes idle."""
        stripped = self._raw_str_buf.lstrip("\r\n")
        if stripped:
            if self.mode == "auto" and self.decoder.hdlc_detected:
                if (
                    any(ind in stripped for ind in _RESET_INDICATORS)
                    or stripped.endswith("> ")
                    or stripped.endswith(">")
                ):
                    self.decoder.hdlc_detected = False
            if not self.decoder.hdlc_detected and not stripped.startswith("["):
                self._write_shell_output(self._raw_str_buf.encode())
                self._raw_str_buf = ""

    def _on_frame_received(self, address: int, payload: bytes):
        if self.mode != "raw":
            self.decoder.hdlc_detected = True
        if self._raw_str_buf:
            self._raw_str_buf = ""
        if address == HDLC_ADDR_SHELL:
            self._write_shell_output(payload)
        elif address == HDLC_ADDR_LOG:
            msg = payload.decode(errors="replace")
            sys.stderr.write(f"[LOG] {msg}")
            sys.stderr.flush()
        else:
            sys.stderr.write(
                f"[HDLC] Unhandled frame on address {hex(address)}: "
                f"{payload!r}\n"
            )
            sys.stderr.flush()

    def send_shell_data(self, data: bytes):
        """Transmits shell input data over HDLC or raw UART."""
        if self.mode == "raw" or (
            self.mode == "auto" and not self.decoder.hdlc_detected
        ):
            os.write(self.uart_fd, data)
        else:
            frame = encode_ui_frame(HDLC_ADDR_SHELL, data)
            os.write(self.uart_fd, frame)

    def _probe_transport(self):
        """Probes the UART to determine if HDLC or raw serial is active."""
        if self.mode == "raw":
            self.decoder.hdlc_detected = False
            return

        if self.mode == "hdlc":
            self.decoder.hdlc_detected = True
            os.write(self.uart_fd, encode_ui_frame(HDLC_ADDR_SHELL, b"\r"))
            return

        # Auto mode: safe probe sequence
        os.write(self.uart_fd, b"\r")
        rlist, _, _ = select.select([self.uart_fd], [], [], 0.25)
        if rlist:
            data = os.read(self.uart_fd, 512)
            if data:
                self.decoder.process_bytes(data)
                while True:
                    drain, _, _ = select.select([self.uart_fd], [], [], 0.05)
                    if not drain:
                        break
                    more = os.read(self.uart_fd, 512)
                    if not more:
                        break
                    self.decoder.process_bytes(more)

                if not self.decoder.hdlc_detected:
                    return

        if not self.decoder.hdlc_detected:
            os.write(self.uart_fd, encode_ui_frame(HDLC_ADDR_SHELL, b"\r"))
            rlist, _, _ = select.select([self.uart_fd], [], [], 0.25)
            if rlist:
                data = os.read(self.uart_fd, 512)
                if data:
                    self.decoder.process_bytes(data)
                while True:
                    drain, _, _ = select.select([self.uart_fd], [], [], 0.05)
                    if not drain:
                        break
                    more = os.read(self.uart_fd, 512)
                    if not more:
                        break
                    self.decoder.process_bytes(more)

    def run_interactive(self):
        """Runs the interactive console session in terminal mode."""
        self._open_uart()
        self._setup_pty()

        self.running = True
        orig_stdin_attr = None
        use_stdin = self.pty_master_fd < 0 and sys.stdin.isatty()
        is_pty_mode = self.pty_master_fd >= 0

        print(
            "[HDLC] Console started. Press Ctrl+] to exit.",
            file=sys.stderr,
        )

        self._probe_transport()

        if use_stdin:
            orig_stdin_attr = termios.tcgetattr(sys.stdin.fileno())
            tty.setraw(sys.stdin.fileno())

        try:
            while self.running:
                rlist = [self.uart_fd]
                if self.pty_master_fd >= 0:
                    rlist.append(self.pty_master_fd)
                if sys.stdin.isatty():
                    rlist.append(sys.stdin.fileno())

                readable, _, _ = select.select(rlist, [], [], 0.05)

                if self.uart_fd in readable:
                    try:
                        data = os.read(self.uart_fd, 512)
                    except OSError:
                        time.sleep(0.05)
                        continue
                    if not data:
                        break
                    self.decoder.process_bytes(data)
                elif self.decoder:
                    self.decoder.flush_pending_raw()
                    if self._raw_str_buf:
                        self._flush_raw_buffer_idle()

                if self.pty_master_fd >= 0 and self.pty_master_fd in readable:
                    data = os.read(self.pty_master_fd, 256)
                    if data:
                        self.send_shell_data(data)

                if sys.stdin.isatty() and sys.stdin.fileno() in readable:
                    data = os.read(sys.stdin.fileno(), 256)
                    if not data:
                        break
                    if b"\x1d" in data or b"\x1c" in data:  # Ctrl+] to exit
                        break
                    if is_pty_mode and (b"\x03" in data or data == b"q"):
                        break
                    if use_stdin:
                        self.send_shell_data(data)
        finally:
            if orig_stdin_attr is not None:
                termios.tcsetattr(
                    sys.stdin.fileno(), termios.TCSADRAIN, orig_stdin_attr
                )
            if self.uart_fd >= 0:
                os.close(self.uart_fd)
            if self.pty_master_fd >= 0:
                os.close(self.pty_master_fd)
            if self.pty_slave_fd >= 0:
                os.close(self.pty_slave_fd)
            if self.pty_symlink and os.path.islink(self.pty_symlink):
                os.unlink(self.pty_symlink)

    def _execute_command_impl(self, command: str, timeout: float = 3.0) -> str:
        self._open_uart()
        collected = bytearray()
        done = threading.Event()

        is_reboot_cmd = command.strip().startswith("reboot")
        seen_reboot_banner = False

        def on_cmd_frame(address: int, payload: bytes):
            nonlocal seen_reboot_banner
            if self.mode != "raw":
                self.decoder.hdlc_detected = True
            if self._raw_str_buf:
                self._raw_str_buf = ""
            if address == HDLC_ADDR_SHELL:
                collected.extend(payload)
                if not is_reboot_cmd:
                    if b":~$" in collected or b">" in collected:
                        done.set()
                elif seen_reboot_banner and (
                    b":~$" in payload or b">" in payload
                ):
                    done.set()
            elif address == HDLC_ADDR_LOG:
                log_text = payload.decode(errors="replace")
                if is_reboot_cmd and any(
                    ind in log_text
                    for ind in ("Booting", "Image:", "Jumping", "Reset cause:")
                ):
                    seen_reboot_banner = True
                collected.extend(log_text.encode())

        def on_cmd_raw(data: bytes):
            nonlocal seen_reboot_banner
            self._raw_str_buf += data.decode(errors="replace")
            self._check_raw_mode_indicators(self._raw_str_buf)

            if "\n" in self._raw_str_buf:
                lines = self._raw_str_buf.split("\n")
                for line in lines[:-1]:
                    line = line.rstrip("\r")
                    self._check_raw_mode_indicators(line)
                    if is_reboot_cmd and any(
                        ind in line
                        for ind in (
                            "Booting",
                            "Image:",
                            "Jumping",
                            "Reset cause:",
                        )
                    ):
                        seen_reboot_banner = True
                    collected.extend((line + "\n").encode())
                    if not is_reboot_cmd:
                        if b":~$" in collected or b">" in collected:
                            done.set()
                    elif seen_reboot_banner:
                        if b":~$" in collected or b">" in collected:
                            done.set()
                self._raw_str_buf = lines[-1]

            stripped = self._raw_str_buf.lstrip("\r\n")
            if stripped and not stripped.startswith("["):
                self._check_raw_mode_indicators(stripped)
                if not self.decoder.hdlc_detected:
                    collected.extend(self._raw_str_buf.encode())
                    self._raw_str_buf = ""
                    if not is_reboot_cmd:
                        if b":~$" in collected or b">" in collected:
                            done.set()
                    elif seen_reboot_banner:
                        if b":~$" in collected or b">" in collected:
                            done.set()

        self.decoder = HdlcDecoder(on_cmd_frame, raw_callback=on_cmd_raw)
        self._probe_transport()

        collected.clear()
        done.clear()
        self._raw_str_buf = ""

        cmd_bytes = command.encode() + b"\r"
        self.send_shell_data(cmd_bytes)

        start_time = time.time()
        while time.time() - start_time < timeout and not done.is_set():
            rlist, _, _ = select.select([self.uart_fd], [], [], 0.1)
            if rlist:
                try:
                    data = os.read(self.uart_fd, 512)
                except OSError:
                    time.sleep(0.05)
                    continue
                if data:
                    self.decoder.process_bytes(data)
            else:
                self.decoder.flush_pending_raw()

        if self._raw_str_buf:
            for line in self._raw_str_buf.splitlines():
                line = line.strip()
                if line:
                    collected.extend((line + "\n").encode())
            self._raw_str_buf = ""

        os.close(self.uart_fd)
        self.uart_fd = -1
        return collected.decode(errors="replace")

    def execute_command(self, command: str, timeout: float = 3.0) -> str:
        """Executes a single shell command over HDLC or raw UART."""
        return self._execute_command_impl(command, timeout)


# Backward compatibility alias
EcConsoleInteractive = EcConsole


def query_default_device() -> str:
    """Queries servod via dut-control for active EC UART pty."""
    for ctrl in ("raw_ec_uart_pty", "ec_uart_pty"):
        try:
            res = subprocess.run(
                ["dut-control", "--", ctrl],
                capture_output=True,
                text=True,
                check=True,
                timeout=10,
            )
            pty_path = res.stdout.strip()
            if pty_path:
                return pty_path
        except FileNotFoundError as e:
            raise RuntimeError(
                "dut-control not found. Ensure dut-control is in PATH, "
                "or specify --device / -d explicitly."
            ) from e
        except (subprocess.CalledProcessError, subprocess.TimeoutExpired):
            continue

    raise RuntimeError(
        "Could not determine EC UART pty from servod. Ensure servod is "
        "running, or specify --device / -d explicitly."
    )


class ServoInterpManager:
    """Manages servod ec3po interpreter state during ec_console runs."""

    def __init__(self, device: Optional[str] = None, enabled: bool = True):
        self.device = device
        self.enabled = enabled
        self.needs_restore = False

    def disconnect(self):
        """Disconnects ec3po interpreter if needed."""
        if not self.enabled:
            return

        try:
            res = subprocess.run(
                ["dut-control", "--", "ec_uart_pty", "ec_ec3po_interp_connect"],
                capture_output=True,
                text=True,
                check=False,
                timeout=10,
            )
            if res.returncode != 0:
                return

            controls = dict(
                line.split(":", 1)
                for line in res.stdout.splitlines()
                if ":" in line
            )
            ec_pty = controls.get("ec_uart_pty", "").strip()
            status = controls.get("ec_ec3po_interp_connect", "").strip().lower()

            if self.device and self.device == ec_pty:
                if status == "off":
                    subprocess.run(
                        ["dut-control", "--", "ec_ec3po_interp_connect:on"],
                        capture_output=True,
                        text=True,
                        check=False,
                        timeout=10,
                    )
                return

            if status == "on":
                set_res = subprocess.run(
                    ["dut-control", "--", "ec_ec3po_interp_connect:off"],
                    capture_output=True,
                    text=True,
                    check=False,
                    timeout=10,
                )
                if set_res.returncode == 0:
                    self.needs_restore = True
                    sys.stderr.write(
                        "[HDLC] Disconnected ec3po interpreter "
                        "(ec_ec3po_interp_connect:off)\n"
                    )
                    sys.stderr.flush()
        except (subprocess.SubprocessError, FileNotFoundError, OSError):
            pass

    def restore(self):
        """Restores ec3po interpreter connection if previously disconnected."""
        if not self.enabled or not self.needs_restore:
            return

        try:
            res = subprocess.run(
                ["dut-control", "--", "ec_ec3po_interp_connect:on"],
                capture_output=True,
                text=True,
                check=False,
                timeout=10,
            )
            if res.returncode == 0:
                sys.stderr.write(
                    "[HDLC] Restored ec3po interpreter "
                    "(ec_ec3po_interp_connect:on)\n"
                )
                sys.stderr.flush()
                self.needs_restore = False
        except (subprocess.SubprocessError, FileNotFoundError, OSError):
            pass


def main():
    """Main entry point for ec_console."""
    parser = argparse.ArgumentParser(
        description="Host console utility for HDLC multiplexed EC UART."
    )
    parser.add_argument(
        "-d",
        "--device",
        help="Serial device path (default: queried via dut-control)",
    )
    parser.add_argument(
        "-b",
        "--baudrate",
        type=int,
        default=115200,
        help="UART baudrate (default: 115200)",
    )
    parser.add_argument(
        "-m",
        "--mode",
        choices=["auto", "hdlc", "raw"],
        default="auto",
        help="Transport mode (default: auto)",
    )
    parser.add_argument(
        "-p",
        "--pty",
        help="Symlink path for PTY forwarder (e.g. /tmp/ec_uart_pty)",
    )
    parser.add_argument(
        "-c",
        "--command",
        help="Execute a single command on EC shell and print output, then exit",
    )
    parser.add_argument(
        "--no-interp-control",
        action="store_true",
        help="Do not manage ec_ec3po_interp_connect via dut-control",
    )

    args = parser.parse_args()

    device = args.device
    if not device:
        device = query_default_device()

    interp_mgr = ServoInterpManager(
        device=device, enabled=not args.no_interp_control
    )
    interp_mgr.disconnect()

    try:
        console = EcConsole(
            device_path=device,
            baudrate=args.baudrate,
            pty_symlink=args.pty,
            mode=args.mode,
        )

        if args.command is not None:
            output = console.execute_command(args.command)
            sys.stdout.write(output)
            sys.stdout.flush()
        else:
            console.run_interactive()
    finally:
        interp_mgr.restore()


if __name__ == "__main__":
    main()
