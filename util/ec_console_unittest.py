#!/usr/bin/env python3
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Unit tests for ec_console.py HDLC framing, decoder, and CLI utility."""

import io
import os
import sys
import unittest
from unittest import mock


sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

# pylint: disable=wrong-import-position,protected-access
import ec_console


class HdlcAddressingTest(unittest.TestCase):
    """Tests for HDLC variable-length address encoding and decoding."""

    def test_encode_address_single_byte(self):
        """Test encoding single byte addresses."""
        # Address 1 -> (1 << 1) | 1 = 3
        self.assertEqual(ec_console.encode_address(1), bytes([0x03]))
        # Address 2 -> (2 << 1) | 1 = 5
        self.assertEqual(ec_console.encode_address(2), bytes([0x05]))

    def test_encode_address_multi_byte(self):
        """Test encoding and decoding multi-byte addresses."""
        addr = 0x82
        encoded = ec_console.encode_address(addr)
        decoded, length = ec_console.decode_address(encoded)
        self.assertEqual(decoded, addr)
        self.assertEqual(length, len(encoded))

    def test_decode_address_invalid(self):
        """Test decoding empty data returns failure."""
        decoded, length = ec_console.decode_address(b"")
        self.assertEqual(decoded, -1)
        self.assertEqual(length, 0)


class HdlcEncoderTest(unittest.TestCase):
    """Tests for HDLC frame encoding and escaping."""

    def test_encode_simple_frame(self):
        """Test encoding a basic UI frame."""
        frame = ec_console.encode_ui_frame(
            address=ec_console.HDLC_ADDR_SHELL, payload=b"help\n"
        )
        self.assertTrue(frame.startswith(bytes([ec_console.HDLC_FLAG])))
        self.assertTrue(frame.endswith(bytes([ec_console.HDLC_FLAG])))
        self.assertEqual(frame[1], 0x05)
        self.assertEqual(frame[2], 0x03)

    def test_encode_escaped_bytes(self):
        """Test escaping flag and escape control bytes."""
        payload = bytes([ec_console.HDLC_FLAG, ec_console.HDLC_ESCAPE])
        frame = ec_console.encode_ui_frame(
            address=ec_console.HDLC_ADDR_SHELL, payload=payload
        )
        inner = frame[1:-1]
        self.assertNotIn(ec_console.HDLC_FLAG, inner)
        self.assertIn(ec_console.HDLC_ESCAPE, inner)


class HdlcDecoderTest(unittest.TestCase):
    """Tests for HDLC frame decoding, state machine, and raw mode fallback."""

    def setUp(self):
        self.raw_out = []
        self.hdlc_out = []
        self.decoder = ec_console.HdlcDecoder(
            callback=self._hdlc_cb,
            raw_callback=self.raw_out.append,
        )

    def _hdlc_cb(self, address: int, payload: bytes):
        self.hdlc_out.append((address, payload))

    def test_raw_passthrough_before_hdlc(self):
        """Test raw UART bytes are passed through prior to HDLC detection."""
        self.decoder.process_bytes(b"*** Booting Zephyr OS ***\r\n")
        self.assertEqual(
            b"".join(self.raw_out), b"*** Booting Zephyr OS ***\r\n"
        )
        self.assertFalse(self.decoder.hdlc_detected)
        self.assertEqual(len(self.hdlc_out), 0)

    def test_raw_prompt_with_tilde_handled(self):
        """Test prompt containing tilde is not misinterpreted as HDLC flag."""
        self.decoder.process_bytes(b"ec:~> ")
        self.assertEqual(b"".join(self.raw_out), b"ec:~> ")
        self.assertFalse(self.decoder.hdlc_detected)
        self.assertEqual(len(self.hdlc_out), 0)

    def test_raw_token_pattern_with_tilde(self):
        """Test base64 token patterns are passed through in raw mode."""
        self.decoder.process_bytes(b"`TOKEN1~`TOKEN2~")
        self.decoder.flush_pending_raw()
        self.assertEqual(b"".join(self.raw_out), b"`TOKEN1~`TOKEN2~")
        self.assertFalse(self.decoder.hdlc_detected)

    def test_hdlc_frame_decoding_and_detection(self):
        """Test valid HDLC frame triggers detection and payload dispatch."""
        shell_frame = ec_console.encode_ui_frame(
            address=ec_console.HDLC_ADDR_SHELL, payload=b"uart:~$ "
        )
        self.decoder.process_bytes(shell_frame)

        self.assertTrue(self.decoder.hdlc_detected)
        self.assertEqual(
            self.hdlc_out, [(ec_console.HDLC_ADDR_SHELL, b"uart:~$ ")]
        )
        self.assertEqual(self.raw_out, [])

    def test_split_hdlc_frame_with_idle_flush(self):
        """Test split HDLC frames are not flushed as raw during idle timeouts."""
        shell_frame = ec_console.encode_ui_frame(
            address=ec_console.HDLC_ADDR_SHELL, payload=b"uart:~$ "
        )
        chunk1 = shell_frame[:8]
        chunk2 = shell_frame[8:]

        self.decoder.process_bytes(chunk1)
        self.decoder.flush_pending_raw()
        self.assertEqual(self.raw_out, [])

        self.decoder.process_bytes(chunk2)
        self.assertTrue(self.decoder.hdlc_detected)
        self.assertEqual(
            self.hdlc_out, [(ec_console.HDLC_ADDR_SHELL, b"uart:~$ ")]
        )
        self.assertEqual(self.raw_out, [])

    def test_corrupted_hdlc_frame_dropped(self):
        """Test corrupted frame with bad CRC is dropped and not sent to raw."""
        self.decoder.hdlc_detected = True
        bad_frame = (
            bytes([ec_console.HDLC_FLAG, 0x03, 0x03])
            + b"corrupted payload"
            + b"\x00\x00\x00\x00"
            + bytes([ec_console.HDLC_FLAG])
        )
        self.decoder.process_bytes(bad_frame)
        self.assertEqual(self.hdlc_out, [])
        self.assertEqual(self.raw_out, [])

    def test_interframe_raw_bytes_forwarded_to_raw_callback(self):
        """Test interframe raw bytes are forwarded so reset indicators work."""
        self.decoder.hdlc_detected = True
        self.decoder.process_bytes(b"Image: RO\r\n")
        self.assertEqual(b"".join(self.raw_out), b"Image: RO\r\n")

    def test_consecutive_flags(self):
        """Test consecutive flag bytes between frames are handled cleanly."""
        frame = ec_console.encode_ui_frame(
            address=ec_console.HDLC_ADDR_LOG, payload=b"test log"
        )
        self.decoder.process_bytes(b"~~" + frame + b"~~")
        self.assertEqual(
            self.hdlc_out, [(ec_console.HDLC_ADDR_LOG, b"test log")]
        )


class EcConsoleRawModeFallbackTest(unittest.TestCase):
    """Tests for raw mode indicator detection and falling back from HDLC."""

    @mock.patch("os.open")
    @mock.patch("os.isatty", return_value=False)
    @mock.patch("termios.tcgetattr")
    @mock.patch("termios.tcsetattr")
    def test_noise_suppressed_when_hdlc_active(
        self, _mock_tcset, _mock_tcget, _mock_isatty, mock_open
    ):
        """Test unformatted noise without reset indicators is not output."""
        mock_open.return_value = 10
        console = ec_console.EcConsole(device_path="/dev/null")
        console.decoder.hdlc_detected = True

        with mock.patch.object(console, "_write_shell_output") as mock_write:
            console._on_raw_data_received(b"stray uart noise without newline")
            console._flush_raw_buffer_idle()
            mock_write.assert_not_called()

    @mock.patch("os.open")
    @mock.patch("os.isatty", return_value=False)
    @mock.patch("termios.tcgetattr")
    @mock.patch("termios.tcsetattr")
    def test_reset_indicator_resets_hdlc_mode(
        self, _mock_tcset, _mock_tcget, _mock_isatty, mock_open
    ):
        """Test boot banner resets HDLC state to raw mode."""
        mock_open.return_value = 10
        console = ec_console.EcConsole(device_path="/dev/null")
        console.decoder.hdlc_detected = True

        with mock.patch.object(console, "_write_shell_output") as mock_write:
            console._on_raw_data_received(b"*** Booting Zephyr OS ***\r\n")
            self.assertFalse(console.decoder.hdlc_detected)
            mock_write.assert_called()


class LogParserTest(unittest.TestCase):
    """Tests for structured log parsing and categorization."""

    def test_zephyr_log_parsing(self):
        """Test parsing standard Zephyr logs."""
        line = "[00:00:01,123] <inf> usb_pd: USB PD init complete"
        record = ec_console.LogParser.parse(line)
        self.assertEqual(record.timestamp, "00:00:01.123")
        self.assertEqual(record.level, "INF")
        self.assertEqual(record.module, "usb_pd")
        self.assertEqual(record.message, "USB PD init complete")

    def test_ec_bracket_log_parsing(self):
        """Test parsing legacy EC bracketed logs."""
        line = "[0.137115 VB Main]"
        record = ec_console.LogParser.parse(line)
        self.assertEqual(record.timestamp, "0.137115")
        self.assertEqual(record.level, "INF")
        self.assertEqual(record.module, "VB")
        self.assertEqual(record.message, "Main")

    def test_ec_bracket_error_log(self):
        """Test parsing EC bracketed logs with error indicator."""
        line = "[0.140045 ERR: VB fail]"
        record = ec_console.LogParser.parse(line)
        self.assertEqual(record.level, "ERR")
        self.assertEqual(record.module, "VB")

    def test_sys_bracket_log_parsing(self):
        """Test parsing system bracket logs."""
        line = "[Image: RO, version 1.0]"
        record = ec_console.LogParser.parse(line)
        self.assertEqual(record.level, "INF")
        self.assertEqual(record.module, "sys")
        self.assertEqual(record.message, "Image: RO, version 1.0")

    def test_isr_prefix_handling(self):
        """Test [ISR] tag is recognized and preserved."""
        line = "[ISR] [00:00:01.123] <err> gpio: isr error"
        record = ec_console.LogParser.parse(line)
        self.assertEqual(record.level, "ERR")
        self.assertEqual(record.module, "gpio")
        self.assertTrue(record.message.startswith("[ISR]"))

    def test_is_log(self):
        """Test is_log distinguishes log lines from shell prompts/output."""
        self.assertTrue(
            ec_console.LogParser.is_log("[00:00:01.123] <inf> test: msg")
        )
        self.assertTrue(ec_console.LogParser.is_log("[0.123 ec test]"))
        self.assertFalse(ec_console.LogParser.is_log("uart:~$ "))
        self.assertFalse(ec_console.LogParser.is_log("help command output"))


class DetokenizerTest(unittest.TestCase):
    """Tests for Pigweed detokenizer integration."""

    @mock.patch("os.open")
    @mock.patch("os.isatty", return_value=False)
    @mock.patch("termios.tcgetattr")
    @mock.patch("termios.tcsetattr")
    def test_format_log_detokenization(
        self, _mock_tcset, _mock_tcget, _mock_isatty, mock_open
    ):
        """Test formatting log string with mocked detokenizer."""
        mock_open.return_value = 10
        console = ec_console.EcConsole(device_path="/dev/null")
        mock_detok = mock.Mock()
        mock_detok.detokenize_text.side_effect = lambda text: text.replace(
            "`TOKEN", "Detokenized message"
        )
        console.detokenizer = mock_detok

        formatted = console._format_log("`TOKEN~")
        self.assertIn("Detokenized message", formatted)
        self.assertNotIn("~", formatted)

    @mock.patch("os.open")
    @mock.patch("os.isatty", return_value=False)
    @mock.patch("termios.tcgetattr")
    @mock.patch("termios.tcsetattr")
    def test_format_log_preserves_prompt_tilde(
        self, _mock_tcset, _mock_tcget, _mock_isatty, mock_open
    ):
        """Test prompt tildes like 'ec:~>' are not stripped by detokenizer."""
        mock_open.return_value = 10
        console = ec_console.EcConsole(device_path="/dev/null")
        mock_detok = mock.Mock()
        mock_detok.detokenize_text.side_effect = lambda text: text
        console.detokenizer = mock_detok

        formatted = console._format_log("ec:~> help")
        self.assertEqual(formatted, "ec:~> help")


class LogFileTest(unittest.TestCase):
    """Tests for file logging of EC log records."""

    @mock.patch("os.open")
    @mock.patch("os.isatty", return_value=False)
    @mock.patch("termios.tcgetattr")
    @mock.patch("termios.tcsetattr")
    def test_handle_log_line_writes_to_file(
        self, _mock_tcset, _mock_tcget, _mock_isatty, mock_open
    ):
        """Test handle_log_line writes clean formatted lines to log file."""
        mock_open.return_value = 10
        console = ec_console.EcConsole(device_path="/dev/null")
        fake_file = io.StringIO()
        console.log_fp = fake_file

        console._handle_log_line("[00:00:01.000] <inf> test: Hello world")
        output = fake_file.getvalue()
        self.assertIn("test: Hello world", output)
        self.assertIn("[INF]", output)


class TokenDbResolutionTest(unittest.TestCase):
    """Tests for default EC directory and token database discovery."""

    def test_get_default_ec_dir(self):
        """Test EC directory path resolution."""
        ec_dir = ec_console.get_default_ec_dir()
        self.assertTrue(ec_dir.exists())

    def test_get_default_token_db(self):
        """Test default token database path string generation."""
        token_db = ec_console.get_default_token_db()
        self.assertIsInstance(token_db, str)
        self.assertTrue(token_db.endswith(".bin"))


class ServoInterpManagerTest(unittest.TestCase):
    """Tests for Servo EC-3PO interpreter management."""

    @mock.patch("subprocess.run")
    def test_disconnect_and_restore(self, mock_run):
        """Test interpreter disconnect and restore commands."""
        mock_run.return_value.returncode = 0
        mock_run.return_value.stdout = "ec_ec3po_interp_connect:on\n"

        mgr = ec_console.ServoInterpManager(device="/dev/pts/11", enabled=True)
        mgr.setup()
        self.assertTrue(mgr.needs_restore)

        mgr.restore()
        self.assertFalse(mgr.needs_restore)

    def test_disabled_manager(self):
        """Test disabled manager does not issue commands."""
        mgr = ec_console.ServoInterpManager(device="/dev/pts/11", enabled=False)
        mgr.setup()
        self.assertFalse(mgr.needs_restore)
        mgr.restore()


class LogThemeTest(unittest.TestCase):
    """Tests for TUI log color styling and ANSI formatting."""

    def test_format_row_colored(self):
        """Test formatting log row with ANSI colors enabled."""
        theme = ec_console.LogTheme()
        record = ec_console.LogRecord(
            "00:01:00.000", "INF", "usb_pd", "init ok", ""
        )
        row = theme.format_row(record, use_color=True)
        self.assertIn("usb_pd", row)
        self.assertIn("init ok", row)
        self.assertIn("\033[", row)

    def test_format_row_uncolored(self):
        """Test formatting log row without ANSI colors."""
        theme = ec_console.LogTheme()
        record = ec_console.LogRecord(
            "00:01:00.000", "INF", "usb_pd", "init ok", ""
        )
        row = theme.format_row(record, use_color=False)
        self.assertIn("usb_pd", row)
        self.assertIn("init ok", row)
        self.assertNotIn("\033[", row)

    def test_strip_ansi(self):
        """Test strip_ansi removes all escape sequences."""
        raw = "\033[1;32mGreen message\033[0m"
        self.assertEqual(ec_console.strip_ansi(raw), "Green message")


class TuiLayoutTest(unittest.TestCase):
    """Tests for curses-based dual-pane TUI layout."""

    def test_tui_layout_init(self):
        """Test TuiLayout default properties."""
        tui = ec_console.TuiLayout(device="/dev/pts/11", baudrate=115200)
        self.assertEqual(tui.device, "/dev/pts/11")
        self.assertEqual(tui.baudrate, 115200)
        self.assertFalse(tui.is_scrolled)

    @mock.patch("sys.stdout.write")
    def test_tui_add_log(self, _mock_write):
        """Test adding logs to TuiLayout history."""
        tui = ec_console.TuiLayout(device="/dev/pts/11")
        record = ec_console.LogRecord("00:00:00.000", "INF", "ec", "test", "")
        tui.add_log(record)
        self.assertEqual(tui.log_count, 1)
        self.assertEqual(len(tui.log_history), 1)

    @mock.patch("sys.stdout.write")
    def test_tui_scroll(self, _mock_write):
        """Test scrolling up and down in log history."""
        tui = ec_console.TuiLayout(device="/dev/pts/11")
        for i in range(50):
            tui.add_log(
                ec_console.LogRecord(
                    "00:00:00.000", "INF", "ec", f"test {i}", ""
                )
            )
        tui.scroll_up(lines=5)
        self.assertTrue(tui.is_scrolled)
        tui.scroll_bottom()
        self.assertFalse(tui.is_scrolled)

    @mock.patch("sys.stdout.write")
    def test_tui_toggle_pause(self, _mock_write):
        """Test toggling pause state."""
        tui = ec_console.TuiLayout(device="/dev/pts/11")
        self.assertFalse(tui.paused)
        tui.toggle_pause()
        self.assertTrue(tui.paused)
        tui.toggle_pause()
        self.assertFalse(tui.paused)

    @mock.patch("sys.stdout.write")
    def test_tui_set_hdlc_active(self, _mock_write):
        """Test updating HDLC active badge."""
        tui = ec_console.TuiLayout(device="/dev/pts/11")
        tui.set_hdlc_active(True)
        self.assertTrue(tui.hdlc_active)
        tui.set_hdlc_active(False)
        self.assertFalse(tui.hdlc_active)


if __name__ == "__main__":
    unittest.main()
