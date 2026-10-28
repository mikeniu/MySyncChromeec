#!/usr/bin/env python3
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Unit tests for the Zephyr system workqueue (k_sys_work_q) static analyzer."""

import json
import os
import struct
import sys
import unittest


# Ensure current directory is in path for module imports
script_dir = os.path.dirname(os.path.abspath(__file__))
if script_dir not in sys.path:
    sys.path.insert(0, script_dir)


# pylint: disable=wrong-import-position,import-error,missing-function-docstring,missing-class-docstring
from analyze_funcprops import AnalysisRoot
from analyze_funcprops import evaluate_violations
from analyze_sysworkq import discover_sysworkq_roots
from analyze_sysworkq import SYSWORKQ_DEFAULT_ALLOWLIST
from funcprops_test import MockELF
from funcprops_test import MockSection


class TestAnalyzeSysworkq(unittest.TestCase):
    """Unit tests for analyze_sysworkq.py root discovery and rule evaluation."""

    def test_discover_sysworkq_roots_hooks_deferred_and_kwork(self):
        # Hook struct: void (*routine)(void) (4 bytes), int priority (4 bytes)
        # Entry 1 at 0x2000: second_hook_fn (0x3001 with Thumb bit) -> HOOK_SECOND (included)
        # Entry 2 at 0x2008: init_hook_fn (0x3005 with Thumb bit) -> HOOK_INIT (excluded)
        # Entry 3 at 0x2010: pd_hook_fn (0x3009 with Thumb bit) -> HOOK_USB_PD_CONNECT (excluded)
        # Entry 4 at 0x2018: soc_hook_fn (0x300D with Thumb bit) -> HOOK_BATTERY_SOC_CHANGE
        hook_data = (
            struct.pack("<II", 0x3001, 0)
            + struct.pack("<II", 0x3005, 0)
            + struct.pack("<II", 0x3009, 0)
            + struct.pack("<II", 0x300D, 0)
        )
        # Statically defined struct k_work at 0x6000 (16 bytes: node[2], handler at +8, flags):
        # handler = 0x5009 (Thumb bit for 0x5008 "dpm_work_handler")
        work_data = struct.pack("<IIII", 0, 0, 0x5009, 0)
        sec_hooks = MockSection("._cros_shim_hook_list", 0x2000, hook_data)
        sec_data = MockSection(".data", 0x6000, work_data)
        elf = MockELF([sec_hooks, sec_data])

        syms = {
            "_zephyr_shim_hook_HOOK_SECOND_list_start": 0x2000,
            "_zephyr_shim_hook_HOOK_SECOND_list_end": 0x2008,
            "_zephyr_shim_hook_HOOK_INIT_list_start": 0x2008,
            "_zephyr_shim_hook_HOOK_INIT_list_end": 0x2010,
            "_zephyr_shim_hook_HOOK_USB_PD_CONNECT_list_start": 0x2010,
            "_zephyr_shim_hook_HOOK_USB_PD_CONNECT_list_end": 0x2018,
            "_zephyr_shim_hook_HOOK_BATTERY_SOC_CHANGE_list_start": 0x2018,
            "_zephyr_shim_hook_HOOK_BATTERY_SOC_CHANGE_list_end": 0x2020,
            "second_hook_fn": 0x3000,
            "init_hook_fn": 0x3004,
            "pd_hook_fn": 0x3008,
            "soc_hook_fn": 0x300C,
            "deferred_work_work_data": 0x5000,
            "deferred_work": 0x5004,
            "dpm_work": 0x6000,
            "dpm_work_handler": 0x5008,
        }
        rev_syms = {
            0x3000: "second_hook_fn",
            0x3004: "init_hook_fn",
            0x3008: "pd_hook_fn",
            0x300C: "soc_hook_fn",
            0x5004: "deferred_work",
            0x5008: "dpm_work_handler",
        }
        sym_sizes = {"dpm_work": 16}

        # 1. Without charger_task: HOOK_BATTERY_SOC_CHANGE runs on k_sys_work_q
        roots = discover_sysworkq_roots(elf, syms, rev_syms, sym_sizes)
        root_map = {r.name: r for r in roots}

        self.assertIn("second_hook_fn", root_map)
        self.assertEqual(root_map["second_hook_fn"].source, "HOOK_SECOND")
        self.assertIn("soc_hook_fn", root_map)
        self.assertEqual(
            root_map["soc_hook_fn"].source, "HOOK_BATTERY_SOC_CHANGE"
        )

        # Boot and PD-thread hooks must be excluded
        self.assertNotIn("init_hook_fn", root_map)
        self.assertNotIn("pd_hook_fn", root_map)

        # DECLARE_DEFERRED (*_work_data)
        self.assertIn("deferred_work", root_map)
        self.assertEqual(root_map["deferred_work"].source, "DECLARE_DEFERRED")

        # Raw K_WORK_DEFINE (dpm_work -> dpm_work_handler)
        self.assertIn("dpm_work_handler", root_map)
        self.assertEqual(root_map["dpm_work_handler"].source, "k_work:dpm_work")

        # 2. With charger_task present: HOOK_BATTERY_SOC_CHANGE runs on charger_task, not sysworkq
        syms_with_charger = dict(syms, charger_task=0x8000)
        roots_chg = discover_sysworkq_roots(
            elf, syms_with_charger, rev_syms, sym_sizes
        )
        root_map_chg = {r.name: r for r in roots_chg}
        self.assertNotIn("soc_hook_fn", root_map_chg)

    def test_sysworkq_rules_evaluation(self):
        rules_path = os.path.join(script_dir, "sysworkq_rules.json")
        with open(rules_path, "r", encoding="utf-8") as f:
            rules = json.load(f)

        funcprops_db = {
            "crec_usleep": {
                "props": ["sleep"],
                "file": "include/timer.h",
            },
            "i2c_read8": {
                "props": ["sleep"],
                "file": "include/i2c.h",
            },
            "k_mutex_lock": {
                "props": ["no_wait", "sleep"],
                "file": "zephyr/kernel.h",
            },
            "k_mutex_unlock": {
                "props": ["sleep"],
                "file": "zephyr/kernel.h",
            },
            "k_sem_take": {
                "props": ["no_wait", "sleep"],
                "file": "zephyr/kernel.h",
            },
            "cprints": {
                "props": ["isr_ok", "pre_kernel_ok", "sleep"],
                "file": "include/console.h",
            },
            "hook_notify": {
                "props": ["sleep"],
                "file": "include/hooks.h",
            },
        }

        roots = [
            AnalysisRoot(
                context="SYSWORKQ",
                kind="deferred",
                name="my_deferred",
                source="DECLARE_DEFERRED",
                address=0x1000,
            ),
        ]

        call_graph = {
            "my_deferred": {
                "crec_usleep",
                "i2c_read8",
                "espi_read_lpc_request",
                "k_mutex_lock",
                "k_mutex_unlock",
                "k_sem_take",
                "cprints",
                "hook_notify",
            },
            "cprints": {"k_mutex_lock"},
        }

        # 1. Default run (include_no_wait enabled):
        #    - i2c_read8 -> IO_IN_SYSWORKQ (only once, not duplicated as SLEEP_IN_SYSWORKQ)
        #    - crec_usleep -> SLEEP_IN_SYSWORKQ
        #    - k_mutex_lock, k_sem_take -> NO_WAIT_IN_SYSWORKQ
        #    - cprints -> exempted by @isr_ok
        #    - hook_notify, k_mutex_unlock -> exempted by SYSWORKQ_DEFAULT_ALLOWLIST
        v_default = evaluate_violations(
            roots,
            call_graph,
            rules,
            funcprops_db,
            allowlist=set(SYSWORKQ_DEFAULT_ALLOWLIST),
            enabled_flags={"include_no_wait"},
        )
        v_map = {(v.rule_id, v.target) for v in v_default}
        self.assertEqual(
            v_map,
            {
                ("IO_IN_SYSWORKQ", "i2c_read8"),
                ("SLEEP_IN_SYSWORKQ", "crec_usleep"),
                ("NO_WAIT_IN_SYSWORKQ", "k_mutex_lock"),
                ("NO_WAIT_IN_SYSWORKQ", "k_sem_take"),
            },
        )

        # 2. --exclude-no-wait (enabled_flags=set()):
        #    - Suppresses NO_WAIT_IN_SYSWORKQ, leaving only IO_IN_SYSWORKQ and SLEEP_IN_SYSWORKQ
        v_no_wait_excluded = evaluate_violations(
            roots,
            call_graph,
            rules,
            funcprops_db,
            allowlist=set(SYSWORKQ_DEFAULT_ALLOWLIST),
            enabled_flags=set(),
        )
        v_nw_map = {(v.rule_id, v.target) for v in v_no_wait_excluded}
        self.assertEqual(
            v_nw_map,
            {
                ("IO_IN_SYSWORKQ", "i2c_read8"),
                ("SLEEP_IN_SYSWORKQ", "crec_usleep"),
            },
        )


if __name__ == "__main__":
    unittest.main()
