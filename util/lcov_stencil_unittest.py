#!/usr/bin/env python3
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Unit tests for lcov_stencil.py."""

from pathlib import Path
import sys
import tempfile
import unittest


# Add util directory to path
UTIL_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(UTIL_DIR))

# pylint: disable=wrong-import-position
import lcov_stencil


class TestLcovStencil(unittest.TestCase):
    """Tests for lcov_stencil."""

    def test_stenciling_and_merging(self):
        """Test stenciling and merging lcov files."""
        template_content = """TN:
SF:/src/foo.c
FN:10,foo_func
FNDA:0,foo_func
DA:10,0
DA:20,0
DA:30,0
LF:3
LH:0
end_of_record
"""
        input1_content = """TN:
SF:/src/foo.c
FN:10,foo_func
FNDA:1,foo_func
DA:10,5
DA:20,0
DA:99,10
LF:3
LH:2
end_of_record
"""
        with tempfile.NamedTemporaryFile(
            "w", suffix=".info", delete=False
        ) as tf, tempfile.NamedTemporaryFile(
            "w", suffix=".info", delete=False
        ) as inf, tempfile.NamedTemporaryFile(
            "w", suffix=".info", delete=False
        ) as outf:

            tf.write(template_content)
            tf.flush()
            inf.write(input1_content)
            inf.flush()

            lcov_stencil.main(
                [
                    "-o",
                    outf.name,
                    tf.name,
                    inf.name,
                ]
            )

            out_text = Path(outf.name).read_text(encoding="utf-8")

        # 1. Output should have exactly ONE SF: block for /src/foo.c
        self.assertEqual(out_text.count("SF:/src/foo.c"), 1)
        self.assertEqual(out_text.count("end_of_record"), 1)

        # 2. Lines found (LF) should be 3 (only stenciled lines 10, 20, 30)
        self.assertIn("LF:3\n", out_text)
        # 3. Lines hit (LH) should be 1 (only line 10 executed)
        self.assertIn("LH:1\n", out_text)
        # 4. Out-of-stencil line 99 should not be present
        self.assertNotIn("DA:99", out_text)
        # 5. Stenciled lines 10, 20, 30 should be present
        self.assertIn("DA:10,5\n", out_text)
        self.assertIn("DA:20,0\n", out_text)
        self.assertIn("DA:30,0\n", out_text)


if __name__ == "__main__":
    unittest.main()
