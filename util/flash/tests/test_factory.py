#!/usr/bin/env vpython3
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Unit tests for FlasherFactory and EC source tree chip deduction."""

import unittest
from unittest.mock import MagicMock
from unittest.mock import mock_open
from unittest.mock import patch

from flash.factory import FlasherFactory
from flash.factory import query_ec_source_tree
from flash.servo_utils import ServoDriver
from flash.servo_utils import ServoError


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
        self.servo.get.return_value = "it8320"
        with self.assertRaises(NotImplementedError):
            FlasherFactory.create(self.servo, self.args)


if __name__ == "__main__":
    unittest.main()
