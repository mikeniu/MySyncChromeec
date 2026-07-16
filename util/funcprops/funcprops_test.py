#!/usr/bin/env python3
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Unit tests for @funcprops extraction and static analysis tools."""

import os
import sys
import unittest

from analyze_funcprops import check_path_matchers
from analyze_funcprops import normalize_symbol_name

# Import refactored DRY functions and declarative helpers
from extract_funcprops import BLOCK_PATTERN
from extract_funcprops import extract_qualifiers_from_text


class TestExtractFuncprops(unittest.TestCase):
    def test_extract_qualifiers_from_text(self):
        text1 = "@funcprops \\isr_ok, \\sleep"
        props1 = extract_qualifiers_from_text(text1)
        self.assertEqual(props1, ["isr_ok", "sleep"])

        text2 = "@funcprops \\pre-kernel-ok, \\async"
        props2 = extract_qualifiers_from_text(text2)
        self.assertEqual(props2, ["async", "pre_kernel_ok"])

    def test_block_pattern_extraction(self):
        sample_code = """
        /**
         * @brief Test function with multiple properties.
         *
         * @funcprops \\isr_ok, \\sleep
         */
        void sample_isr_and_sleep_func(void);

        /**
         * @brief Non-annotated function.
         */
        void normal_function(int a);

        /**
         * @brief Function with pre-kernel property.
         * @funcprops \\pre_kernel_ok
         */
        int early_boot_init(void)
        {
            return 0;
        }
        """

        matches = BLOCK_PATTERN.findall(sample_code)
        extracted = {}

        for comment, func_name in matches:
            props = extract_qualifiers_from_text(comment)
            if props:
                extracted[func_name] = props

        self.assertIn("sample_isr_and_sleep_func", extracted)
        self.assertIn("isr_ok", extracted["sample_isr_and_sleep_func"])
        self.assertIn("sleep", extracted["sample_isr_and_sleep_func"])

        self.assertNotIn("normal_function", extracted)

        self.assertIn("early_boot_init", extracted)
        self.assertEqual(extracted["early_boot_init"], ["pre_kernel_ok"])


class TestAnalyzeFuncprops(unittest.TestCase):
    def test_symbol_normalization(self):
        self.assertEqual(
            normalize_symbol_name("k_sem_give.lto_priv.0.lto_priv.0"),
            "k_sem_give",
        )
        self.assertEqual(
            normalize_symbol_name("signal_poll_event.isra.0"),
            "signal_poll_event",
        )
        self.assertEqual(
            normalize_symbol_name("tablet_set_mode.constprop.0"),
            "tablet_set_mode",
        )
        self.assertEqual(
            normalize_symbol_name("crec_usleep.part.0"), "crec_usleep"
        )
        self.assertEqual(
            normalize_symbol_name("simple_function"), "simple_function"
        )

    def test_check_path_matchers(self):
        matchers = [
            "mutex_lock",
            "mutex_unlock",
            "k_mutex",
            "i2c_lock",
            "i2c_unlock",
        ]

        path1 = [
            "gmr_tablet_switch_isr_handler",
            "cprints",
            "k_mutex_unlock.lto_priv.0",
        ]
        has_match, symbol = check_path_matchers(path1, matchers)
        self.assertTrue(has_match)
        self.assertEqual(symbol, "k_mutex_unlock")

        path2 = ["sm5803_init", "chg_write8", "i2c_lock"]
        has_match, symbol = check_path_matchers(path2, matchers)
        self.assertTrue(has_match)
        self.assertEqual(symbol, "i2c_lock")

        # Spinlocks ARE OK in ISRs and should NOT match MUTEX_IN_ISR!
        path3 = ["gpio_isr", "k_spin_lock"]
        has_match, symbol = check_path_matchers(path3, matchers)
        self.assertFalse(has_match)
        self.assertIsNone(symbol)

        path4 = ["clock_init", "get_sys_clock"]
        has_match, symbol = check_path_matchers(path4, matchers)
        self.assertFalse(has_match)


if __name__ == "__main__":
    unittest.main()
