#!/usr/bin/env python3
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Unit tests for test/run_device_tests.py."""

import argparse
import socket
import unittest
from unittest import mock

import run_device_tests


# pylint: disable=missing-function-docstring,protected-access
class RunDeviceTestsTest(unittest.TestCase):
    """Tests for output regexes in run_device_tests."""

    def test_single_check_failed_regex_matches_failures(self) -> None:
        regex = run_device_tests.SINGLE_CHECK_FAILED_REGEX
        self.assertIsNotNone(
            regex.match("test/foo.c:42: ASSERTION failed: x == y")
        )
        self.assertIsNotNone(
            regex.match('assertion "x == y" failed: file "test/foo.c", line 42')
        )
        self.assertIsNotNone(
            regex.match("    FAIL - test_foo in 0.001 seconds")
        )

    def test_single_check_failed_regex_ignores_driver_errors(self) -> None:
        regex = run_device_tests.SINGLE_CHECK_FAILED_REGEX
        self.assertIsNone(
            regex.match(
                "[0.012345 egis630_pal: SPI PAL transaction failed: -116]"
            )
        )
        self.assertIsNone(regex.match("Pass: all checks succeeded"))

    @mock.patch("sys.stdout")
    @mock.patch("builtins.open", new_callable=mock.mock_open, read_data=b"data")
    @mock.patch("socket.create_connection")
    def test_remote_servo_micro_flash_success(
        self, mock_create_conn, mock_open_file, mock_stdout
    ) -> None:
        hardware = run_device_tests.Hardware()
        mock_sock = mock.MagicMock()
        mock_sock.recv.side_effect = [
            b"Flashing...\n",
            run_device_tests._REMOTE_FLASH_SUCCESS_MSG + b"\n",
            b"",
        ]
        mock_create_conn.return_value.__enter__.return_value = mock_sock

        result = hardware.flash(
            board_config=run_device_tests.DARTMONKEY_CONFIG,
            image_path="/tmp/test_image.bin",
            flasher=run_device_tests.SERVO_MICRO,
            remote_ip="127.0.0.1",
            remote_port=None,
            test_config=mock.MagicMock(),
            zephyr=True,
        )

        self.assertTrue(result)
        mock_create_conn.assert_called_once_with(
            ("127.0.0.1", run_device_tests._DEFAULT_REMOTE_FLASH_PORT),
            timeout=run_device_tests._REMOTE_CONNECT_TIMEOUT_SEC,
        )
        mock_sock.settimeout.assert_called_once_with(
            run_device_tests._REMOTE_FLASH_TIMEOUT_SEC
        )
        mock_open_file.assert_called_once_with("/tmp/test_image.bin", "rb")
        mock_sock.sendfile.assert_called_once_with(mock_open_file.return_value)
        mock_sock.shutdown.assert_called_once_with(socket.SHUT_WR)
        self.assertEqual(mock_stdout.buffer.write.call_count, 2)

    @mock.patch("sys.stdout")
    @mock.patch("builtins.open", new_callable=mock.mock_open, read_data=b"data")
    @mock.patch("socket.create_connection")
    def test_remote_servo_micro_flash_failure(
        self, mock_create_conn, _mock_open_file, _mock_stdout
    ) -> None:
        hardware = run_device_tests.Hardware()
        mock_sock = mock.MagicMock()
        mock_sock.recv.side_effect = [b"REMOTE_FLASH_FAILED\n", b""]
        mock_create_conn.return_value.__enter__.return_value = mock_sock

        result = hardware.flash(
            board_config=run_device_tests.DARTMONKEY_CONFIG,
            image_path="/tmp/test_image.bin",
            flasher=run_device_tests.SERVO_MICRO,
            remote_ip="127.0.0.1",
            remote_port=12345,
            test_config=mock.MagicMock(),
            zephyr=True,
        )

        self.assertFalse(result)
        mock_create_conn.assert_called_once_with(
            ("127.0.0.1", 12345),
            timeout=run_device_tests._REMOTE_CONNECT_TIMEOUT_SEC,
        )

    @mock.patch("socket.create_connection")
    def test_remote_servo_micro_flash_os_error(self, mock_create_conn) -> None:
        hardware = run_device_tests.Hardware()
        mock_create_conn.side_effect = OSError("Connection refused")

        result = hardware.flash(
            board_config=run_device_tests.DARTMONKEY_CONFIG,
            image_path="/tmp/test_image.bin",
            flasher=run_device_tests.SERVO_MICRO,
            remote_ip="127.0.0.1",
            remote_port=None,
            test_config=mock.MagicMock(),
            zephyr=True,
        )

        self.assertFalse(result)

    def test_validate_args_combination_servo_micro_remote(self) -> None:
        args = argparse.Namespace(
            board=run_device_tests.DARTMONKEY,
            flasher=run_device_tests.SERVO_MICRO,
            remote="127.0.0.1",
            jlink_port=10002,
            console_port=10000,
        )
        run_device_tests.validate_args_combination(args)


if __name__ == "__main__":
    unittest.main()
