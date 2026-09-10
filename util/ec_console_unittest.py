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

    def test_raw_prompts_passed_when_hdlc_detected_is_true(self):
        """Test raw prompts with ~ are passed through even if hdlc_detected."""
        self.decoder.hdlc_detected = True
        self.decoder.process_bytes(b"uart:~$ ")
        self.assertEqual(b"".join(self.raw_out), b"uart:~$ ")

        self.raw_out.clear()
        self.decoder.process_bytes(b"ec:~> ")
        self.assertEqual(b"".join(self.raw_out), b"ec:~> ")

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

    def test_hdlc_frame_completion_does_not_flush_spurious_tilde(self):
        """Test completed HDLC frame does not emit trailing flag as raw byte."""
        shell_frame = ec_console.encode_ui_frame(
            address=ec_console.HDLC_ADDR_SHELL, payload=b"\r\nuart:~$ "
        )
        self.decoder.process_bytes(shell_frame)
        self.assertTrue(self.decoder.hdlc_detected)
        self.assertEqual(
            self.hdlc_out, [(ec_console.HDLC_ADDR_SHELL, b"\r\nuart:~$ ")]
        )
        self.assertEqual(self.raw_out, [])

        # Idle flush following frame reception must not emit HDLC_FLAG (~)
        self.decoder.flush_pending_raw()
        self.assertEqual(self.raw_out, [])

    def test_raw_stream_after_hdlc_frame_has_no_spurious_tilde(self):
        """Test raw stream following HDLC frame does not prepend ~."""
        shell_frame = ec_console.encode_ui_frame(
            address=ec_console.HDLC_ADDR_SHELL, payload=b"uart:~$ "
        )
        self.decoder.process_bytes(shell_frame)
        self.raw_out.clear()

        # Simulate DUT rebooting into RO and emitting raw boot banner
        self.decoder.process_bytes(b"[0.001000 Jumping to image RO]\r\nec:~> ")
        self.assertEqual(
            b"".join(self.raw_out),
            b"[0.001000 Jumping to image RO]\r\nec:~> ",
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

        with mock.patch.object(
            console, "_write_shell_output"
        ) as mock_write, mock.patch.object(
            console, "_handle_log_line"
        ) as mock_log:
            console._on_raw_data_received(b"*** Booting Zephyr OS ***\r\n")
            self.assertFalse(console.decoder.hdlc_detected)
            mock_log.assert_called()
            mock_write.assert_not_called()

    @mock.patch("os.open")
    @mock.patch("os.isatty", return_value=False)
    @mock.patch("termios.tcgetattr")
    @mock.patch("termios.tcsetattr")
    def test_zephyr_prompt_resets_hdlc_mode(
        self, _mock_tcset, _mock_tcget, _mock_isatty, mock_open
    ):
        """Test Zephyr prompt resets HDLC state to raw mode."""
        mock_open.return_value = 10
        console = ec_console.EcConsole(device_path="/dev/null")
        console.decoder.hdlc_detected = True

        with mock.patch.object(console, "_write_shell_output") as mock_write:
            console.decoder.process_bytes(b"uart:~$ ")
            self.assertFalse(console.decoder.hdlc_detected)
            written = b"".join(c.args[0] for c in mock_write.call_args_list)
            self.assertEqual(written, b"uart:~$ ")

    @mock.patch("os.open")
    @mock.patch("os.isatty", return_value=False)
    @mock.patch("termios.tcgetattr")
    @mock.patch("termios.tcsetattr")
    @mock.patch("os.write")
    def test_send_shell_data_raw_after_switch(
        self, mock_uart_write, _mock_tcset, _mock_tcget, _mock_isatty, mock_open
    ):
        """Test send_shell_data transmits unencapsulated bytes after raw switch."""
        mock_open.return_value = 10
        console = ec_console.EcConsole(device_path="/dev/null")
        console.uart_fd = 10
        console.decoder.hdlc_detected = True

        # DUT sends raw prompt
        with mock.patch.object(console, "_write_shell_output"):
            console.decoder.process_bytes(b"ec:~> ")
        self.assertFalse(console.decoder.hdlc_detected)

        # Typing sends raw bytes, not HDLC frames
        console.send_shell_data(b"help\r")
        mock_uart_write.assert_called_with(10, b"help\r")

    @mock.patch("os.open")
    @mock.patch("os.isatty", return_value=False)
    @mock.patch("termios.tcgetattr")
    @mock.patch("termios.tcsetattr")
    def test_empty_command_enter_does_not_produce_tilde_logs(
        self, _mock_tcset, _mock_tcget, _mock_isatty, mock_open
    ):
        """Test pressing Enter on empty shell command produces no ~ log lines."""
        mock_open.return_value = 10
        logs = []
        console = ec_console.EcConsole(device_path="/dev/null")
        console.decoder.hdlc_detected = True
        console._handle_log_line = lambda line, **kw: logs.append(line)

        shell_frame = ec_console.encode_ui_frame(
            address=ec_console.HDLC_ADDR_SHELL, payload=b"\r\nuart:~$ "
        )
        with mock.patch.object(console, "_write_shell_output"):
            console.decoder.process_bytes(shell_frame)
            console.decoder.flush_pending_raw()
            if console._raw_str_buf:
                console._flush_raw_buffer_idle()

            self.assertEqual(logs, [])
            self.assertEqual(console._raw_str_buf, "")

            # Second Enter keypress
            console.decoder.process_bytes(shell_frame)
            console.decoder.flush_pending_raw()
            self.assertEqual(logs, [])

    @mock.patch("os.open")
    @mock.patch("os.isatty", return_value=False)
    @mock.patch("termios.tcgetattr")
    @mock.patch("termios.tcsetattr")
    def test_raw_uart_driver_logs_not_emitted_to_shell(
        self, _mock_tcset, _mock_tcget, _mock_isatty, mock_open
    ):
        """Test spontaneous raw driver logs route to logs, not shell pane."""
        mock_open.return_value = 10
        console = ec_console.EcConsole(device_path="/dev/null")
        console.decoder.hdlc_detected = False

        with mock.patch.object(
            console, "_write_shell_output"
        ) as mock_write, mock.patch.object(
            console, "_handle_log_line"
        ) as mock_log:
            console.decoder.process_bytes(
                b"Port C0: using driver tps6699-pdc@24\r\n"
                b"TI0: INIT\r\n"
                b"Power state: G3 -> S5\r\n"
            )
            mock_write.assert_not_called()
            self.assertEqual(mock_log.call_count, 3)

    @mock.patch("os.open")
    @mock.patch("os.isatty", return_value=False)
    @mock.patch("termios.tcgetattr")
    @mock.patch("termios.tcsetattr")
    def test_prompt_attached_to_log_decoupled(
        self, _mock_tcset, _mock_tcget, _mock_isatty, mock_open
    ):
        """Test shell prompt attached to raw log is cleanly separated."""
        mock_open.return_value = 10
        console = ec_console.EcConsole(device_path="/dev/null")
        console.decoder.hdlc_detected = False

        with mock.patch.object(
            console, "_write_shell_output"
        ) as mock_write, mock.patch.object(
            console, "_handle_log_line"
        ) as mock_log:
            console.decoder.process_bytes(
                b"ec:> [0.299743 VB Received 0xec07]\r\n"
            )
            mock_write.assert_called_with(b"ec:> ")
            mock_log.assert_called_with("[0.299743 VB Received 0xec07]")
            self.assertFalse(console._waiting_shell_response)

    @mock.patch("os.open")
    @mock.patch("os.isatty", return_value=False)
    @mock.patch("termios.tcgetattr")
    @mock.patch("termios.tcsetattr")
    def test_prompt_attached_to_driver_log_decoupled(
        self, _mock_tcset, _mock_tcget, _mock_isatty, mock_open
    ):
        """Test shell prompt attached to driver log is cleanly separated."""
        mock_open.return_value = 10
        console = ec_console.EcConsole(device_path="/dev/null")
        console.decoder.hdlc_detected = False

        with mock.patch.object(
            console, "_write_shell_output"
        ) as mock_write, mock.patch.object(
            console, "_handle_log_line"
        ) as mock_log:
            console.decoder.process_bytes(
                b"ec:> boot_keys: boot_keys_value=0x00000000\r\n"
            )
            mock_write.assert_called_with(b"ec:> ")
            mock_log.assert_called_with("boot_keys: boot_keys_value=0x00000000")
            self.assertFalse(console._waiting_shell_response)

    @mock.patch("os.open")
    @mock.patch("os.isatty", return_value=False)
    @mock.patch("termios.tcgetattr")
    @mock.patch("termios.tcsetattr")
    @mock.patch("os.write")
    def test_command_output_and_echo_in_raw_mode(
        self, _mock_os_write, _mock_tcset, _mock_tcget, _mock_isatty, mock_open
    ):
        """Test command execution routes output to shell while command runs."""
        mock_open.return_value = 10
        console = ec_console.EcConsole(device_path="/dev/null")
        console.uart_fd = 10
        console.decoder.hdlc_detected = False
        console.log_raw_shell = False

        with mock.patch.object(
            console, "_write_shell_output"
        ) as mock_write, mock.patch.object(
            console, "_handle_log_line"
        ) as mock_log:
            # User types 'h'
            console.send_shell_data(b"h")
            console.decoder.process_bytes(b"h")
            mock_write.assert_called_with(b"h")
            mock_write.reset_mock()

            # User presses Enter
            console.send_shell_data(b"\r")
            self.assertTrue(console._waiting_shell_response)

            # DUT emits command output and then next prompt
            console.decoder.process_bytes(
                b"\r\nAvailable commands:\r\n  help\r\nec:~> "
            )
            self.assertFalse(console._waiting_shell_response)
            mock_log.assert_not_called()
            written = b"".join(c.args[0] for c in mock_write.call_args_list)
            self.assertIn(b"Available commands:", written)
            self.assertIn(b"ec:~> ", written)
            self.assertEqual(console._raw_str_buf, "")

    @mock.patch("os.open")
    @mock.patch("os.isatty", return_value=False)
    @mock.patch("termios.tcgetattr")
    @mock.patch("termios.tcsetattr")
    @mock.patch("os.write")
    def test_background_log_during_command_routed_to_log(
        self, _mock_os_write, _mock_tcset, _mock_tcget, _mock_isatty, mock_open
    ):
        """Test background log emitted during command execution goes to logs."""
        mock_open.return_value = 10
        console = ec_console.EcConsole(device_path="/dev/null")
        console.uart_fd = 10
        console.decoder.hdlc_detected = False
        console.log_raw_shell = False

        with mock.patch.object(
            console, "_write_shell_output"
        ) as mock_write, mock.patch.object(
            console, "_handle_log_line"
        ) as mock_log:
            console.send_shell_data(b"test\r")
            self.assertTrue(console._waiting_shell_response)

            console.decoder.process_bytes(
                b"output 1\r\n[0.123456 ec background log]\r\noutput 2\r\nec:~> "
            )
            self.assertFalse(console._waiting_shell_response)
            mock_log.assert_called_once_with("[0.123456 ec background log]")
            written = b"".join(c.args[0] for c in mock_write.call_args_list)
            self.assertIn(b"output 1", written)
            self.assertIn(b"output 2", written)
            self.assertIn(b"ec:~> ", written)
            self.assertNotIn(b"background log", written)

    @mock.patch("os.open")
    @mock.patch("os.isatty", return_value=False)
    @mock.patch("termios.tcgetattr")
    @mock.patch("termios.tcsetattr")
    @mock.patch("os.write")
    def test_send_shell_data_clears_pending_echo_count(
        self, _mock_os_write, _mock_tcset, _mock_tcget, _mock_isatty, mock_open
    ):
        """Test pressing Enter clears pending echo count."""
        mock_open.return_value = 10
        console = ec_console.EcConsole(device_path="/dev/null")
        console.uart_fd = 10
        console.send_shell_data(b"abc")
        self.assertEqual(console._pending_echo_count, 3)
        console.send_shell_data(b"\r")
        self.assertEqual(console._pending_echo_count, 0)
        self.assertTrue(console._waiting_shell_response)

    @mock.patch("os.open")
    @mock.patch("os.isatty", return_value=False)
    @mock.patch("termios.tcgetattr")
    @mock.patch("termios.tcsetattr")
    @mock.patch("os.write")
    def test_flush_raw_buffer_idle_routes_background_log(
        self, _mock_os_write, _mock_tcset, _mock_tcget, _mock_isatty, mock_open
    ):
        """Test idle flush routes background log to log handler, not shell."""
        mock_open.return_value = 10
        console = ec_console.EcConsole(device_path="/dev/null")
        console.uart_fd = 10
        console._waiting_shell_response = True
        console._raw_str_buf = "[0.123456 ec background log]"

        with mock.patch.object(
            console, "_write_shell_output"
        ) as mock_write, mock.patch.object(
            console, "_handle_log_line"
        ) as mock_log:
            console._flush_raw_buffer_idle()
            mock_write.assert_not_called()
            mock_log.assert_called_once_with("[0.123456 ec background log]")
            self.assertEqual(console._raw_str_buf, "")


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
        self.assertTrue(
            ec_console.LogParser.is_log(
                "*** Booting Zephyr OS build 19d5b928dc62 ***"
            )
        )
        self.assertTrue(
            ec_console.LogParser.is_log("--- UART initialized after reboot ---")
        )
        self.assertTrue(
            ec_console.LogParser.is_log("Port C0: using driver tps6699-pdc@24")
        )
        self.assertTrue(ec_console.LogParser.is_log("TI0: INIT"))
        self.assertTrue(ec_console.LogParser.is_log("Power state: G3 -> S5"))
        self.assertTrue(
            ec_console.LogParser.is_log(
                ">>> ZEPHYR FATAL ERROR 1: CPU exception"
            )
        )
        self.assertTrue(
            ec_console.LogParser.is_log(
                "Connecting to ec_uart_pty - /dev/pts/13"
            )
        )
        self.assertTrue(
            ec_console.LogParser.is_log(
                "Setting up target I2C_PORT_SENSOR::0x20"
            )
        )
        self.assertTrue(ec_console.LogParser.is_log("DSP Initialization rc=0"))
        self.assertTrue(
            ec_console.LogParser.is_log("C0 attached: Attached.SNK")
        )
        self.assertTrue(ec_console.LogParser.is_log("C0, set FRS 1"))
        self.assertTrue(
            ec_console.LogParser.is_log(
                "pdc_is_rdo_valid: status=1, power_op_mode=3, RDO_POS=4"
            )
        )
        self.assertTrue(
            ec_console.LogParser.is_log("PDO: 00064145 (RDO pos 4)")
        )
        self.assertTrue(ec_console.LogParser.is_log("V: 20000"))
        self.assertTrue(ec_console.LogParser.is_log("AP_FW 300"))
        self.assertTrue(ec_console.LogParser.is_log("Port 80 writes:"))
        self.assertTrue(ec_console.LogParser.is_log("host sleep event = 0"))
        self.assertTrue(
            ec_console.LogParser.is_log(
                "Error with UCSI command 0x15. Return was -5"
            )
        )
        self.assertTrue(ec_console.LogParser.is_log("LB_get_params_v3"))
        self.assertTrue(
            ec_console.LogParser.is_log("Failed to decode lighteffect AL_S")
        )
        self.assertTrue(
            ec_console.LogParser.is_log("`v2md1w==soc_edp_bl_interrupt: 0")
        )
        self.assertTrue(
            ec_console.LogParser.is_log(
                "[ISR]`uggXVQjKnYwB[1.152343 charge_request(17920mV, 0mA)]"
            )
        )
        self.assertFalse(ec_console.LogParser.is_log("uart:~$ "))
        self.assertFalse(ec_console.LogParser.is_log("ec:> "))
        self.assertFalse(ec_console.LogParser.is_log("help command output"))

    def test_boot_and_crash_log_parsing(self):
        """Test parsing of boot banners, crash logs, and driver printks."""
        r_boot = ec_console.LogParser.parse("*** Booting Zephyr OS ***")
        self.assertEqual(r_boot.level, "SYS")
        self.assertEqual(r_boot.module, "boot")

        r_crash = ec_console.LogParser.parse(">>> ZEPHYR FATAL ERROR 1")
        self.assertEqual(r_crash.level, "CRT")
        self.assertEqual(r_crash.module, "crash")

        r_drv = ec_console.LogParser.parse("TI0: INIT")
        self.assertEqual(r_drv.level, "INF")
        self.assertEqual(r_drv.module, "TI0")
        self.assertEqual(r_drv.message, "INIT")

    def test_tokenized_log_parsing(self):
        """Test parsing of Pigweed tokenized log lines."""
        r1 = ec_console.LogParser.parse("`v2md1w==soc_edp_bl_interrupt: 0")
        self.assertEqual(r1.module, "soc_edp_bl_i")
        self.assertEqual(r1.level, "INF")

        r2 = ec_console.LogParser.parse(
            "`NpTEvQ==[0.353271 SBS battery supports pec: 1]"
        )
        self.assertEqual(r2.module, "SBS")
        self.assertEqual(r2.message, "battery supports pec: 1")

        r3 = ec_console.LogParser.parse(
            "[ISR]`uggXVQjKnYwB[1.152343 charge_request(17920mV, 0mA)]"
        )
        self.assertTrue(r3.message.startswith("[ISR]"))

        r4 = ec_console.LogParser.parse("`WdH/JgJTNQJTNA==")
        self.assertEqual(r4.module, "token")
        self.assertEqual(r4.message, "`WdH/JgJTNQJTNA==")

    def test_driver_log_parsing_extended(self):
        """Test extended driver log parsing without colons."""
        r1 = ec_console.LogParser.parse("host sleep event = 0")
        self.assertEqual(r1.module, "power")

        r2 = ec_console.LogParser.parse("Querying USB-C port config")
        self.assertEqual(r2.module, "pdc")

        r3 = ec_console.LogParser.parse("Port 80 writes:")
        self.assertEqual(r3.module, "Port")

        r4 = ec_console.LogParser.parse("Error with UCSI command 0x15")
        self.assertEqual(r4.module, "ucsi")


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


if __name__ == "__main__":
    unittest.main()
