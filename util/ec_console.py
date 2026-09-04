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

import argparse
import collections
from dataclasses import dataclass
import enum
import os
from pathlib import Path
import pty
import re
import select
import shutil
import signal
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

_ANSI_RE = re.compile(r"\x1b\[[0-9;]*[a-zA-Z]")


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
    SYS_BRACKET_RE = re.compile(r"^\[(?P<msg>.*)\]$")

    @classmethod
    def parse(cls, line: str) -> LogRecord:
        """Parses a log line into structured components."""
        line = line.strip()
        now_str = time.strftime("%H:%M:%S")

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
            return LogRecord(
                timestamp=ts, level=lvl, module=mod, message=msg, raw=line
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
            return LogRecord(
                timestamp=ts, level=lvl, module=mod, message=msg, raw=line
            )

        # 3. EC generic bracket log: [0.137115 something happened]
        m = cls.EC_GENERIC_RE.match(line)
        if m:
            ts = m.group("ts")
            msg = m.group("msg")
            parts = msg.split(None, 1)
            mod = parts[0] if len(parts) == 2 else "ec"
            msg_text = parts[1] if len(parts) == 2 else msg
            return LogRecord(
                timestamp=ts,
                level="INF",
                module=mod,
                message=msg_text,
                raw=line,
            )

        # 4. System bracket log: [Image: RO, ...]
        m = cls.SYS_BRACKET_RE.match(line)
        if m:
            msg = m.group("msg")
            return LogRecord(
                timestamp=now_str,
                level="INF",
                module="sys",
                message=msg,
                raw=line,
            )

        # 5. Fallback raw line
        return LogRecord(
            timestamp=now_str,
            level="INF",
            module="sys",
            message=line,
            raw=line,
        )


class LogTheme:
    """Color styling matching Pigweed Console."""

    RESET = "\033[0m"
    BOLD = "\033[1m"
    DIM = "\033[2m"
    UNDERLINE = "\033[4m"
    INVERSE = "\033[7m"

    TIME_COLOR = "\033[38;5;244m"  # Dim gray
    PANE_BORDER = "\033[38;5;67m"  # Slate / Steel blue
    LOG_TITLE = "\033[1;36m"  # Cyan bold
    SHELL_TITLE = "\033[1;35m"  # Magenta bold

    LEVEL_STYLES = {
        "INF": ("\033[1;32m", " INF "),  # Bold green
        "WRN": ("\033[1;33m", " WRN "),  # Bold yellow
        "ERR": ("\033[1;31m", " ERR "),  # Bold red
        "DBG": ("\033[38;5;244m", " DBG "),  # Dim gray
        "SYS": ("\033[1;36m", " SYS "),  # Bright cyan
        "CRT": ("\033[41;97;1m", " CRT "),  # White on Red
    }

    MODULE_COLORS = [
        "\033[36m",  # Cyan
        "\033[32m",  # Green
        "\033[33m",  # Yellow
        "\033[35m",  # Magenta
        "\033[38;5;141m",  # Purple
        "\033[34m",  # Blue
        "\033[38;5;208m",  # Orange
        "\033[38;5;37m",  # Teal
    ]

    def get_module_color(self, mod: str) -> str:
        """Deterministically picks an accent color based on module name."""
        h = sum(ord(c) for c in mod)
        return self.MODULE_COLORS[h % len(self.MODULE_COLORS)]

    def format_row(
        self, record: LogRecord, max_width: int = 120, use_color: bool = True
    ) -> str:
        """Formats a LogRecord as a single aligned table row."""
        if not use_color:
            prefix = (
                f"{record.timestamp:>14}  {record.level:3}  "
                f"{record.module:>10}  "
            )
            avail = max(10, max_width - len(prefix))
            msg = record.message[:avail]
            return f"{prefix}{msg}"

        ts = f"{record.timestamp:>14}"
        ts_styled = f"{self.TIME_COLOR}{ts}{self.RESET}"

        lvl_color, lvl_tag = self.LEVEL_STYLES.get(
            record.level, ("\033[1;37m", f" {record.level[:3]} ")
        )
        lvl_styled = f"{lvl_color}{lvl_tag}{self.RESET}"

        mod = record.module[:10]
        mod_color = self.get_module_color(record.module)
        mod_styled = f"{mod_color}{mod:>10}{self.RESET}"

        prefix_len = 14 + 1 + 5 + 1 + 10 + 2  # 33 characters
        avail = max(10, max_width - prefix_len)
        msg = record.message
        if len(msg) > avail:
            msg = msg[: avail - 1] + "…"

        msg_color = self.RESET
        if record.level in ("ERR", "CRT"):
            msg_color = "\033[91m"
        elif record.level == "WRN":
            msg_color = "\033[93m"

        return (
            f"{ts_styled} {lvl_styled} {mod_styled}  "
            f"{msg_color}{msg}{self.RESET}"
        )


class TuiLayout:
    """Terminal User Interface layout for pw_console look-and-feel."""

    def __init__(
        self,
        device: str,
        baudrate: int,
        token_info: str,
        log_file: Optional[str] = None,
        pty_info: Optional[str] = None,
    ):
        self.device = device
        self.baudrate = baudrate
        self.token_info = token_info
        self.log_file = log_file
        self.pty_info = pty_info
        self.theme = LogTheme()

        self.rows = 24
        self.cols = 80
        self.log_top = 4
        self.log_bottom = 14
        self.split_row = 15
        self.shell_top = 16
        self.shell_bottom = 23
        self.status_row = 24

        self.log_history = collections.deque(maxlen=2000)
        self.log_count = 0
        self.paused = False
        self.started = False

    def recalculate_geometry(self):
        """Recalculates pane boundaries based on current terminal dimensions."""
        size = shutil.get_terminal_size((80, 24))
        self.rows = max(10, size.lines)
        self.cols = max(40, size.columns)

        if self.pty_info:
            # Full-screen logs mode
            self.log_top = 4
            self.log_bottom = self.rows - 1
            self.split_row = 0
            self.shell_top = 0
            self.shell_bottom = 0
            self.status_row = self.rows
        else:
            # Split-screen: logs on top, interactive shell on bottom
            shell_height = max(5, min(14, self.rows // 3))
            self.split_row = self.rows - shell_height - 1
            self.log_top = 4
            self.log_bottom = self.split_row - 1
            self.shell_top = self.split_row + 1
            self.shell_bottom = self.rows - 1
            self.status_row = self.rows

    def render_chrome(self):
        """Draws the top header, pane titles, table header, and status footer."""
        # 1. Top Header Bar
        dev_text = f"Device: {self.device} ({self.baudrate})"
        tok_text = f"Tokens: {self.token_info}"
        log_text = f"Logs: {self.log_file}" if self.log_file else ""
        parts = [" PIGWEED / ZEPHYR EC CONSOLE", dev_text, tok_text]
        if log_text:
            parts.append(log_text)
        header_str = " │ ".join(parts) + " "
        header_line = (
            f"\033[1;1H\033[7m{header_str.ljust(self.cols)[:self.cols]}\033[0m"
        )

        # 2. Log Pane Title Bar
        title = " Device Logs "
        badge = " [PAUSED] " if self.paused else " [Follow: ON] "
        badge_style = "\033[1;33m" if self.paused else "\033[1;32m"
        dash_len = max(0, self.cols - len(title) - len(badge) - 5)
        log_title_line = (
            f"\033[2;1H\033[1;36m───\033[1;37m{title}\033[1;36m"
            + ("─" * dash_len)
            + f"{badge_style}{badge}\033[1;36m──\033[0m\033[K"
        )

        # 3. Table Column Headers
        tbl_hdr = "          TIME  LEVEL      MODULE  MESSAGE"
        truncated_tbl = tbl_hdr.ljust(self.cols)[: self.cols]
        tbl_line = f"\033[3;1H\033[4;37m{truncated_tbl}\033[0m\033[K"

        lines = [header_line, log_title_line, tbl_line]

        # 4. Shell Pane Title Bar (if not PTY mode)
        if not self.pty_info:
            shell_title = " EC Shell (Zephyr) "
            s_dash_len = max(0, self.cols - len(shell_title) - 5)
            shell_title_line = (
                f"\033[{self.split_row};1H\033[1;35m───"
                f"\033[1;37m{shell_title}\033[1;35m"
                + ("─" * s_dash_len)
                + "──\033[0m\033[K"
            )
            lines.append(shell_title_line)

        # 5. Status Bar
        if self.pty_info:
            status_parts = [
                " [Ctrl+C] Exit ",
                f"Virtual PTY: {self.pty_info}",
                "HDLC: Connected",
            ]
        else:
            status_parts = [
                " [Ctrl+] Exit ",
                " [Ctrl+L] Redraw ",
                (
                    " [Ctrl+P] Pause Logs "
                    if not self.paused
                    else " [Ctrl+P] Resume Logs "
                ),
                "HDLC: Connected" + (" [PAUSED]" if self.paused else ""),
            ]
        status_str = " │ ".join(status_parts) + " "
        truncated_status = status_str.ljust(self.cols)[: self.cols]
        status_line = (
            f"\033[{self.status_row};1H\033[7m{truncated_status}\033[0m"
        )
        lines.append(status_line)

        sys.stdout.write("".join(lines))
        sys.stdout.flush()

    def redraw(self, clear: bool = True):
        """Redraws display with recent logs and restored cursor."""
        self.recalculate_geometry()
        if clear:
            sys.stdout.write("\033[2J\033[H")
        self.render_chrome()

        # Render recent logs into log region
        visible_rows = self.log_bottom - self.log_top + 1
        recent = list(self.log_history)[-visible_rows:]
        start_row = self.log_bottom - len(recent) + 1
        for idx, record in enumerate(recent):
            row = start_row + idx
            row_str = self.theme.format_row(record, max_width=self.cols)
            sys.stdout.write(f"\033[{row};1H{row_str}\033[K")

        if not self.pty_info:
            sys.stdout.write(
                f"\033[{self.shell_top};{self.shell_bottom}r"
                f"\033[{self.shell_top};1H"
            )
        else:
            sys.stdout.write(
                f"\033[{self.log_top};{self.log_bottom}r"
                f"\033[{self.log_bottom};1H"
            )
        sys.stdout.flush()

    def add_log(self, record: LogRecord):
        """Appends a log record and updates display without shell jitter."""
        self.log_count += 1
        self.log_history.append(record)
        if self.paused:
            return

        row_str = self.theme.format_row(record, max_width=self.cols)

        if not self.pty_info:
            # Split screen mode:
            # 1. Save cursor & graphic rendition
            # 2. Set scroll region to log pane
            # 3. Position cursor at bottom of log pane
            # 4. Scroll up 1 line and print log row
            # 5. Restore scroll region to shell pane
            # 6. Restore cursor & graphic rendition
            sys.stdout.write(
                f"\0337"
                f"\033[{self.log_top};{self.log_bottom}r"
                f"\033[{self.log_bottom};1H\n{row_str}\r"
                f"\033[{self.shell_top};{self.shell_bottom}r"
                f"\0338"
            )
        else:
            # Full screen logs mode:
            sys.stdout.write(f"\033[{self.log_bottom};1H\n{row_str}\r")
        sys.stdout.flush()

    def write_shell_data(self, data: bytes):
        """Passes interactive shell bytes through while preserving layout."""
        if b"\x1b[2J" in data:
            # Clear only shell region lines
            sys.stdout.write("\0337")
            for r in range(self.shell_top, self.shell_bottom + 1):
                sys.stdout.write(f"\033[{r};1H\033[2K")
            sys.stdout.write(f"\033[{self.shell_top};1H\0338")
            sys.stdout.flush()
            data = data.replace(b"\x1b[2J", b"").replace(b"\x1b[H", b"")
        if data:
            sys.stdout.buffer.write(data)
            sys.stdout.buffer.flush()

    def toggle_pause(self):
        """Toggles log stream scrolling pause."""
        self.paused = not self.paused
        self.render_chrome()
        if not self.paused:
            self.redraw(clear=False)

    def _on_resize(self, _signum=None, _frame=None):
        if self.started:
            self.redraw(clear=True)

    def start(self):
        """Initializes alternate screen and starts the TUI."""
        self.started = True
        sys.stdout.write("\033[?1049h\033[2J\033[H")
        sys.stdout.flush()
        signal.signal(signal.SIGWINCH, self._on_resize)
        self.redraw(clear=True)

    def stop(self):
        """Restores the original screen buffer and terminal state."""
        if not self.started:
            return
        self.started = False
        try:
            signal.signal(signal.SIGWINCH, signal.SIG_DFL)
        except (ValueError, OSError):
            pass
        sys.stdout.write("\033[r\033[?25h\033[?1049l")
        sys.stdout.flush()


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
    ):
        self.device_path = device_path
        self.baudrate = baudrate
        self.log_file_path = log_file
        self.pty_symlink = pty_symlink
        self.token_db = token_db
        self.token_prefix = token_prefix
        self.raw_mode = raw_mode
        self.detokenizer = None
        self.token_info = "None"
        if token_db:
            self._init_detokenizer(token_db)

        self.theme = LogTheme()
        self.tui: Optional[TuiLayout] = None
        self._log_buffer = ""

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
            self.token_info = f"{count} tokens" if count else "Active"

    def _format_log(self, payload: bytes) -> str:
        """Detokenizes or decodes a log payload."""
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
        if self.detokenizer:
            text = str(self.detokenizer.detokenize_text(text))
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

            if self.raw_mode or not sys.stdout.isatty():
                print(
                    f"[HDLC] Virtual shell PTY created at: {slave_name}",
                    file=sys.stderr,
                )
                print(
                    f"[HDLC] Symlinked to: {self.pty_symlink}",
                    file=sys.stderr,
                )

    def _handle_log_line(self, line: str):
        """Processes and outputs a single completed log line."""
        record = LogParser.parse(line)
        if self.log_fp:
            clean_msg = strip_ansi(record.message)
            self.log_fp.write(
                f"{record.timestamp:>14} [{record.level:3}] "
                f"{record.module:>10}: {clean_msg}\n"
            )
            self.log_fp.flush()
        if self.tui:
            self.tui.add_log(record)
        else:
            sys.stderr.write(
                self.theme.format_row(record, use_color=sys.stderr.isatty())
                + "\n"
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

    def _flush_log_buffer(self):
        """Flushes any remaining partial log line in the buffer."""
        if self._log_buffer:
            line = self._log_buffer.rstrip("\r\n").strip()
            self._log_buffer = ""
            if line:
                self._handle_log_line(line)

    def _on_frame_received(self, address: int, payload: bytes):
        if address == HDLC_ADDR_LOG:
            self._process_log_payload(payload)
        elif address == HDLC_ADDR_SHELL:
            if self.pty_master_fd >= 0:
                os.write(self.pty_master_fd, payload)
            elif self.tui:
                self.tui.write_shell_data(payload)
            else:
                sys.stdout.buffer.write(payload)
                sys.stdout.buffer.flush()
        else:
            sys.stderr.write(
                f"[HDLC] Unhandled frame on address {hex(address)}: "
                f"{payload!r}\n"
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
        is_pty_mode = self.pty_master_fd >= 0
        use_tui = not self.raw_mode and sys.stdout.isatty()

        if use_tui:
            pty_info = (
                f"{self.pty_symlink} ({os.ttyname(self.pty_slave_fd)})"
                if is_pty_mode
                else None
            )
            self.tui = TuiLayout(
                device=self.device_path,
                baudrate=self.baudrate,
                token_info=self.token_info,
                log_file=self.log_file_path,
                pty_info=pty_info,
            )
            self.tui.start()
            if not is_pty_mode:
                # Wake shell prompt
                self.send_shell_data(b"\r")
        elif not self.raw_mode:
            print(
                "[HDLC] Console started. Press Ctrl+] to exit.",
                file=sys.stderr,
            )

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

                if sys.stdin.isatty() and sys.stdin.fileno() in readable:
                    data = os.read(sys.stdin.fileno(), 256)
                    if not data:
                        break
                    if (
                        b"\x1d" in data or b"\x1c" in data
                    ):  # Ctrl+] or Ctrl+\ to exit
                        break
                    if (
                        is_pty_mode and b"\x03" in data
                    ):  # Ctrl+C to exit in PTY mode
                        break
                    if self.tui and b"\x0c" in data:  # Ctrl+L to redraw
                        self.tui.redraw(clear=True)
                        if not is_pty_mode:
                            self.send_shell_data(b"\r")
                        continue
                    if self.tui and b"\x10" in data:  # Ctrl+P to toggle pause
                        self.tui.toggle_pause()
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
                self._process_log_payload(payload)

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

        self._flush_log_buffer()
        os.close(self.uart_fd)
        return collected.decode(errors="replace")


# Backward compatibility alias
HdlcConsole = EcConsole


def main():
    """Main entry point for EC console utility."""
    parser = argparse.ArgumentParser(
        description="EC Console Utility for ChromiumOS",
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

    args = parser.parse_args()

    console = EcConsole(
        device_path=args.device,
        baudrate=args.baudrate,
        log_file=args.log_file,
        pty_symlink=args.pty,
        token_db=args.token_db,
        token_prefix=args.token_prefix,
        raw_mode=args.raw,
    )

    if args.command:
        output = console.execute_command(args.command)
        print(output)
    else:
        console.run_interactive()


if __name__ == "__main__":
    main()
