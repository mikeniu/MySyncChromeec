#!/usr/bin/env vpython3

"""Module docstring."""

# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
import os
import subprocess  # pylint: disable=wrong-import-order
import unittest
from unittest.mock import patch

from util.flash.helpers import dump_fmap
from util.flash.helpers import preserve_sections_with_cur_image


class TestHelpers(unittest.TestCase):
    """Docstring."""

    @patch("util.flash.helpers.locate_tool")
    @patch("subprocess.check_output")
    def test_dump_fmap_success(self, mock_check_output, mock_locate):
        """Docstring."""
        mock_locate.return_value = "/mock/dump_fmap"
        mock_check_output.return_value = "hit FMAP\n"
        ret = dump_fmap("fake.bin")
        mock_check_output.assert_called_once_with(
            ["/mock/dump_fmap", "-e", "fake.bin"], encoding="utf-8"
        )
        self.assertEqual(ret, "hit FMAP\n")

    @patch("util.flash.helpers.locate_tool")
    @patch("subprocess.check_output")
    def test_dump_fmap_failure(self, mock_check_output, mock_locate):
        """Docstring."""
        mock_locate.return_value = "/mock/dump_fmap"
        mock_check_output.side_effect = subprocess.CalledProcessError(
            1, ["dump_fmap"]
        )
        ret = dump_fmap("fake.bin")
        self.assertEqual(ret, "")

    @patch("subprocess.run")
    @patch("shutil.move")
    @patch("util.flash.helpers.dump_fmap")
    def test_preserve_sections_with_cur_image(
        self, mock_dump_fmap, mock_move, mock_run
    ):
        """Docstring."""
        mock_dump_fmap.return_value = (
            "RO_FRID 0x0 0x100 preserve\nRW_FWID 0x100 0x100 preserve\n"
        )
        temp_dir = "/tmp/fakedir"

        result = preserve_sections_with_cur_image(
            "cur.bin", "new.bin", temp_dir
        )
        self.assertTrue(result)

        # Should be called 4 times total: 2 subprocess calls per section (dump, load)
        self.assertEqual(mock_run.call_count, 4)

        # Verify first section calls
        mock_run.assert_any_call(
            ["futility", "dump_fmap", "-x", "cur.bin", "RO_FRID"],
            check=True,
            cwd=temp_dir,
        )
        mock_move.assert_any_call(
            os.path.join(temp_dir, "RO_FRID"),
            os.path.join(temp_dir, "ec.cur.RO_FRID.bin"),
        )
        mock_run.assert_any_call(
            [
                "futility",
                "load_fmap",
                "new.bin",
                f"RO_FRID:{os.path.join(temp_dir, 'ec.cur.RO_FRID.bin')}",
            ],
            check=True,
            cwd=temp_dir,
        )

    @patch("util.flash.helpers.dump_fmap")
    def test_preserve_sections_no_preserve(self, mock_dump_fmap):
        """Docstring."""
        mock_dump_fmap.return_value = "RO_FRID 0x0 0x100\n"
        result = preserve_sections_with_cur_image(
            "cur.bin", "new.bin", "/tmp/faketmp"
        )
        self.assertFalse(result)
