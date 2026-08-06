#!/usr/bin/env python3
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Unit tests for find_unused.py."""

from pathlib import Path
import tempfile
import unittest

# pylint: disable=import-error
from util.find_unused import find_functions_in_file
from util.find_unused import is_header_included
from util.find_unused import normalize_path


class FindUnusedTest(unittest.TestCase):
    """Test suite for find_unused utility functions."""

    def test_normalize_path_ec_root(self):
        """Test normalize_path with standard ec_root path."""
        ec_root = Path("/home/user/ec")
        zephyr_root = Path("/home/user/zephyr")
        p = Path("/home/user/ec/common/hooks.c")
        norm = normalize_path(ec_root, zephyr_root, p)
        self.assertEqual(norm, Path("common/hooks.c"))

    def test_normalize_path_cros_sdk_mount(self):
        """Test normalize_path with cros_sdk mount path."""
        ec_root = Path("/home/user/ec")
        zephyr_root = Path("/home/user/zephyr")
        p = Path("/mnt/host/source/src/platform/ec/common/hooks.c")
        norm = normalize_path(ec_root, zephyr_root, p)
        self.assertEqual(norm, Path("common/hooks.c"))

    def test_find_functions_in_file(self):
        """Test function extraction from C source code."""
        sample_code = """
        /* Comment with fake function() { */
        // line comment func2() {

        static inline void helper_func(int a,
                                       int b)
        {
            if (a > b) {
                return;
            }
        }

        int main_function(void) {
            while (1) {
                helper_func(1, 2);
            }
        }
        """
        with tempfile.NamedTemporaryFile(
            mode="w+", suffix=".c", delete=False
        ) as f:
            f.write(sample_code)
            f_path = Path(f.name)

        try:
            funcs = find_functions_in_file(f_path)
            func_names = [name for _, name in funcs]
            self.assertIn("helper_func", func_names)
            self.assertIn("main_function", func_names)
            self.assertNotIn("if", func_names)
            self.assertNotIn("while", func_names)
        finally:
            f_path.unlink()

    def test_is_header_included(self):
        """Test header inclusion matcher."""
        full_includes = {"drivers/sensor.h", "hooks.h"}
        base_includes = {"sensor.h", "hooks.h"}

        header1 = Path("drivers/sensor.h")
        self.assertTrue(
            is_header_included(header1, full_includes, base_includes)
        )

        header2 = Path("other/sensor.h")
        self.assertFalse(
            is_header_included(header2, full_includes, base_includes)
        )

        header3 = Path("include/hooks.h")
        self.assertTrue(
            is_header_included(header3, full_includes, base_includes)
        )

        header4 = Path("include/unused.h")
        self.assertFalse(
            is_header_included(header4, full_includes, base_includes)
        )


if __name__ == "__main__":
    unittest.main()
