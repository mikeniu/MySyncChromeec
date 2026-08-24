#!/usr/bin/env vpython3
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
import os
import subprocess
import tempfile
import unittest
from unittest.mock import ANY
from unittest.mock import call
from unittest.mock import MagicMock
from unittest.mock import mock_open
from unittest.mock import patch

from util.flash.flashrom_flasher import FlashromFlasher


class TestFlashromFlasher(unittest.TestCase):
    def setUp(self):
        self.servo = MagicMock()
        self.servo.get.return_value = ""
        self.args = MagicMock()
        self.args.raiden = False
        self.flasher = FlashromFlasher(self.servo, self.args)

    def test_on_servov3(self):
        self.servo.get.return_value = "servo_v3"
        self.assertTrue(self.flasher._on_servov3())
        self.servo.get.return_value = "servo_v4"
        self.assertFalse(self.flasher._on_servov3())
        self.flasher.servo = None
        self.assertFalse(self.flasher._on_servov3())

    def test_on_raiden(self):
        self.flasher.servo = self.servo
        self.servo.get.return_value = "servo_v4"
        self.assertTrue(self.flasher._on_raiden())
        self.servo.get.return_value = "ccd"
        self.assertTrue(self.flasher._on_raiden())
        self.servo.get.return_value = "servo_micro"
        self.assertTrue(self.flasher._on_raiden())
        self.servo.get.return_value = "other"
        self.args.raiden = True
        self.assertTrue(self.flasher._on_raiden())
        self.args.raiden = False
        self.assertFalse(self.flasher._on_raiden())
        self.flasher.servo = None
        self.args.raiden = False
        self.assertFalse(self.flasher._on_raiden())

    def test_prepare_dut_coral(self):
        self.args.board = "coral"
        self.flasher.prepare_dut()
        self.servo.set.assert_any_call("spi1_vref", "pp1800")

    def test_prepare_dut_ite_spi(self):
        self.args.chip = "ite_spi"
        self.flasher.prepare_dut()
        self.servo.set.assert_any_call("spi1_vref", "pp1800")
        self.servo.set.assert_any_call("fw_up", "on")

    def test_prepare_dut_npcx_int_spi(self):
        self.args.chip = "npcx_int_spi"
        self.flasher.prepare_dut()
        self.servo.set.assert_any_call("fw_up", "on")

    def test_prepare_dut_npcx_spi(self):
        self.args.chip = "npcx_spi"
        self.flasher.prepare_dut()
        self.servo.set.assert_any_call("dev_mode", "on")

    def test_prepare_dut_other(self):
        self.args.chip = "other"
        self.args.board = "other"
        self.flasher.prepare_dut()
        self.servo.set.assert_any_call("cold_reset", "on")

    def test_prepare_dut_exceptions(self):
        self.args.chip = "other"

        def mock_set(key, val):
            if "vref" in key:
                raise Exception("failed")

        self.servo.set.side_effect = mock_set
        self.flasher.prepare_dut()

    def test_prepare_dut_other_raiden(self):
        self.args.chip = "other"
        self.args.board = "other"
        self.args.raiden = True
        self.flasher.prepare_dut()
        self.servo.set.assert_any_call("cold_reset", "on")

    def test_cleanup_dut(self):
        self.flasher.cleanup_dut()

    @patch("util.flash.flashrom_flasher.locate_tool")
    @patch("os.path.exists")
    def test_execute_flash_no_read_no_image(self, mock_exists, mock_locate):
        self.args.read = None
        self.args.image = "nonexistent.bin"
        mock_exists.return_value = False
        with self.assertRaises(FileNotFoundError):
            self.flasher.execute_flash()

    @patch("util.flash.flashrom_flasher.locate_tool")
    @patch("subprocess.run")
    def test_execute_flash_read(self, mock_run, mock_locate):
        mock_locate.return_value = "flashrom"
        self.args.read = "read.bin"
        self.args.image = None
        self.servo.get.return_value = "servo_v3"
        self.flasher.execute_flash()
        mock_run.assert_called_once_with(
            ["sudo", "flashrom", "--programmer=linux_spi", "-r", "read.bin"],
            check=True,
        )

    @patch("util.flash.flashrom_flasher.locate_tool")
    @patch("subprocess.run")
    @patch("subprocess.check_output")
    @patch("os.path.getsize")
    @patch("os.path.exists")
    @patch("tempfile.TemporaryDirectory")
    @patch("builtins.open", new_callable=mock_open, read_data=b"testdata")
    @patch("util.flash.flashrom_flasher.preserve_sections_with_cur_image")
    def test_execute_flash_write_servov3(
        self,
        mock_preserve,
        mock_file,
        mock_tmp,
        mock_exists,
        mock_getsize,
        mock_check_output,
        mock_run,
        mock_locate,
    ):
        mock_locate.return_value = "flashrom"
        mock_exists.return_value = True
        self.args.read = None
        self.args.image = "image.bin"
        self.args.chip = "npcx_int_spi"  # append_ff = True
        self.args.no_preserve = False
        self.servo.get.return_value = "servo_v3"

        mock_getsize.return_value = 8
        mock_check_output.return_value = "something\n16\n"

        mock_tmp_instance = MagicMock()
        mock_tmp_instance.__enter__.return_value = "/tmp/dir"
        mock_tmp.return_value = mock_tmp_instance

        self.flasher.execute_flash()

        mock_check_output.assert_called_once_with(
            ["sudo", "flashrom", "--programmer=linux_spi", "--flash-size"],
            encoding="utf-8",
        )
        self.assertTrue(mock_run.call_count >= 2)
        mock_file.return_value.write.assert_has_calls(
            [call(b"testdata"), call(b"\xff" * 8)], any_order=False
        )

    @patch("util.flash.flashrom_flasher.locate_tool")
    @patch("subprocess.run")
    @patch("subprocess.check_output")
    @patch("os.path.getsize")
    @patch("os.path.exists")
    @patch("tempfile.TemporaryDirectory")
    @patch("builtins.open", new_callable=mock_open, read_data=b"testdata")
    def test_execute_flash_write_raiden_micro(
        self,
        mock_file,
        mock_tmp,
        mock_exists,
        mock_getsize,
        mock_check_output,
        mock_run,
        mock_locate,
    ):
        mock_locate.return_value = "flashrom"
        mock_exists.return_value = True
        self.args.read = None
        self.args.image = "image.bin"
        self.args.chip = "other"  # append_ff = False
        self.args.no_preserve = True
        self.servo.get.return_value = "servo_micro"
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
                "--programmer=raiden_debug_spi",
                "-w",
                "/tmp/dir/flash_temp.bin", "-n",
            ],
            check=True,
        )

    @patch("util.flash.flashrom_flasher.locate_tool")
    @patch("subprocess.run")
    @patch("subprocess.check_output")
    @patch("os.path.getsize")
    @patch("os.path.exists")
    @patch("tempfile.TemporaryDirectory")
    @patch("builtins.open", new_callable=mock_open, read_data=b"testdata")
    def test_execute_flash_write_raiden_ccd(
        self,
        mock_file,
        mock_tmp,
        mock_exists,
        mock_getsize,
        mock_check_output,
        mock_run,
        mock_locate,
    ):
        mock_locate.return_value = "flashrom"
        mock_exists.return_value = True
        self.args.read = None
        self.args.image = "image.bin"
        self.args.chip = "other"  # append_ff = False
        self.args.no_preserve = True
        self.servo.get.return_value = "ccd"
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
                "/tmp/dir/flash_temp.bin", "-n",
            ],
            check=True,
        )
        mock_file.return_value.write.assert_has_calls(
            [call(b"testdata")], any_order=False
        )
        self.assertEqual(mock_file.return_value.write.call_count, 1)

    @patch("util.flash.flashrom_flasher.locate_tool")
    @patch("subprocess.run")
    @patch("subprocess.check_output")
    @patch("os.path.getsize")
    @patch("os.path.exists")
    @patch("tempfile.TemporaryDirectory")
    @patch("builtins.open", new_callable=mock_open, read_data=b"testdata")
    def test_execute_flash_write_v2_serial(
        self,
        mock_file,
        mock_tmp,
        mock_exists,
        mock_getsize,
        mock_check_output,
        mock_run,
        mock_locate,
    ):
        mock_locate.return_value = "flashrom"
        mock_exists.return_value = True
        self.args.read = None
        self.args.image = "image.bin"
        self.args.chip = "other"  # append_ff = False
        self.args.no_preserve = True
        self.args.raiden = False

        def mock_servo_get(key, default=""):
            if key == "servo_type":
                return "servo_v2"
            if key == "serialname":
                return "12345"
            return default

        self.servo.get.side_effect = mock_servo_get

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
                "--programmer=ft2232_spi:type=google-servo-v2,port=B,serial=12345",
                "-w",
                "/tmp/dir/flash_temp.bin", "-n",
            ],
            check=True,
        )

    @patch("util.flash.flashrom_flasher.locate_tool")
    @patch("subprocess.check_output")
    @patch("os.path.getsize")
    @patch("os.path.exists")
    def test_execute_flash_size_0(
        self, mock_exists, mock_getsize, mock_check_output, mock_locate
    ):
        mock_locate.return_value = "flashrom"
        mock_exists.return_value = True
        self.args.read = None
        self.args.image = "image.bin"
        self.servo.get.return_value = "servo_v3"

        mock_getsize.return_value = 8
        mock_check_output.return_value = "something\n0\n"

        with self.assertRaises(ValueError) as cm:
            self.flasher.execute_flash()
        self.assertIn("Chip size is 0!", str(cm.exception))

    @patch("util.flash.flashrom_flasher.locate_tool")
    @patch("subprocess.check_output")
    @patch("os.path.getsize")
    @patch("os.path.exists")
    def test_execute_flash_img_too_large(
        self, mock_exists, mock_getsize, mock_check_output, mock_locate
    ):
        mock_locate.return_value = "flashrom"
        mock_exists.return_value = True
        self.args.read = None
        self.args.image = "image.bin"
        self.servo.get.return_value = "servo_v3"

        mock_getsize.return_value = 16
        mock_check_output.return_value = "something\n8\n"

        with self.assertRaises(ValueError) as cm:
            self.flasher.execute_flash()
        self.assertIn("Image is larger than SPI size!", str(cm.exception))

    @patch("util.flash.flashrom_flasher.locate_tool")
    @patch("subprocess.run")
    @patch("subprocess.check_output")
    @patch("os.path.getsize")
    @patch("os.path.exists")
    @patch("tempfile.TemporaryDirectory")
    @patch("builtins.open", new_callable=mock_open, read_data=b"testdata")
    @patch("util.flash.flashrom_flasher.preserve_sections_with_cur_image")
    def test_execute_flash_preserve_exception(
        self,
        mock_preserve,
        mock_file,
        mock_tmp,
        mock_exists,
        mock_getsize,
        mock_check_output,
        mock_run,
        mock_locate,
    ):
        mock_locate.return_value = "flashrom"
        mock_exists.return_value = True
        self.args.read = None
        self.args.image = "image.bin"
        self.args.chip = "npcx_int_spi"  # append_ff = True
        self.args.no_preserve = False
        self.servo.get.return_value = "servo_v3"

        mock_getsize.return_value = 8
        mock_check_output.return_value = "something\n16\n"

        mock_tmp_instance = MagicMock()
        mock_tmp_instance.__enter__.return_value = "/tmp/dir"
        mock_tmp.return_value = mock_tmp_instance

        mock_run.side_effect = [
            subprocess.CalledProcessError(1, "flashrom"),
            None,
        ]

        self.flasher.execute_flash()

    @patch("util.flash.flashrom_flasher.locate_tool")
    @patch("subprocess.run")
    @patch("subprocess.check_output")
    @patch("os.path.getsize")
    @patch("os.path.exists")
    @patch("tempfile.TemporaryDirectory")
    @patch("builtins.open", new_callable=mock_open, read_data=b"testdata")
    def test_execute_flash_write_v2_no_serial(
        self,
        mock_file,
        mock_tmp,
        mock_exists,
        mock_getsize,
        mock_check_output,
        mock_run,
        mock_locate,
    ):
        mock_locate.return_value = "flashrom"
        mock_exists.return_value = True
        self.args.read = None
        self.args.image = "image.bin"
        self.args.chip = "other"
        self.args.no_preserve = True
        self.args.raiden = False

        def mock_servo_get(key, default=""):
            if key == "servo_type":
                return "servo_v2"
            return default

        self.servo.get.side_effect = mock_servo_get
        mock_getsize.return_value = 8
        mock_check_output.return_value = "something\n16\n"
        mock_tmp_instance = MagicMock()
        mock_tmp_instance.__enter__.return_value = "/tmp/dir"
        mock_tmp.return_value = mock_tmp_instance

        self.flasher.execute_flash()

        mock_run.assert_called_with(
            [
                "sudo",
                "flashrom",
                "--programmer=ft2232_spi:type=google-servo-v2,port=B",
                "-w",
                "/tmp/dir/flash_temp.bin", "-n",
            ],
            check=True,
        )
        mock_file.return_value.write.assert_has_calls(
            [call(b"\xff" * 8), call(b"testdata")], any_order=False
        )
