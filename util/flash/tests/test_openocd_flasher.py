#!/usr/bin/env vpython3
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Unit tests for OpenOcdFlasher."""

import os
import subprocess
import unittest
from unittest.mock import MagicMock
from unittest.mock import patch

from flash.helpers import EC_DIR
from flash.openocd_flasher import OpenOcdFlasher
from flash.servo_utils import ServoDriver


class TestOpenOcdFlasher(unittest.TestCase):
    """Tests for OpenOcdFlasher JTAG buffer setup and OpenOCD invocation."""

    def setUp(self) -> None:
        self.servo = MagicMock(spec=ServoDriver)
        self.servo.has_control.return_value = True
        self.args = MagicMock()
        self.args.image = "/tmp/image.bin"
        self.args.read = None
        self.args.offset = "0"
        self.args.timeout = 600
        self.args.dry_run = False
        self.args.ocd_chip_cfg = "stm32f1x.cfg"
        self.args.ocd_cmds = None

        self.flasher = OpenOcdFlasher(self.servo, self.args)

    def test_prepare_dut(self) -> None:
        """Verify JTAG buffer controls are saved and enabled in prepare_dut."""
        self.flasher.prepare_dut()
        self.servo.save_state.assert_any_call("jtag_buf_on_flex_en")
        self.servo.save_state.assert_any_call("jtag_buf_en")
        self.servo.set.assert_any_call("jtag_buf_on_flex_en", "on")
        self.servo.set.assert_any_call("jtag_buf_en", "on")

    def test_cleanup_dut(self) -> None:
        """Verify cleanup_dut is a safe no-op (restored by ServoDriver)."""
        self.flasher.cleanup_dut()

    @patch("flash.openocd_flasher.os.path.exists", return_value=True)
    @patch("flash.openocd_flasher.subprocess.run")
    def test_execute_flash(
        self, mock_run: MagicMock, _mock_exists: MagicMock
    ) -> None:
        """Verify OpenOCD command line with util/openocd path."""
        self.flasher.execute_flash()

        ocd_path = os.path.join(EC_DIR, "util", "openocd")
        expected_cmds = (
            "init; reset halt; "
            "flash write_image erase /tmp/image.bin 0; "
            "reset run; shutdown"
        )
        expected_cmd = [
            "sudo",
            "timeout",
            "-k",
            "10",
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

    @patch("flash.openocd_flasher.os.path.exists")
    @patch("flash.openocd_flasher.subprocess.run")
    def test_execute_flash_fallback_path(
        self, mock_run: MagicMock, mock_exists: MagicMock
    ) -> None:
        """Verify fallback to /usr/share/ec-devutils when util/openocd absent."""
        mock_exists.side_effect = [True, False, True]

        self.flasher.execute_flash()

        expected_cmds = (
            "init; reset halt; "
            "flash write_image erase /tmp/image.bin 0; "
            "reset run; shutdown"
        )
        expected_cmd = [
            "sudo",
            "timeout",
            "-k",
            "10",
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

    @patch("flash.openocd_flasher.os.path.exists", return_value=True)
    @patch("flash.openocd_flasher.subprocess.run")
    def test_execute_flash_with_custom_cmds_and_braces(
        self, mock_run: MagicMock, _mock_exists: MagicMock
    ) -> None:
        """Verify custom ocd_cmds preserves non-placeholder braces."""
        self.args.ocd_cmds = (
            "init; mww 0x40022000 {val}; flash {image} {offset}; { }"
        )
        self.args.offset = "100"

        self.flasher.execute_flash()

        ocd_path = os.path.join(EC_DIR, "util", "openocd")
        expected_cmds = (
            "init; mww 0x40022000 {val}; flash /tmp/image.bin 100; { }"
        )
        expected_cmd = [
            "sudo",
            "timeout",
            "-k",
            "10",
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

    @patch("flash.openocd_flasher.os.path.exists")
    def test_execute_flash_no_path(self, mock_exists: MagicMock) -> None:
        """Raise FileNotFoundError when openocd configs are missing."""
        mock_exists.side_effect = [True, False, False]
        with self.assertRaises(FileNotFoundError):
            self.flasher.execute_flash()

    @patch("flash.openocd_flasher.os.path.exists", return_value=True)
    @patch("flash.openocd_flasher.subprocess.run")
    def test_execute_flash_called_process_error(
        self, mock_run: MagicMock, _mock_exists: MagicMock
    ) -> None:
        """Raise RuntimeError when openocd exits non-zero."""
        mock_run.side_effect = subprocess.CalledProcessError(1, "openocd")

        with self.assertRaises(RuntimeError):
            self.flasher.execute_flash()

    @patch("flash.openocd_flasher.os.path.exists", return_value=True)
    @patch("flash.openocd_flasher.subprocess.run")
    def test_execute_flash_dry_run(
        self, mock_run: MagicMock, _mock_exists: MagicMock
    ) -> None:
        """Do not invoke subprocess.run when dry_run is True."""
        self.args.dry_run = True

        self.flasher.execute_flash()
        mock_run.assert_not_called()

    @patch("flash.openocd_flasher.os.path.exists", return_value=True)
    @patch("flash.openocd_flasher.subprocess.run")
    def test_execute_flash_requires_chip_cfg(
        self, mock_run: MagicMock, _mock_exists: MagicMock
    ) -> None:
        """Raise ValueError instead of assuming an STM32 chip config."""
        self.args.ocd_chip_cfg = None

        with self.assertRaises(ValueError):
            self.flasher.execute_flash()
        mock_run.assert_not_called()

    @patch("flash.openocd_flasher.os.path.exists", return_value=True)
    @patch("flash.openocd_flasher.subprocess.run")
    def test_execute_flash_quotes_path_with_spaces(
        self, mock_run: MagicMock, _mock_exists: MagicMock
    ) -> None:
        """Quote image paths containing spaces in the Tcl command."""
        self.args.image = "/tmp/my image.bin"

        self.flasher.execute_flash()
        cmd = mock_run.call_args.args[0]
        self.assertIn('flash write_image erase "/tmp/my image.bin" 0;', cmd[-1])

    @patch("flash.openocd_flasher.os.path.exists", return_value=True)
    @patch("flash.openocd_flasher.subprocess.run")
    def test_execute_flash_rejects_quoted_path(
        self, mock_run: MagicMock, _mock_exists: MagicMock
    ) -> None:
        """Raise ValueError for image paths containing double quotes."""
        self.args.image = '/tmp/"ec".bin'

        with self.assertRaises(ValueError):
            self.flasher.execute_flash()
        mock_run.assert_not_called()


if __name__ == "__main__":
    unittest.main()
