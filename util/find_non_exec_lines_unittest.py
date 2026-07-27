#!/usr/bin/env python3
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Unit tests for find_non_exec_lines.py."""

import io
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch


UTIL_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(UTIL_DIR))

# pylint: disable=wrong-import-position
import find_non_exec_lines


class TestFindNonExecLines(unittest.TestCase):
    """Tests for find_non_exec_lines."""

    def test_detection_of_bad_lines(self):
        """Test detecting non-executable lines (blank, closing comment, closing brace)."""
        c_code = (
            "    */\n"  # Line 1: Block comment end
            "#include <stdio.h>\n"  # Line 2: Include
            "\n"  # Line 3: Blank line
            "int foo(int cond) {\n"  # Line 4: Function start
            "    if (cond) {\n"  # Line 5: Conditional branch
            "        return 1;\n"  # Line 6: Executable statement
            "    }\n"  # Line 7: Closing brace inside function
            "    return 0;\n"  # Line 8: Executable statement
            "}\n"  # Line 9: Standalone closing brace
        )
        with tempfile.NamedTemporaryFile("w", suffix=".c", delete=False) as cf:
            cf.write(c_code)
            cf.flush()
            c_path = cf.name

        lcov_info = f"""SF:{c_path}
DA:1,0
DA:3,0
DA:5,1
DA:6,0
DA:7,0
DA:8,0
DA:9,0
LF:7
LH:1
end_of_record
"""
        with tempfile.NamedTemporaryFile(
            "w", suffix=".info", delete=False
        ) as lf:
            lf.write(lcov_info)
            lf.flush()
            lcov_path = lf.name

        with patch("sys.stdout", new_callable=io.StringIO) as mock_stdout:
            exit_code = find_non_exec_lines.main([lcov_path])

        self.assertEqual(exit_code, 1)
        output = mock_stdout.getvalue()

        # Non-executable lines that SHOULD be filtered (printed to stdout):
        # Line 1 (*/), Line 3 (blank line), Line 7 (internal }), Line 9 (closing brace })
        self.assertIn(f"{c_path}:1=", output)
        self.assertIn(f"{c_path}:3=", output)
        self.assertIn(f"{c_path}:7=", output)
        self.assertIn(f"{c_path}:9=", output)

        # Executable statements that MUST NOT be filtered:
        # Line 5 (if), Line 6 (return 1), Line 8 (return 0)
        self.assertNotIn(f"{c_path}:5=", output)
        self.assertNotIn(f"{c_path}:6=", output)
        self.assertNotIn(f"{c_path}:8=", output)


if __name__ == "__main__":
    unittest.main()
