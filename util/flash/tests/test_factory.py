#!/usr/bin/env vpython3
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
import os
import unittest
from unittest.mock import MagicMock, call, patch, mock_open

from util.flash.factory import FlasherFactory, query_ec_source_tree
from util.flash.flashrom_flasher import FlashromFlasher
from util.flash.ite_flasher import IteI2cFlasher, IteUartFlasher
from util.flash.mchp_flasher import MchpFlasher
from util.flash.npcx_flasher import NpcxJtagFlasher, NpcxSpiFlasher, NpcxUutFlasher
from util.flash.openocd_flasher import OpenOcdFlasher
from util.flash.rtk_flasher import RtkFlasher
from util.flash.stm32_flasher import Stm32DfuFlasher, Stm32UartFlasher
from util.flash.servo_utils import ServoDriver

class TestQueryEcSourceTree(unittest.TestCase):

    @patch("util.flash.factory.os.path.isdir")
    def test_query_no_prog_dir(self, mock_isdir):
        mock_isdir.return_value = False
        self.assertIsNone(query_ec_source_tree("myboard", "/path/to/ec"))

    @patch("util.flash.factory.os.path.isdir")
    @patch("util.flash.factory.os.listdir")
    @patch("util.flash.factory.os.path.exists")
    def test_query_no_build_py(self, mock_exists, mock_listdir, mock_isdir):
        mock_isdir.return_value = True
        mock_listdir.return_value = ["myprog"]
        mock_exists.return_value = False
        self.assertIsNone(query_ec_source_tree("myboard", "/path/to/ec"))

    @patch("util.flash.factory.os.path.isdir")
    @patch("util.flash.factory.os.listdir")
    @patch("util.flash.factory.os.path.exists")
    def test_query_zephyr_board(self, mock_exists, mock_listdir, mock_isdir):
        mock_isdir.return_value = True
        mock_listdir.return_value = ["myprog"]
        mock_exists.return_value = True
        content = """
        register_project(
            project_name="myboard",
            zephyr_board="mychip/foo",
        )
        """
        with patch("builtins.open", mock_open(read_data=content)):
            chip = query_ec_source_tree("myboard", "/path/to/ec")
            self.assertEqual(chip, "mychip")

    @patch("util.flash.factory.os.path.isdir")
    @patch("util.flash.factory.os.listdir")
    @patch("util.flash.factory.os.path.exists")
    def test_query_chip(self, mock_exists, mock_listdir, mock_isdir):
        mock_isdir.return_value = True
        mock_listdir.return_value = ["myprog"]
        mock_exists.return_value = True
        content = """
        register_project(
            project_name="myboard",
            chip="otherchip/bar",
        )
        """
        with patch("builtins.open", mock_open(read_data=content)):
            chip = query_ec_source_tree("myboard", "/path/to/ec")
            self.assertEqual(chip, "otherchip")

    @patch("util.flash.factory.os.path.isdir")
    @patch("util.flash.factory.os.listdir")
    @patch("util.flash.factory.os.path.exists")
    def test_query_no_match(self, mock_exists, mock_listdir, mock_isdir):
        mock_isdir.return_value = True
        mock_listdir.return_value = ["myprog"]
        mock_exists.return_value = True
        content = """
        register_project(
            project_name="otherboard",
            chip="otherchip/bar",
        )
        """
        with patch("builtins.open", mock_open(read_data=content)):
            chip = query_ec_source_tree("myboard", "/path/to/ec")
            self.assertIsNone(chip)


