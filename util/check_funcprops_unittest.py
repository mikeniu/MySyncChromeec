#!/usr/bin/env vpython3
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Unit tests for check_funcprops.py."""

from pathlib import Path
import unittest
from unittest import mock

import check_funcprops


class TestCheckFuncprops(unittest.TestCase):
    """Unit tests for check_funcprops.py."""

    def test_validate_doxygen_qualifiers_valid(self):
        """Test valid Doxygen qualifier comment blocks."""
        # 1. Single qualifier at end of comment block
        t1 = """/**
 * Short description.
 *
 * @isr_ok
 */
void foo(void);"""
        self.assertEqual(check_funcprops.validate_doxygen_qualifiers(t1), [])

        # 2. Multiple consecutive qualifiers
        t2 = """/**
 * @isr_ok
 * @pre_kernel_ok
 * @async
 */
void bar(void);"""
        self.assertEqual(check_funcprops.validate_doxygen_qualifiers(t2), [])

        # 3. Qualifiers separated from @param and @return by blank comment line
        t3 = """/**
 * Process an event.
 *
 * @isr_ok
 * @no_wait
 *
 * @param event The event ID.
 * @return 0 on success, negative error code on failure.
 */
int process_event(int event);"""
        self.assertEqual(check_funcprops.validate_doxygen_qualifiers(t3), [])

        # 4. Qualifiers separated from @note or @retval by blank line
        t4 = """/**
 * Wait for completion.
 *
 * @sleep
 *
 * @note This blocks the calling thread.
 * @retval 0 Success.
 */
int wait_done(void);"""
        self.assertEqual(check_funcprops.validate_doxygen_qualifiers(t4), [])

    def test_validate_doxygen_qualifiers_invalid(self):
        """Test detection of invalid or malformed Doxygen qualifiers."""
        # 1. Deprecated @funcprops / \funcprops wrapper
        t1 = """/**
 * @funcprops \\isr_ok
 */
void legacy_func(void);"""
        errs1 = check_funcprops.validate_doxygen_qualifiers(t1, "test.h")
        self.assertTrue(any("deprecated '@funcprops'" in e for e in errs1))

        t1b = """/**
 * \\funcprops \\sleep
 */
void legacy_func_b(void);"""
        errs1b = check_funcprops.validate_doxygen_qualifiers(t1b, "test.h")
        self.assertTrue(any("deprecated '\\funcprops'" in e for e in errs1b))

        # 2. Backslash qualifier syntax
        t2 = """/**
 * \\isr_ok
 * \\sleep
 */
void backslash_func(void);"""
        errs2 = check_funcprops.validate_doxygen_qualifiers(t2, "test.h")
        self.assertTrue(
            any("backslash qualifier '\\isr_ok'" in e for e in errs2)
        )
        self.assertTrue(
            any("backslash qualifier '\\sleep'" in e for e in errs2)
        )

        # 3. Hyphenated qualifier typos
        t3 = """/**
 * @isr-ok
 * @no-wait
 * @pre-kernel-ok
 */
void hyphen_func(void);"""
        errs3 = check_funcprops.validate_doxygen_qualifiers(t3, "test.h")
        self.assertTrue(
            any("hyphenated qualifier '@isr-ok'" in e for e in errs3)
        )
        self.assertTrue(
            any("hyphenated qualifier '@no-wait'" in e for e in errs3)
        )
        self.assertTrue(
            any("hyphenated qualifier '@pre-kernel-ok'" in e for e in errs3)
        )

        # 4. Missing blank comment line before @param
        t4 = """/**
 * @isr_ok
 * @param a Parameter a.
 */
void missing_blank_param(int a);"""
        errs4 = check_funcprops.validate_doxygen_qualifiers(t4, "test.h")
        self.assertTrue(
            any(
                "missing blank comment line" in e and "@param" in e
                for e in errs4
            )
        )

        # 5. Missing blank comment line before @return
        t5 = """/**
 * @sleep
 * @return status code.
 */
int missing_blank_return(void);"""
        errs5 = check_funcprops.validate_doxygen_qualifiers(t5, "test.h")
        self.assertTrue(
            any(
                "missing blank comment line" in e and "@return" in e
                for e in errs5
            )
        )

        # 6. Missing blank comment line before @arg:
        t6 = """/**
 * @isr_ok
 * @arg: The argument pointer.
 */
void missing_blank_arg(void *p);"""
        errs6 = check_funcprops.validate_doxygen_qualifiers(t6, "test.h")
        self.assertTrue(
            any(
                "missing blank comment line" in e and "@arg:" in e
                for e in errs6
            )
        )

    @mock.patch("preupload.lib.cat_file")
    @mock.patch("preupload.lib.parse_args")
    def test_main_pass(self, mock_parse_args, mock_cat_file):
        """Test check_funcprops.main() returns 0 on clean files."""
        fake_args = mock.Mock()
        fake_file = mock.Mock(spec=Path)
        fake_file.is_file.return_value = True
        fake_file.suffix = ".h"
        fake_file.__str__ = mock.Mock(return_value="include/clean.h")
        fake_args.filename = [fake_file]

        mock_parse_args.return_value = fake_args
        mock_cat_file.return_value = """/**
 * Clean function.
 *
 * @isr_ok
 *
 * @param x Input value.
 */
void clean(int x);"""

        ret = check_funcprops.main([])
        self.assertEqual(ret, 0)

    @mock.patch("preupload.lib.cat_file")
    @mock.patch("preupload.lib.parse_args")
    def test_main_fail(self, mock_parse_args, mock_cat_file):
        """Test check_funcprops.main() returns 1 when violations exist."""
        fake_args = mock.Mock()
        fake_file = mock.Mock(spec=Path)
        fake_file.is_file.return_value = True
        fake_file.suffix = ".c"
        fake_file.__str__ = mock.Mock(return_value="src/bad.c")
        fake_args.filename = [fake_file]

        mock_parse_args.return_value = fake_args
        mock_cat_file.return_value = """/**
 * Bad function.
 *
 * @isr-ok
 * @param x Input value.
 */
void bad(int x);"""

        ret = check_funcprops.main([])
        self.assertEqual(ret, 1)


if __name__ == "__main__":
    unittest.main()
