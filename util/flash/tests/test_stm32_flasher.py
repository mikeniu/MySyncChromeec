#!/usr/bin/env vpython3
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Unit tests for Stm32UartFlasher and Stm32DfuFlasher."""

import unittest
from unittest.mock import MagicMock
from unittest.mock import patch

from flash.servo_utils import ServoDriver
from flash.stm32_flasher import Stm32DfuFlasher
from flash.stm32_flasher import Stm32UartFlasher


class TestStm32UartFlasher(unittest.TestCase):
    """Tests for Stm32UartFlasher."""

    def setUp(self) -> None:
        self.servo = MagicMock(spec=ServoDriver)
        self.args = MagicMock()
        self.args.board = "bloonchipper"
        self.args.image = "/tmp/ec.bin"
        self.args.read = None
        self.args.verify = False
        self.args.no_preserve = True
        self.args.unprotect = False
        self.args.bitbang = False
        self.args.baud = None
        self.args.timeout = 60
        self.args.dry_run = False
        self.flasher = Stm32UartFlasher(self.servo, self.args)

    @patch("time.sleep")
    def test_prepare_and_cleanup_fpmcu(self, _mock_sleep: MagicMock) -> None:
        """Verify fpmcu boot_mode held on during prepare and released on cleanup."""
        self.servo.get.return_value = "servo_micro"
        self.flasher.prepare_dut()
        self.servo.set.assert_any_call("fpmcu_platform_uart_parity", "even")
        self.servo.set.assert_any_call("fpmcu_boot_mode", "on")
        self.servo.set.assert_any_call("fpmcu_reset", "on")
        self.flasher.cleanup_dut()
        self.servo.set.assert_any_call("fpmcu_boot_mode", "off")

    @patch("time.sleep")
    def test_prepare_and_cleanup_ccd(self, _mock_sleep: MagicMock) -> None:
        """Verify ccd_ec_boot_mode_bitbang and 57600 baudrate on CCD."""
        self.args.board = "hatch"
        self.servo.get.return_value = "ccd_cr50"
        self.flasher.prepare_dut()
        self.servo.set.assert_any_call("ccd_ec_boot_mode_bitbang", "on")
        self.servo.set.assert_any_call("ec_uart_baudrate", "57600")
        self.flasher.cleanup_dut()
        self.servo.set.assert_any_call("ccd_ec_boot_mode_bitbang", "off")

    @patch("os.path.exists", return_value=True)
    @patch("flash.stm32_flasher.locate_tool", return_value="/mock/stm32mon")
    @patch("subprocess.run")
    def test_execute_flash_write(
        self,
        mock_run: MagicMock,
        _mock_locate: MagicMock,
        _mock_exists: MagicMock,
    ) -> None:
        """Verify stm32mon command invocation with -U -u -e -w."""
        self.servo.get.return_value = "/dev/pts/4"
        self.flasher.execute_flash()
        mock_run.assert_called_once_with(
            [
                "sudo",
                "timeout",
                "-k",
                "10",
                "-s",
                "9",
                "60",
                "/mock/stm32mon",
                "-d",
                "/dev/pts/4",
                "-U",
                "-u",
                "-e",
                "-w",
                "/tmp/ec.bin",
            ],
            check=True,
        )

    @patch("time.sleep")
    def test_c2d2_resets_only_the_ec(self, _mock_sleep: MagicMock) -> None:
        """Use gsc_ec_reset on C2D2 so the GSC is not rebooted."""
        self.args.board = "hatch"
        self.servo.get.return_value = "c2d2"
        self.flasher.prepare_dut()
        self.flasher.cleanup_dut()
        self.servo.set.assert_any_call("gsc_ec_reset", "on")
        self.servo.set.assert_any_call("gsc_ec_reset", "off")
        self.assertNotIn(
            "cold_reset", [c.args[0] for c in self.servo.set.call_args_list]
        )

    @patch("os.path.exists", return_value=True)
    @patch("flash.stm32_flasher.locate_tool", return_value="/mock/stm32mon")
    @patch("subprocess.run")
    def test_execute_flash_read_does_not_unprotect(
        self,
        mock_run: MagicMock,
        _mock_locate: MagicMock,
        _mock_exists: MagicMock,
    ) -> None:
        """Read without -U, which would mass-erase a read-protected chip."""
        self.args.read = "/tmp/read.bin"
        self.servo.get.return_value = "/dev/pts/4"
        self.flasher.execute_flash()
        cmd = mock_run.call_args.args[0]
        self.assertEqual(cmd[-2:], ["-r", "/tmp/read.bin"])
        self.assertNotIn("-U", cmd)


class TestStm32DfuFlasher(unittest.TestCase):
    """Tests for Stm32DfuFlasher."""

    def setUp(self) -> None:
        self.args = MagicMock()
        self.args.board = "hammer"
        self.args.image = "/tmp/ec.bin"
        self.args.read = None
        self.args.offset = 0
        self.args.timeout = 60
        self.args.dry_run = False
        self.flasher = Stm32DfuFlasher(None, self.args)

    @patch("time.sleep")
    @patch("os.path.getsize", return_value=1024)
    @patch("os.path.exists", return_value=True)
    @patch("flash.stm32_flasher.locate_tool", return_value="/mock/dfu-util")
    @patch(
        "subprocess.check_output",
        return_value="Bus 001 Device 005: ID 0483:df11\n",
    )
    @patch("subprocess.run")
    def test_execute_flash(
        self,
        mock_run: MagicMock,
        _mock_check_output: MagicMock,
        _mock_locate: MagicMock,
        _mock_exists: MagicMock,
        _mock_getsize: MagicMock,
        _mock_sleep: MagicMock,
    ) -> None:
        """Verify dfu-util unprotect and write invocations."""
        self.flasher.execute_flash()
        self.assertEqual(mock_run.call_count, 2)


if __name__ == "__main__":
    unittest.main()
