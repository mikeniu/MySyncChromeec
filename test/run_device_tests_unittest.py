#!/usr/bin/env python3
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Unit tests for test/run_device_tests.py."""

import unittest

import run_device_tests


class RunDeviceTestsTest(unittest.TestCase):
    """Tests for output regexes in run_device_tests."""

    def test_single_check_failed_regex_matches_failures(self) -> None:
        self.assertRegex(
            "ASSERTION failed: x == y, file: test/foo.c, line: 42",
            run_device_tests.SINGLE_CHECK_FAILED_REGEX,
        )
        self.assertRegex(
            'assertion "x == y" failed: file "test/foo.c", line 42',
            run_device_tests.SINGLE_CHECK_FAILED_REGEX,
        )
        self.assertRegex(
            "    FAIL - test_foo in 0.001 seconds",
            run_device_tests.SINGLE_CHECK_FAILED_REGEX,
        )

    def test_single_check_failed_regex_ignores_driver_errors(self) -> None:
        self.assertNotRegex(
            "[0.012345 egis630_pal: SPI PAL transaction failed: -116]",
            run_device_tests.SINGLE_CHECK_FAILED_REGEX,
        )
        self.assertNotRegex(
            "Pass: all checks succeeded",
            run_device_tests.SINGLE_CHECK_FAILED_REGEX,
        )


if __name__ == "__main__":
    unittest.main()
