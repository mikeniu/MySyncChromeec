#!/usr/bin/env python3
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Unit tests for find_non_exec_lines.py."""

from pathlib import Path
import sys
import tempfile
import unittest


UTIL_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(UTIL_DIR))

# pylint: disable=wrong-import-position
import find_non_exec_lines


class TestFindNonExecLines(unittest.TestCase):
    """Tests for find_non_exec_lines."""

    def test_detection_of_bad_lines(self):
        """Test detecting non-executable lines."""
        c_code = """/* Header comment */
#include <stdio.h>

void foo(void) {
    int x = 1;

    // A single line comment
    /* Another comment */
#endif
}
"""
        with tempfile.NamedTemporaryFile("w", suffix=".c", delete=False) as cf:
            cf.write(c_code)
            cf.flush()
            c_path = cf.name

        lcov_info = f"""SF:{c_path}
DA:1,0
DA:4,5
DA:5,0
DA:6,0
DA:7,0
DA:8,0
DA:9,0
DA:10,0
LF:8
LH:1
end_of_record
"""
        with tempfile.NamedTemporaryFile(
            "w", suffix=".info", delete=False
        ) as lf:
            lf.write(lcov_info)
            lf.flush()
            lcov_path = lf.name

        bad_lines = find_non_exec_lines.find_non_exec_in_file(lcov_path)
        bad_line_nums = [b[1] for b in bad_lines]

        # Line 1 (/* */), Line 6 (blank), Line 7 (//), Line 8 (/* */), Line 9 (#endif), Line 10 (})
        self.assertIn(1, bad_line_nums)
        self.assertIn(6, bad_line_nums)
        self.assertIn(7, bad_line_nums)
        self.assertIn(8, bad_line_nums)
        self.assertIn(9, bad_line_nums)
        self.assertNotIn(4, bad_line_nums)
        self.assertNotIn(5, bad_line_nums)


if __name__ == "__main__":
    unittest.main()
