#!/usr/bin/env python3
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Unit tests for find_unused.py."""

from pathlib import Path
import tempfile
import unittest

# pylint: disable=import-error
from util.find_unused import find_configs
from util.find_unused import find_functions_in_file
from util.find_unused import find_set_configs
from util.find_unused import is_copybot_synced
from util.find_unused import is_header_included
from util.find_unused import load_copybot_patterns
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

    def test_find_functions_in_file(self):
        """Test function extraction from C and C++ source code."""
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

        int register_cb(int (*cb)(int), void *ctx) __attribute__((aligned(8))) {
            return cb ? cb(0) : 0;
        }

        pw::Status Driver::Init(int arg) const {
            return pw::OkStatus();
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
            self.assertIn("register_cb", func_names)
            self.assertIn("Driver::Init", func_names)
            self.assertIn("main_function", func_names)
            self.assertNotIn("if", func_names)
            self.assertNotIn("while", func_names)
        finally:
            f_path.unlink()

    def test_is_header_included(self):
        """Test header inclusion matcher."""
        full_includes = {
            "sensor.h": {"drivers/sensor.h"},
            "baz.h": {"bar/baz.h"},
            "hooks.h": {"hooks.h"},
        }
        base_includes = {"sensor.h", "baz.h", "hooks.h"}

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

        header5 = Path("mybar/baz.h")
        self.assertFalse(
            is_header_included(header5, full_includes, base_includes)
        )

        header6 = Path("include/bar/baz.h")
        self.assertTrue(
            is_header_included(header6, full_includes, base_includes)
        )

    def test_find_configs_and_set_configs(self):
        """Test config extraction and conditional header evaluation."""
        with tempfile.TemporaryDirectory() as tmpdir:
            root = Path(tmpdir)
            src_dir = root / "src"
            build_dir = root / "build"
            src_dir.mkdir()
            build_dir.mkdir()

            (src_dir / "code.c").write_text(
                "#ifdef CONFIG_FOO\n"
                "#endif\n"
                "#ifdef CONFIG_BAR\n"
                "#endif\n"
                "#ifdef CONFIG_UNUSED_OPT\n"
                "#endif\n"
                "int x = GPIO_CONFIG_0;\n"
            )
            (src_dir / "config_chip.h").write_text(
                "#ifndef __CROS_EC_CONFIG_CHIP_H\n"
                "#define __CROS_EC_CONFIG_CHIP_H\n"
                "#ifdef CONFIG_ZTEST\n"
                "#define CONFIG_BAR\n"
                "#endif\n"
                "#if defined(CONFIG_FOO) && \\\n"
                "    defined(CONFIG_MISSING_CONTINUATION)\n"
                "#define CONFIG_CONT_GUARDED\n"
                "#endif\n"
                "#if defined(CONFIG_FOO) || defined(CONFIG_NEVER_A) && defined(CONFIG_NEVER_B)\n"
                "#define CONFIG_MIXED_PRECEDENCE_TRUE\n"
                "#endif\n"
                "#if defined(CONFIG_NEVER_A) || defined(CONFIG_FOO) && defined(CONFIG_NEVER_B)\n"
                "#define CONFIG_MIXED_PRECEDENCE_FALSE\n"
                "#endif\n"
                "#ifdef CONFIG_NEVER_ENABLED\n"
                "#define CONFIG_UNUSED_OPT\n"
                "#else\n"
                "#define CONFIG_ELSE_FALLBACK\n"
                "#endif\n"
                "#ifdef PLATFORM_EC_TYPO_WITHOUT_CONFIG_PREFIX\n"
                "#define CONFIG_TYPO_GUARDED\n"
                "#endif\n"
                "#endif\n"
            )
            (build_dir / ".config").write_text(
                "CONFIG_PLATFORM_EC_FOO=y\nCONFIG_ZTEST=y\n"
            )

            all_cfgs = find_configs([src_dir])
            self.assertIn("CONFIG_FOO", all_cfgs)
            self.assertIn("CONFIG_BAR", all_cfgs)
            self.assertIn("CONFIG_UNUSED_OPT", all_cfgs)
            self.assertNotIn("CONFIG_0", all_cfgs)
            self.assertNotIn("CONFIG_CHIP_H", all_cfgs)

            set_cfgs = find_set_configs([src_dir], [build_dir])
            self.assertIn("CONFIG_FOO", set_cfgs)
            self.assertIn("CONFIG_BAR", set_cfgs)
            self.assertIn("CONFIG_MIXED_PRECEDENCE_TRUE", set_cfgs)
            self.assertNotIn("CONFIG_MIXED_PRECEDENCE_FALSE", set_cfgs)
            self.assertIn("CONFIG_ELSE_FALLBACK", set_cfgs)
            self.assertNotIn("CONFIG_CONT_GUARDED", set_cfgs)
            self.assertNotIn("CONFIG_TYPO_GUARDED", set_cfgs)
            self.assertNotIn("CONFIG_UNUSED_OPT", set_cfgs)

    def test_copybot_patterns(self):
        """Test loading and matching copybot include-downstream patterns."""
        with tempfile.TemporaryDirectory() as tmpdir:
            ini_path = Path(tmpdir) / "ec-legacy.ini"
            ini_path.write_text(
                "[copybot]\n"
                'include-downstream = ["driver/fingerprint/*", "include/ec_cmd_api.h"]\n'
            )
            patterns = load_copybot_patterns(Path(tmpdir), ini_path)
            self.assertEqual(
                patterns, ["driver/fingerprint/*", "include/ec_cmd_api.h"]
            )
            self.assertTrue(
                is_copybot_synced(
                    Path("driver/fingerprint/egis/egis_private.c"), patterns
                )
            )
            self.assertTrue(is_copybot_synced("include/ec_cmd_api.h", patterns))
            self.assertFalse(is_copybot_synced("common/crc8.c", patterns))


if __name__ == "__main__":
    unittest.main()
