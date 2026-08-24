#!/usr/bin/env vpython3
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
import unittest
from unittest.mock import MagicMock, patch
import os

from util.flash.helpers import dump_fmap, preserve_sections_with_cur_image

class TestHelpers(unittest.TestCase):
    @patch("subprocess.check_output")
    def test_dump_fmap_success(self, mock_check_output):
        mock_check_output.return_value = "hit FMAP\n"
        ret = dump_fmap("fake.bin")
        mock_check_output.assert_called_once_with(["dump_fmap", "-e", "fake.bin"], encoding="utf-8")
        self.assertEqual(ret, "hit FMAP\n")

    @patch("subprocess.check_output")
    def test_dump_fmap_failure(self, mock_check_output):
        import subprocess
        mock_check_output.side_effect = subprocess.CalledProcessError(1, ["dump_fmap"])
        ret = dump_fmap("fake.bin")
        self.assertEqual(ret, "")

    @patch("subprocess.run")
    @patch("util.flash.helpers.dump_fmap")
    def test_preserve_sections_with_cur_image(self, mock_dump_fmap, mock_run):
        mock_dump_fmap.return_value = "RO_FRID 0x0 0x100 preserve\nRW_FWID 0x100 0x100 preserve\n"
        temp_dir = "/tmp/fakedir"
        
        result = preserve_sections_with_cur_image("cur.bin", "new.bin", temp_dir)
        self.assertTrue(result)
        
        # Should be called 6 times total: 3 per preserved section (dump, mv, load)
        self.assertEqual(mock_run.call_count, 6)
        
        # Verify first section calls
        mock_run.assert_any_call(["futility", "dump_fmap", "-x", "cur.bin", "RO_FRID"], check=True, cwd=temp_dir)
        mock_run.assert_any_call(["mv", "RO_FRID", os.path.join(temp_dir, "ec.cur.RO_FRID.bin")], check=True, cwd=temp_dir)
        mock_run.assert_any_call(["futility", "load_fmap", "new.bin", f"RO_FRID:{os.path.join(temp_dir, 'ec.cur.RO_FRID.bin')}"], check=True, cwd=temp_dir)

    @patch("util.flash.helpers.dump_fmap")
    def test_preserve_sections_no_preserve(self, mock_dump_fmap):
        mock_dump_fmap.return_value = "RO_FRID 0x0 0x100\n"
        result = preserve_sections_with_cur_image("cur.bin", "new.bin", "/tmp/faketmp")
        self.assertFalse(result)
