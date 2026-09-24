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

    def test_filter_coverage_file_brda_taken_tracking(self):
        """Tests that BRDA taken count (4th field) determines branch hits."""
        template_data = defaultdict(set, {"SF:/path/to/branch.c": {"10"}})
        # branch 0 taken 5 -> hit
        # branch 1 taken 0 -> not hit
        # branch 2 taken - -> not hit
        coverage_content = (
            "SF:/path/to/branch.c\n"
            "BRDA:10,0,0,5\n"
            "BRDA:10,0,1,0\n"
            "BRDA:10,0,2,-\n"
            "BRF:3\n"
            "BRH:1\n"
            "end_of_record\n"
        )
        with tempfile.NamedTemporaryFile("w+", encoding="utf-8") as f:
            f.write(coverage_content)
            f.flush()
            out = io.StringIO()
            lcov_stencil.filter_coverage_file(f.name, out, template_data)
            filtered = out.getvalue()

        self.assertIn("BRF:3\n", filtered)
        self.assertIn("BRH:1\n", filtered)

    def test_compile_exclude_patterns(self):
        """Tests compiling glob patterns into regex."""
        self.assertIsNone(lcov_stencil.compile_exclude_patterns([]))
        exclude_re = lcov_stencil.compile_exclude_patterns(
            ["*/drivers/*", "**/mock/**"]
        )
        self.assertIsNotNone(exclude_re)
        self.assertTrue(exclude_re.match("/path/drivers/cros_rtc.c"))
        self.assertTrue(exclude_re.match("/path/test/mock/test.c"))
        self.assertFalse(exclude_re.match("/path/common/main.c"))

    def test_parse_template_file_with_exclude(self):
        """Tests that excluded files are omitted from template parsing."""
        template_content = (
            "SF:/ec/common/main.c\n"
            "DA:10,1\n"
            "end_of_record\n"
            "SF:/ec/zephyr/drivers/cros_rtc.c\n"
            "DA:20,1\n"
            "end_of_record\n"
        )
        exclude_re = lcov_stencil.compile_exclude_patterns(["*/drivers/*"])
        with tempfile.NamedTemporaryFile("w+", encoding="utf-8") as f:
            f.write(template_content)
            f.flush()
            result = lcov_stencil.parse_template_file(f.name, exclude_re)

        self.assertIn("SF:/ec/common/main.c", result)
        self.assertNotIn("SF:/ec/zephyr/drivers/cros_rtc.c", result)

    def test_main_with_exclude_patterns(self):
        """Tests CLI execution with --exclude-pattern."""
        template_content = (
            "SF:/ec/common/main.c\n"
            "DA:10,1\n"
            "end_of_record\n"
            "SF:/ec/zephyr/drivers/cros_rtc.c\n"
            "DA:20,1\n"
            "end_of_record\n"
        )
        coverage_content = (
            "SF:/ec/common/main.c\n"
            "DA:10,5\n"
            "end_of_record\n"
            "SF:/ec/zephyr/drivers/cros_rtc.c\n"
            "DA:20,3\n"
            "end_of_record\n"
        )
        with tempfile.TemporaryDirectory() as tmpdir:
            template_path = Path(tmpdir) / "template.info"
            cov_path = Path(tmpdir) / "cov.info"
            out_path = Path(tmpdir) / "out.info"

            template_path.write_text(template_content, encoding="utf-8")
            cov_path.write_text(coverage_content, encoding="utf-8")

            lcov_stencil.main(
                [
                    "--exclude-pattern",
                    "*/drivers/*",
                    "-o",
                    str(out_path),
                    str(template_path),
                    str(cov_path),
                ]
            )

            result = out_path.read_text(encoding="utf-8")
            self.assertIn("/ec/common/main.c", result)
            self.assertNotIn("/ec/zephyr/drivers/cros_rtc.c", result)

    def test_exclude_patterns_from_file(self):
        """Tests streaming exclusion filtering of records."""
        content = (
            "TN:my_test\n"
            "SF:/ec/common/main.c\n"
            "DA:10,1\n"
            "end_of_record\n"
            "TN:my_test\n"
            "SF:/ec/zephyr/drivers/cros_rtc.c\n"
            "DA:20,1\n"
            "end_of_record\n"
        )
        exclude_re = lcov_stencil.compile_exclude_patterns(["*/drivers/*"])
        with tempfile.NamedTemporaryFile("w+", encoding="utf-8") as f:
            f.write(content)
            f.flush()
            out = io.StringIO()
            lcov_stencil.exclude_patterns_from_file(f.name, out, exclude_re)

        result = out.getvalue()
        self.assertIn("TN:my_test\nSF:/ec/common/main.c", result)
        self.assertNotIn("cros_rtc.c", result)

    def test_exclude_patterns_from_file_no_filter(self):
        """Tests that exclude_patterns_from_file passes through when no regex."""
        content = "SF:/ec/common/main.c\nDA:10,1\nend_of_record\n"
        with tempfile.NamedTemporaryFile("w+", encoding="utf-8") as f:
            f.write(content)
            f.flush()
            out = io.StringIO()
            lcov_stencil.exclude_patterns_from_file(f.name, out, None)

        self.assertEqual(out.getvalue(), content)

    def test_main_single_file_exclude(self):
        """Tests CLI execution when only a single input file is provided."""
        content = (
            "TN:test\n"
            "SF:/ec/common/main.c\n"
            "DA:10,1\n"
            "end_of_record\n"
            "SF:/ec/zephyr/test/mock/mock.c\n"
            "DA:5,1\n"
            "end_of_record\n"
        )
        with tempfile.TemporaryDirectory() as tmpdir:
            in_path = Path(tmpdir) / "in.info"
            out_path = Path(tmpdir) / "out.info"
            in_path.write_text(content, encoding="utf-8")

            lcov_stencil.main(
                [
                    "--exclude-pattern",
                    "*/mock/*",
                    "-o",
                    str(out_path),
                    str(in_path),
                ]
            )

            result = out_path.read_text(encoding="utf-8")
            self.assertIn("/ec/common/main.c", result)
            self.assertNotIn("/ec/zephyr/test/mock/mock.c", result)


if __name__ == "__main__":
    unittest.main()
