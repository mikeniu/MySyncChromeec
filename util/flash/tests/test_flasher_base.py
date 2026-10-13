#!/usr/bin/env vpython3
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Unit tests for BaseEcFlasher."""

import unittest
from unittest.mock import call
from unittest.mock import MagicMock
from unittest.mock import patch

from flash.flasher_base import BaseEcFlasher
from flash.servo_utils import ServoDriver


class StubFlasher(BaseEcFlasher):
    """Concrete stub implementation of BaseEcFlasher for testing."""

    def prepare_dut(self) -> None:
        """No-op prepare_dut for testing."""

    def execute_flash(self) -> None:
        """No-op execute_flash for testing."""

    def cleanup_dut(self) -> None:
        """No-op cleanup_dut for testing."""


class TestBaseEcFlasher(unittest.TestCase):
    """Tests for BaseEcFlasher lifecycle and apshutdown handling."""

    def setUp(self) -> None:
        self.servo = MagicMock(spec=ServoDriver)
        self.args = MagicMock()
        self.args.board = "testboard"
        self.args.verify = False
        self.args.read = None
        self.args.try_apshutdown = False
        self.args.try_apshutdown_count = 16

        self.flasher = StubFlasher(self.servo, self.args)

    @patch("time.sleep")
    def test_run_with_apshutdown(self, mock_sleep: MagicMock) -> None:
        """Verify apshutdown is sent before and after flashing when enabled."""
        self.args.try_apshutdown = True
        self.args.try_apshutdown_count = 3

        self.flasher.run()

        self.servo.set.assert_any_call("ec_uart_regexp", "None")
        self.assertEqual(
            self.servo.set.call_args_list.count(
                call("ec_uart_cmd", "apshutdown")
            ),
            6,
        )
        self.assertEqual(mock_sleep.call_count, 6)

    @patch("time.sleep")
    def test_run_without_apshutdown(self, mock_sleep: MagicMock) -> None:
        """Verify apshutdown is not sent when disabled."""
        self.flasher.run()

        calls = [
            c
            for c in self.servo.set.call_args_list
            if c[0][0] == "ec_uart_cmd" and c[0][1] == "apshutdown"
        ]
        self.assertEqual(len(calls), 0)
        self.assertEqual(mock_sleep.call_count, 0)

    @patch("time.sleep")
    def test_run_with_apshutdown_on_failure(
        self, mock_sleep: MagicMock
    ) -> None:
        """Verify cleanup and apshutdown still run when execute_flash fails."""
        self.args.try_apshutdown = True
        self.args.try_apshutdown_count = 3

        self.flasher.execute_flash = MagicMock(
            side_effect=RuntimeError("Flash error")
        )

        with self.assertRaises(RuntimeError):
            self.flasher.run()

        self.servo.set.assert_any_call("ec_uart_regexp", "None")
        self.assertEqual(
            self.servo.set.call_args_list.count(
                call("ec_uart_cmd", "apshutdown")
            ),
            6,
        )
        self.assertEqual(mock_sleep.call_count, 6)
        self.servo.restore_state.assert_called_once()

    def test_run_always_releases_cold_reset(self) -> None:
        """Verify cold_reset is restored to off, not to its initial value."""
        self.flasher.run()

        self.servo.save_state.assert_called_once_with(
            "cold_reset", override_restore_val="off"
        )
        self.servo.restore_state.assert_called_once()


if __name__ == "__main__":
    unittest.main()
