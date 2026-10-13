#!/usr/bin/env vpython3
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Unit tests for MchpFlasher."""

import os
import sys
import unittest
from unittest.mock import call
from unittest.mock import MagicMock
from unittest.mock import patch

from flash.helpers import EC_DIR
from flash.mchp_flasher import MchpFlasher
from flash.servo_utils import ServoDriver


class TestMchpFlasher(unittest.TestCase):
    """Tests for MchpFlasher preparation and crisis recovery execution."""

    def setUp(self) -> None:
        self.servo = MagicMock(spec=ServoDriver)
        self.servo.get.return_value = "servo_v4"
        self.args = MagicMock()
        self.args.board = "mtlrvp_mchp"
        self.args.image = "/tmp/ec.bin"
        self.args.mchp_loader = "/tmp/second_loader_fw.bin"
        self.args.read = None
        self.args.verbose = False
        self.args.dry_run = False
        self.flasher = MchpFlasher(self.servo, self.args)

    def test_prepare_and_cleanup(self) -> None:
        """Verify fw_up and cold_reset sequence on prepare/cleanup."""
        self.flasher.prepare_dut()
        self.servo.set.assert_any_call("ec_uart_parity", "none")
        self.servo.set_many.assert_called_with(
            [("cold_reset", "on"), ("fw_up", "on"), ("cold_reset", "off")]
        )
        self.flasher.cleanup_dut()
        self.servo.set_many.assert_called_with(
            [("cold_reset", "on"), ("fw_up", "off"), ("cold_reset", "off")]
        )

    @patch("flash.mchp_flasher.os.path.exists", return_value=True)
    @patch("flash.mchp_flasher.subprocess.run")
    def test_execute_flash(
        self, mock_run: MagicMock, _mock_exists: MagicMock
    ) -> None:
        """Verify two-stage crisis_rcvry_util.py invocation."""
        self.servo.get.return_value = "/dev/pts/5"
        self.flasher.execute_flash()
        crisis_tool = os.path.join(
            EC_DIR, "util", "mchp", "crisis_rcvry_util.py"
        )
        mock_run.assert_has_calls(
            [
                call(
                    [
                        sys.executable,
                        crisis_tool,
                        "-s",
                        "/tmp/second_loader_fw.bin",
                        "-p",
                        "/dev/pts/5",
                    ],
                    check=True,
                ),
                call(
                    [
                        sys.executable,
                        crisis_tool,
                        "-e",
                        "/tmp/ec.bin",
                        "-p",
                        "/dev/pts/5",
                    ],
                    check=True,
                ),
            ]
        )


if __name__ == "__main__":
    unittest.main()
