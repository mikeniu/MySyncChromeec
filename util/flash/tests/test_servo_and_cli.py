#!/usr/bin/env vpython3
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Unit tests for ServoDriver and flash_ec CLI entrypoint."""

import subprocess
import unittest
from unittest.mock import MagicMock
from unittest.mock import patch

from flash.flash_ec import main
from flash.flash_ec import parse_args
from flash.servo_utils import ServoDriver
from flash.servo_utils import ServoError


class TestServoDriver(unittest.TestCase):
    """Tests for ServoDriver wrapper around dut-control."""

    @patch("flash.servo_utils.subprocess.run")
    def test_dual_servo_prefix_and_effective_type(
        self, mock_run: MagicMock
    ) -> None:
        """Normalize servo_type and set prefix when dual-v4 second controller active."""
        mock_run.side_effect = [
            subprocess.CompletedProcess(
                ["dut-control"],
                0,
                stdout="servo_v4_with_servo_micro_and_ccd_cr50\n",
                stderr="",
            ),
            subprocess.CompletedProcess(
                ["dut-control"], 0, stdout="ccd_cr50\n", stderr=""
            ),
        ]
        servo = ServoDriver(host="localhost", port=9999)
        self.assertEqual(servo.prefix, "ccd_cr50.")
        self.assertEqual(servo.get("servo_type"), "servo_v4_with_ccd_cr50")

    @patch("flash.servo_utils.subprocess.run")
    def test_save_and_restore_state(self, mock_run: MagicMock) -> None:
        """Save controls and restore them in LIFO order."""
        mock_run.side_effect = [
            subprocess.CompletedProcess(
                ["dut-control"], 0, stdout="ccd_cr50\n", stderr=""
            ),
            subprocess.CompletedProcess(
                ["dut-control"], 0, stdout="off\n", stderr=""
            ),
            subprocess.CompletedProcess(
                ["dut-control"], 0, stdout="", stderr=""
            ),
            subprocess.CompletedProcess(
                ["dut-control"], 0, stdout="", stderr=""
            ),
        ]
        servo = ServoDriver(host="127.0.0.1", port=9998)
        servo.save_state("cold_reset")
        servo.save_state("ec_flash_select", override_restore_val="off")
        servo.restore_state()
        mock_run.reset_mock()
        servo.restore_state()
        mock_run.assert_not_called()

    @patch("flash.servo_utils.subprocess.run")
    def test_dry_run_skips_mutations(self, mock_run: MagicMock) -> None:
        """Do not execute mutating dut-control calls when dry_run is True."""
        mock_run.return_value = subprocess.CompletedProcess(
            ["dut-control"], 0, stdout="ccd_cr50\n", stderr=""
        )
        servo = ServoDriver(dry_run=True)
        mock_run.reset_mock()
        servo.set("cold_reset", "on")
        mock_run.assert_not_called()


class TestFlashEcCli(unittest.TestCase):
    """Tests for flash_ec CLI argument parsing and main entrypoint."""

    def test_parse_args_defaults(self) -> None:
        """Verify default timeout (600s), port, and OpenOCD flags."""
        args = parse_args(["--board=veluza", "--ocd_chip_cfg=custom.cfg"])
        self.assertEqual(args.board, "veluza")
        self.assertEqual(args.timeout, 600)
        self.assertEqual(args.port, 9999)
        self.assertEqual(args.ocd_chip_cfg, "custom.cfg")
        self.assertTrue(args.verify)

    @patch("flash.flash_ec.FlasherFactory.create")
    @patch("flash.flash_ec.ServoDriver")
    def test_main_success(
        self, _mock_servo: MagicMock, mock_create: MagicMock
    ) -> None:
        """Verify main() returns 0 when flasher.run() succeeds."""
        ret = main(["--board=veluza", "--image=/tmp/ec.bin"])
        self.assertEqual(ret, 0)
        mock_create.return_value.run.assert_called_once()

    @patch(
        "flash.flash_ec.ServoDriver",
        side_effect=ServoError("servod unreachable"),
    )
    def test_main_error_returns_1(self, _mock_servo: MagicMock) -> None:
        """Verify main() returns 1 when ServoError is raised."""
        ret = main(["--board=veluza", "--image=/tmp/ec.bin"])
        self.assertEqual(ret, 1)

    @patch("flash.flash_ec.FlasherFactory.create")
    @patch("flash.flash_ec.ServoDriver")
    def test_main_requires_image(
        self, _mock_servo: MagicMock, mock_create: MagicMock
    ) -> None:
        """Verify main() fails without --image unless --read is given."""
        self.assertEqual(main(["--board=veluza"]), 1)
        mock_create.assert_not_called()
        self.assertEqual(main(["--board=veluza", "--read=/tmp/ec.bin"]), 0)


if __name__ == "__main__":
    unittest.main()
