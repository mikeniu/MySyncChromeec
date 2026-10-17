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
from flash.helpers import FmapArea
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
        self.args.no_preserve = False
        self.args.ro = False
        self.args.rw = False
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

    @patch(
        "flash.flashrom_flasher.parse_fmap_areas",
        return_value={"WP_RO": MagicMock(), "EC_RW": MagicMock()},
    )
    @patch("flash.flashrom_flasher.get_writable_ranges", return_value=[(0, 8)])
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
        _mock_writable: MagicMock,
        _mock_parse_areas: MagicMock,
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

    @patch("flash.flashrom_flasher.parse_fmap_areas")
    @patch("flash.flashrom_flasher.get_writable_ranges")
    @patch("flash.flashrom_flasher.preserve_sections_with_cur_image")
    @patch("flash.flashrom_flasher.locate_tool", return_value="flashrom")
    @patch("subprocess.run")
    @patch("subprocess.check_output", return_value="1048576\n")
    @patch("os.path.getsize", return_value=0x100000)
    @patch("os.path.exists", return_value=True)
    @patch("tempfile.TemporaryDirectory")
    @patch("builtins.open", new_callable=mock_open, read_data=b"testdata")
    def test_execute_flash_write_skips_cbi_via_layout(
        self,
        mock_file: MagicMock,
        mock_tmp: MagicMock,
        _mock_exists: MagicMock,
        _mock_getsize: MagicMock,
        _mock_check_output: MagicMock,
        mock_run: MagicMock,
        _mock_locate: MagicMock,
        mock_preserve: MagicMock,
        mock_writable: MagicMock,
        mock_parse_areas: MagicMock,
    ) -> None:
        """Skip reading/writing CBI by generating a flashrom layout file."""
        self.args.read = None
        self.args.image = "image.bin"
        self.args.chip = "npcx_int_spi"
        self.args.no_preserve = False
        self.args.ro = False
        self.args.rw = False
        self.servo.get.side_effect = lambda ctrl, default="": (
            "ccd" if ctrl == "servo_type" else ""
        )
        mock_parse_areas.return_value = {
            "WP_RO": MagicMock(),
            "CBI": MagicMock(),
            "EC_RW": MagicMock(),
        }
        mock_writable.return_value = [
            (0x0, 0x5E000),
            (0x5F000, 0x60000),
            (0x80000, 0x100000),
        ]
        mock_tmp.return_value.__enter__.return_value = "/tmp/dir"

        self.flasher.execute_flash()

        mock_preserve.assert_not_called()
        mock_writable.assert_called_once_with(
            "image.bin",
            0x100000,
            target_regions=["WP_RO", "EC_RW"],
            no_preserve=False,
            areas=mock_parse_areas.return_value,
        )
        mock_file.return_value.write.assert_any_call(
            "00000000:0005dfff WRITE_0\n"
        )
        mock_file.return_value.write.assert_any_call(
            "0005f000:0005ffff WRITE_1\n"
        )
        mock_file.return_value.write.assert_any_call(
            "00080000:000fffff WRITE_2\n"
        )
        mock_run.assert_called_once_with(
            [
                "sudo",
                "flashrom",
                "--programmer=raiden_debug_spi:target=EC",
                "-i",
                "WRITE_0",
                "-i",
                "WRITE_1",
                "-i",
                "WRITE_2",
                "-l",
                "/tmp/dir/flashrom.layout",
                "--noverify-all",
                "-w",
                "/tmp/dir/flash_temp.bin",
            ],
            check=True,
        )

        # Also verify --ro passes target_regions=['EC_RO'].
        mock_writable.reset_mock()
        mock_run.reset_mock()
        self.args.ro = True
        self.servo.get.side_effect = None
        self.servo.get.return_value = "servo_v3"
        mock_parse_areas.return_value = {}
        mock_writable.return_value = [(0x0, 0x5E000)]

        self.flasher.execute_flash()

        mock_writable.assert_called_once_with(
            "image.bin",
            0x100000,
            target_regions=["EC_RO"],
            no_preserve=False,
            areas={},
        )
        mock_run.assert_called_once_with(
            [
                "sudo",
                "flashrom",
                "--programmer=linux_spi",
                "-i",
                "WRITE_0",
                "-l",
                "/tmp/dir/flashrom.layout",
                "--noverify-all",
                "-w",
                "/tmp/dir/flash_temp.bin",
            ],
            check=True,
        )


