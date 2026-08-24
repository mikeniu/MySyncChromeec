#!/usr/bin/env vpython3

"""Module docstring."""

# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
import os
import subprocess
import unittest
import subprocess
from unittest.mock import MagicMock
from unittest.mock import patch

from util.flash.helpers import EC_DIR
from util.flash.openocd_flasher import OpenOcdFlasher
from util.flash.servo_utils import ServoDriver


class TestOpenOcdFlasher(unittest.TestCase):
    """Docstring."""

    def setUp(self):
        self.servo = MagicMock(spec=ServoDriver)
        self.args = MagicMock()
        self.args.image = "/tmp/image.bin"
        self.args.offset = "0"
        self.args.timeout = 600
        self.args.dry_run = False
        self.args.ocd_chip_cfg = "stm32f1x.cfg"
        self.args.ocd_cmds = None

        self.flasher = OpenOcdFlasher(self.servo, self.args)

    def test_prepare_dut(self):
        """Docstring."""
        self.flasher.prepare_dut()
        self.servo.set.assert_any_call("jtag_buf_on_flex_en", "on")
        self.servo.set.assert_any_call("jtag_buf_en", "on")

    def test_cleanup_dut(self):
        """Docstring."""
        self.flasher.cleanup_dut()
        self.servo.set.assert_any_call("jtag_buf_on_flex_en", "off")
        self.servo.set.assert_any_call("jtag_buf_en", "off")

    @patch("os.path.exists")
    @patch("subprocess.run")
    def test_execute_flash(self, mock_run, mock_exists):
        """Docstring."""
        # Assume ocd_path check passes for ec_dir
        mock_exists.side_effect = lambda path: True

        self.flasher.execute_flash()

        ec_dir = EC_DIR
        ocd_path = os.path.join(ec_dir, "util", "openocd")

        expected_cmds = f"init; reset halt; flash write_image erase /tmp/image.bin 0; reset run; shutdown"  # pylint: disable=line-too-long,f-string-without-interpolation
        expected_cmd = [
            "sudo",
            "timeout",
            "-k",
            "10",
            "-s",
            "9",
            "600",
            "openocd",
            "-s",
            ocd_path,
            "-f",
            "servo.cfg",
            "-f",
            "stm32f1x.cfg",
            "-c",
            expected_cmds,
        ]

        mock_run.assert_called_once_with(expected_cmd, check=True)

    @patch("os.path.exists")
    @patch("subprocess.run")
    def test_execute_flash_fallback_path(self, mock_run, mock_exists):
        """Docstring."""
        # Fail the first exists, succeed on the fallback
        mock_exists.side_effect = [False, True]

        self.flasher.execute_flash()

        expected_cmds = f"init; reset halt; flash write_image erase /tmp/image.bin 0; reset run; shutdown"  # pylint: disable=line-too-long,f-string-without-interpolation
        expected_cmd = [
            "sudo",
            "timeout",
            "-k",
            "10",
            "-s",
            "9",
            "600",
            "openocd",
            "-s",
            "/usr/share/ec-devutils",
            "-f",
            "servo.cfg",
            "-f",
            "stm32f1x.cfg",
            "-c",
            expected_cmds,
        ]

        mock_run.assert_called_once_with(expected_cmd, check=True)

    @patch("os.path.exists")
    @patch("subprocess.run")
    def test_execute_flash_with_custom_cmds_and_braces(
        self, mock_run, mock_exists
    ):
        """Docstring."""
        mock_exists.return_value = True
        self.args.ocd_cmds = (
            "init; mww 0x40022000 {val}; flash {image} {offset}; { }"
        )
        self.args.offset = "100"

        self.flasher.execute_flash()

        ec_dir = EC_DIR
        ocd_path = os.path.join(ec_dir, "util", "openocd")
        expected_cmds = (
            "init; mww 0x40022000 {val}; flash /tmp/image.bin 100; { }"
        )
        expected_cmd = [
            "sudo",
            "timeout",
            "-k",
            "10",
            "-s",
            "9",
            "600",
            "openocd",
            "-s",
            ocd_path,
            "-f",
            "servo.cfg",
            "-f",
            "stm32f1x.cfg",
            "-c",
            expected_cmds,
        ]
        mock_run.assert_called_once_with(expected_cmd, check=True)

    @patch("os.path.exists")
    def test_execute_flash_no_path(self, mock_exists):
        """Docstring."""
        mock_exists.return_value = False

        with self.assertRaises(FileNotFoundError):
            self.flasher.execute_flash()

    @patch("os.path.exists")
    @patch("subprocess.run")
    def test_execute_flash_called_process_error(self, mock_run, mock_exists):
        """Docstring."""
        mock_exists.return_value = True
        mock_run.side_effect = subprocess.CalledProcessError(1, "openocd")

        with self.assertRaises(RuntimeError):
            self.flasher.execute_flash()

    @patch("os.path.exists")
    @patch("subprocess.run")
    def test_execute_flash_dry_run(self, mock_run, mock_exists):
        """Docstring."""
        mock_exists.return_value = True
        self.args.dry_run = True

        self.flasher.execute_flash()
        mock_run.assert_not_called()
