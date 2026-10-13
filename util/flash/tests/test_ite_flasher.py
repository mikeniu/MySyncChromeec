#!/usr/bin/env vpython3
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Unit tests for IteI2cFlasher and IteUartFlasher."""

import unittest
from unittest.mock import MagicMock
from unittest.mock import patch

from flash.ite_flasher import IteI2cFlasher
from flash.ite_flasher import IteUartFlasher
from flash.servo_utils import ServoDriver


class TestIteI2cFlasher(unittest.TestCase):
    """Tests for IteI2cFlasher over CCD and servo_micro."""

    def setUp(self) -> None:
        self.servo = MagicMock(spec=ServoDriver)
        self.args = MagicMock()
        self.args.board = "veluza"
        self.args.image = "/tmp/image.bin"
        self.args.read = None
        self.args.verify = True
        self.args.no_preserve = True
        self.args.verbose = False
        self.args.dry_run = False
        self.flasher = IteI2cFlasher(self.servo, self.args)

    @patch("time.sleep")
    def test_prepare_and_cleanup_ccd(self, _mock_sleep: MagicMock) -> None:
        """Verify sysrst assert/deassert and ite_debugger_mode on CCD."""

        def mock_get(key: str, default: str = "") -> str:
            if key == "servo_type":
                return "servo_v4_with_ccd_cr50"
            if key == "ccd_i2c_en":
                return "Always"
            return default

        self.servo.get.side_effect = mock_get
        self.servo.has_control.return_value = True

        self.flasher.prepare_dut()

        self.servo.set.assert_any_call("gsc_uart_cmd", "sysrst assert")
        self.servo.set.assert_any_call("gsc_i2c_ctrl", "ite_debugger_mode")

        self.flasher.cleanup_dut()
        self.servo.set.assert_any_call("gsc_uart_cmd", "sysrst deassert")

    @patch("os.path.exists", return_value=True)
    @patch("flash.ite_flasher.locate_tool", return_value="/mock/iteflash")
    @patch("subprocess.run")
    @patch("time.sleep")
    def test_dual_servo_prefers_servo_micro(
        self,
        _mock_sleep: MagicMock,
        mock_run: MagicMock,
        _mock_locate: MagicMock,
        _mock_exists: MagicMock,
    ) -> None:
        """Use active_dut_controller to pick servo_micro on a dual servo."""

        def mock_get(key: str, default: str = "") -> str:
            if key == "active_dut_controller":
                return "servo_micro"
            if key == "servo_type":
                return "servo_v4p1_with_servo_micro_and_ccd_cr50"
            if key == "servo_micro_serialname":
                return "SM123"
            return default

        self.servo.get.side_effect = mock_get
        self.servo.has_control.return_value = False

        self.flasher.prepare_dut()
        self.servo.set.assert_any_call("enable_ite_dfu", "on")

        self.flasher.execute_flash()
        mock_run.assert_called_once_with(
            [
                "/mock/iteflash",
                "--send-waveform=0",
                "--i2c-interface=usb",
                "-s",
                "SM123",
                "--dbgr-preserve-pin=G1",
                "--erase",
                "--write=/tmp/image.bin",
            ],
            check=True,
        )

    @patch("os.path.exists", return_value=True)
    @patch("flash.ite_flasher.locate_tool", return_value="/mock/iteflash")
    @patch("subprocess.run")
    @patch("time.sleep")
    def test_dual_servo_uses_active_ccd(
        self,
        _mock_sleep: MagicMock,
        mock_run: MagicMock,
        _mock_locate: MagicMock,
        _mock_exists: MagicMock,
    ) -> None:
        """Use CCD when it is the active controller of a dual servo."""

        def mock_get(key: str, default: str = "") -> str:
            values = {
                "active_dut_controller": "ccd_cr50",
                "servo_type": "servo_v4p1_with_servo_micro_and_ccd_cr50",
                "ccd_serialname": "CCD123",
                "ccd_i2c_en": "Always",
            }
            return values.get(key, default)

        self.servo.get.side_effect = mock_get
        self.servo.has_control.return_value = False

        self.flasher.prepare_dut()
        self.servo.set.assert_any_call("gsc_i2c_ctrl", "ite_debugger_mode")

        self.flasher.execute_flash()
        cmd = mock_run.call_args.args[0]
        self.assertIn("CCD123", cmd)
        self.assertIn("--block-write-size=256", cmd)

    @patch("os.path.exists", return_value=True)
    @patch("flash.ite_flasher.locate_tool", return_value="/mock/iteflash")
    @patch("subprocess.run")
    def test_execute_flash_ccd_preserve_pin_fallback(
        self,
        mock_run: MagicMock,
        _mock_locate: MagicMock,
        _mock_exists: MagicMock,
    ) -> None:
        """Verify --block-write-size=256 and --dbgr-preserve-pin=G1 on veluza."""

        def mock_get(key: str, default: str = "") -> str:
            if key == "servo_type":
                return "servo_v4_with_ccd_cr50"
            if key == "ccd_serialname":
                return "CCD123"
            return default

        self.servo.get.side_effect = mock_get
        self.servo.has_control.return_value = False

        self.flasher.execute_flash()

        mock_run.assert_called_once_with(
            [
                "/mock/iteflash",
                "--send-waveform=0",
                "--i2c-interface=usb",
                "-s",
                "CCD123",
                "--block-write-size=256",
                "--dbgr-preserve-pin=G1",
                "--erase",
                "--write=/tmp/image.bin",
            ],
            check=True,
        )

    @patch("time.sleep")
    @patch("shutil.copyfile")
    @patch(
        "flash.ite_flasher.preserve_sections_with_cur_image",
        return_value=False,
    )
    @patch("os.path.exists", return_value=True)
    @patch("flash.ite_flasher.locate_tool", return_value="/mock/iteflash")
    @patch("subprocess.run")
    def test_execute_flash_re_enters_dfu_after_preserve_read(
        self,
        mock_run: MagicMock,
        _mock_locate: MagicMock,
        _mock_exists: MagicMock,
        _mock_preserve: MagicMock,
        _mock_copy: MagicMock,
        _mock_sleep: MagicMock,
    ) -> None:
        """Verify DFU waveform is re-sent between iteflash read and write."""
        self.args.no_preserve = False

        def mock_get(key: str, default: str = "") -> str:
            if key == "servo_type":
                return "servo_v4_with_ccd_cr50"
            if key == "ccd_serialname":
                return "CCD123"
            return default

        self.servo.get.side_effect = mock_get
        self.servo.has_control.return_value = True

        self.flasher.execute_flash()
        self.assertEqual(mock_run.call_count, 2)
        self.servo.set.assert_any_call("gsc_i2c_ctrl", "ite_debugger_mode")


class TestIteUartFlasher(unittest.TestCase):
    """Tests for IteUartFlasher."""

    def setUp(self) -> None:
        self.servo = MagicMock(spec=ServoDriver)
        self.args = MagicMock()
        self.args.board = "testboard"
        self.args.chip = "ite_uart"
        self.args.image = "/tmp/image.bin"
        self.args.dry_run = False
        self.args.baud = None
        self.args.read = None
        self.args.verify = True
        self.args.no_preserve = True
        self.args.bitbang = False
        self.flasher = IteUartFlasher(self.servo, self.args)

    @patch("time.sleep")
    def test_prepare_dut_with_ccd(self, _mock_sleep: MagicMock) -> None:
        """Verify sysrst assert and ite_debugger_mode on CCD."""

        def mock_get(key: str, default: str = "") -> str:
            if key == "servo_type":
                return "ccd_cr50"
            return default

        self.servo.get.side_effect = mock_get

        self.flasher.prepare_dut()

        self.servo.set.assert_any_call("gsc_uart_cmd", "sysrst assert")
        self.servo.set.assert_any_call("gsc_i2c_ctrl", "ite_debugger_mode")
        self.servo.set.assert_any_call("ec_uart_parity", "even")

    @patch("time.sleep")
    def test_prepare_dut(self, _mock_sleep: MagicMock) -> None:
        """Verify boot0 pulse, cold_reset pulse, and parity on non-CCD servo."""

        def mock_get(key: str, default: str = "") -> str:
            if key == "servo_type":
                return "servo_v4"
            return default

        self.servo.get.side_effect = mock_get

        self.flasher.prepare_dut()
        self.servo.set.assert_any_call("ec_boot_mode", "on")
        self.servo.set.assert_any_call("ec_boot_mode", "off")
        self.servo.set.assert_any_call("ec_uart_en", "on")
        self.servo.save_state.assert_any_call("ec_ec3po_interp_connect")
        self.servo.set.assert_any_call("ec_ec3po_interp_connect", "off")
        self.servo.set.assert_any_call("ec_uart_parity", "even")

    @patch("time.sleep")
    def test_prepare_dut_with_baud_override(
        self, _mock_sleep: MagicMock
    ) -> None:
        """Verify custom ec_uart_baudrate configuration."""
        self.args.baud = 57600
        self.servo.get.return_value = "servo_v4"
        self.flasher.prepare_dut()
        self.servo.set.assert_any_call("ec_uart_baudrate", "57600")

    @patch("os.path.exists", return_value=True)
    @patch("flash.ite_flasher.locate_tool")
    @patch("subprocess.run")
    def test_execute_flash(
        self,
        mock_run: MagicMock,
        mock_locate: MagicMock,
        _mock_exists: MagicMock,
    ) -> None:
        """Verify itecomdbgr invocation."""
        self.servo.get.return_value = "/dev/pts/1"
        mock_locate.return_value = "/mock/itecomdbgr"
        self.flasher.execute_flash()
        mock_run.assert_called_once_with(
            [
                "/mock/itecomdbgr",
                "-d",
                "/dev/pts/1",
                "-b",
                "115200",
                "-f",
                "/tmp/image.bin",
            ],
            check=True,
        )

    def test_cleanup_dut_with_ccd(self) -> None:
        """Verify sysrst deassert on CCD cleanup."""

        def mock_get(key: str, default: str = "") -> str:
            if key == "servo_type":
                return "ccd_cr50"
            return default

        self.servo.get.side_effect = mock_get

        self.flasher.cleanup_dut()
        self.servo.set.assert_any_call("gsc_uart_cmd", "sysrst deassert")

    def test_cleanup_dut(self) -> None:
        """Verify cold_reset pulse on non-CCD cleanup."""

        def mock_get(key: str, default: str = "") -> str:
            if key == "servo_type":
                return "servo_v4"
            return default

        self.servo.get.side_effect = mock_get

        self.flasher.cleanup_dut()
        self.servo.set_many.assert_called_once_with(
            [("cold_reset", "on"), ("cold_reset", "off")]
        )


if __name__ == "__main__":
    unittest.main()