class TestFlashromSerial(unittest.TestCase):
    """Tests for selecting the SPI programmer serial number."""

    @patch("flash.flashrom_flasher.locate_tool", return_value="flashrom")
    @patch("subprocess.run")
    def test_read_uses_programmer_serial(
        self, mock_run: MagicMock, _mock_locate: MagicMock
    ) -> None:
        """Verify the serial matches flash_ec's get_serial() per servo type."""
        serials = {
            "serialname": "V4",
            "servo_micro_serialname": "SM",
            "c2d2_serialname": "C2D2",
            "ccd_serialname": "CCD",
        }
        cases = (
            (
                "servo_v4_with_servo_micro_and_ccd_cr50",
                "--programmer=raiden_debug_spi:serial=SM",
            ),
            (
                "servo_v4p1_with_servo_micro",
                "--programmer=raiden_debug_spi:serial=SM",
            ),
            (
                "servo_v4_with_c2d2",
                "--programmer=raiden_debug_spi:target=EC,serial=C2D2",
            ),
            (
                "servo_v4_with_ccd_cr50",
                "--programmer=raiden_debug_spi:target=EC,serial=CCD",
            ),
            ("servo_micro", "--programmer=raiden_debug_spi:serial=V4"),
        )
        for servo_type, programmer in cases:
            with self.subTest(servo_type=servo_type):
                servo = MagicMock()
                servo.get.side_effect = (
                    lambda ctrl, default="", st=servo_type: (
                        st
                        if ctrl == "servo_type"
                        else serials.get(ctrl, default)
                    )
                )
                args = MagicMock(read="out.bin", dry_run=False, raiden=False)
                FlashromFlasher(servo, args).execute_flash()
                mock_run.assert_called_with(
                    ["sudo", "flashrom", programmer, "-r", "out.bin"],
                    check=True,
                )


class TestFlashromColdResetHold(unittest.TestCase):
    """Tests for holding cold_reset during debug-header SPI flashing."""

    def setUp(self) -> None:
        self.servo = MagicMock()
        self.args = MagicMock()
        self.args.raiden = False
        self.flasher = FlashromFlasher(self.servo, self.args)

    @patch("time.sleep")
    def test_prepare_dut_holds_cold_reset_dbg_hdr(
        self, _mock_sleep: MagicMock
    ) -> None:
        """Verify coral/reef keep cold_reset on over a debug header."""
        for servo_type in ("", "servo_micro"):
            for board in ("coral", "reef"):
                for chip in ("npcx_int_spi", "npcx_spi"):
                    with self.subTest(servo=servo_type, board=board, chip=chip):
                        self.servo.reset_mock()
                        self.servo.get.return_value = servo_type
                        self.args.board = board
                        self.args.chip = chip
                        self.flasher.prepare_dut()
                        self.servo.set.assert_any_call("cold_reset", "on")
                        self.assertNotIn(
                            call("cold_reset", "off"),
                            self.servo.set.call_args_list,
                        )

    @patch("time.sleep")
    def test_prepare_dut_releases_cold_reset(
        self, _mock_sleep: MagicMock
    ) -> None:
        """Verify cold_reset is released for other boards and over CCD."""
        for servo_type, board in (("", "other"), ("ccd_cr50", "coral")):
            for chip in ("npcx_int_spi", "npcx_spi"):
                with self.subTest(servo=servo_type, board=board, chip=chip):
                    self.servo.reset_mock()
                    self.servo.get.return_value = servo_type
                    self.args.board = board
                    self.args.chip = chip
                    self.flasher.prepare_dut()
                    self.servo.set.assert_any_call("cold_reset", "off")