class TestFactory(unittest.TestCase):
    def setUp(self):
        self.servo = MagicMock(spec=ServoDriver)
        self.args = MagicMock()
        self.args.chip = None
        self.args.interface = None
        self.args.board = "fake_board"
        self.args.ec_source = None

    def test_no_chip_detected(self):
        self.servo.get.side_effect = Exception("failed")
        with self.assertRaises(ValueError):
            FlasherFactory.create(self.servo, self.args)

    def test_chip_from_servo(self):
        self.servo.get.return_value = "it8320"
        self.args.chip = None
        flasher = FlasherFactory.create(self.servo, self.args)
        self.assertIsInstance(flasher, IteUartFlasher)

    @patch("util.flash.factory.query_ec_source_tree")
    def test_ec_source_deduce(self, mock_query):
        self.servo.get.return_value = "unknown"
        self.args.ec_source = "/some/path"
        mock_query.return_value = "it8320"
        flasher = FlasherFactory.create(self.servo, self.args)
        self.assertIsInstance(flasher, IteUartFlasher)

    @patch("util.flash.factory.get_chip_from_board")
    def test_board_map_fallback(self, mock_get):
        self.servo.get.return_value = "unknown"
        mock_get.return_value = "it8320"
        flasher = FlasherFactory.create(self.servo, self.args)
        self.assertIsInstance(flasher, IteUartFlasher)

    def test_interface_dfu(self):
        self.args.chip = "somechip"
        self.args.interface = "dfu"
        flasher = FlasherFactory.create(self.servo, self.args)
        self.assertIsInstance(flasher, Stm32DfuFlasher)

    def test_interface_uart_ite(self):
        self.args.chip = "it8xyz"
        self.args.interface = "uart"
        flasher = FlasherFactory.create(self.servo, self.args)
        self.assertIsInstance(flasher, IteUartFlasher)

    def test_interface_spi_npcx(self):
        self.args.chip = "npcx9"
        self.args.interface = "spi"
        flasher = FlasherFactory.create(self.servo, self.args)
        self.assertIsInstance(flasher, NpcxSpiFlasher)

    def test_ite_i2c_flashrom(self):
        self.args.chip = "it8xxx2"
        self.servo.get.return_value = "servo_micro"
        flasher = FlasherFactory.create(self.servo, self.args)
        self.assertIsInstance(flasher, FlashromFlasher)

    def test_ite_i2c(self):
        self.args.chip = "ite_i2c"
        self.servo.get.return_value = "c2d2"
        flasher = FlasherFactory.create(self.servo, self.args)
        self.assertIsInstance(flasher, IteI2cFlasher)

    def test_ite_uart(self):
        self.args.chip = "ite_uart"
        flasher = FlasherFactory.create(self.servo, self.args)
        self.assertIsInstance(flasher, IteUartFlasher)

    def test_stm32_dfu(self):
        self.args.chip = "stm32_dfu"
        flasher = FlasherFactory.create(self.servo, self.args)
        self.assertIsInstance(flasher, Stm32DfuFlasher)

    def test_stm32_uart(self):
        self.args.chip = "stm32"
        flasher = FlasherFactory.create(self.servo, self.args)
        self.assertIsInstance(flasher, Stm32UartFlasher)

    def test_npcx_uut(self):
        self.args.chip = "npcx_uut"
        flasher = FlasherFactory.create(self.servo, self.args)
        self.assertIsInstance(flasher, NpcxUutFlasher)

    def test_npcx_jtag(self):
        self.args.chip = "npcx_jtag"
        flasher = FlasherFactory.create(self.servo, self.args)
        self.assertIsInstance(flasher, NpcxJtagFlasher)
        self.args.chip = "5m5g_jtag"
        flasher = FlasherFactory.create(self.servo, self.args)
        self.assertIsInstance(flasher, NpcxJtagFlasher)

    def test_npcx(self):
        self.args.chip = "npcx"
        flasher = FlasherFactory.create(self.servo, self.args)
        self.assertIsInstance(flasher, NpcxSpiFlasher)

    def test_openocd(self):
        self.args.chip = "openocd"
        flasher = FlasherFactory.create(self.servo, self.args)
        self.assertIsInstance(flasher, OpenOcdFlasher)

    def test_mchp(self):
        self.args.chip = "mchp"
        flasher = FlasherFactory.create(self.servo, self.args)
        self.assertIsInstance(flasher, MchpFlasher)

    def test_rtk(self):
        self.args.chip = "rtk"
        flasher = FlasherFactory.create(self.servo, self.args)
        self.assertIsInstance(flasher, RtkFlasher)
        
    def test_spi_fallback(self):
        self.args.chip = "somerandomspi"
        self.args.interface = "spi"
        flasher = FlasherFactory.create(self.servo, self.args)
        self.assertIsInstance(flasher, FlashromFlasher)
        
    def test_invalid_strategy(self):
        self.args.chip = "invalid_chip"
        self.args.interface = "invalid"
        with self.assertRaises(ValueError):
            FlasherFactory.create(self.servo, self.args)

if __name__ == "__main__":
    unittest.main()
