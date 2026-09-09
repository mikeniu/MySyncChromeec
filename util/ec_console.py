#!/usr/bin/env python3
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""EC Console Utility for ChromiumOS.

Multiplexes and demultiplexes HDLC-encapsulated UART channels between the host
and the Zephyr EC with a Pigweed Console (pw console) inspired user interface.

Channels:
  - HDLC_ADDR_LOG (1): Dedicated EC log channel (formatted in table view or
    log file).
  - HDLC_ADDR_SHELL (2): Interactive Zephyr shell channel (routed to terminal
    or virtual PTY).
  - HDLC_ADDR_RPC (0x52): Reserved Pigweed RPC channel.
"""

# pylint: disable=too-many-lines

import argparse
import atexit
from dataclasses import dataclass
import enum
import fcntl
import os
from pathlib import Path
import pty
import re
import select
import signal
import subprocess
import sys
import termios
import threading
import time
import tty
from typing import Callable, Optional, Union
import zlib


HDLC_FLAG = 0x7E
HDLC_ESCAPE = 0x7D
HDLC_ESCAPE_MASK = 0x20

HDLC_ADDR_LOG = 1
HDLC_ADDR_SHELL = 2
HDLC_ADDR_RPC = 0x52

_ESCAPE_BYTE = bytes([HDLC_ESCAPE])
_FLAG_BYTE = bytes([HDLC_FLAG])

_ANSI_RE = re.compile(r"\x1b\[[0-9;]*[a-zA-Z]")

# Terminal Key Escape Sequences for TUI Scrollback
_PAGE_UP_SEQS = (b"\x1b[5~", b"\x1b[[5~", b"\x1b[5;2~", b"\x1b[5;5~")
_PAGE_DOWN_SEQS = (b"\x1b[6~", b"\x1b[[6~", b"\x1b[6;2~", b"\x1b[6;5~")
_LINE_UP_SEQS = (b"\x1b[1;2A", b"\x1b[1;5A", b"\x1b[1;3A", b"\x1b[a")
_LINE_DOWN_SEQS = (b"\x1b[1;2B", b"\x1b[1;5B", b"\x1b[1;3B", b"\x1b[b")
_TOP_SEQS = (b"\x1b[1;2H", b"\x1b[1;5H")
_BOTTOM_SEQS = (b"\x1b[1;2F", b"\x1b[1;5F")
_HOME_SEQS = (b"\x1b[H", b"\x1b[1~", b"\x1b[7~", b"\x1bOH")
_END_SEQS = (b"\x1b[F", b"\x1b[4~", b"\x1b[8~", b"\x1bOF")
_PTY_UP_SEQS = _LINE_UP_SEQS + (b"\x1b[A", b"k")
_PTY_DOWN_SEQS = _LINE_DOWN_SEQS + (b"\x1b[B", b"j")
_PTY_TOP_SEQS = _TOP_SEQS + _HOME_SEQS + (b"g",)
_PTY_BOTTOM_SEQS = _BOTTOM_SEQS + _END_SEQS + (b"G",)
_MOUSE_UP_RE = re.compile(rb"\x1b\[<64;\d+;\d+[Mm]")
_MOUSE_DOWN_RE = re.compile(rb"\x1b\[<65;\d+;\d+[Mm]")
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
    """Encodes an HDLC unnumbered information (UI) frame."""
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
        self.hdlc_detected = False

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
                self._state = DecoderState.FRAME
                self._decoded_data.clear()
            else:
                # Raw byte received outside HDLC frame (e.g. from RO firmware)
                if self._raw_callback:
                    self._raw_callback(bytes([byte]))
        elif self._state == DecoderState.FRAME:
            if byte == HDLC_FLAG:
                if len(self._decoded_data) >= 6:  # 1B addr + 1B ctrl + 4B CRC
                    if self._validate_and_dispatch():
                        self.hdlc_detected = True
                    elif not self.hdlc_detected and self._raw_callback:
                        if not self._is_hdlc_header():
                            self._raw_callback(
                                bytes([HDLC_FLAG])
                                + bytes(self._decoded_data)
                                + bytes([HDLC_FLAG])
                            )
                elif (
                    not self.hdlc_detected
                    and self._raw_callback
                    and self._decoded_data
                ):
                    if not self._is_hdlc_header():
                        self._raw_callback(
                            bytes([HDLC_FLAG])
                            + bytes(self._decoded_data)
                            + bytes([HDLC_FLAG])
                        )
                self._decoded_data.clear()
            elif byte == HDLC_ESCAPE:
                self._state = DecoderState.FRAME_ESCAPE
            else:
                self._decoded_data.append(byte)
                if len(self._decoded_data) > self._max_frame_size:
                    self._decoded_data.clear()
                    self._state = DecoderState.INTERFRAME
                    return

                # In raw serial, prompt or text may contain '~' (0x7E).
                # Check if header matches known HDLC channels. If not, abort raw.
                if not self._is_hdlc_header():
                    if not self.hdlc_detected:
                        self._state = DecoderState.INTERFRAME
                        if self._raw_callback:
                            self._raw_callback(
                                bytes([HDLC_FLAG]) + bytes(self._decoded_data)
                            )
                        self._decoded_data.clear()
                    else:
                        self._decoded_data.clear()
                        self._state = DecoderState.INTERFRAME
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
                if not self.hdlc_detected:
                    self._state = DecoderState.INTERFRAME
                    if self._raw_callback:
                        self._raw_callback(
                            bytes([HDLC_FLAG])
                            + bytes(self._decoded_data)
                            + bytes([byte])
                        )
                    self._decoded_data.clear()
                else:
                    self._decoded_data.clear()
                    self._state = DecoderState.INTERFRAME

    def flush_pending_raw(self):
        """Flushes any buffered bytes waiting for frame completion in raw mode."""
        if self.hdlc_detected:
            return
        if self._state == DecoderState.FRAME:
            if self._decoded_data and self._is_hdlc_header():
                return
            if self._raw_callback:
                self._raw_callback(
                    bytes([HDLC_FLAG]) + bytes(self._decoded_data)
                )
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


@dataclass
class LogRecord:
    """Structured representation of an EC or Zephyr log message."""

    timestamp: str
    level: str
    module: str
    message: str
    raw: str


class LogParser:
    """Parses Zephyr and EC log strings into structured LogRecords."""

    ZEPHYR_RE = re.compile(
        r"^(?:\[(?P<ts>[0-9:.,]+)\]\s*)?"
        r"\<(?P<lvl>[a-zA-Z]+)\>\s*(?P<mod>[a-zA-Z0-9_\-]+):\s*(?P<msg>.*)$"
    )
    EC_BRACKET_RE = re.compile(
        r"^\[\s*(?P<ts>\d+\.\d+)\s+(?:(?P<lvl>[A-Z]{3,4}):\s*)?"
        r"(?P<mod>[a-zA-Z0-9_\-]+)\s+(?P<msg>.*)\]$"
    )
    EC_GENERIC_RE = re.compile(r"^\[\s*(?P<ts>\d+\.\d+)\s+(?P<msg>.*)\]$")
    SYS_BRACKET_RE = re.compile(
        r"^\[(?P<msg>(?:Image|Reset|Build|Boot|Panic|Warm|Sysjump)[^\]]*)\]$",
        re.IGNORECASE,
    )

    @classmethod
    def parse(cls, line: str, default_module: str = "sys") -> LogRecord:
        """Parses a log line into structured components."""
        raw_line = line
        line = line.strip()
        now_str = time.strftime("%H:%M:%S")

        is_isr = False
        if line.startswith("[ISR]"):
            is_isr = True
            line = line[5:].lstrip()

        # 1. Zephyr structured log: [00:00:00.123,456] <inf> usb_pd: message
        m = cls.ZEPHYR_RE.match(line)
        if m:
            ts = m.group("ts") or now_str
            if "," in ts:
                parts = ts.split(",")
                ts = f"{parts[0]}.{parts[1][:3]}"
            lvl = m.group("lvl").upper()[:3]
            mod = m.group("mod")
            msg = m.group("msg")
            if is_isr and not msg.startswith("[ISR]"):
                msg = f"[ISR] {msg}" if msg else "[ISR]"
            return LogRecord(
                timestamp=ts, level=lvl, module=mod, message=msg, raw=raw_line
            )

        # 2. EC bracket log: [0.137115 VB Main] or [0.140045 ERR: VB fail]
        m = cls.EC_BRACKET_RE.match(line)
        if m:
            ts = m.group("ts")
            lvl = (m.group("lvl") or "INF").upper()[:3]
            mod = m.group("mod")
            msg = m.group("msg")
            if lvl == "INF":
                msg_upper = msg.upper()
                if any(
                    msg_upper.startswith(p)
                    for p in ("FAIL", "ERR", "ERROR", "PANIC")
                ):
                    lvl = "ERR"
                elif any(msg_upper.startswith(p) for p in ("WARN", "WRN")):
                    lvl = "WRN"
            if is_isr and not msg.startswith("[ISR]"):
                msg = f"[ISR] {msg}" if msg else "[ISR]"
            return LogRecord(
                timestamp=ts, level=lvl, module=mod, message=msg, raw=raw_line
            )

        # 3. EC generic bracket log: [0.137115 something happened]
        m = cls.EC_GENERIC_RE.match(line)
        if m:
            ts = m.group("ts")
            msg = m.group("msg")
            parts = msg.split(None, 1)
            mod = parts[0] if len(parts) == 2 else "ec"
            msg_text = parts[1] if len(parts) == 2 else msg
            if is_isr and not msg_text.startswith("[ISR]"):
                msg_text = f"[ISR] {msg_text}" if msg_text else "[ISR]"
            return LogRecord(
                timestamp=ts,
                level="INF",
                module=mod,
                message=msg_text,
                raw=raw_line,
            )

        # 4. System bracket log: [Image: RO, ...]
        m = cls.SYS_BRACKET_RE.match(line)
        if m:
            msg = m.group("msg")
            if is_isr and not msg.startswith("[ISR]"):
                msg = f"[ISR] {msg}" if msg else "[ISR]"
            return LogRecord(
                timestamp=now_str,
                level="INF",
                module="sys",
                message=msg,
                raw=raw_line,
            )

        # 5. Fallback raw line
        msg = line
        if is_isr and not msg.startswith("[ISR]"):
            msg = f"[ISR] {msg}" if msg else "[ISR]"
        return LogRecord(
            timestamp=now_str,
            level="INF",
            module=default_module,
            message=msg,
            raw=raw_line,
        )

    @classmethod
    def is_log(cls, line: str) -> bool:
        """Returns True if the line matches any known structured log pattern."""
        s = line.strip()
        if s.startswith("[ISR]"):
            s = s[5:].lstrip()
        return bool(
            cls.ZEPHYR_RE.match(s)
            or cls.EC_BRACKET_RE.match(s)
            or cls.EC_GENERIC_RE.match(s)
            or cls.SYS_BRACKET_RE.match(s)
        )


class EcConsole:
    """Manages HDLC transport over UART, demuxing logs and shell."""

    def __init__(
        self,
        device_path: str,
        baudrate: int = 115200,
        log_file: Optional[str] = None,
        pty_symlink: Optional[str] = None,
        token_db: Optional[str] = None,
        token_prefix: str = "`",
        raw_mode: bool = False,
        mouse_mode: bool = False,
        mode: str = "auto",
        log_raw_shell: bool = True,
        suppress_shell_frame: bool = True,
    ):
        self.device_path = device_path
        self.baudrate = baudrate
        self.log_file_path = log_file
        self.pty_symlink = pty_symlink
        self.token_db = token_db
        self.token_prefix = token_prefix
        self.raw_mode = raw_mode
        self.mouse_mode = mouse_mode
        self.mode = mode
        self.log_raw_shell = log_raw_shell
        self.suppress_shell_frame = suppress_shell_frame
        self.detokenizer = None
        self.token_info = "None"
        if token_db:
            self._init_detokenizer(token_db)

        self.tui = None
        self._log_buffer = ""
        self._raw_str_buf = ""

        self.uart_fd = -1
        self.pty_master_fd = -1
        self.pty_slave_fd = -1
        self.log_fp = None
        self.running = False

        self.decoder = HdlcDecoder(
            self._on_frame_received,
            raw_callback=self._on_raw_data_received,
        )

    def _init_detokenizer(self, token_db: str):
        """Initializes the Pigweed detokenizer with the specified database."""
        # pylint: disable=import-outside-toplevel,import-error
        try:
            from pw_tokenizer import detokenize

            self.detokenizer = detokenize.AutoUpdatingDetokenizer(
                token_db, prefix=self.token_prefix
            )
        except ImportError:
            try:
                candidates = [
                    Path(__file__).resolve().parents[3]
                    / "third_party"
                    / "pigweed"
                    / "pw_tokenizer"
                    / "py",
                    Path(__file__).resolve().parents[2]
                    / "third_party"
                    / "pigweed"
                    / "pw_tokenizer"
                    / "py",
                ]
                for pw_path in candidates:
                    if pw_path.exists():
                        sys.path.append(str(pw_path))
                        break
                from pw_tokenizer import detokenize

                self.detokenizer = detokenize.AutoUpdatingDetokenizer(
                    token_db, prefix=self.token_prefix
                )
            except (ImportError, OSError, ValueError) as e:
                sys.stderr.write(
                    f"[HDLC] Warning: Failed to load detokenizer: {e}\n"
                )

        if self.detokenizer:
            count = 0
            if hasattr(self.detokenizer, "database"):
                count = len(self.detokenizer.database.entries())
            if count:
                self.token_info = f"{count} tokens"
            elif os.path.exists(token_db):
                self.token_info = "Active"
            else:
                self.token_info = "None"

    def _format_log(self, payload: Union[bytes, str]) -> str:
        """Detokenizes or decodes a log payload."""
        if isinstance(payload, bytes):
            if self.detokenizer:
                try:
                    if (
                        payload
                        and payload[0] < 0x20
                        and payload[0] not in (9, 10, 13)
                    ):
                        detok = self.detokenizer.detokenize(payload)
                        if detok.ok():
                            return str(detok)
                except Exception:  # pylint: disable=broad-except
                    pass
            text = payload.decode(errors="replace")
        else:
            text = payload

        if self.detokenizer:
            prefix = re.escape(self.token_prefix)
            # Remove trailing delimiter '~' attached to base64 tokens (e.g. `base64~).
            # Avoid global .replace("~", "") which corrupts prompts like "ec:~>".
            text = re.sub(rf"({prefix}[A-Za-z0-9+/\-_]+=*)~", r"\1", text)
            text = str(self.detokenizer.detokenize_text(text))
        return text

    def _open_uart(self):
        # Open the UART device or PTY (O_NONBLOCK prevents hanging on physical serial DCD)
        self.uart_fd = os.open(
            self.device_path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK
        )
        flags = fcntl.fcntl(self.uart_fd, fcntl.F_GETFL)
        fcntl.fcntl(self.uart_fd, fcntl.F_SETFL, flags & ~os.O_NONBLOCK)
        if os.isatty(self.uart_fd):
            tty.setraw(self.uart_fd)
            attr = termios.tcgetattr(self.uart_fd)
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

            if self.raw_mode or not sys.stdout.isatty():
                print(
                    f"[HDLC] Virtual shell PTY created at: {slave_name}",
                    file=sys.stderr,
                )
                print(
                    f"[HDLC] Symlinked to: {self.pty_symlink}",
                    file=sys.stderr,
                )

    def _handle_log_line(self, line: str, default_module: str = "sys"):
        """Processes and outputs a single completed log line."""
        record = LogParser.parse(line, default_module=default_module)
        if self.log_fp:
            clean_msg = strip_ansi(record.message)
            self.log_fp.write(
                f"{record.timestamp:>14} [{record.level:3}] "
                f"{record.module:>10}: {clean_msg}\n"
            )
            self.log_fp.flush()
        if self.running:
            sys.stderr.write(
                f"{record.timestamp:>14} [{record.level:3}] "
                f"{record.module:>10}: {record.message}\n"
            )
            sys.stderr.flush()

    def _process_log_payload(self, payload: bytes):
        """Buffers incoming log chunks, only emitting lines on newline."""
        log_text = self._format_log(payload)
        self._log_buffer += log_text
        while "\n" in self._log_buffer:
            line, self._log_buffer = self._log_buffer.split("\n", 1)
            if line.endswith("\r"):
                line = line[:-1]
            line = line.strip()
            if line:
                self._handle_log_line(line)

    def _write_shell_output(self, payload: bytes):
        """Routes shell output to the PTY master, TUI shell pane, or stdout."""
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
                if self.tui:
                    self.tui.set_hdlc_active(False)

    def _process_raw_text_stream(
        self,
        data: bytes,
        on_shell_output: Callable[[str, bool], None],
    ):
        """Processes incoming raw serial text, parsing logs and routing shell text."""
        self._raw_str_buf += data.decode(errors="replace")
        self._check_raw_mode_indicators(self._raw_str_buf)

        # Check if there is an incomplete token at the end of the buffer.
        remaining = ""
        to_process = self._raw_str_buf
        if self.detokenizer:
            prefix = re.escape(self.token_prefix)
            m = re.search(rf"{prefix}[A-Za-z0-9+/\-_=]*$", self._raw_str_buf)
            if m:
                to_process = self._raw_str_buf[: m.start()]
                remaining = self._raw_str_buf[m.start() :]

        if to_process:
            formatted = self._format_log(to_process)
            self._check_raw_mode_indicators(formatted)
            if "\n" in formatted:
                lines = formatted.split("\n")
                for line in lines[:-1]:
                    line = line.rstrip("\r")
                    self._check_raw_mode_indicators(line)
                    if LogParser.is_log(line):
                        if self.mode == "auto" and self.decoder.hdlc_detected:
                            self.decoder.hdlc_detected = False
                        self._handle_log_line(line)
                    else:
                        if self.log_raw_shell and line.strip():
                            self._handle_log_line(line, default_module="shell")
                        on_shell_output(line, True)
                self._raw_str_buf = lines[-1] + remaining
            else:
                self._raw_str_buf = formatted + remaining

        # If remaining buffer doesn't look like an incomplete bracketed log line or incomplete
        # token, flush it immediately so interactive prompts aren't delayed.
        stripped = self._raw_str_buf.lstrip("\r\n")
        has_partial_token = bool(
            self.detokenizer
            and re.search(
                rf"{re.escape(self.token_prefix)}[A-Za-z0-9+/\-_=]*$",
                self._raw_str_buf,
            )
        )
        if stripped and not stripped.startswith("[") and not has_partial_token:
            if self.mode == "auto" and self.decoder.hdlc_detected:
                if (
                    any(ind in stripped for ind in _RESET_INDICATORS)
                    or stripped.endswith("> ")
                    or stripped.endswith(">")
                ):
                    self.decoder.hdlc_detected = False
            if not self.decoder.hdlc_detected:
                on_shell_output(self._raw_str_buf, False)
                self._raw_str_buf = ""

    def _on_raw_data_received(self, data: bytes):
        """Handles raw bytes received outside HDLC frames.

        Used when receiving data from RO firmware or non-HDLC DUTs.
        """

        def _emit_shell(text: str, is_line: bool):
            if not self.decoder.hdlc_detected:
                suffix = "\r\n" if is_line else ""
                self._write_shell_output((text + suffix).encode())

        self._process_raw_text_stream(data, _emit_shell)

    def _flush_raw_buffer_idle(self):
        """Flushes buffered raw shell text when the UART stream goes idle."""
        stripped = self._raw_str_buf.lstrip("\r\n")
        has_partial_token = bool(
            self.detokenizer
            and re.search(
                rf"{re.escape(self.token_prefix)}[A-Za-z0-9+/\-_=]*$",
                self._raw_str_buf,
            )
        )
        if stripped and not has_partial_token:
            if self.mode == "auto" and self.decoder.hdlc_detected:
                if (
                    any(ind in stripped for ind in _RESET_INDICATORS)
                    or stripped.endswith("> ")
                    or stripped.endswith(">")
                ):
                    self.decoder.hdlc_detected = False
                    if self.tui:
                        self.tui.set_hdlc_active(False)
            if not self.decoder.hdlc_detected and not stripped.startswith("["):
                self._write_shell_output(self._raw_str_buf.encode())
                self._raw_str_buf = ""

    def _flush_log_buffer(self):
        """Flushes any remaining partial log lines in the buffers."""
        self.decoder.flush_pending_raw()
        if self._log_buffer:
            line = self._log_buffer.rstrip("\r\n").strip()
            self._log_buffer = ""
            if line:
                self._handle_log_line(line)
        if self._raw_str_buf:
            formatted = self._format_log(self._raw_str_buf)
            self._raw_str_buf = ""
            for line in formatted.splitlines():
                line = line.strip()
                if line:
                    if LogParser.is_log(line):
                        self._handle_log_line(line)
                    else:
                        if self.log_raw_shell and line.strip():
                            self._handle_log_line(line, default_module="shell")
                        should_suppress = (
                            self.suppress_shell_frame
                            and self.log_raw_shell
                            and self.tui is not None
                            and self.pty_master_fd < 0
                            and bool(line.strip())
                        )
                        if not should_suppress:
                            self._write_shell_output((line + "\r\n").encode())

    def _on_frame_received(self, address: int, payload: bytes):
        if self.mode != "raw":
            self.decoder.hdlc_detected = True
            if self.tui and not self.tui.hdlc_active:
                self.tui.set_hdlc_active(True)
        if self._raw_str_buf:
            raw_leftover = self._raw_str_buf.strip()
            self._raw_str_buf = ""
            if raw_leftover:
                formatted = self._format_log(raw_leftover)
                self._handle_log_line(formatted)
        if address == HDLC_ADDR_LOG:
            self._process_log_payload(payload)
        elif address == HDLC_ADDR_SHELL:
            self._write_shell_output(payload)
        else:
            sys.stderr.write(
                f"[HDLC] Unhandled frame on address {hex(address)}: "
                f"{payload!r}\n"
            )
            sys.stderr.flush()

    def send_shell_data(self, data: bytes):
        """Transmits shell input data over HDLC or raw UART depending on mode."""
        if self.mode == "raw" or (
            self.mode == "auto" and not self.decoder.hdlc_detected
        ):
            os.write(self.uart_fd, data)
        else:
            frame = encode_ui_frame(HDLC_ADDR_SHELL, data)
            os.write(self.uart_fd, frame)

    def _probe_transport(self):
        """Probes the UART to determine if HDLC or raw serial is active.

        Sends a plain carriage return first to check for an active raw shell.
        If a raw response is received (or in raw mode), remains in raw mode
        without transmitting binary HDLC frames (preventing syntax errors /
        garbage on non-HDLC DUTs). If no raw response is received within
        timeout, probes with an HDLC shell frame.
        """
        if self.mode == "raw":
            self.decoder.hdlc_detected = False
            return

        if self.mode == "hdlc":
            self.decoder.hdlc_detected = True
            os.write(self.uart_fd, encode_ui_frame(HDLC_ADDR_SHELL, b"\r"))
            return

        # Auto mode: Safe probe sequence
        # 1. Send plain newline first to test for raw shell / non-HDLC DUT
        os.write(self.uart_fd, b"\r")
        rlist, _, _ = select.select([self.uart_fd], [], [], 0.25)
        if rlist:
            data = os.read(self.uart_fd, 512)
            if data:
                self.decoder.process_bytes(data)
                # Drain any immediate trailing prompt/echo bytes
                while True:
                    drain, _, _ = select.select([self.uart_fd], [], [], 0.05)
                    if not drain:
                        break
                    more = os.read(self.uart_fd, 512)
                    if not more:
                        break
                    self.decoder.process_bytes(more)

                # If raw response received without HDLC, the DUT is non-HDLC or in RO.
                # Stay in raw mode and do NOT send an HDLC probe.
                if not self.decoder.hdlc_detected:
                    return

        # 2. If no response to raw newline, the DUT may be running HDLC in RW
        # (the HDLC parser ignores non-HDLC bytes). Probe with HDLC shell frame.
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
        """Runs the interactive console session."""
        self._open_uart()
        self._setup_pty()

        if self.log_file_path:
            # pylint: disable=consider-using-with
            self.log_fp = open(self.log_file_path, "a", encoding="utf-8")

        self.running = True
        orig_stdin_attr = None
        use_stdin = self.pty_master_fd < 0 and sys.stdin.isatty()
        is_pty_mode = self.pty_master_fd >= 0
        if not self.raw_mode:
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
                    if (
                        b"\x1d" in data or b"\x1c" in data
                    ):  # Ctrl+] or Ctrl+\ to exit
                        break
                    if is_pty_mode and (
                        b"\x03" in data or data == b"q"
                    ):  # Ctrl+C or 'q' to exit in PTY mode
                        break
                    if self.tui and b"\x0c" in data:  # Ctrl+L to redraw
                        self.tui.redraw(clear=True)
                        if not is_pty_mode:
                            self.send_shell_data(b"\r")
                        continue
                    if self.tui and b"\x10" in data:  # Ctrl+P to toggle pause
                        self.tui.toggle_pause()
                        continue

                    # Handle scroll keys in TUI mode
                    if self.tui:
                        handled = False
                        if is_pty_mode:
                            if data in _PTY_UP_SEQS:
                                self.tui.scroll_up(1)
                                handled = True
                            elif data in _PTY_DOWN_SEQS:
                                self.tui.scroll_down(1)
                                handled = True
                            elif data in _PTY_TOP_SEQS:
                                self.tui.scroll_top()
                                handled = True
                            elif data in _PTY_BOTTOM_SEQS:
                                self.tui.scroll_bottom()
                                handled = True
                            elif data == b" ":
                                self.tui.scroll_down()
                                handled = True

                        if not handled:
                            if any(s in data for s in _PAGE_UP_SEQS):
                                self.tui.scroll_up()
                                handled = True
                            elif any(s in data for s in _PAGE_DOWN_SEQS):
                                self.tui.scroll_down()
                                handled = True
                            elif any(s in data for s in _LINE_UP_SEQS):
                                self.tui.scroll_up(1)
                                handled = True
                            elif any(s in data for s in _LINE_DOWN_SEQS):
                                self.tui.scroll_down(1)
                                handled = True
                            elif any(s in data for s in _TOP_SEQS):
                                self.tui.scroll_top()
                                handled = True
                            elif any(s in data for s in _BOTTOM_SEQS):
                                self.tui.scroll_bottom()
                                handled = True
                            elif self.tui.is_scrolled and any(
                                s in data for s in _END_SEQS
                            ):
                                self.tui.scroll_bottom()
                                handled = True
                            elif self.tui.is_scrolled and any(
                                s in data for s in _HOME_SEQS
                            ):
                                self.tui.scroll_top()
                                handled = True

                        if not handled and self.mouse_mode:
                            if _MOUSE_UP_RE.search(data):
                                self.tui.scroll_up(3)
                                handled = True
                            elif _MOUSE_DOWN_RE.search(data):
                                self.tui.scroll_down(3)
                                handled = True
                            elif data.startswith(b"\x1b[<"):
                                handled = True

                        if handled:
                            continue

                    if use_stdin:
                        self.send_shell_data(data)

        finally:
            self._flush_log_buffer()
            if self.tui:
                self.tui.stop()
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
                raw_leftover = self._raw_str_buf.strip()
                self._raw_str_buf = ""
                if raw_leftover:
                    formatted = self._format_log(raw_leftover)
                    self._handle_log_line(formatted)
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
                self._process_log_payload(payload)
                log_text = self._format_log(payload)
                if is_reboot_cmd and any(
                    ind in log_text
                    for ind in ("Booting", "Image:", "Jumping", "Reset cause:")
                ):
                    seen_reboot_banner = True
                collected.extend(log_text.encode())

        def on_cmd_raw(data: bytes):
            def _emit_cmd_shell(text: str, is_line: bool):
                nonlocal seen_reboot_banner
                if (
                    is_line
                    and is_reboot_cmd
                    and any(
                        ind in text
                        for ind in (
                            "Booting",
                            "Image:",
                            "Jumping",
                            "Reset cause:",
                        )
                    )
                ):
                    seen_reboot_banner = True

                suffix = "\n" if is_line else ""
                collected.extend((text + suffix).encode())
                if not is_reboot_cmd:
                    if b":~$" in collected or b">" in collected:
                        done.set()
                elif seen_reboot_banner:
                    if b":~$" in collected or b">" in collected:
                        done.set()

            self._process_raw_text_stream(data, _emit_cmd_shell)

        self.decoder = HdlcDecoder(on_cmd_frame, raw_callback=on_cmd_raw)

        # Probe transport
        self._probe_transport()

        # Clear any initial prompt output from waking
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
            formatted = self._format_log(self._raw_str_buf)
            self._raw_str_buf = ""
            for line in formatted.splitlines():
                line = line.strip()
                if line:
                    if LogParser.is_log(line):
                        self._handle_log_line(line)
                    else:
                        collected.extend((line + "\n").encode())
                        if self.log_raw_shell and self.log_fp:
                            self._handle_log_line(line, default_module="shell")

        self._flush_log_buffer()
        os.close(self.uart_fd)
        self.uart_fd = -1
        result = collected.decode(errors="replace")
        return self._format_log(result)

    def execute_command(self, command: str, timeout: float = 3.0) -> str:
        """Executes a single shell command over HDLC or raw UART and returns output."""
        if self.log_file_path:
            with open(self.log_file_path, "a", encoding="utf-8") as fp:
                self.log_fp = fp
                try:
                    return self._execute_command_impl(command, timeout)
                finally:
                    self.log_fp = None
        return self._execute_command_impl(command, timeout)


# Backward compatibility alias
HdlcConsole = EcConsole


def get_default_ec_dir() -> Path:
    """Returns the EC repository root directory."""
    if "EC_DIR" in os.environ:
        return Path(os.environ["EC_DIR"])
    return Path(__file__).resolve().parent.parent


def get_default_token_db() -> str:
    """Returns default token database path (${EC_DIR}/build/tokens.bin or zephyr database.bin)."""
    ec_dir = get_default_ec_dir()
    tokens_bin = ec_dir / "build" / "tokens.bin"
    candidates = []
    if tokens_bin.is_file():
        candidates.append(tokens_bin)
    candidates.extend(ec_dir.glob("build/zephyr/*/output/database.bin"))
    if candidates:
        latest = max(candidates, key=lambda p: p.stat().st_mtime)
        return str(latest)
    return str(tokens_bin)


def query_default_device() -> str:
    """Queries dut-control for the EC UART PTY (preferring raw_ec_uart_pty)."""
    last_err = ""
    for control in ("raw_ec_uart_pty", "ec_uart_pty"):
        cmd = ["dut-control", "--", control, "-o"]
        try:
            res = subprocess.run(
                cmd,
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
        except (subprocess.CalledProcessError, subprocess.TimeoutExpired) as e:
            err_output = (
                getattr(e, "stderr", "") or getattr(e, "stdout", "") or str(e)
            )
            last_err = err_output.strip()
            continue

    raise RuntimeError(
        f"Failed to query dut-control for EC UART PTY ({last_err}). "
        "Ensure servod is running, or specify --device / -d explicitly."
    )


class ServoInterpManager:
    """Manages servod ec3po interpreter connection state to prevent conflicts.

    When ec_ec3po_interp_connect is 'on', servod's ec3po interpreter reads from
    raw_ec_uart_pty, causing byte-stealing and interfering with HDLC framing.
    This manager temporarily turns it 'off' while ec_console.py is active,
    and restores it to 'on' upon exit.
    """

    def __init__(self, enabled: bool = True, device: Optional[str] = None):
        self.enabled = enabled
        self.device = device
        self.needs_restore = False
        self._registered = False

    def setup(self):
        """Checks if ec3po interpreter is active and disconnects it."""
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

            # If device is explicitly ec_uart_pty (ec3po's own output PTY),
            # do not disconnect ec3po, otherwise ec_uart_pty would receive no data!
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
                    self._register_cleanup()
        except (subprocess.SubprocessError, FileNotFoundError, OSError):
            pass

    def restore(self):
        """Restores ec_ec3po_interp_connect to 'on' if it was disconnected."""
        if not self.needs_restore:
            return
        self.needs_restore = False
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
        except (subprocess.SubprocessError, FileNotFoundError, OSError):
            pass

    def _register_cleanup(self):
        if self._registered:
            return
        self._registered = True
        atexit.register(self.restore)

        def _sig_handler(signum, _frame):
            self.restore()
            sys.exit(128 + signum)

        for sig in (signal.SIGTERM, signal.SIGHUP):
            try:
                signal.signal(sig, _sig_handler)
            except (ValueError, OSError):
                pass


def main():
    """Main entry point for EC console utility."""
    default_token_db = get_default_token_db()
    parser = argparse.ArgumentParser(
        description="EC Console Utility for ChromiumOS",
    )
    parser.add_argument(
        "-d",
        "--device",
        default=None,
        help=(
            "Path to UART device or PTY (default: query "
            "'dut-control -- raw_ec_uart_pty -o')"
        ),
    )
    parser.add_argument(
        "-b",
        "--baudrate",
        type=int,
        default=115200,
        help="Baud rate (default: 115200)",
    )
    parser.add_argument(
        "-m",
        "--mode",
        choices=["auto", "hdlc", "raw"],
        default="auto",
        help=(
            "Framing protocol mode: 'auto' (detect HDLC or fallback to raw), "
            "'hdlc' (force HDLC framing), or 'raw' (unframed raw serial) "
            "(default: auto)"
        ),
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
        default=default_token_db,
        help=(
            f"Path to Pigweed token database to detokenize logs "
            f"(default: {default_token_db})"
        ),
    )
    parser.add_argument(
        "--token-prefix",
        default="`",
        help="Prefix character for tokenized messages (default: '`')",
    )
    parser.add_argument(
        "-c",
        "--command",
        help="Send a single shell command and exit after printing response",
    )
    parser.add_argument(
        "--raw",
        action="store_true",
        help="Disable interactive split TUI and stream logs in raw mode",
    )
    parser.add_argument(
        "--mouse",
        action="store_true",
        help="Enable mouse wheel scrolling in TUI",
    )
    parser.add_argument(
        "--no-raw-shell-logs",
        dest="log_raw_shell",
        action="store_false",
        default=True,
        help="Do not log raw UART shell responses under Device Logs",
    )
    parser.add_argument(
        "--mirror-shell-frame",
        dest="suppress_shell_frame",
        action="store_false",
        default=True,
        help=(
            "Do not suppress shell responses from the shell frame when "
            "logged under Device Logs"
        ),
    )
    parser.add_argument(
        "--no-interp-control",
        action="store_true",
        help=(
            "Disable automatic disconnect and restoration of servod's "
            "ec_ec3po_interp_connect"
        ),
    )

    args = parser.parse_args()

    device = args.device
    if not device:
        try:
            device = query_default_device()
        except RuntimeError as e:
            parser.error(str(e))

    interp_mgr = ServoInterpManager(
        enabled=not args.no_interp_control,
        device=device,
    )
    interp_mgr.setup()

    try:
        console = EcConsole(
            device_path=device,
            baudrate=args.baudrate,
            log_file=args.log_file,
            pty_symlink=args.pty,
            token_db=args.token_db if args.token_db else None,
            token_prefix=args.token_prefix,
            raw_mode=args.raw,
            mouse_mode=args.mouse,
            mode=args.mode,
            log_raw_shell=args.log_raw_shell,
            suppress_shell_frame=args.suppress_shell_frame,
        )

        if args.command:
            output = console.execute_command(args.command)
            print(output)
        else:
            console.run_interactive()
    finally:
        interp_mgr.restore()


if __name__ == "__main__":
    main()
