#!/usr/bin/env vpython3
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Unit tests for FMAP and host tool helper utilities."""

import os
import subprocess
import unittest
from unittest.mock import MagicMock
from unittest.mock import patch

from flash.helpers import dump_fmap
from flash.helpers import locate_tool
from flash.helpers import preserve_sections_with_cur_image


class TestHelpers(unittest.TestCase):
    """Tests for dump_fmap and preserve_sections_with_cur_image."""

    @patch("flash.helpers.locate_tool")
    @patch("subprocess.check_output")
    def test_dump_fmap_success(
        self, mock_check_output: MagicMock, mock_locate: MagicMock
    ) -> None:
        """Return FMAP output when dump_fmap succeeds."""
        mock_locate.return_value = "/mock/dump_fmap"
        mock_check_output.return_value = "hit FMAP\n"
        ret = dump_fmap("fake.bin")
        mock_check_output.assert_called_once_with(
            ["/mock/dump_fmap", "-e", "fake.bin"], encoding="utf-8"
        )
        self.assertEqual(ret, "hit FMAP\n")

    @patch("flash.helpers.locate_tool")
    @patch("subprocess.check_output")
    def test_dump_fmap_failure(
        self, mock_check_output: MagicMock, mock_locate: MagicMock
    ) -> None:
        """Return empty string when dump_fmap raises CalledProcessError."""
        mock_locate.return_value = "/mock/dump_fmap"
        mock_check_output.side_effect = subprocess.CalledProcessError(
            1, ["dump_fmap"]
        )
        ret = dump_fmap("fake.bin")
        self.assertEqual(ret, "")

    @patch("flash.helpers.locate_tool", return_value="futility")
    @patch("os.path.exists")
    @patch("subprocess.run")
    @patch("shutil.move")
    @patch("flash.helpers.dump_fmap")
    def test_preserve_sections_with_cur_image(
        self,
        mock_dump_fmap: MagicMock,
        mock_move: MagicMock,
        mock_run: MagicMock,
        mock_exists: MagicMock,
        _mock_locate: MagicMock,
    ) -> None:
        """Dump and reload all FMAP sections marked as 'preserve'."""
        mock_exists.return_value = True
        mock_dump_fmap.return_value = (
            "RO_FRID 0x0 0x100 preserve\n"
            "RW_FWID 0x100 0x100 static,preserve\n"
        )
        temp_dir = "/tmp/fakedir"

        result = preserve_sections_with_cur_image(
            "cur.bin", "new.bin", temp_dir
        )
        self.assertTrue(result)
        self.assertEqual(mock_run.call_count, 4)

        mock_run.assert_any_call(
            [
                "futility",
                "dump_fmap",
                "-x",
                os.path.abspath("cur.bin"),
                "RO_FRID",
            ],
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
                os.path.abspath("new.bin"),
                f"RO_FRID:{os.path.join(temp_dir, 'ec.cur.RO_FRID.bin')}",
            ],
            check=True,
            cwd=temp_dir,
        )

    @patch("flash.helpers.dump_fmap")
    def test_preserve_sections_no_preserve(
        self, mock_dump_fmap: MagicMock
    ) -> None:
        """Return False when no FMAP sections are marked as 'preserve'."""
        mock_dump_fmap.return_value = "RO_FRID 0x0 0x100\n"
        result = preserve_sections_with_cur_image(
            "cur.bin", "new.bin", "/tmp/faketmp"
        )
        self.assertFalse(result)

    @patch("flash.helpers.locate_tool", return_value="futility")
    @patch("subprocess.run")
    @patch("flash.helpers.dump_fmap")
    def test_preserve_sections_futility_failure(
        self,
        mock_dump_fmap: MagicMock,
        mock_run: MagicMock,
        _mock_locate: MagicMock,
    ) -> None:
        """Skip the dump_fmap header and explain how to skip preservation."""
        mock_dump_fmap.return_value = (
            "name offset size flags\nCBI 0x0 0x100 preserve\n"
        )
        mock_run.side_effect = subprocess.CalledProcessError(1, "futility")
        with self.assertRaisesRegex(RuntimeError, "--no_preserve"):
            preserve_sections_with_cur_image("cur.bin", "new.bin", "/tmp/t")
        self.assertEqual(mock_run.call_args.args[0][-1], "CBI")

    @patch("shutil.which", return_value="/usr/bin/flashrom")
    def test_locate_tool_path_has_no_empty_entries(
        self, mock_which: MagicMock
    ) -> None:
        """Build PATH without empty entries when $PATH is unset."""
        with patch.dict(os.environ, {}, clear=True):
            locate_tool("flashrom", ec_dir="/ec")
        path = mock_which.call_args.kwargs["path"]
        self.assertEqual(path, "/ec/build/host/util")


if __name__ == "__main__":
    unittest.main()
