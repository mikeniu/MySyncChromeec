#!/usr/bin/env vpython3
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Unit tests for FlashromFlasher."""

import subprocess
import unittest
from unittest.mock import call
from unittest.mock import MagicMock
from unittest.mock import mock_open
from unittest.mock import patch

from flash.flashrom_flasher import FlashromFlasher
from flash.servo_utils import ServoDriver
from flash.servo_utils import ServoError


class TestFlashromFlasher(unittest.TestCase):
    """Tests for FlashromFlasher SPI preparation and execution."""

    def setUp(self) -> None:
        self.servo = MagicMock()
        self.servo.get.return_value = ""
        self.args = MagicMock()
        self.args.dry_run = False
        self.args.raiden = False
        self.flasher = FlashromFlasher(self.servo, self.args)

    def test_on_servov3(self) -> None:
        """Verify servo_v3 detection."""
        self.servo.get.return_value = "servo_v3"
        self.assertTrue(self.flasher.on_servov3())
        self.servo.get.return_value = "servo_v4"
        self.assertFalse(self.flasher.on_servov3())
        self.flasher.servo = None
        self.assertFalse(self.flasher.on_servov3())

    def test_on_raiden(self) -> None:
        """Verify Raiden SPI programmer detection."""
        self.flasher.servo = self.servo
        self.servo.get.return_value = "servo_v4"
        self.assertTrue(self.flasher.on_raiden())
        self.servo.get.return_value = "ccd"
        self.assertTrue(self.flasher.on_raiden())
        self.servo.get.return_value = "servo_micro"
        self.assertTrue(self.flasher.on_raiden())
        self.servo.get.return_value = "c2d2"
        self.assertTrue(self.flasher.on_raiden())
        self.servo.get.return_value = "other"
        self.args.raiden = True
        self.assertTrue(self.flasher.on_raiden())
        self.args.raiden = False
        self.assertFalse(self.flasher.on_raiden())
        self.flasher.servo = None
        self.args.raiden = False
        self.assertFalse(self.flasher.on_raiden())

    def test_prepare_dut_coral_and_reef(self) -> None:
        """Verify pp1800 vref selection for coral and reef."""
        for board in ("coral", "reef"):
            self.servo.reset_mock()
            self.args.board = board
            self.flasher.prepare_dut()
            self.servo.set.assert_any_call("spi1_vref", "pp1800")

    @patch("time.sleep")
    def test_prepare_dut_ite_spi(self, _mock_sleep: MagicMock) -> None:
        """Verify fw_up strap sequence and pp1800 setup for ite_spi."""
        self.args.chip = "ite_spi"
        self.flasher.prepare_dut()
        self.servo.set.assert_any_call("spi1_vref", "pp1800")
        self.servo.set.assert_any_call("fw_up", "on")
        self.servo.set.assert_any_call("fw_up", "off")

    @patch("time.sleep")
    def test_prepare_dut_npcx_int_spi(self, _mock_sleep: MagicMock) -> None:
        """Verify fw_up setup for npcx_int_spi."""
        self.args.chip = "npcx_int_spi"
        self.flasher.prepare_dut()
        self.servo.set.assert_any_call("fw_up", "on")
        self.servo.set.assert_any_call("cold_reset", "on")
        self.servo.set.assert_any_call("cold_reset", "off")

    def test_prepare_dut_npcx_spi(self) -> None:
        """Verify dev_mode setup for npcx_spi."""
        self.args.chip = "npcx_spi"
        self.flasher.prepare_dut()
        self.servo.set.assert_any_call("dev_mode", "on")

    def test_prepare_dut_other(self) -> None:
        """Verify cold_reset assertion for generic SPI chips."""
        self.args.chip = "other"
        self.args.board = "other"
        self.flasher.prepare_dut()
        self.servo.set.assert_any_call("cold_reset", "on")

    @patch.object(FlashromFlasher, "execute_flash")
    @patch("flash.servo_utils.subprocess.run")
    def test_run_releases_cold_reset(
        self, mock_run: MagicMock, _mock_execute: MagicMock
    ) -> None:
        """Verify SPI controls are restored before the EC is reset."""
        mock_run.return_value = subprocess.CompletedProcess(
            [], 0, stdout="off\n", stderr=""
        )
        self.args.chip = "other"
        self.args.board = "other"
        self.args.read = None
        self.args.try_apshutdown = False
        FlashromFlasher(ServoDriver(), self.args).run()
        controls = [
            arg for c in mock_run.call_args_list for arg in c.args[0][2:]
        ]
        self.assertEqual(
            controls[-7:],
            [
                "cold_reset:on",
                "spi1_buf_on_flex_en:off",
                "spi1_buf_en:off",
                "spi1_vref:off",
                "cold_reset:off",
                "cold_reset:on",
                "cold_reset:off",
            ],
        )

    def test_prepare_dut_exceptions(self) -> None:
        """Verify ServoError on optional SPI vref/buffer controls is ignored."""
        self.args.chip = "other"

        def mock_set(key: str, _val: str) -> None:
            if "vref" in key:
                raise ServoError("failed")

        self.servo.set.side_effect = mock_set
        self.flasher.prepare_dut()

    def test_cleanup_dut(self) -> None:
        """Verify cleanup_dut restores servo state, then resets the EC."""
        self.flasher.cleanup_dut()
        self.assertEqual(
            self.servo.method_calls,
            [
                call.restore_state(),
                call.set_many([("cold_reset", "on"), ("cold_reset", "off")]),
            ],
        )
        self.flasher.servo = None
        self.flasher.cleanup_dut()

    @patch("flash.flashrom_flasher.locate_tool")
    @patch("os.path.exists")
    def test_execute_flash_no_read_no_image(
        self, mock_exists: MagicMock, _mock_locate: MagicMock
    ) -> None:
        """Raise FileNotFoundError when image does not exist."""
        self.args.read = None
        self.args.image = "nonexistent.bin"
        mock_exists.return_value = False
        with self.assertRaises(FileNotFoundError):
            self.flasher.execute_flash()

    @patch("flash.flashrom_flasher.locate_tool")
    @patch("subprocess.run")
    def test_execute_flash_read(
        self, mock_run: MagicMock, mock_locate: MagicMock
    ) -> None:
        """Verify flashrom read invocation on servo_v3."""
        mock_locate.return_value = "flashrom"
        self.args.read = "read.bin"
        self.args.image = None
        self.servo.get.return_value = "servo_v3"
        self.flasher.execute_flash()
        mock_run.assert_called_once_with(
            ["sudo", "flashrom", "--programmer=linux_spi", "-r", "read.bin"],
            check=True,
        )

    @patch("flash.flashrom_flasher.locate_tool")
    @patch("subprocess.run")
    @patch("subprocess.check_output")
    @patch("os.path.getsize")
    @patch("os.path.exists")
    @patch("tempfile.TemporaryDirectory")
    @patch("builtins.open", new_callable=mock_open, read_data=b"testdata")
    @patch("flash.flashrom_flasher.preserve_sections_with_cur_image")
    def test_execute_flash_write_servov3(
        self,
        _mock_preserve: MagicMock,
        mock_file: MagicMock,
        mock_tmp: MagicMock,
        mock_exists: MagicMock,
        mock_getsize: MagicMock,
        mock_check_output: MagicMock,
        mock_run: MagicMock,
        mock_locate: MagicMock,
    ) -> None:
        """Verify size parsing around banner/warning lines and FF padding."""
        mock_locate.return_value = "flashrom"
        mock_exists.return_value = True
        self.args.read = None
        self.args.image = "image.bin"
        self.args.chip = "npcx_int_spi"
        self.args.no_preserve = False
        self.servo.get.return_value = "servo_v3"

        mock_getsize.return_value = 8
        mock_check_output.return_value = "banner\n1\n16\nWARNING: trailing\n"

        mock_tmp_instance = MagicMock()
        mock_tmp_instance.__enter__.return_value = "/tmp/dir"
        mock_tmp.return_value = mock_tmp_instance

        self.flasher.execute_flash()

        mock_check_output.assert_called_once_with(
            ["sudo", "flashrom", "--programmer=linux_spi", "--flash-size"],
            encoding="utf-8",
        )
        self.assertGreaterEqual(mock_run.call_count, 2)
        mock_file.return_value.write.assert_has_calls(
            [call(b"testdata"), call(b"\xff" * 8)], any_order=False
        )

    @patch("flash.flashrom_flasher.locate_tool")
    @patch("subprocess.run")
    @patch("subprocess.check_output")
    @patch("os.path.getsize")
    @patch("os.path.exists")
    @patch("tempfile.TemporaryDirectory")
    @patch("builtins.open", new_callable=mock_open, read_data=b"testdata")
    def test_execute_flash_write_raiden_micro(
        self,
        _mock_file: MagicMock,
        mock_tmp: MagicMock,
        mock_exists: MagicMock,
        mock_getsize: MagicMock,
        mock_check_output: MagicMock,
        mock_run: MagicMock,
        mock_locate: MagicMock,
    ) -> None:
        """Verify raiden_debug_spi programmer selection on servo_micro."""
        mock_locate.return_value = "flashrom"
        mock_exists.return_value = True
        self.args.read = None
        self.args.image = "image.bin"
        self.args.chip = "other"
        self.args.no_preserve = True
        self.servo.get.side_effect = lambda ctrl, default="": (
            "servo_micro" if ctrl == "servo_type" else "SM123"
        )
        self.args.raiden = True

        mock_getsize.return_value = 8
        mock_check_output.side_effect = subprocess.CalledProcessError(
            1, "flashrom"
        )

        mock_tmp_instance = MagicMock()
        mock_tmp_instance.__enter__.return_value = "/tmp/dir"
        mock_tmp.return_value = mock_tmp_instance

        self.flasher.execute_flash()

        mock_run.assert_called_with(
            [
                "sudo",
                "flashrom",
                "--programmer=raiden_debug_spi:serial=SM123",
                "-w",
                "/tmp/dir/flash_temp.bin",
            ],
            check=True,
        )

    @patch("flash.flashrom_flasher.locate_tool")
    @patch("subprocess.run")
    @patch("subprocess.check_output")
    @patch("os.path.getsize")
    @patch("os.path.exists")
    @patch("tempfile.TemporaryDirectory")
    @patch("builtins.open", new_callable=mock_open, read_data=b"testdata")
    def test_execute_flash_write_raiden_ccd(
        self,
        mock_file: MagicMock,
        mock_tmp: MagicMock,
        mock_exists: MagicMock,
        mock_getsize: MagicMock,
        mock_check_output: MagicMock,
        mock_run: MagicMock,
        mock_locate: MagicMock,
    ) -> None:
        """Verify raiden_debug_spi:target=EC programmer selection on CCD."""
        mock_locate.return_value = "flashrom"
        mock_exists.return_value = True
        self.args.read = None
        self.args.image = "image.bin"
        self.args.chip = "other"
        self.args.no_preserve = True
        self.servo.get.side_effect = lambda ctrl, default="": (
            "ccd" if ctrl == "servo_type" else ""
        )
        self.args.raiden = True

        mock_getsize.return_value = 8
        mock_check_output.return_value = "something\n8\n"

        mock_tmp_instance = MagicMock()
        mock_tmp_instance.__enter__.return_value = "/tmp/dir"
        mock_tmp.return_value = mock_tmp_instance

        self.flasher.execute_flash()

        mock_run.assert_called_with(
            [
                "sudo",
                "flashrom",
                "--programmer=raiden_debug_spi:target=EC",
                "-w",
                "/tmp/dir/flash_temp.bin",
            ],
            check=True,
        )
        mock_file.return_value.write.assert_has_calls(
            [call(b"testdata")], any_order=False
        )

    @patch("flash.flashrom_flasher.locate_tool")
    @patch("subprocess.run")
    @patch("subprocess.check_output")
    @patch("os.path.getsize")
    @patch("os.path.exists")
    @patch("tempfile.TemporaryDirectory")
    @patch("builtins.open", new_callable=mock_open, read_data=b"testdata")
    def test_execute_flash_size_missing(
        self,
        mock_file: MagicMock,
        mock_tmp: MagicMock,
        mock_exists: MagicMock,
        mock_getsize: MagicMock,
        mock_check_output: MagicMock,
        _mock_run: MagicMock,
        mock_locate: MagicMock,
    ) -> None:
        """Warn and skip padding when flashrom prints no size line."""
        mock_locate.return_value = "flashrom"
        mock_exists.return_value = True
        self.args.read = None
        self.args.image = "image.bin"
        self.args.no_preserve = True
        self.servo.get.return_value = "servo_v3"
        mock_getsize.return_value = 8
        mock_check_output.return_value = "banner only\n"
        mock_tmp.return_value.__enter__.return_value = "/tmp/dir"

        with self.assertLogs("flash.flashrom_flasher", level="WARNING") as logs:
            self.flasher.execute_flash()

        self.assertIn("No flash size", logs.output[0])
        mock_file.return_value.write.assert_called_once_with(b"testdata")

    @patch("flash.flashrom_flasher.locate_tool")
    @patch("subprocess.check_output")
    @patch("os.path.getsize")
    @patch("os.path.exists")
    def test_execute_flash_size_0(
        self,
        mock_exists: MagicMock,
        mock_getsize: MagicMock,
        mock_check_output: MagicMock,
        mock_locate: MagicMock,
    ) -> None:
        """Raise ValueError when flashrom reports chip size 0."""
        mock_locate.return_value = "flashrom"
        mock_exists.return_value = True
        self.args.read = None
        self.args.image = "image.bin"
        self.servo.get.return_value = "servo_v3"

        mock_getsize.return_value = 8
        mock_check_output.return_value = "something\n0\n"

        with self.assertRaises(ValueError) as ctx:
            self.flasher.execute_flash()
        self.assertIn("Chip size is 0!", str(ctx.exception))

    @patch("flash.flashrom_flasher.locate_tool")
    @patch("subprocess.check_output")
    @patch("os.path.getsize")
    @patch("os.path.exists")
    def test_execute_flash_img_too_large(
        self,
        mock_exists: MagicMock,
        mock_getsize: MagicMock,
        mock_check_output: MagicMock,
        mock_locate: MagicMock,
    ) -> None:
        """Raise ValueError when image size exceeds SPI flash size."""
        mock_locate.return_value = "flashrom"
        mock_exists.return_value = True
        self.args.read = None
        self.args.image = "image.bin"
        self.servo.get.return_value = "servo_v3"

        mock_getsize.return_value = 16
        mock_check_output.return_value = "something\n8\n"

        with self.assertRaises(ValueError) as ctx:
            self.flasher.execute_flash()
        self.assertIn("Image is larger than SPI size!", str(ctx.exception))


if __name__ == "__main__":
    unittest.main()
