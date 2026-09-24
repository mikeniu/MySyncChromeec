#!/usr/bin/env python3
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Unit tests for lcov_stencil.py."""

from collections import defaultdict
import io
from pathlib import Path
import sys
import tempfile
import unittest


# pylint: disable=wrong-import-position
sys.path.insert(0, str(Path(__file__).resolve().parent))
import lcov_stencil


# pylint: enable=wrong-import-position


class TestLcovStencil(unittest.TestCase):
    """Test suite for lcov_stencil functions."""

    def test_parse_template_file(self):
        """Tests parsing line numbers from an LCOV template file."""
        template_content = (
            "TN:my_test\n"
            "SF:/path/to/driver.c\n"
            "FN:10,driver_init\n"
            "FN:25,driver_read\n"
            "FNDA:1,driver_init\n"
            "DA:10,1\n"
            "DA:11,1\n"
            "DA:25,0\n"
            "BRDA:10,0,0,1\n"
            "BRDA:10,0,1,0\n"
            "end_of_record\n"
            "SF:/path/to/stub.c\n"
            "DA:5,0\n"
            "end_of_record\n"
        )
        with tempfile.NamedTemporaryFile("w+", encoding="utf-8") as f:
            f.write(template_content)
            f.flush()
            result = lcov_stencil.parse_template_file(f.name)

        self.assertIn("SF:/path/to/driver.c", result)
        self.assertIn("SF:/path/to/stub.c", result)
        self.assertEqual(result["SF:/path/to/driver.c"], {"10", "11", "25"})
        self.assertEqual(result["SF:/path/to/stub.c"], {"5"})

    def test_filter_coverage_file_matches_and_skips(self):
        """Tests filtering lines and skipping unreferenced files."""
        template_data = defaultdict(
            set,
            {
                "SF:/path/to/board.c": {"15", "16", "20"},
            },
        )
        coverage_content = (
            "TN:unit_test\n"
            "SF:/path/to/board.c\n"
            "FN:15,board_init\n"
            "FN:50,unreferenced_fn\n"
            "FNDA:2,board_init\n"
            "FNDA:1,unreferenced_fn\n"
            "DA:15,2\n"
            "DA:16,2\n"
            "DA:30,0\n"
            "DA:50,1\n"
            "BRDA:15,0,0,1\n"
            "BRDA:15,0,1,0\n"
            "BRDA:50,0,0,1\n"
            "FNF:2\n"
            "FNH:2\n"
            "BRF:3\n"
            "BRH:2\n"
            "LF:4\n"
            "LH:3\n"
            "end_of_record\n"
            "SF:/path/to/unrelated.c\n"
            "DA:1,1\n"
            "DA:2,1\n"
            "end_of_record\n"
        )
        with tempfile.NamedTemporaryFile("w+", encoding="utf-8") as f:
            f.write(coverage_content)
            f.flush()
            out = io.StringIO()
            lcov_stencil.filter_coverage_file(f.name, out, template_data)
            filtered = out.getvalue()

        # Unrelated file should be completely omitted
        self.assertNotIn("/path/to/unrelated.c", filtered)

        # Referencing lines should be included
        self.assertIn("SF:/path/to/board.c", filtered)
        self.assertIn("FN:15,board_init", filtered)
        self.assertIn("FNDA:2,board_init", filtered)
        self.assertIn("DA:15,2", filtered)
        self.assertIn("DA:16,2", filtered)
        self.assertIn("BRDA:15,0,0,1", filtered)
        self.assertIn("BRDA:15,0,1,0", filtered)

        # Non-matching lines in board.c should be omitted
        self.assertNotIn("unreferenced_fn", filtered)
        self.assertNotIn("DA:30", filtered)
        self.assertNotIn("DA:50", filtered)
        self.assertNotIn("BRDA:50", filtered)

        # Recalculated totals
        self.assertIn("FNF:1\n", filtered)
        self.assertIn("FNH:1\n", filtered)
        self.assertIn("BRF:2\n", filtered)
        self.assertIn("BRH:1\n", filtered)
        self.assertIn("LF:2\n", filtered)
        self.assertIn("LH:2\n", filtered)

    def test_filter_coverage_file_omits_unmatched_record(self):
        """Tests that a record in template but with zero matching lines is omitted."""
        template_data = defaultdict(
            set,
            {
                "SF:/path/to/empty.c": {"99"},
            },
        )
        coverage_content = "SF:/path/to/empty.c\nDA:1,1\nend_of_record\n"
        with tempfile.NamedTemporaryFile("w+", encoding="utf-8") as f:
            f.write(coverage_content)
            f.flush()
            out = io.StringIO()
            lcov_stencil.filter_coverage_file(f.name, out, template_data)
            self.assertEqual(out.getvalue(), "")


if __name__ == "__main__":
    unittest.main()
