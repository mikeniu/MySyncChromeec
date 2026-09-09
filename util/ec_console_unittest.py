#!/usr/bin/env python3
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Unit tests for ec_console.py HDLC framing, decoder, and CLI utility."""

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

        with mock.patch.object(console, "_write_shell_output") as mock_write:
            console._on_raw_data_received(b"*** Booting Zephyr OS ***\r\n")
            self.assertFalse(console.decoder.hdlc_detected)
            mock_write.assert_called()


class ServoInterpManagerTest(unittest.TestCase):
    """Tests for Servo EC-3PO interpreter management."""

    @mock.patch("subprocess.run")
    def test_disconnect_and_restore(self, mock_run):
        """Test interpreter disconnect and restore commands."""
        mock_run.return_value.returncode = 0
        mock_run.return_value.stdout = "ec_ec3po_interp_connect:on\n"

        mgr = ec_console.ServoInterpManager(device="/dev/pts/11", enabled=True)
        mgr.disconnect()
        self.assertTrue(mgr.needs_restore)

        mgr.restore()
        self.assertFalse(mgr.needs_restore)

    def test_disabled_manager(self):
        """Test disabled manager does not issue commands."""
        mgr = ec_console.ServoInterpManager(device="/dev/pts/11", enabled=False)
        mgr.disconnect()
        self.assertFalse(mgr.needs_restore)
        mgr.restore()


if __name__ == "__main__":
    unittest.main()