class TestFlashromRegionSelection(unittest.TestCase):
    """Tests for FlashromFlasher --ro/--rw and CCD region selection."""

    def setUp(self) -> None:
        self.servo = MagicMock()
        self.servo.get.return_value = "servo_v3"
        self.args = MagicMock()
        self.args.dry_run = False
        self.args.raiden = False
        self.args.no_preserve = False
        self.args.ro = False
        self.args.rw = False
        self.args.read = None
        self.args.image = "image.bin"
        self.flasher = FlashromFlasher(self.servo, self.args)

    @patch("flash.flashrom_flasher.locate_tool", return_value="flashrom")
    @patch("subprocess.check_output", return_value="1048576\n")
    @patch("os.path.getsize", return_value=0x100000)
    @patch("os.path.exists", return_value=True)
    @patch("tempfile.TemporaryDirectory")
    @patch("builtins.open", new_callable=mock_open, read_data=b"testdata")
    def test_rejects_both_ro_and_rw(
        self,
        _mock_file: MagicMock,
        mock_tmp: MagicMock,
        _mock_exists: MagicMock,
        _mock_getsize: MagicMock,
        _mock_check_output: MagicMock,
        _mock_locate: MagicMock,
    ) -> None:
        """Raise when both --ro and --rw are set."""
        mock_tmp.return_value.__enter__.return_value = "/tmp/dir"
        self.args.ro = True
        self.args.rw = True
        with self.assertRaisesRegex(
            ValueError, "Cannot specify both --ro and --rw"
        ):
            self.flasher.execute_flash()

    @patch(
        "flash.flashrom_flasher.parse_fmap_areas",
        return_value={"EC_RO": FmapArea("EC_RO", 0, 0x80000)},
    )
    @patch("flash.flashrom_flasher.locate_tool", return_value="flashrom")
    @patch("subprocess.run")
    @patch("subprocess.check_output", return_value="1048576\n")
    @patch("os.path.getsize", return_value=0x100000)
    @patch("os.path.exists", return_value=True)
    @patch("tempfile.TemporaryDirectory")
    @patch("builtins.open", new_callable=mock_open, read_data=b"testdata")
    def test_rejects_missing_regions(
        self,
        _mock_file: MagicMock,
        mock_tmp: MagicMock,
        _mock_exists: MagicMock,
        _mock_getsize: MagicMock,
        _mock_check_output: MagicMock,
        mock_run: MagicMock,
        _mock_locate: MagicMock,
        _mock_parse_areas: MagicMock,
    ) -> None:
        """Raise instead of writing the whole chip when a region is missing."""
        mock_tmp.return_value.__enter__.return_value = "/tmp/dir"

        self.args.rw = True
        with self.assertRaisesRegex(ValueError, "no EC_RW area"):
            self.flasher.execute_flash()

        self.args.rw = False
        self.servo.get.return_value = "ccd_cr50"
        with self.assertRaisesRegex(ValueError, "no WP_RO area"):
            self.flasher.execute_flash()

        mock_run.assert_not_called()

    @patch(
        "flash.flashrom_flasher.parse_fmap_areas",
        return_value={"EC_RW": FmapArea("EC_RW", 0x80000, 0x80000)},
    )
    @patch("flash.flashrom_flasher.locate_tool", return_value="flashrom")
    @patch("subprocess.run")
    @patch("subprocess.check_output", return_value="1048576\n")
    @patch("os.path.getsize", return_value=0x100000)
    @patch("os.path.exists", return_value=True)
    @patch("tempfile.TemporaryDirectory")
    @patch("builtins.open", new_callable=mock_open, read_data=b"testdata")
    def test_rw_region(
        self,
        mock_file: MagicMock,
        mock_tmp: MagicMock,
        _mock_exists: MagicMock,
        _mock_getsize: MagicMock,
        _mock_check_output: MagicMock,
        mock_run: MagicMock,
        _mock_locate: MagicMock,
        _mock_parse_areas: MagicMock,
    ) -> None:
        """Write only EC_RW for --rw."""
        self.args.rw = True
        mock_tmp.return_value.__enter__.return_value = "/tmp/dir"

        self.flasher.execute_flash()

        mock_file.return_value.write.assert_any_call(
            "00080000:000fffff WRITE_0\n"
        )
        cmd = mock_run.call_args.args[0]
        self.assertEqual(
            cmd[-7:],
            [
                "-i",
                "WRITE_0",
                "-l",
                "/tmp/dir/flashrom.layout",
                "--noverify-all",
                "-w",
                "/tmp/dir/flash_temp.bin",
            ],
        )


if __name__ == "__main__":
    unittest.main()
