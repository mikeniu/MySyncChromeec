#!/usr/bin/env python3
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Unit tests for find_unused.py."""

from pathlib import Path
import unittest

# pylint: disable=import-error
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

    def test_normalize_path_other_tree_and_parent_traversal(self):
        """Test normalize_path with another workspace tree and '..' segments."""
        ec_root = Path("/home/user/cros-trees/work/src/platform/ec")
        zephyr_root = Path(
            "/home/user/cros-trees/work/src/third_party/zephyrproject/zephyr"
        )
        p = Path(
            "/home/user/cros-trees/main/src/platform/ec/zephyr/test/"
            "hostcmd_console/../../../chip/host/lpc.c"
        )
        norm = normalize_path(ec_root, zephyr_root, p)
        self.assertEqual(norm, Path("chip/host/lpc.c"))

    def test_normalize_path_twister_out(self):
        """Test normalize_path strips twister-out module prefix."""
        ec_root = Path("/home/user/ec")
        zephyr_root = Path("/home/user/zephyr")
        p = Path(
            "/home/user/ec/twister-out/native_sim_native/zephyr/test/"
            "drivers/drivers.default/modules/ec/common/hooks.c"
        )
        norm = normalize_path(ec_root, zephyr_root, p)
        self.assertEqual(norm, Path("common/hooks.c"))


if __name__ == "__main__":
    unittest.main()
