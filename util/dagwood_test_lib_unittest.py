#!/usr/bin/env python3
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Unit tests for dagwood_test_lib.py."""

import argparse
import unittest
from unittest import mock

import dagwood_test_lib


class TestDagwoodTestLib(unittest.TestCase):
    """Tests for dagwood_test_lib argument parsing and twister arg generation."""

    def setUp(self):
        self.parser = argparse.ArgumentParser()
        dagwood_test_lib.add_common_args(self.parser)

    @mock.patch("dagwood_test_lib.dagwood_utils")
    def test_default_device_testing(self, mock_dagwood_utils):
        """Test default device testing auto-detects USB device and serial port."""
        mock_dev = mock.Mock()
        mock_dev.serial_number = "BOARD_1234"
        mock_dagwood_utils.find_usb_device.return_value = mock_dev
        mock_dagwood_utils.find_ec_port.return_value = "/dev/ttyACM0"

        args, extra_args = self.parser.parse_known_args(
            ["-p", "it8xxx2/it82002aw", "-s", "aic.fan"]
        )
        twister_args = dagwood_test_lib.get_twister_args(args, extra_args)

        mock_dagwood_utils.find_usb_device.assert_called_once_with(None)
        mock_dagwood_utils.find_ec_port.assert_called_once_with(mock_dev)

        self.assertIn("-p", twister_args)
        self.assertIn("it8xxx2/it82002aw", twister_args)
        self.assertIn("--device-testing", twister_args)
        self.assertIn("--device-serial", twister_args)
        self.assertIn("/dev/ttyACM0", twister_args)
        self.assertIn("--flash-command", twister_args)
        self.assertIn("../dagwood/flash.py", twister_args)
        self.assertIn("--device-flash-timeout", twister_args)
        self.assertIn("60", twister_args)
        self.assertIn("--pytest-args=--board-id=BOARD_1234", twister_args)
        self.assertIn("-s", twister_args)
        self.assertIn("aic.fan", twister_args)
        self.assertNotIn("--hardware-map", twister_args)

    @mock.patch("dagwood_test_lib.dagwood_utils")
    def test_hardware_map(self, mock_dagwood_utils):
        """Test --hardware-map configures hardware map and omits --device-serial."""
        args, extra_args = self.parser.parse_known_args(
            [
                "-p",
                "it8xxx2/it82002aw",
                "-s",
                "aic.fan",
                "--hardware-map",
                "dagwood-hwmap",
            ]
        )
        twister_args = dagwood_test_lib.get_twister_args(args, extra_args)

        # dagwood_utils should not be queried when hardware map is provided
        mock_dagwood_utils.find_usb_device.assert_not_called()
        mock_dagwood_utils.find_ec_port.assert_not_called()

        self.assertIn("-p", twister_args)
        self.assertIn("it8xxx2/it82002aw", twister_args)
        self.assertIn("--device-testing", twister_args)
        self.assertIn("--hardware-map", twister_args)
        self.assertIn("dagwood-hwmap", twister_args)
        self.assertNotIn("--device-serial", twister_args)
        self.assertIn("--flash-command", twister_args)
        self.assertIn("../dagwood/flash.py", twister_args)
        self.assertIn("--device-flash-timeout", twister_args)
        self.assertIn("60", twister_args)
        self.assertIn("-s", twister_args)
        self.assertIn("aic.fan", twister_args)

    @mock.patch("dagwood_test_lib.dagwood_utils")
    def test_hardware_map_in_extra_args(self, mock_dagwood_utils):
        """Test --hardware-map passed via extra_args avoids --device-serial."""
        # Simulate args parsed by a parser that didn't include --hardware-map
        bare_parser = argparse.ArgumentParser()
        bare_parser.add_argument("-p", "--platform", required=True)
        args, extra_args = bare_parser.parse_known_args(
            [
                "-p",
                "it8xxx2/it82002aw",
                "--hardware-map",
                "dagwood-hwmap",
            ]
        )
        twister_args = dagwood_test_lib.get_twister_args(args, extra_args)

        mock_dagwood_utils.find_usb_device.assert_not_called()
        self.assertIn("--device-testing", twister_args)
        self.assertIn("--hardware-map", twister_args)
        self.assertIn("dagwood-hwmap", twister_args)
        self.assertNotIn("--device-serial", twister_args)
        # Verify --hardware-map is not duplicated
        self.assertEqual(twister_args.count("--hardware-map"), 1)

    @mock.patch("dagwood_test_lib.dagwood_utils")
    def test_hardware_map_with_board_id(self, mock_dagwood_utils):
        """Test --hardware-map with explicit --board-id."""
        args, extra_args = self.parser.parse_known_args(
            [
                "-p",
                "realtek/rts5912",
                "--hardware-map",
                "dagwood-hwmap",
                "--board-id",
                "MY_BOARD_ID",
            ]
        )
        twister_args = dagwood_test_lib.get_twister_args(args, extra_args)

        mock_dagwood_utils.find_usb_device.assert_not_called()
        self.assertIn("--hardware-map", twister_args)
        self.assertIn("dagwood-hwmap", twister_args)
        self.assertNotIn("--device-serial", twister_args)
        self.assertIn(
            "../dagwood/flash.py,--board-id,MY_BOARD_ID", twister_args
        )
        self.assertIn("--pytest-args=--board-id=MY_BOARD_ID", twister_args)

    def test_hardware_map_and_device_serial_mutually_exclusive(self):
        """Test that -d and --hardware-map cannot be used together."""
        with self.assertRaises(SystemExit):
            with mock.patch("sys.stderr"):
                self.parser.parse_known_args(
                    [
                        "-p",
                        "it8xxx2/it82002aw",
                        "-d",
                        "/dev/ttyACM0",
                        "--hardware-map",
                        "dagwood-hwmap",
                    ]
                )

    def test_build_only(self):
        """Test -b / --build-only omits device testing parameters."""
        args, extra_args = self.parser.parse_known_args(
            ["-p", "it8xxx2/it82002aw", "-b"]
        )
        twister_args = dagwood_test_lib.get_twister_args(args, extra_args)

        self.assertIn("-b", twister_args)
        self.assertNotIn("--device-testing", twister_args)
        self.assertNotIn("--device-serial", twister_args)
        self.assertNotIn("--hardware-map", twister_args)

    @mock.patch("dagwood_test_lib.dagwood_utils")
    def test_hardware_map_without_platform(self, mock_dagwood_utils):
        """Test --hardware-map works without specifying -p/--platform."""
        args, extra_args = self.parser.parse_known_args(
            [
                "-s",
                "aic.fan",
                "--hardware-map",
                "dagwood-hwmap",
            ]
        )
        twister_args = dagwood_test_lib.get_twister_args(args, extra_args)

        mock_dagwood_utils.find_usb_device.assert_not_called()
        self.assertNotIn("-p", twister_args)
        self.assertIn("--device-testing", twister_args)
        self.assertIn("--hardware-map", twister_args)
        self.assertIn("dagwood-hwmap", twister_args)
        self.assertNotIn("--device-serial", twister_args)
        self.assertIn("-s", twister_args)
        self.assertIn("aic.fan", twister_args)

    @mock.patch("dagwood_test_lib.dagwood_utils")
    def test_hardware_map_with_multiple_platforms(self, mock_dagwood_utils):
        """Test --hardware-map works with multiple -p/--platform arguments."""
        args, extra_args = self.parser.parse_known_args(
            [
                "-p",
                "realtek/rts5912",
                "-p",
                "npcx9/npcx9m7f",
                "--hardware-map",
                "dagwood-hwmap",
            ]
        )
        twister_args = dagwood_test_lib.get_twister_args(args, extra_args)

        mock_dagwood_utils.find_usb_device.assert_not_called()
        self.assertIn("realtek/rts5912", twister_args)
        self.assertIn("npcx9/npcx9m7f", twister_args)
        self.assertEqual(twister_args.count("-p"), 2)
        self.assertIn("--hardware-map", twister_args)

    def test_missing_platform_without_hardware_map(self):
        """Test that omitting -p/--platform without --hardware-map exits with error."""
        args, extra_args = self.parser.parse_known_args(["-s", "aic.fan"])
        with self.assertRaises(SystemExit):
            dagwood_test_lib.get_twister_args(args, extra_args)

    def test_sram_option(self):
        """Test -r / --sram adds SRAM snippet and flash command parameter."""
        args, extra_args = self.parser.parse_known_args(
            ["-p", "npcx9/npcx9m7f", "-d", "/dev/ttyACM0", "-r"]
        )
        twister_args = dagwood_test_lib.get_twister_args(args, extra_args)

        self.assertIn("-x=SNIPPET=sram-only", twister_args)
        self.assertIn("../dagwood/flash.py,-r", twister_args)


if __name__ == "__main__":
    unittest.main()
