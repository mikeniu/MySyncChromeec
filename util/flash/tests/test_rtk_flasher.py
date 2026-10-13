#!/usr/bin/env vpython3
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Unit tests for RtkFlasher."""

import unittest
from unittest.mock import call
from unittest.mock import MagicMock
from unittest.mock import mock_open
from unittest.mock import patch

from flash.rtk_flasher import RtkFlasher
from flash.servo_utils import ServoDriver


def _open_same_bytes(*_args, **_kwargs) -> MagicMock:
    """Return a fresh file handle so the image and read-back match."""
    return mock_open(read_data=b"\xaa" * 16)()


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
        self.servo.save_state.assert_any_call("ec_uart_baudrate")
        self.servo.save_state.assert_any_call("ec_uart_parity")
        self.servo.set.assert_any_call("ec_uart_baudrate", "115200")
        self.servo.set.assert_any_call("ec_uart_parity", "none")
        self.flasher.cleanup_dut()
        self.servo.restore_state.assert_called_once()
        self.servo.set_many.assert_called_with(
            [("cold_reset", "on"), ("cold_reset", "off")]
        )

    @patch("os.path.isfile", return_value=True)
    @patch("flash.rtk_flasher.locate_tool", return_value="/mock/rtkupdate")
    @patch("subprocess.run")
    def test_execute_flash_read(
        self,
        mock_run: MagicMock,
        _mock_locate: MagicMock,
        _mock_isfile: MagicMock,
    ) -> None:
        """Read the whole RTK flash with --read."""
        self.args.read = "/tmp/read.bin"
        self.servo.get.return_value = "/dev/pts/6"
        self.flasher.execute_flash()
        mock_run.assert_called_with(
            [
                "/mock/rtkupdate",
                "--method=read_bin",
                "--spi_start=0x00000000",
                "--file=/tmp/read.bin",
                "--uart_device=/dev/pts/6",
                "--bin_length=0x100000",
            ],
            check=True,
        )

    @patch(
        "flash.rtk_flasher.preserve_sections_with_cur_image",
        return_value=False,
    )
    @patch("flash.rtk_flasher.shutil.copyfile")
    @patch(
        "flash.rtk_flasher.open",
        side_effect=_open_same_bytes,
        create=True,
    )
    @patch("os.path.getsize", return_value=0x80000)
    @patch("os.path.isfile", return_value=True)
    @patch("os.path.exists", return_value=True)
    @patch("flash.rtk_flasher.locate_tool", return_value="/mock/rtkupdate")
    @patch("subprocess.run")
    def test_execute_flash_reads_back_image_size(
        self,
        mock_run: MagicMock,
        _mock_locate: MagicMock,
        _mock_exists: MagicMock,
        _mock_isfile: MagicMock,
        _mock_getsize: MagicMock,
        _mock_file: MagicMock,
        _mock_copy: MagicMock,
        _mock_preserve: MagicMock,
    ) -> None:
        """Read back only the image size when preserving and verifying."""
        self.args.no_preserve = False
        self.args.verify = True
        self.servo.get.return_value = "/dev/pts/6"
        self.flasher.execute_flash()
        read_cmds = [
            c.args[0]
            for c in mock_run.call_args_list
            if "--method=read_bin" in c.args[0]
        ]
        self.assertEqual(len(read_cmds), 2)
        for cmd in read_cmds:
            self.assertIn("--bin_length=0x80000", cmd)

    @patch("os.path.getsize", return_value=0x80000)
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
        _mock_getsize: MagicMock,
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


if __name__ == "__main__":
    unittest.main()
