#!/usr/bin/env python3
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Comprehensive unit tests for @funcprops extraction and static analysis tools."""

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
from analyze_funcprops import check_path_matchers
from analyze_funcprops import decode_arm_thumb2_instruction
from analyze_funcprops import decode_riscv_instruction
from analyze_funcprops import discover_isr_roots
from analyze_funcprops import discover_pre_kernel_roots
from analyze_funcprops import discover_sysworkq_roots
from analyze_funcprops import evaluate_violations
from analyze_funcprops import normalize_symbol_name
from analyze_funcprops import parse_call_graph_pure_python
from extract_funcprops import extract_qualifiers_from_text
from extract_funcprops import parse_immediate_declaration
from extract_funcprops import validate_doxygen_qualifiers


class MockSection:
    """Mock ELF section matching pyelftools interface for testing."""

    def __init__(self, name: str, sh_addr: int, data: bytes):
        self.name = name
        self.header = {
            "sh_addr": sh_addr,
            "sh_size": len(data),
        }
        self._data = data

    def data(self) -> bytes:
        return self._data

    def __getitem__(self, key: str):
        return self.header[key]


class MockELF:
    """Mock ELF file matching pyelftools interface for testing."""

    def __init__(self, sections, elfclass: int = 32):
        self.elfclass = elfclass
        self.little_endian = True
        self._sections = sections
        self._by_name = {s.name: s for s in sections}

    def iter_sections(self):
        return iter(self._sections)

    def get_section_by_name(self, name: str):
        return self._by_name.get(name)


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

        # 7. Detect @no_wait without @sleep
        t7 = "/**\n * @isr_ok\n * @no_wait\n */\nvoid trigger(void);"
        e7 = validate_doxygen_qualifiers(t7, "test.h")
        self.assertTrue(
            any(
                "'@no_wait' is only valid on functions that also specify '@sleep'"
                in e
                for e in e7
            )
        )

        # 8. Detect redundant @sleep and @reschedule
        t8 = "/**\n * @sleep\n * @reschedule\n */\nvoid sleep_fn(void);"
        e8 = validate_doxygen_qualifiers(t8, "test.h")
        self.assertTrue(
            any(
                "'@reschedule' is redundant when '@sleep' is specified" in e
                for e in e8
            )
        )

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


class TestAnalyzeFuncprops(unittest.TestCase):
    """Unit tests for analyze_funcprops.py analyzers and instruction decoders."""

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
        # System call unwrapping
        self.assertEqual(
            normalize_symbol_name("z_impl_k_sem_take"), "k_sem_take"
        )
        self.assertEqual(
            normalize_symbol_name("z_vrfy_k_sem_take"), "k_sem_take"
        )

    def test_decode_arm_thumb2_instruction(self):
        syms = {0x1004: "bl_target_func", 0x1008: "bw_target_func"}

        # 32-bit BL: hw1 = 0xF000, hw2 = 0xF800 -> offset = 0, target = curr_pc + 4
        width, target = decode_arm_thumb2_instruction(
            0x1000, 0xF000, 0xF800, syms
        )
        self.assertEqual(width, 4)
        self.assertEqual(target, "bl_target_func")

        # 32-bit B.W tail call: hw1 = 0xF000, hw2 = 0xB800 (offset = 0)
        syms_bw = {0x1004: "bw_target_func"}
        width, target = decode_arm_thumb2_instruction(
            0x1000, 0xF000, 0xB800, syms_bw
        )
        self.assertEqual(width, 4)
        self.assertEqual(target, "bw_target_func")

        # 16-bit B tail call: hw1 = 0xE000 -> offset = 0, target = curr_pc + 4
        syms_b = {0x1004: "b_target_func"}
        width, target = decode_arm_thumb2_instruction(
            0x1000, 0xE000, None, syms_b
        )
        self.assertEqual(width, 2)
        self.assertEqual(target, "b_target_func")

    def test_decode_riscv_instruction(self):
        syms = {0x2000: "riscv_jal_target"}

        # 32-bit JAL: opcode 0x6F
        width, target = decode_riscv_instruction(
            0x2000, 0x0000006F, 0x006F, syms
        )
        self.assertEqual(width, 4)
        self.assertEqual(target, "riscv_jal_target")

        # 16-bit C.JAL: 0x2001
        syms_cjal = {0x2000: "riscv_cjal_target"}
        width, target = decode_riscv_instruction(
            0x2000, 0x2001, 0x2001, syms_cjal
        )
        self.assertEqual(width, 2)
        self.assertEqual(target, "riscv_cjal_target")

        # 16-bit C.J: 0xA001
        syms_cj = {0x2000: "riscv_cj_target"}
        width, target = decode_riscv_instruction(
            0x2000, 0xA001, 0xA001, syms_cj
        )
        self.assertEqual(width, 2)
        self.assertEqual(target, "riscv_cj_target")

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
        self.assertTrue(check_path_matchers(path1, matchers))

        path2 = ["sm5803_init", "chg_write8", "i2c_lock"]
        self.assertTrue(check_path_matchers(path2, matchers))

        # Spinlocks are safe in ISRs and should not match mutex matchers
        path3 = ["gpio_isr", "k_spin_lock"]
        self.assertFalse(check_path_matchers(path3, matchers))

    def test_evaluate_violations(self):
        rules = [
            {
                "rule_id": "MUTEX_IN_ISR",
                "category": "MUTEX_IN_ISR",
                "contexts": ["ISR"],
                "prohibited_props": ["reschedule"],
                "path_matchers": ["mutex"],
                "exemption_props": ["isr_ok"],
                "match_target": True,
                "description": "Mutex acquisition in ISR context",
            },
            {
                "rule_id": "SLEEP_IN_SYSWORKQ",
                "category": "SLEEP_IN_SYSWORKQ",
                "contexts": ["SYSWORKQ"],
                "prohibited_props": ["sleep"],
                "required_absent_props": ["no_wait"],
                "description": "Unconditional sleep in sysworkq context",
            },
            {
                "rule_id": "TIMEOUT_SLEEP_IN_SYSWORKQ",
                "category": "SLEEP_IN_SYSWORKQ",
                "contexts": ["SYSWORKQ"],
                "prohibited_props": ["sleep"],
                "requires_flag": "include_timeout_primitives",
                "description": "Timeout-capable blocking call in sysworkq",
            },
        ]

        funcprops_db = {
            "k_mutex_lock": {
                "props": ["no_wait", "reschedule", "sleep"],
                "file": "zephyr/kernel.h",
            },
            "k_sleep": {
                "props": ["reschedule", "sleep"],
                "file": "zephyr/kernel.h",
            },
            "k_sem_give": {
                "props": ["isr_ok"],
                "file": "zephyr/kernel.h",
            },
        }

        roots = [
            AnalysisRoot(
                context="ISR",
                kind="hardware_isr",
                name="gpio_isr_handler",
                source="vector_15",
                address=0x1000,
            ),
            AnalysisRoot(
                context="SYSWORKQ",
                kind="deferred",
                name="battery_deferred_work",
                source="DECLARE_DEFERRED",
                address=0x2000,
            ),
        ]

        call_graph = {
            "gpio_isr_handler": {"k_mutex_lock", "k_sem_give"},
            "battery_deferred_work": {"k_sleep", "k_mutex_lock"},
        }

        # 1. Standard run: include_timeout_primitives defaults to True, so both
        #    k_sleep (SLEEP_IN_SYSWORKQ) and k_mutex_lock (TIMEOUT_SLEEP_IN_SYSWORKQ)
        #    are flagged.
        v1 = evaluate_violations(
            roots,
            call_graph,
            rules,
            funcprops_db,
            allowlist=set(),
        )
        v1_ids = {(v.rule_id, v.target) for v in v1}
        self.assertIn(("MUTEX_IN_ISR", "k_mutex_lock"), v1_ids)
        self.assertIn(("SLEEP_IN_SYSWORKQ", "k_sleep"), v1_ids)
        self.assertIn(("TIMEOUT_SLEEP_IN_SYSWORKQ", "k_mutex_lock"), v1_ids)

        # 2. Flag-disabled run: include_timeout_primitives=False suppresses
        #    TIMEOUT_SLEEP_IN_SYSWORKQ
        v2 = evaluate_violations(
            roots,
            call_graph,
            rules,
            funcprops_db,
            allowlist=set(),
            include_timeout_primitives=False,
        )
        v2_ids = {(v.rule_id, v.target) for v in v2}
        self.assertNotIn(("TIMEOUT_SLEEP_IN_SYSWORKQ", "k_mutex_lock"), v2_ids)

        # 3. Allowlist suppression
        v3 = evaluate_violations(
            roots,
            call_graph,
            rules,
            funcprops_db,
            allowlist={"k_mutex_lock"},
            include_timeout_primitives=False,
        )
        v3_ids = {(v.rule_id, v.target) for v in v3}
        self.assertNotIn(("MUTEX_IN_ISR", "k_mutex_lock"), v3_ids)

    def test_parse_call_graph_pure_python_arm_and_riscv(self):
        # 1. ARM Thumb-2 call graph test
        # Caller at 0x1000 with BL (0xF000, 0xF800 -> target at 0x1004)
        text_data_arm = struct.pack("<HHH", 0xF000, 0xF800, 0xBF00)
        text_sec_arm = MockSection(".text", 0x1000, text_data_arm)
        elf_arm = MockELF([text_sec_arm])
        rev_syms_arm = {0x1000: "arm_caller", 0x1004: "arm_callee"}

        cg_arm = parse_call_graph_pure_python(elf_arm, rev_syms_arm, arch="arm")
        self.assertIn("arm_caller", cg_arm)
        self.assertIn("arm_callee", cg_arm["arm_caller"])

        # 2. RISC-V call graph test
        # Caller at 0x2000 with 32-bit JAL (0x0040006F -> target at 0x2004)
        # Followed by AUIPC + JALR pair:
        # At 0x2008: AUIPC x1, 0x1000 -> 0x01000097
        # At 0x200C: JALR x1, 0x10(x1) -> 0x010080E7
        # Target = ((0x2008 + 0x01000000) + 0x10) & 0xFFFFFFFE = 0x01002018
        text_data_rv = struct.pack(
            "<IIII",
            0x0040006F,
            0x00000013,
            0x01000097,
            0x010080E7,
        )
        text_sec_rv = MockSection(".text", 0x2000, text_data_rv)
        elf_rv = MockELF([text_sec_rv])
        rev_syms_rv = {
            0x2000: "rv_caller",
            0x2004: "rv_callee",
            0x01002018: "rv_long_callee",
        }

        cg_rv = parse_call_graph_pure_python(elf_rv, rev_syms_rv, arch="riscv")
        self.assertIn("rv_caller", cg_rv)
        self.assertIn("rv_callee", cg_rv["rv_caller"])
        self.assertIn("rv_callee", cg_rv)
        self.assertIn("rv_long_callee", cg_rv["rv_callee"])

    def test_discover_sysworkq_roots(self):
        # Hook struct: void (*routine)(void) (4 bytes), int priority (4 bytes)
        # Entry 1 at 0x2000: second_hook_fn (0x3001 with Thumb bit)
        # Entry 2 at 0x2008: init_hook_fn (0x3005 with Thumb bit)
        sec_data = struct.pack("<II", 0x3001, 0) + struct.pack("<II", 0x3005, 0)
        sec = MockSection("._cros_shim_hook_list", 0x2000, sec_data)
        elf = MockELF([sec])

        syms = {
            "_zephyr_shim_hook_HOOK_SECOND_list_start": 0x2000,
            "_zephyr_shim_hook_HOOK_SECOND_list_end": 0x2008,
            "_zephyr_shim_hook_HOOK_INIT_list_start": 0x2008,
            "_zephyr_shim_hook_HOOK_INIT_list_end": 0x2010,
            "second_hook_fn": 0x3000,
            "init_hook_fn": 0x3004,
            "deferred_work_work_data": 0x5000,
            "deferred_work": 0x5004,
        }
        rev_syms = {
            0x3000: "second_hook_fn",
            0x3004: "init_hook_fn",
            0x5004: "deferred_work",
        }

        roots = discover_sysworkq_roots(elf, syms, rev_syms)
        root_map = {r.name: r for r in roots}

        # HOOK_SECOND should be discovered
        self.assertIn("second_hook_fn", root_map)
        self.assertEqual(root_map["second_hook_fn"].source, "HOOK_SECOND")
        self.assertEqual(root_map["second_hook_fn"].context, "SYSWORKQ")

        # HOOK_INIT should be excluded!
        self.assertNotIn("init_hook_fn", root_map)

        # DECLARE_DEFERRED (*_work_data)
        self.assertIn("deferred_work", root_map)
        self.assertEqual(root_map["deferred_work"].source, "DECLARE_DEFERRED")
        self.assertEqual(root_map["deferred_work"].address, 0x5004)

    def test_discover_pre_kernel_roots(self):
        # .initlevel holds 8-byte struct init_entry:
        # int (*init)(const struct device *dev); const struct device *dev;
        # Entry 0 (at 0x2000): pre_kernel_fn1 (0x3001 with Thumb bit)
        # Entry 1 (at 0x2008): pre_kernel_fn2 (0x3005 with Thumb bit)
        # Entry 2 (at 0x2010): post_kernel_fn (0x3009 with Thumb bit)
        sec_data = (
            struct.pack("<II", 0x3001, 0)
            + struct.pack("<II", 0x3005, 0)
            + struct.pack("<II", 0x3009, 0)
        )
        sec = MockSection(".initlevel", 0x2000, sec_data)
        elf = MockELF([sec])

        syms = {
            "__init_POST_KERNEL_start": 0x2010,
            "pre_kernel_fn1": 0x3000,
            "pre_kernel_fn2": 0x3004,
            "post_kernel_fn": 0x3008,
        }
        rev_syms = {
            0x3000: "pre_kernel_fn1",
            0x3004: "pre_kernel_fn2",
            0x3008: "post_kernel_fn",
        }

        roots = discover_pre_kernel_roots(elf, syms, rev_syms)
        root_map = {r.name: r for r in roots}

        self.assertIn("pre_kernel_fn1", root_map)
        self.assertIn("pre_kernel_fn2", root_map)
        self.assertEqual(root_map["pre_kernel_fn1"].context, "PRE_KERNEL")
        self.assertEqual(root_map["pre_kernel_fn2"].context, "PRE_KERNEL")
        # Post-kernel init should be strictly excluded
        self.assertNotIn("post_kernel_fn", root_map)

        # 64-bit pre-kernel test (elfclass = 64)
        sec_data_64 = (
            struct.pack("<QQ", 0x3000, 0)
            + struct.pack("<QQ", 0x3008, 0)
            + struct.pack("<QQ", 0x3010, 0)
        )
        sec_64 = MockSection(".initlevel", 0x2000, sec_data_64)
        elf_64 = MockELF([sec_64], elfclass=64)
        syms_64 = {
            "__init_POST_KERNEL_start": 0x2020,
            "pk_64_fn1": 0x3000,
            "pk_64_fn2": 0x3008,
            "post_64_fn": 0x3010,
        }
        rev_syms_64 = {
            0x3000: "pk_64_fn1",
            0x3008: "pk_64_fn2",
            0x3010: "post_64_fn",
        }
        roots_64 = discover_pre_kernel_roots(elf_64, syms_64, rev_syms_64)
        root_map_64 = {r.name: r for r in roots_64}
        self.assertIn("pk_64_fn1", root_map_64)
        self.assertIn("pk_64_fn2", root_map_64)
        self.assertEqual(root_map_64["pk_64_fn1"].address, 0x3000)

    def test_discover_isr_roots(self):
        # _sw_isr_table holds 8-byte entries: void *arg, void (*isr)(const void *)
        # Entry 0: arg = 0, isr = 0x4001 (Thumb bit for 0x4000 "gpio_isr")
        # Entry 1: arg = 0, isr = 0x5000 ("spurious_isr")
        sec_data = struct.pack("<II", 0, 0x4001) + struct.pack("<II", 0, 0x5000)
        sec = MockSection(".rodata", 0x1000, sec_data)
        elf = MockELF([sec])

        syms = {
            "_sw_isr_table": 0x1000,
            "CONFIG_NUM_IRQS": 2,
            "z_irq_spurious": 0x5000,
            "gpio_isr": 0x4000,
            "spurious_isr": 0x5000,
            "hard_fault_handler": 0x6000,
        }
        rev_syms = {
            0x4000: "gpio_isr",
            0x5000: "spurious_isr",
            0x6000: "hard_fault_handler",
        }

        roots = discover_isr_roots(elf, syms, rev_syms)
        root_map = {r.name: r for r in roots}

        self.assertIn("gpio_isr", root_map)
        self.assertEqual(root_map["gpio_isr"].context, "ISR")
        self.assertEqual(root_map["gpio_isr"].source, "_sw_isr_table[0]")

        self.assertIn("hard_fault_handler", root_map)
        self.assertEqual(root_map["hard_fault_handler"].context, "ISR")
        self.assertEqual(root_map["hard_fault_handler"].source, "vector_table")

        # Spurious ISR should be excluded
        self.assertNotIn("spurious_isr", root_map)

        # 64-bit ISR table test (elfclass = 64)
        sec_data_64 = struct.pack("<QQ", 0, 0x4000) + struct.pack(
            "<QQ", 0, 0x5000
        )
        sec_64 = MockSection(".rodata", 0x1000, sec_data_64)
        elf_64 = MockELF([sec_64], elfclass=64)
        syms_64 = {
            "_sw_isr_table": 0x1000,
            "CONFIG_NUM_IRQS": 2,
            "z_irq_spurious": 0x5000,
            "gpio_isr_64": 0x4000,
            "spurious_isr": 0x5000,
        }
        rev_syms_64 = {
            0x4000: "gpio_isr_64",
            0x5000: "spurious_isr",
        }
        roots_64 = discover_isr_roots(elf_64, syms_64, rev_syms_64)
        root_map_64 = {r.name: r for r in roots_64}
        self.assertIn("gpio_isr_64", root_map_64)
        self.assertEqual(root_map_64["gpio_isr_64"].address, 0x4000)
        self.assertNotIn("spurious_isr", root_map_64)

    def test_context_adaptive_boundary_pruning(self):
        rules = [
            {
                "rule_id": "MUTEX_IN_ISR",
                "category": "MUTEX_IN_ISR",
                "contexts": ["ISR"],
                "prohibited_props": ["reschedule"],
                "path_matchers": ["mutex"],
                "exemption_props": ["isr_ok"],
                "match_target": True,
                "description": "Mutex in ISR",
            },
            {
                "rule_id": "MUTEX_IN_PRE_KERNEL",
                "category": "MUTEX_IN_PRE_KERNEL",
                "contexts": ["PRE_KERNEL"],
                "prohibited_props": ["reschedule"],
                "path_matchers": ["mutex"],
                "exemption_props": ["pre_kernel_ok"],
                "match_target": True,
                "description": "Mutex in PRE_KERNEL",
            },
            {
                "rule_id": "TIMEOUT_SLEEP_IN_SYSWORKQ",
                "category": "SLEEP_IN_SYSWORKQ",
                "contexts": ["SYSWORKQ"],
                "prohibited_props": ["sleep", "no_wait"],
                "requires_flag": "include_timeout_primitives",
                "description": "Timeout sleep in SYSWORKQ",
            },
        ]

        funcprops_db = {
            "cprints": {
                "props": ["isr_ok", "pre_kernel_ok"],
                "file": "include/console.h",
            },
            "unannotated_wrapper": {
                "props": [],
                "file": "common/wrapper.c",
            },
            "k_mutex_lock": {
                "props": ["no_wait", "reschedule", "sleep"],
                "file": "zephyr/kernel.h",
            },
        }

        isr_root = AnalysisRoot(
            context="ISR",
            kind="hardware_isr",
            name="test_isr",
            source="irq_1",
            address=0x1000,
        )
        pre_kernel_root = AnalysisRoot(
            context="PRE_KERNEL",
            kind="init_hook",
            name="test_pre_kernel",
            source="initlevel",
            address=0x2000,
        )
        sysworkq_root = AnalysisRoot(
            context="SYSWORKQ",
            kind="deferred",
            name="test_deferred",
            source="DECLARE_DEFERRED",
            address=0x3000,
        )

        call_graph = {
            "test_isr": {"cprints"},
            "test_pre_kernel": {"cprints"},
            "test_deferred": {"cprints"},
            "cprints": {"k_mutex_lock"},
        }

        # 1. cprints in ISR and PRE_KERNEL: pruned at cprints boundary because
        # of @isr_ok and @pre_kernel_ok
        v_isr = evaluate_violations(
            [isr_root], call_graph, rules, funcprops_db, set()
        )
        self.assertEqual(len(v_isr), 0)

        v_pre = evaluate_violations(
            [pre_kernel_root], call_graph, rules, funcprops_db, set()
        )
        self.assertEqual(len(v_pre), 0)

        # 2. cprints in SYSWORKQ: not pruned, reaches k_mutex_lock and triggers
        # TIMEOUT_SLEEP_IN_SYSWORKQ
        v_sys = evaluate_violations(
            [sysworkq_root],
            call_graph,
            rules,
            funcprops_db,
            set(),
            include_timeout_primitives=True,
        )
        self.assertEqual(len(v_sys), 1)
        self.assertEqual(v_sys[0].target, "k_mutex_lock")
        self.assertEqual(v_sys[0].rule_id, "TIMEOUT_SLEEP_IN_SYSWORKQ")

        # 3. Unannotated wrapper in ISR: NOT pruned, reaches k_mutex_lock and triggers MUTEX_IN_ISR
        call_graph_unannotated = {
            "test_isr": {"unannotated_wrapper"},
            "unannotated_wrapper": {"k_mutex_lock"},
        }
        v_unannotated = evaluate_violations(
            [isr_root], call_graph_unannotated, rules, funcprops_db, set()
        )
        self.assertEqual(len(v_unannotated), 1)
        self.assertEqual(v_unannotated[0].target, "k_mutex_lock")
        self.assertEqual(v_unannotated[0].rule_id, "MUTEX_IN_ISR")

    def test_target_only_rule_matching(self):
        rules = [
            {
                "rule_id": "I2C_IN_ISR",
                "category": "ISR Context Violation",
                "contexts": ["ISR"],
                "path_matchers": ["i2c_transfer", "i2c_write"],
                "match_target": True,
                "prohibited_props": [],
                "exemption_props": ["isr_ok"],
                "description": "Synchronous I2C transaction in ISR",
            },
            {
                "rule_id": "MUTEX_IN_ISR",
                "category": "ISR Context Violation",
                "contexts": ["ISR"],
                "path_matchers": ["mutex_lock", "mutex_unlock"],
                "match_target": True,
                "prohibited_props": [],
                "exemption_props": ["isr_ok"],
                "description": "Mutex operation in ISR",
            },
        ]

        funcprops_db = {
            "i2c_transfer": {"props": []},
            "i2c_sub_helper": {"props": []},
            "k_mutex_unlock": {"props": ["reschedule"]},
        }

        roots = [
            AnalysisRoot(
                context="ISR",
                kind="hardware_isr",
                name="my_isr",
                source="irq",
                address=0x1000,
            ),
        ]

        # Call chain: my_isr -> i2c_transfer -> i2c_sub_helper
        # and my_isr -> k_mutex_unlock
        call_graph = {
            "my_isr": {"i2c_transfer", "k_mutex_unlock"},
            "i2c_transfer": {"i2c_sub_helper"},
        }

        violations = evaluate_violations(
            roots, call_graph, rules, funcprops_db, allowlist=set()
        )
        v_targets = {v.target for v in violations}

        # i2c_transfer and k_mutex_unlock should be flagged
        self.assertIn("i2c_transfer", v_targets)
        self.assertIn("k_mutex_unlock", v_targets)

        # Transitive callee inside i2c_transfer must NOT trigger duplicate I2C_IN_ISR violation!
        self.assertNotIn("i2c_sub_helper", v_targets)


if __name__ == "__main__":
    unittest.main()
