#!/usr/bin/env vpython3
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Unit tests for NpcxSpiFlasher, NpcxUutFlasher, and NpcxJtagFlasher."""

import unittest
from unittest.mock import call
from unittest.mock import MagicMock
from unittest.mock import patch

from flash.npcx_flasher import NpcxJtagFlasher
from flash.npcx_flasher import NpcxSpiFlasher
from flash.npcx_flasher import NpcxUutFlasher
from flash.servo_utils import ServoDriver


class TestNpcxSpiFlasher(unittest.TestCase):
    """Tests for NpcxSpiFlasher."""

    def setUp(self) -> None:
        self.servo = MagicMock(spec=ServoDriver)
        self.args = MagicMock()
        self.args.board = "brya"
        self.args.chip = "npcx9"
        self.args.image = "/tmp/ec.bin"
        self.args.read = None
        self.args.dry_run = True
        self.args.raiden = True
        self.flasher = NpcxSpiFlasher(self.servo, self.args)

    def test_prepare_dut_sets_npcx_int_spi(self) -> None:
        """Verify prepare_dut sets chip to npcx_int_spi when not npcx_spi."""
        self.servo.get.return_value = "servo_v4"
        self.flasher.prepare_dut()
        self.assertEqual(self.args.chip, "npcx_int_spi")
        self.servo.set.assert_any_call("fw_up", "on")

    def test_prepare_dut_preserves_npcx_spi(self) -> None:
        """Verify prepare_dut preserves npcx_spi and enables dev_mode."""
        self.args.chip = "npcx_spi"
        self.servo.get.return_value = "servo_v4"
        self.flasher.prepare_dut()
        self.assertEqual(self.args.chip, "npcx_spi")
        self.servo.set.assert_any_call("dev_mode", "on")

    @patch("flash.flashrom_flasher.FlashromFlasher.execute_flash")
    def test_execute_flash_writes_image_unchanged(
        self, mock_execute: MagicMock
    ) -> None:
        """Hand flashrom the image as-is, like flash_ec's flash_npcx_spi."""
        self.flasher.execute_flash()
        mock_execute.assert_called_once_with()
        self.assertEqual(self.args.image, "/tmp/ec.bin")


class TestNpcxUutFlasher(unittest.TestCase):
    """Tests for NpcxUutFlasher."""

    def setUp(self) -> None:
        self.servo = MagicMock(spec=ServoDriver)
        self.args = MagicMock()
        self.args.board = "brya"
        self.args.image = "/tmp/ec.bin"
        self.args.read = None
        self.args.verify = False
        self.args.no_preserve = True
        self.args.ro = False
        self.args.rw = False
        self.args.verbose = False
        self.args.dry_run = False
        self.args.offset = 0
        self.flasher = NpcxUutFlasher(self.servo, self.args)

    def test_prepare_and_cleanup_ccd(self) -> None:
        """Verify CCD UUT boot0 strapping and EC reset sequence."""
        self.servo.get.return_value = "servo_v4_with_ccd_cr50"
        self.flasher.prepare_dut()
        self.servo.set.assert_any_call("ccd_ec_boot_mode_uut", "on")
        self.servo.set.assert_any_call("ccd_ec_boot_mode_uut", "off")
        self.flasher.cleanup_dut()
        self.servo.restore_state.assert_called_once()
        self.servo.set_many.assert_called_with(
            [("cold_reset", "on"), ("cold_reset", "off")]
        )

    @patch("os.path.isfile", return_value=True)
    @patch("os.path.exists", return_value=True)
    @patch(
        "flash.npcx_flasher.locate_tool",
        return_value="/mock/uartupdatetool",
    )
    @patch("subprocess.run")
    def test_execute_flash_write(
        self,
        mock_run: MagicMock,
        _mock_locate: MagicMock,
        _mock_exists: MagicMock,
        _mock_isfile: MagicMock,
    ) -> None:
        """Verify uartupdatetool monitor load and auto write invocations."""
        self.servo.get.return_value = "/dev/pts/3"
        self.flasher.execute_flash()
        mock_run.assert_has_calls(
            [
                call(
                    [
                        "/mock/uartupdatetool",
                        "--port=pts/3",
                        "--baudrate=115200",
                        "--opr=wr",
                        "--addr=0x200C3020",
                        "--file=/tmp/chip/npcx/spiflashfw/npcx_monitor.bin",
                    ],
                    check=True,
                ),
                call(
                    [
                        "/mock/uartupdatetool",
                        "--port=pts/3",
                        "--baudrate=115200",
                        "--auto",
                        "--offset=0",
                        "--file=/tmp/ec.bin",
                    ],
                    check=True,
                ),
            ]
        )

    @patch(
        "flash.npcx_flasher.resolve_image_writable_ranges",
        return_value=(
            {"CBI": MagicMock(offset=0x3E000, size=0x1000)},
            0x80000,
            [(0, 0x3E000), (0x3F000, 0x80000)],
        ),
    )
    @patch("flash.npcx_flasher.preserve_sections_with_cur_image")
    @patch("os.path.isfile", return_value=True)
    @patch("os.path.exists", return_value=True)
    @patch(
        "flash.npcx_flasher.locate_tool",
        return_value="/mock/uartupdatetool",
    )
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
        """Verify NpcxUutFlasher writes slices around CBI without reading it."""
        self.args.no_preserve = False
        self.servo.get.return_value = "/dev/pts/3"
        with patch("builtins.open", unittest.mock.mock_open(read_data=b"\xff")):
            self.flasher.execute_flash()

        mock_preserve.assert_not_called()
        write_calls = [
            c.args[0]
            for c in mock_run.call_args_list
            if "--auto" in c.args[0] and "--opr=rd" not in c.args[0]
        ]
        self.assertEqual(len(write_calls), 2)
        self.assertIn("--offset=0", write_calls[0])
        self.assertIn(f"--offset={0x3F000}", write_calls[1])

    @patch(
        "flash.npcx_flasher.resolve_image_writable_ranges",
        return_value=(
            {"EC_RO": MagicMock(offset=0, size=0x40000)},
            0x80000,
            [(0, 0x80000)],
        ),
    )
    @patch("os.path.isfile", return_value=True)
    @patch("os.path.exists", return_value=True)
    @patch(
        "flash.npcx_flasher.locate_tool",
        return_value="/mock/uartupdatetool",
    )
    @patch("subprocess.run")
    def test_execute_flash_writes_whole_image_when_nothing_to_skip(
        self,
        mock_run: MagicMock,
        _mock_locate: MagicMock,
        _mock_exists: MagicMock,
        _mock_isfile: MagicMock,
        _mock_resolve: MagicMock,
    ) -> None:
        """Write the whole image if it has an FMAP but nothing to skip."""
        self.args.no_preserve = False
        self.servo.get.return_value = "/dev/pts/3"
        self.flasher.execute_flash()

        write_calls = [
            c.args[0] for c in mock_run.call_args_list if "--auto" in c.args[0]
        ]
        self.assertEqual(len(write_calls), 1)
        self.assertIn("--offset=0", write_calls[0])
        self.assertIn("--file=/tmp/ec.bin", write_calls[0])


