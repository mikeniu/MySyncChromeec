#!/usr/bin/env vpython3
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Unit tests for RtkFlasher."""

import unittest
from unittest.mock import call
from unittest.mock import MagicMock
from unittest.mock import patch

from flash.rtk_flasher import RtkFlasher
from flash.servo_utils import ServoDriver


class TestRtkFlasher(unittest.TestCase):
    """Tests for RtkFlasher UART preparation and rtkupdate execution."""

    def setUp(self) -> None:
        self.servo = MagicMock(spec=ServoDriver)
        self.args = MagicMock()
        self.args.board = "testboard"
        self.args.image = "/tmp/ec.bin"
        self.args.read = None
        self.args.verify = False
        self.args.no_preserve = True
        self.args.dry_run = False
        self.flasher = RtkFlasher(self.servo, self.args)

    @patch("time.sleep")
    def test_prepare_and_cleanup(self, _mock_sleep: MagicMock) -> None:
        """Verify ec_boot_mode and cold_reset sequence on prepare/cleanup."""
        self.flasher.prepare_dut()
        self.servo.set.assert_any_call("ec_boot_mode", "on")
        self.servo.set.assert_any_call("ec_boot_mode", "off")
        self.flasher.cleanup_dut()
        self.servo.set_many.assert_called_with(
            [("cold_reset", "on"), ("cold_reset", "off")]
        )

    @patch("os.path.isfile", return_value=True)
    @patch("os.path.exists", return_value=True)
    @patch("flash.rtk_flasher.locate_tool", return_value="/mock/rtkupdate")
    @patch("subprocess.run")
    def test_execute_flash_write(
        self,
        mock_run: MagicMock,
        _mock_locate: MagicMock,
        _mock_exists: MagicMock,
        _mock_isfile: MagicMock,
    ) -> None:
        """Verify rtkupdate wp, frame, and flash command invocations."""
        self.servo.get.return_value = "/dev/pts/6"
        self.flasher.execute_flash()
        mock_run.assert_has_calls(
            [
                call(
                    [
                        "/mock/rtkupdate",
                        "--method=wp",
                        "--protect=0",
                        "--uart_device=/dev/pts/6",
                    ],
                    check=True,
                ),
                call(
                    [
                        "/mock/rtkupdate",
                        "--method=frame",
                        "--file=/tmp/rts5915_flash_upload.bin",
                        "--uart_device=/dev/pts/6",
                    ],
                    check=True,
                ),
                call(
                    [
                        "/mock/rtkupdate",
                        "--method=flash",
                        "--spi_start=0x00000000",
                        "--file=/tmp/ec.bin",
                        "--uart_device=/dev/pts/6",
                    ],
                    check=True,
                ),
            ]
        )

    @patch(
        "flash.rtk_flasher.resolve_image_writable_ranges",
        return_value=(
            {"CBI": MagicMock(offset=0x50000, size=0x1000)},
            0x100000,
            [(0, 0x50000), (0x51000, 0x100000)],
        ),
    )
    @patch("flash.rtk_flasher.preserve_sections_with_cur_image")
    @patch("os.path.isfile", return_value=True)
    @patch("os.path.exists", return_value=True)
    @patch("flash.rtk_flasher.locate_tool", return_value="/mock/rtkupdate")
    @patch("subprocess.run")
    def test_execute_flash_write_skips_cbi_via_slices(
        self,
        mock_run: MagicMock,
        _mock_locate: MagicMock,
        _mock_exists: MagicMock,
        _mock_isfile: MagicMock,
        mock_preserve: MagicMock,
        _mock_resolve: MagicMock,
    ) -> None:
        """Verify RtkFlasher writes slices around CBI without reading it."""
        self.args.no_preserve = False
        self.servo.get.return_value = "/dev/pts/6"
        with patch("builtins.open", unittest.mock.mock_open(read_data=b"\xff")):
            self.flasher.execute_flash()

        mock_preserve.assert_not_called()
        flash_calls = [
            c.args[0]
            for c in mock_run.call_args_list
            if "--method=flash" in c.args[0]
        ]
        self.assertEqual(len(flash_calls), 2)
        self.assertIn("--spi_start=0x00000000", flash_calls[0])
        self.assertIn("--spi_start=0x00051000", flash_calls[1])


if __name__ == "__main__":
    unittest.main()
