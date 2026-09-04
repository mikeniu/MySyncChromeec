#!/usr/bin/env python3
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""HDLC Console Utility for ChromiumOS EC.

Multiplexes and demultiplexes HDLC-encapsulated UART channels between the host
and the Zephyr EC.

Channels:
  - HDLC_ADDR_LOG (1): Dedicated EC log channel (routed to stderr or log file).
  - HDLC_ADDR_SHELL (2): Interactive Zephyr shell channel (routed to terminal or
    virtual PTY).
  - HDLC_ADDR_RPC (0x52): Reserved Pigweed RPC channel.
"""

import argparse
import enum
import os
from pathlib import Path
import pty
import select
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

_ESCAPE_BYTE = bytes([HDLC_ESCAPE])
_FLAG_BYTE = bytes([HDLC_FLAG])


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


def encode_ui_frame(address: int, payload: bytes) -> bytes:
    """Encodes an HDLC unnumbered information (UI) frame."""
    frame = encode_address(address) + b"\x03" + payload
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
    """Decodes HDLC frames from a stream of bytes."""

    def __init__(self, callback: Callable[[int, bytes], None]):
        self._callback = callback
        self._state = DecoderState.INTERFRAME
        self._decoded_data = bytearray()

    def process_byte(self, byte: int):
        """Feeds a single byte into the HDLC frame decoder state machine."""
        if self._state == DecoderState.INTERFRAME:
            if byte == HDLC_FLAG:
                self._state = DecoderState.FRAME
                self._decoded_data.clear()
        elif self._state == DecoderState.FRAME:
            if byte == HDLC_FLAG:
                if len(self._decoded_data) >= 6:  # 1B addr + 1B ctrl + 4B CRC
                    self._validate_and_dispatch()
                self._decoded_data.clear()
            elif byte == HDLC_ESCAPE:
                self._state = DecoderState.FRAME_ESCAPE
            else:
                self._decoded_data.append(byte)
        elif self._state == DecoderState.FRAME_ESCAPE:
            if byte in (0x5D, 0x5E):
                self._state = DecoderState.FRAME
                self._decoded_data.append(byte ^ HDLC_ESCAPE_MASK)
            else:
                self._state = DecoderState.INTERFRAME
                self._decoded_data.clear()

    def _validate_and_dispatch(self):
        frame_bytes = bytes(self._decoded_data)
        payload_with_header = frame_bytes[:-4]
        expected_crc = int.from_bytes(frame_bytes[-4:], "little")

        if zlib.crc32(payload_with_header) != expected_crc:
            return

        address, addr_len = decode_address(payload_with_header)
        if addr_len <= 0 or addr_len >= len(payload_with_header):
            return

        # Skip control byte (1 byte)
        payload = payload_with_header[addr_len + 1 :]
        self._callback(address, payload)

    def process_bytes(self, data: bytes):
        """Feeds a byte array into the HDLC frame decoder state machine."""
        for b in data:
            self.process_byte(b)


class HdlcConsole:
    """Manages HDLC transport over UART, demuxing logs and shell."""

    def __init__(
        self,
        device_path: str,
        baudrate: int = 115200,
        log_file: Optional[str] = None,
        pty_symlink: Optional[str] = None,
        token_db: Optional[str] = None,
    ):
        self.device_path = device_path
        self.baudrate = baudrate
        self.log_file_path = log_file
        self.pty_symlink = pty_symlink
        self.token_db = token_db
        self.detokenizer = None
        if token_db:
            self._init_detokenizer(token_db)

        self.uart_fd = -1
        self.pty_master_fd = -1
        self.pty_slave_fd = -1
        self.log_fp = None
        self.running = False

        self.decoder = HdlcDecoder(self._on_frame_received)

    def _init_detokenizer(self, token_db: str):
        """Initializes the Pigweed detokenizer with the specified database."""
        # pylint: disable=import-outside-toplevel,import-error
        try:
            from pw_tokenizer import detokenize

            self.detokenizer = detokenize.AutoUpdatingDetokenizer(token_db)
        except ImportError:
            try:
                cros_checkout = Path(__file__).resolve().parents[4]
                pw_path = (
                    cros_checkout
                    / "src"
                    / "third_party"
                    / "pigweed"
                    / "pw_tokenizer"
                    / "py"
                )
                if pw_path.exists():
                    sys.path.append(str(pw_path))
                    from pw_tokenizer import detokenize

                    self.detokenizer = detokenize.AutoUpdatingDetokenizer(
                        token_db
                    )
            except (ImportError, OSError, ValueError) as e:
                sys.stderr.write(
                    f"[HDLC] Warning: Failed to load detokenizer: {e}\n"
                )

    def _format_log(self, payload: bytes) -> str:
        text = payload.decode(errors="replace")
        if self.detokenizer:
            text = str(self.detokenizer.detokenize_text(text, prefix="`"))
            text = text.replace("~", "")
        return text

    def _open_uart(self):
        # Open the UART device or PTY
        self.uart_fd = os.open(self.device_path, os.O_RDWR | os.O_NOCTTY)
        if os.isatty(self.uart_fd):
            attr = termios.tcgetattr(self.uart_fd)
            tty.setraw(self.uart_fd)
            baud = getattr(termios, f"B{self.baudrate}", termios.B115200)
            attr[4] = baud
            attr[5] = baud
            termios.tcsetattr(self.uart_fd, termios.TCSANOW, attr)

    def _setup_pty(self):
        if self.pty_symlink:
            self.pty_master_fd, self.pty_slave_fd = pty.openpty()
            slave_name = os.ttyname(self.pty_slave_fd)
            tty.setraw(self.pty_master_fd)

            if os.path.exists(self.pty_symlink) or os.path.islink(
                self.pty_symlink
            ):
                os.unlink(self.pty_symlink)
            os.symlink(slave_name, self.pty_symlink)

            print(
                f"[HDLC] Virtual shell PTY created at: {slave_name}",
                file=sys.stderr,
            )
            print(f"[HDLC] Symlinked to: {self.pty_symlink}", file=sys.stderr)

    def _on_frame_received(self, address: int, payload: bytes):
        if address == HDLC_ADDR_LOG:
            log_text = self._format_log(payload)
            if self.log_fp:
                self.log_fp.write(log_text)
                self.log_fp.flush()
            else:
                sys.stderr.write(f"\033[33m[LOG]\033[0m {log_text}")
                sys.stderr.flush()
        elif address == HDLC_ADDR_SHELL:
            if self.pty_master_fd >= 0:
                os.write(self.pty_master_fd, payload)
            else:
                sys.stdout.buffer.write(payload)
                sys.stdout.buffer.flush()
        else:
            sys.stderr.write(
                f"[HDLC] Unhandled frame on address {hex(address)}: {payload!r}\n"
            )
            sys.stderr.flush()

    def send_shell_data(self, data: bytes):
        """Encapsulates and transmits shell input data over HDLC."""
        frame = encode_ui_frame(HDLC_ADDR_SHELL, data)
        os.write(self.uart_fd, frame)

    def run_interactive(self):
        """Runs the interactive console session."""
        self._open_uart()
        self._setup_pty()

        if self.log_file_path:
            # pylint: disable=consider-using-with
            self.log_fp = open(self.log_file_path, "a", encoding="utf-8")

        self.running = True
        orig_stdin_attr = None
        use_stdin = self.pty_master_fd < 0 and sys.stdin.isatty()

        if use_stdin:
            orig_stdin_attr = termios.tcgetattr(sys.stdin.fileno())
            tty.setraw(sys.stdin.fileno())

        try:
            print(
                "[HDLC] Console started. Press Ctrl+] to exit.", file=sys.stderr
            )
            while self.running:
                rlist = [self.uart_fd]
                if self.pty_master_fd >= 0:
                    rlist.append(self.pty_master_fd)
                elif use_stdin:
                    rlist.append(sys.stdin.fileno())

                readable, _, _ = select.select(rlist, [], [])

                if self.uart_fd in readable:
                    data = os.read(self.uart_fd, 512)
                    if not data:
                        break
                    self.decoder.process_bytes(data)

                if self.pty_master_fd >= 0 and self.pty_master_fd in readable:
                    data = os.read(self.pty_master_fd, 256)
                    if data:
                        self.send_shell_data(data)

                if use_stdin and sys.stdin.fileno() in readable:
                    data = os.read(sys.stdin.fileno(), 256)
                    if b"\x1d" in data:  # Ctrl+] to exit
                        break
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
            if self.log_fp:
                self.log_fp.close()

    def execute_command(self, command: str, timeout: float = 3.0) -> str:
        """Executes a single shell command over HDLC and returns the output."""
        self._open_uart()
        collected = bytearray()
        done = threading.Event()

        def on_cmd_frame(address: int, payload: bytes):
            if address == HDLC_ADDR_SHELL:
                collected.extend(payload)
                if b"uart:~$" in collected or b">" in collected:
                    done.set()
            elif address == HDLC_ADDR_LOG:
                log_text = self._format_log(payload)
                sys.stderr.write(f"\033[33m[LOG]\033[0m {log_text}")
                sys.stderr.flush()

        self.decoder = HdlcDecoder(on_cmd_frame)

        # Send newline first to wake/clear prompt
        self.send_shell_data(b"\r")
        time.sleep(0.05)

        cmd_bytes = command.encode() + b"\r"
        self.send_shell_data(cmd_bytes)

        start_time = time.time()
        while time.time() - start_time < timeout and not done.is_set():
            rlist, _, _ = select.select([self.uart_fd], [], [], 0.1)
            if rlist:
                data = os.read(self.uart_fd, 512)
                if data:
                    self.decoder.process_bytes(data)

        os.close(self.uart_fd)
        return collected.decode(errors="replace")


def main():
    """Main entry point for HDLC console utility."""
    parser = argparse.ArgumentParser(
        description="HDLC Console Utility for ChromiumOS EC"
    )
    parser.add_argument(
        "-d",
        "--device",
        required=True,
        help="Path to UART device or PTY (e.g., /dev/pts/X or /dev/ttyUSB0)",
    )
    parser.add_argument(
        "-b",
        "--baudrate",
        type=int,
        default=115200,
        help="Baud rate (default: 115200)",
    )
    parser.add_argument(
        "-p",
        "--pty",
        help="Create a virtual unencapsulated shell PTY symlinked at this path",
    )
    parser.add_argument(
        "-l",
        "--log-file",
        help="Path to file to write received EC log lines to",
    )
    parser.add_argument(
        "-t",
        "--token-db",
        help="Path to Pigweed token database (e.g. build/tokens.bin) to detokenize logs",
    )
    parser.add_argument(
        "-c",
        "--command",
        help="Send a single shell command and exit after printing response",
    )

    args = parser.parse_args()

    console = HdlcConsole(
        device_path=args.device,
        baudrate=args.baudrate,
        log_file=args.log_file,
        pty_symlink=args.pty,
        token_db=args.token_db,
    )

    if args.command:
        output = console.execute_command(args.command)
        print(output)
    else:
        console.run_interactive()


if __name__ == "__main__":
    main()