class TestNpcxJtagFlasher(unittest.TestCase):
    """Tests for NpcxJtagFlasher."""

    def setUp(self) -> None:
        self.servo = MagicMock(spec=ServoDriver)
        self.args = MagicMock()
        self.args.board = "testboard"
        self.args.chip = "npcx_5m5g_jtag"
        self.args.image = "/build/testboard/ec.bin"
        self.args.read = None
        self.args.ro = False
        self.args.rw = False
        self.args.offset = 0
        self.args.timeout = 60
        self.args.dry_run = False
        self.flasher = NpcxJtagFlasher(self.servo, self.args)

    @patch("time.sleep")
    def test_prepare_and_cleanup(self, _mock_sleep: MagicMock) -> None:
        """Verify JTAG vref and buffer controls on prepare and cleanup."""
        self.flasher.prepare_dut()
        self.servo.set.assert_any_call("jtag_buf_en", "on")
        self.flasher.cleanup_dut()
        self.servo.restore_state.assert_called_once()
        self.servo.set.assert_any_call("cold_reset", "off")

    @patch("os.path.exists", return_value=True)
    @patch("subprocess.run")
    def test_execute_flash(
        self, mock_run: MagicMock, _mock_exists: MagicMock
    ) -> None:
        """Run OpenOCD's flash_npcx_all on the image's build directory."""
        self.flasher.execute_flash()
        cmd = mock_run.call_args.args[0]
        self.assertEqual(
            cmd[:8],
            ["sudo", "timeout", "-k", "10", "-s", "9", "60", "openocd"],
        )
        self.assertEqual(cmd[-5:-2], ["servo.cfg", "-f", "npcx_chip.cfg"])
        self.assertEqual(
            cmd[-1],
            "init; flash_npcx_all npcx_5m5g_jtag /build/testboard 0; "
            "shutdown;",
        )

    @patch("os.path.exists", return_value=True)
    @patch("subprocess.run")
    def test_execute_flash_ro(
        self, mock_run: MagicMock, _mock_exists: MagicMock
    ) -> None:
        """Use flash_npcx_ro for --ro, like flash_ec."""
        self.args.ro = True
        self.flasher.execute_flash()
        self.assertIn("flash_npcx_ro", mock_run.call_args.args[0][-1])

    @patch("os.path.exists", return_value=True)
    def test_execute_flash_rw_rejected(self, _mock_exists: MagicMock) -> None:
        """Reject --rw on NPCX JTAG."""
        self.args.rw = True
        with self.assertRaisesRegex(
            ValueError, "--rw is not supported over NPCX JTAG"
        ):
            self.flasher.execute_flash()

    @patch("os.path.exists", return_value=True)
    @patch("subprocess.run")
    def test_execute_flash_rejects_unknown_chip(
        self, mock_run: MagicMock, _mock_exists: MagicMock
    ) -> None:
        """Raise for chips the flash_npcx_* procs would silently skip."""
        self.args.chip = "npcx_jtag"
        with self.assertRaisesRegex(ValueError, "npcx_jtag"):
            self.flasher.execute_flash()
        mock_run.assert_not_called()


if __name__ == "__main__":
    unittest.main()
