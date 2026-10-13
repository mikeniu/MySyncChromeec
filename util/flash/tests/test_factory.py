#!/usr/bin/env vpython3
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Unit tests for FlasherFactory and EC source tree chip deduction."""

import unittest
from unittest.mock import MagicMock
from unittest.mock import mock_open
from unittest.mock import patch

from flash.board_map import get_chip_from_board
from flash.factory import FlasherFactory
from flash.factory import query_ec_source_tree
from flash.flashrom_flasher import FlashromFlasher
from flash.ite_flasher import IteI2cFlasher
from flash.ite_flasher import IteUartFlasher
from flash.mchp_flasher import MchpFlasher
from flash.npcx_flasher import NpcxJtagFlasher
from flash.npcx_flasher import NpcxSpiFlasher
from flash.npcx_flasher import NpcxUutFlasher
from flash.servo_utils import ServoDriver
from flash.servo_utils import ServoError
from flash.stm32_flasher import Stm32DfuFlasher
from flash.stm32_flasher import Stm32UartFlasher


class TestQueryEcSourceTree(unittest.TestCase):
    """Tests for query_ec_source_tree."""

    @patch("flash.factory.os.path.isdir")
    def test_query_no_prog_dir(self, mock_isdir: MagicMock) -> None:
        """Return None when zephyr/program directory does not exist."""
        mock_isdir.return_value = False
        self.assertIsNone(query_ec_source_tree("myboard", "/path/to/ec"))

    @patch("flash.factory.os.path.isdir")
    @patch("flash.factory.os.listdir")
    @patch("flash.factory.os.path.exists")
    def test_query_no_build_py(
        self,
        mock_exists: MagicMock,
        mock_listdir: MagicMock,
        mock_isdir: MagicMock,
    ) -> None:
        """Return None when BUILD.py does not exist in program directory."""
        mock_isdir.return_value = True
        mock_listdir.return_value = ["myprog"]
        mock_exists.return_value = False
        self.assertIsNone(query_ec_source_tree("myboard", "/path/to/ec"))

    @patch("flash.factory.os.path.isdir")
    @patch("flash.factory.os.listdir")
    @patch("flash.factory.os.path.exists")
    def test_query_zephyr_board(
        self,
        mock_exists: MagicMock,
        mock_listdir: MagicMock,
        mock_isdir: MagicMock,
    ) -> None:
        """Extract chip prefix from zephyr_board registration."""
        mock_isdir.return_value = True
        mock_listdir.return_value = ["myprog"]
        mock_exists.return_value = True
        content = """
        register_ish_project(
            project_name="myboard",
            zephyr_board="mychip/foo",
        )
        """
        with patch("builtins.open", mock_open(read_data=content)):
            chip = query_ec_source_tree("myboard", "/path/to/ec")
            self.assertEqual(chip, "mychip")

    @patch("flash.factory.os.path.isdir")
    @patch("flash.factory.os.listdir")
    @patch("flash.factory.os.path.exists")
    def test_query_chip(
        self,
        mock_exists: MagicMock,
        mock_listdir: MagicMock,
        mock_isdir: MagicMock,
    ) -> None:
        """Extract chip prefix from chip registration."""
        mock_isdir.return_value = True
        mock_listdir.return_value = ["myprog"]
        mock_exists.return_value = True
        content = """
        register_npcx_project(
            project_name="myboard",
            chip="otherchip/bar",
        )
        """
        with patch("builtins.open", mock_open(read_data=content)):
            chip = query_ec_source_tree("myboard", "/path/to/ec")
            self.assertEqual(chip, "otherchip")

    @patch("flash.factory.os.path.isdir")
    @patch("flash.factory.os.listdir")
    @patch("flash.factory.os.path.exists")
    def test_query_no_match(
        self,
        mock_exists: MagicMock,
        mock_listdir: MagicMock,
        mock_isdir: MagicMock,
    ) -> None:
        """Return None when project_name does not match."""
        mock_isdir.return_value = True
        mock_listdir.return_value = ["myprog"]
        mock_exists.return_value = True
        content = """
        register_npcx_project(
            project_name="otherboard",
            chip="otherchip/bar",
        )
        """
        with patch("builtins.open", mock_open(read_data=content)):
            chip = query_ec_source_tree("myboard", "/path/to/ec")
            self.assertIsNone(chip)


class TestFactory(unittest.TestCase):
    """Tests for FlasherFactory chip detection and stubbed dispatch."""

    def setUp(self) -> None:
        self.servo = MagicMock(spec=ServoDriver)
        self.args = MagicMock()
        self.args.chip = None
        self.args.interface = None
        self.args.board = "fake_board"
        self.args.ec_source = None

    def test_no_chip_detected(self) -> None:
        """Raise ValueError when chip cannot be determined."""
        self.servo.get.side_effect = ServoError("failed")
        with self.assertRaises(ValueError):
            FlasherFactory.create(self.servo, self.args)

    def test_stubbed_chip_raises_not_implemented(self) -> None:
        """Raise NotImplementedError for detected chip in base skeleton."""
        self.servo.get.return_value = "unsupported_chip"
        with self.assertRaises(NotImplementedError):
            FlasherFactory.create(self.servo, self.args)
        self.assertEqual(self.args.chip, "unsupported_chip")

    def test_chip_from_servo_ite_uart(self) -> None:
        """Route it8320 from servo ec_chip to IteUartFlasher."""
        self.servo.get.return_value = "it8320"
        flasher = FlasherFactory.create(self.servo, self.args)
        self.assertIsInstance(flasher, IteUartFlasher)

    def test_ite_i2c(self) -> None:
        """Route ite_i2c and it8xxx2 over CCD to IteI2cFlasher."""
        self.args.chip = "ite_i2c"
        self.servo.get.return_value = "ccd_cr50"
        flasher = FlasherFactory.create(self.servo, self.args)
        self.assertIsInstance(flasher, IteI2cFlasher)

    def test_npcx_dispatch(self) -> None:
        """Route npcx_uut, npcx_jtag, and npcx to their respective flashers."""
        self.args.chip = "npcx_uut"
        self.assertIsInstance(
            FlasherFactory.create(self.servo, self.args), NpcxUutFlasher
        )
        self.args.chip = "npcx_jtag"
        self.assertIsInstance(
            FlasherFactory.create(self.servo, self.args), NpcxJtagFlasher
        )
        self.args.chip = "npcx_spi"
        self.assertIsInstance(
            FlasherFactory.create(self.servo, self.args), NpcxSpiFlasher
        )
        self.args.chip = "npcx9"
        self.assertIsInstance(
            FlasherFactory.create(self.servo, self.args), NpcxSpiFlasher
        )

    def test_stm32_dispatch(self) -> None:
        """Route stm32_dfu, stm32, and google_fp to STM32 flashers."""
        self.args.chip = "stm32_dfu"
        dfu_flasher = FlasherFactory.create(self.servo, self.args)
        self.assertIsInstance(dfu_flasher, Stm32DfuFlasher)
        self.assertIsNone(dfu_flasher.servo)
        self.args.chip = "stm32"
        self.assertIsInstance(
            FlasherFactory.create(self.servo, self.args), Stm32UartFlasher
        )
        self.args.chip = "google_dragonclaw"
        self.assertIsInstance(
            FlasherFactory.create(self.servo, self.args), Stm32UartFlasher
        )

    def test_mchp_dispatch(self) -> None:
        """Route mchp and mec chips to MchpFlasher."""
        self.args.chip = "mchp"
        self.assertIsInstance(
            FlasherFactory.create(self.servo, self.args), MchpFlasher
        )
        self.args.chip = "mec1727"
        self.assertIsInstance(
            FlasherFactory.create(self.servo, self.args), MchpFlasher
        )

    def test_board_map_lookup(self) -> None:
        """Verify legacy board_map lookup returns expected chip types."""
        self.assertEqual(get_chip_from_board("veluza"), "it83xx")
        self.assertEqual(get_chip_from_board("corsola"), "it83xx")
        self.assertEqual(get_chip_from_board("brya"), "npcx_uut")
        self.assertEqual(get_chip_from_board("buccaneer"), "npcx_spi")
        self.assertEqual(get_chip_from_board("servo_v4p1"), "stm32_dfu")
        self.assertIsNone(get_chip_from_board("nonexistent_board"))

    def test_ite_spi_flashrom_with_servo_micro(self) -> None:
        """Route it8xxx2 to FlashromFlasher when connected via servo_micro."""
        self.args.chip = "it8xxx2"
        self.servo.get.return_value = "servo_micro"
        flasher = FlasherFactory.create(self.servo, self.args)
        self.assertIsInstance(flasher, FlashromFlasher)

    def test_spi_interface_override(self) -> None:
        """Route non-NPCX chip with interface=spi to FlashromFlasher."""
        self.args.chip = "somerandomspi"
        self.args.interface = "spi"
        flasher = FlasherFactory.create(self.servo, self.args)
        self.assertIsInstance(flasher, FlashromFlasher)


if __name__ == "__main__":
    unittest.main()
