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

    @patch("time.sleep")
    def test_prepare_dut_reads_enable_ite_dfu(
        self, _mock_sleep: MagicMock
    ) -> None:
        """Trigger the DFU waveform by reading enable_ite_dfu, like flash_ec."""
        self.servo.has_control.return_value = True
        for servo_type in ("servo_micro", "c2d2"):
            with self.subTest(servo_type=servo_type):
                self.servo.reset_mock()
                self.servo.get.return_value = servo_type
                self.flasher.prepare_dut()
                self.servo.get.assert_any_call("enable_ite_dfu")
                self.assertNotIn(
                    "enable_ite_dfu",
                    [c.args[0] for c in self.servo.set.call_args_list],
                )

    @patch("time.sleep")
    def test_prepare_dut_dry_run_skips_enable_ite_dfu(
        self, mock_sleep: MagicMock
    ) -> None:
        """Do not send the DFU waveform or wait for it in dry-run mode."""
        self.args.dry_run = True
        self.servo.get.return_value = "servo_micro"
        self.flasher.prepare_dut()
        self.assertNotIn(
            "enable_ite_dfu",
            [c.args[0] for c in self.servo.get.call_args_list],
        )
        mock_sleep.assert_not_called()

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
        self.servo.get.assert_any_call("enable_ite_dfu")

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

    @patch("flash.ite_flasher.parse_fmap_areas", return_value={})
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
        _mock_areas: MagicMock,
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

    @patch(
        "flash.ite_flasher.parse_fmap_areas",
        return_value={"CBI": MagicMock()},
    )
    @patch(
        "flash.ite_flasher.get_preserve_ranges",
        return_value=[(0x5E000, 0x5F000)],
    )
    @patch("flash.ite_flasher.preserve_sections_with_cur_image")
    @patch("os.path.exists", return_value=True)
    @patch("flash.ite_flasher.locate_tool", return_value="/mock/iteflash")
    @patch("subprocess.run")
    def test_execute_flash_skips_cbi_range_without_read(
        self,
        mock_run: MagicMock,
        _mock_locate: MagicMock,
        _mock_exists: MagicMock,
        mock_preserve: MagicMock,
        _mock_ranges: MagicMock,
        _mock_areas: MagicMock,
    ) -> None:
        """Verify IteI2cFlasher passes --skip-range and skips reading CBI."""
        self.args.no_preserve = False

        def mock_get(key: str, default: str = "") -> str:
            if key == "servo_type":
                return "servo_v4_with_ccd_cr50"
            if key == "ccd_serialname":
                return "CCD123"
            return default

        self.servo.get.side_effect = mock_get
        self.servo.has_control.return_value = False

        self.flasher.execute_flash()

        mock_preserve.assert_not_called()
        mock_run.assert_called_once_with(
            [
                "/mock/iteflash",
                "--send-waveform=0",
                "--i2c-interface=usb",
                "-s",
                "CCD123",
                "--block-write-size=256",
                "--dbgr-preserve-pin=G1",
                "--skip-range=0x5e000:0x1000",
                "--erase",
                "--write=/tmp/image.bin",
            ],
            check=True,
        )


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

    @patch(
        "flash.ite_flasher.parse_fmap_areas",
        return_value={"CBI": MagicMock()},
    )
    @patch(
        "flash.ite_flasher.get_preserve_ranges",
        return_value=[(0x5E000, 0x5F000)],
    )
    @patch("flash.ite_flasher.preserve_sections_with_cur_image")
    @patch("os.path.exists", return_value=True)
    @patch("flash.ite_flasher.locate_tool", return_value="/mock/itecomdbgr")
    @patch("subprocess.run")
    def test_execute_flash_skips_cbi_range_without_read(
        self,
        mock_run: MagicMock,
        _mock_locate: MagicMock,
        _mock_exists: MagicMock,
        mock_preserve: MagicMock,
        _mock_ranges: MagicMock,
        _mock_areas: MagicMock,
    ) -> None:
        """Verify IteUartFlasher passes -S start size and skips reading CBI."""
        self.args.no_preserve = False
        self.servo.get.return_value = "/dev/pts/1"
        self.flasher.execute_flash()

        mock_preserve.assert_not_called()
        mock_run.assert_called_once_with(
            [
                "/mock/itecomdbgr",
                "-d",
                "/dev/pts/1",
                "-b",
                "115200",
                "-S",
                "0x5e000",
                "0x1000",
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
