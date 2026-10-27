#!/usr/bin/env python3
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Unit tests for @funcprops extraction and presubmit check."""

import os
import sys
import unittest


# Ensure current directory is in path for module imports
script_dir = os.path.dirname(os.path.abspath(__file__))
if script_dir not in sys.path:
    sys.path.insert(0, script_dir)

# pylint: disable=wrong-import-position,missing-function-docstring,missing-class-docstring
from extract_funcprops import extract_qualifiers_from_text
from extract_funcprops import parse_immediate_declaration
from extract_funcprops import validate_doxygen_qualifiers


class TestExtractFuncprops(unittest.TestCase):
    """Unit tests for extract_funcprops.py parser and qualifier extractor."""

    def test_extract_qualifiers_from_text(self):
        # 1. Canonical @qualifiers
        text1 = "@isr_ok @sleep"
        props1 = extract_qualifiers_from_text(text1)
        # Zephyr implication rule: 'sleep' implies 'reschedule'
        self.assertEqual(props1, ["isr_ok", "reschedule", "sleep"])

        # 2. Modern bare @qualifier tags in doc comment
        text2 = """
        * @brief Kernel spinlock acquisition.
        * @isr_ok
        * @no_wait
        """
        props2 = extract_qualifiers_from_text(text2)
        self.assertEqual(props2, ["isr_ok", "no_wait"])

        # 3. All 7 supported canonical qualifiers
        text3 = (
            "@reschedule @sleep @no_wait @isr_ok @pre_kernel_ok @async"
            " @supervisor"
        )
        props3 = extract_qualifiers_from_text(text3)
        expected = sorted(
            [
                "reschedule",
                "sleep",
                "no_wait",
                "isr_ok",
                "pre_kernel_ok",
                "async",
                "supervisor",
            ]
        )
        self.assertEqual(props3, expected)

        # 4. Immunity to non-canonical or legacy formats in extract_qualifiers_from_text
        text4 = "@funcprops \\isr_ok, \\sleep"
        self.assertEqual(extract_qualifiers_from_text(text4), [])

        # 5. Immunity to plain English prose containing "sleep" or "async"
        text5 = "/** Minimum half sleep duration tHS time */"
        self.assertEqual(extract_qualifiers_from_text(text5), [])

        text6 = "/* This function puts the thread to sleep for 10ms */"
        self.assertEqual(extract_qualifiers_from_text(text6), [])

        text7 = "/* Performs an asynchronous transaction without waiting */"
        self.assertEqual(extract_qualifiers_from_text(text7), [])

    def test_validate_doxygen_qualifiers(self):
        # 1. Detect deprecated @funcprops / \funcprops
        t1 = "/** @funcprops \\isr_ok */\nvoid foo(void);"
        e1 = validate_doxygen_qualifiers(t1, "test.h")
        self.assertTrue(any("deprecated '@funcprops'" in e for e in e1))

        # 2. Detect backslash-prefixed qualifier
        t2 = "/** \\isr_ok */\nvoid foo(void);"
        e2 = validate_doxygen_qualifiers(t2, "test.h")
        self.assertTrue(any("backslash qualifier '\\isr_ok'" in e for e in e2))

        # 3. Detect hyphenated qualifier typo
        t3 = "/** @isr-ok */\nvoid foo(void);"
        e3 = validate_doxygen_qualifiers(t3, "test.h")
        self.assertTrue(any("hyphenated qualifier '@isr-ok'" in e for e in e3))

        # 4. Detect orphaned qualifier (not followed by function declaration)
        t4 = "/** @isr_ok */\nstruct bar { int x; };"
        e4 = validate_doxygen_qualifiers(t4, "test.h")
        self.assertTrue(any("orphaned qualifier tag(s)" in e for e in e4))

        # 5. Detect missing blank line between qualifier and @param
        t5 = "/**\n * @isr_ok\n * @param a An integer\n */\nvoid foo(int a);"
        e5 = validate_doxygen_qualifiers(t5, "test.h")
        self.assertTrue(any("missing blank comment line" in e for e in e5))

        # 6. Valid canonical declaration with intervening blank line passes cleanly
        t6 = (
            "/**\n * @isr_ok\n * @pre_kernel_ok\n *\n * @param a An integer\n"
            " */\nint valid_func(int a);"
        )
        e6 = validate_doxygen_qualifiers(t6, "test.h")
        self.assertEqual(e6, [])

    def test_parse_immediate_declaration_pointer_returns(self):
        code1 = "void *k_heap_alloc(struct k_heap *h, size_t bytes, k_timeout_t timeout);"
        self.assertEqual(parse_immediate_declaration(code1), "k_heap_alloc")

        code2 = "struct k_thread *k_sched_current_thread_query(void);"
        self.assertEqual(
            parse_immediate_declaration(code2), "k_sched_current_thread_query"
        )

        code3 = "const char *device_get_name(const struct device *dev);"
        self.assertEqual(parse_immediate_declaration(code3), "device_get_name")

    def test_parse_immediate_declaration_syscall_and_inlines(self):
        code1 = (
            "__syscall int k_sem_take(struct k_sem *sem, k_timeout_t timeout);"
        )
        self.assertEqual(parse_immediate_declaration(code1), "k_sem_take")

        code2 = """
        static inline void k_busy_wait(uint32_t usec_to_wait)
        {
            z_impl_k_busy_wait(usec_to_wait);
        }
        """
        self.assertEqual(parse_immediate_declaration(code2), "k_busy_wait")

        code3 = "ALWAYS_INLINE int fast_calc(uint32_t val);"
        self.assertEqual(parse_immediate_declaration(code3), "fast_calc")

    def test_parse_immediate_declaration_attributes_and_callbacks(self):
        code1 = """
        __attribute__((__warn_unused_result__))
        int checked_func(void);
        """
        self.assertEqual(parse_immediate_declaration(code1), "checked_func")

        code2 = "int gpio_init_callback(void (*handler)(const struct device *port));"
        self.assertEqual(
            parse_immediate_declaration(code2), "gpio_init_callback"
        )

    def test_parse_immediate_declaration_nested_attributes(self):
        code1 = """
        __attribute__((__format__(__printf__, 2, 3))) int
        cprintf(enum console_channel channel, const char *format, ...);
        """
        self.assertEqual(parse_immediate_declaration(code1), "cprintf")

        code2 = """
        __attribute__((__format__(__printf__, 2, 3))) int
        cprints(enum console_channel channel, const char *format, ...);
        """
        self.assertEqual(parse_immediate_declaration(code2), "cprints")

    def test_parse_immediate_declaration_macros(self):
        code1 = "#define k_sleep(d) k_msleep(d)"
        self.assertEqual(parse_immediate_declaration(code1), "k_sleep")

    def test_parse_immediate_declaration_rejects_statements_and_non_functions(
        self,
    ):
        # 1. Bare statement call (e.g. inside a function body)
        code1 = "k_busy_wait(4);"
        self.assertIsNone(parse_immediate_declaration(code1))

        # 2. Assignment expression
        code2 = "ret = k_busy_wait(4);"
        self.assertIsNone(parse_immediate_declaration(code2))

        # 3. Return statement
        code3 = "return k_busy_wait(4);"
        self.assertIsNone(parse_immediate_declaration(code3))

        # 4. Struct definition without function declaration
        code4 = """
        struct device_config {
            int baudrate;
            uint32_t flags;
        };
        """
        self.assertIsNone(parse_immediate_declaration(code4))


if __name__ == "__main__":
    unittest.main()
