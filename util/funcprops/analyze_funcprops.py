#!/usr/bin/env python3
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""

Declarative Static violation analyzer for @funcprops compliance.

Operates directly on compiled firmware ELF binaries with ZERO external CLI dependencies
by utilizing a Pure-Python Opcode Disassembler (supporting RISC-V 32-bit and ARM Thumb-2 32-bit)
and Driver Table Relocation Scanning for indirect function pointer resolution.
"""

import argparse
import json
import os
import re
import struct
import sys

from elftools.elf.elffile import ELFFile
from elftools.elf.sections import SymbolTableSection


def load_rules(rules_path):
    with open(rules_path, "r", encoding="utf-8") as f:
        return json.load(f)


def normalize_symbol_name(name):
    """Normalize mangled LTO and compiler internal symbol names."""
    if not name:
        return name
    clean = re.sub(r"\.(?:lto_priv|constprop|isra|part|str1)\.[0-9]+", "", name)
    clean = re.sub(r"__device_dts_ord_[0-9]+", "", clean)
    clean = re.sub(r"\.c\.[0-9a-fA-F]+", "", clean)
    return clean


def decode_riscv_jal(curr_pc, raw_inst, reverse_symbol_table):
    """Decode 32-bit RISC-V JAL opcode (0x6f)."""
    if (raw_inst & 0x7F) == 0x6F:
        imm20 = (raw_inst >> 31) & 0x1
        imm10_1 = (raw_inst >> 21) & 0x3FF
        imm11 = (raw_inst >> 20) & 0x1
        imm19_12 = (raw_inst >> 12) & 0xFF

        imm = (imm20 << 20) | (imm19_12 << 12) | (imm11 << 11) | (imm10_1 << 1)
        if imm & 0x100000:
            imm -= 0x200000
        target_pc = curr_pc + imm
        return reverse_symbol_table.get(target_pc & ~1)
    return None


def decode_arm_thumb2_bl(curr_pc, hw1, hw2, reverse_symbol_table):
    """Decode 32-bit ARM Thumb-2 BL instruction."""
    if (hw1 & 0xF800) == 0xF000 and (hw2 & 0xD000) == 0xD000:
        s = (hw1 >> 10) & 1
        j1 = (hw2 >> 13) & 1
        j2 = (hw2 >> 11) & 1
        imm10 = hw1 & 0x3FF
        imm11 = hw2 & 0x7FF

        i1 = ~(j1 ^ s) & 1
        i2 = ~(j2 ^ s) & 1

        offset = (
            (s << 24) | (i1 << 23) | (i2 << 22) | (imm10 << 12) | (imm11 << 1)
        )
        if s:
            offset -= 1 << 25

        target_pc = curr_pc + 4 + offset
        return reverse_symbol_table.get(target_pc & ~1)
    return None


def parse_call_graph_pure_python(elffile, reverse_symbol_table, arch):
    """Disassemble binary text section in pure Python and build static call graph."""
    call_graph = {}
    text_sec = elffile.get_section_by_name(
        ".text"
    ) or elffile.get_section_by_name("text")
    if not text_sec:
        return call_graph

    text_data = text_sec.data()
    text_addr = text_sec["sh_addr"]
    size = len(text_data)

    current_func = None

    if arch == "riscv":
        for i in range(0, size - 4, 2):
            curr_pc = text_addr + i
            if curr_pc in reverse_symbol_table:
                current_func = reverse_symbol_table[curr_pc]

            if current_func:
                raw_inst = struct.unpack("<I", text_data[i : i + 4])[0]
                target_func = decode_riscv_jal(
                    curr_pc, raw_inst, reverse_symbol_table
                )
                if target_func and target_func != current_func:
                    call_graph.setdefault(current_func, set()).add(target_func)

    elif arch == "arm":
        for i in range(0, size - 4, 2):
            curr_pc = text_addr + i
            if curr_pc in reverse_symbol_table:
                current_func = reverse_symbol_table[curr_pc]

            if current_func:
                hw1 = struct.unpack("<H", text_data[i : i + 2])[0]
                hw2 = struct.unpack("<H", text_data[i + 2 : i + 4])[0]
                target_func = decode_arm_thumb2_bl(
                    curr_pc, hw1, hw2, reverse_symbol_table
                )
                if target_func and target_func != current_func:
                    call_graph.setdefault(current_func, set()).add(target_func)

    return {k: list(v) for k, v in call_graph.items()}


def resolve_driver_function_pointers(
    elffile, symbol_table, reverse_symbol_table, call_graph, isr_roots
):
    """Scan data/rodata sections to resolve function pointers inside driver descriptors and inject virtual call edges."""
    driver_ptrs_by_type = {}

    for section in elffile.iter_sections():
        if section.name in [
            ".rodata",
            ".data",
            "._cros_subsystem_drv",
            ".ec_rodata",
            "rodata",
        ]:
            sec_data = section.data()
            sh_addr = section["sh_addr"]
            sh_size = section["sh_size"]

            for sym_name, sym_addr in symbol_table.items():
                if (
                    sym_name.endswith("_drv")
                    or sym_name.endswith("_api")
                    or sym_name.startswith("__device_")
                ):
                    if sh_addr <= sym_addr < sh_addr + sh_size:
                        offset = sym_addr - sh_addr
                        end_offset = min(offset + 148, len(sec_data))
                        for i in range(offset, end_offset - 4, 4):
                            val = (
                                struct.unpack("<I", sec_data[i : i + 4])[0] & ~1
                            )
                            if val in reverse_symbol_table:
                                fn_name = reverse_symbol_table[val]
                                if fn_name != sym_name:
                                    driver_ptrs_by_type.setdefault(
                                        sym_name, []
                                    ).append(fn_name)

    for drv_name, fn_list in driver_ptrs_by_type.items():
        norm_drv = normalize_symbol_name(drv_name)
        if "charger" in norm_drv or "sm5803" in norm_drv or "chg" in norm_drv:
            for dispatcher in [
                "charger_init",
                "chg_init",
                "cros_charger_init",
                "battery_init",
            ]:
                if dispatcher in symbol_table:
                    call_graph.setdefault(dispatcher, []).extend(fn_list)
        elif "gpio" in norm_drv:
            for dispatcher in ["gpio_init", "gpio_set_level", "gpio_get_level"]:
                if dispatcher in symbol_table:
                    call_graph.setdefault(dispatcher, []).extend(fn_list)
        elif "i2c" in norm_drv:
            for dispatcher in ["i2c_write", "i2c_read", "i2c_transceive"]:
                if dispatcher in symbol_table:
                    call_graph.setdefault(dispatcher, []).extend(fn_list)

    # Resolve dynamic runtime callback structures (gpio_callback, uart_irq_callback, etc.)
    gpio_isrs = [
        f
        for f in isr_roots
        if any(k in f.lower() for k in ["gpio", "miwu", "intc"])
    ]
    uart_isrs = [f for f in isr_roots if "uart" in f.lower()]

    for sym_name, sym_addr in symbol_table.items():
        sym_lower = sym_name.lower()
        if any(
            k in sym_lower
            for k in [
                "gpio_cb",
                "gpio_callback",
                "interrupt_cb",
                "uart_cb",
                "i2c_cb",
            ]
        ):
            for section in elffile.iter_sections():
                sh_addr = section["sh_addr"]
                sh_size = section["sh_size"]
                if sh_addr <= sym_addr < sh_addr + sh_size:
                    sec_data = section.data()
                    offset = sym_addr - sh_addr
                    for i in range(
                        offset, min(len(sec_data) - 4, offset + 32), 4
                    ):
                        val = struct.unpack("<I", sec_data[i : i + 4])[0] & ~1
                        if val in reverse_symbol_table:
                            fn_name = reverse_symbol_table[val]
                            if (
                                fn_name != sym_name
                                and not fn_name.endswith(".c")
                                and not fn_name.endswith(".obj")
                            ):
                                target_isrs = (
                                    uart_isrs
                                    if "uart" in sym_lower
                                    else gpio_isrs
                                )
                                for isr in target_isrs:
                                    if fn_name not in call_graph.setdefault(
                                        isr, []
                                    ):
                                        call_graph[isr].append(fn_name)


def find_isr_roots(elffile, symbol_table, reverse_symbol_table):
    """Find ground truth registered hardware ISR vectors from _sw_isr_table in ELF."""
    isr_roots = set()
    sw_isr_addr = symbol_table.get("_sw_isr_table")

    if sw_isr_addr:
        num_irqs = symbol_table.get("CONFIG_NUM_IRQS", 256)
        for section in elffile.iter_sections():
            sh_addr = section["sh_addr"]
            sh_size = section["sh_size"]
            if sh_addr <= sw_isr_addr < sh_addr + sh_size:
                sec_data = section.data()
                offset = sw_isr_addr - sh_addr
                for i in range(num_irqs):
                    entry_offset = offset + (i * 8)
                    if entry_offset + 8 > len(sec_data):
                        break
                    isr_ptr = (
                        struct.unpack(
                            "<I", sec_data[entry_offset + 4 : entry_offset + 8]
                        )[0]
                        & ~1
                    )
                    if isr_ptr and isr_ptr != symbol_table.get(
                        "z_irq_spurious", 0
                    ):
                        isr_name = reverse_symbol_table.get(isr_ptr)
                        if isr_name:
                            isr_roots.add(isr_name)
                break

    # Supplemental: System exception, SVC, NMI, and fault vector handlers across ARM & RISC-V
    core_exception_symbols = [
        "_isr_wrapper",
        "z_arm_hard_fault",
        "z_arm_bus_fault",
        "z_arm_usage_fault",
        "z_arm_mpu_fault",
        "z_arm_svc",
        "z_arm_nmi",
        "z_riscv_fault",
        "z_riscv_fatal_error",
        "z_SysNmiOnReset",
        "bus_fault",
        "usage_fault",
        "mem_manage_fault",
        "exception_panic",
        "do_fault",
    ]
    for name in core_exception_symbols:
        if name in symbol_table:
            isr_roots.add(name)

    return sorted(list(isr_roots))


def check_path_matchers(path, matchers):
    """Check if any function in call stack path matches defined rule path matchers."""
    if not matchers:
        return True, None
    for func in path:
        norm = normalize_symbol_name(func)
        for m in matchers:
            if m.lower() in norm.lower():
                return True, norm
    return False, None


def evaluate_context_rules(
    roots, context_name, rules, call_graph, funcprops_db
):
    """Traverse static call graph from roots and evaluate declarative violation rules."""
    violations = []
    applicable_rules = [
        r for r in rules if context_name in r.get("contexts", [])
    ]
    target_exemption_prop = (
        "isr_ok" if context_name == "ISR" else "pre_kernel_ok"
    )

    for root in roots:
        visited = set()
        queue = [[(root, 0)]]

        while queue:
            path = queue.pop(0)
            curr_func, depth = path[-1]

            if curr_func in visited:
                continue
            visited.add(curr_func)

            norm_func = normalize_symbol_name(curr_func)
            curr_props = funcprops_db.get(norm_func, {}).get("props", [])

            # Context Barrier Pruning: If curr_func is annotated as safe for this context, prune call-tree traversal!
            if target_exemption_prop in curr_props or "isr_ok" in curr_props:
                continue

            best_matched_rule = None
            matched_symbol = None

            for rule in applicable_rules:
                exemption_props = rule.get("exemption_props", [])

                if norm_func in funcprops_db:
                    props = funcprops_db[norm_func]["props"]
                    prohibited_props = rule.get("prohibited_props", [])
                    path_matchers = rule.get("path_matchers", [])

                    has_matcher, symbol = check_path_matchers(
                        [p[0] for p in path], path_matchers
                    )

                    matches_prop = (
                        any(pp in props for pp in prohibited_props)
                        if prohibited_props
                        else True
                    )
                    matches_path = has_matcher if path_matchers else True

                    if not prohibited_props and not path_matchers:
                        matches_prop = not any(
                            ep in props for ep in exemption_props
                        )

                    if matches_prop and matches_path:
                        if path_matchers:
                            best_matched_rule = rule
                            matched_symbol = symbol
                            break
                        elif not best_matched_rule:
                            best_matched_rule = rule
                            matched_symbol = symbol

            if best_matched_rule:
                violations.append(
                    {
                        "category": best_matched_rule["category"],
                        "rule": best_matched_rule["rule_id"],
                        "description": best_matched_rule["description"],
                        "root": root,
                        "violating_func": curr_func,
                        "norm_func": norm_func,
                        "matched_symbol": matched_symbol,
                        "props": funcprops_db.get(norm_func, {}).get(
                            "props", []
                        ),
                        "path": [p[0] for p in path],
                    }
                )

            if curr_func in call_graph:
                for callee in call_graph[curr_func]:
                    if callee not in visited:
                        queue.append(list(path) + [(callee, len(path))])

    return violations


def analyze_elf(elf_path, rules, funcprops_db):
    symbol_table = {}
    reverse_symbol_table = {}
    arch = "unknown"

    pre_kernel_roots = set()
    ec_application_hooks = set()

    with open(elf_path, "rb") as f:
        elffile = ELFFile(f)
        machine = elffile["e_machine"]
        if "RISC-V" in machine or "EM_RISCV" in machine:
            arch = "riscv"
        elif "ARM" in machine or "EM_ARM" in machine:
            arch = "arm"

        for section in elffile.iter_sections():
            if isinstance(section, SymbolTableSection):
                for symbol in section.iter_symbols():
                    if symbol.name and symbol["st_value"]:
                        name = symbol.name
                        if name.startswith("$") or name.startswith("."):
                            continue
                        addr = symbol["st_value"] & ~1
                        symbol_table[name] = addr
                        existing = reverse_symbol_table.get(addr)
                        if (
                            not existing
                            or existing.endswith(".c")
                            or existing.endswith(".obj")
                        ):
                            reverse_symbol_table[addr] = name

        post_kernel_start = symbol_table.get(
            "__init_POST_KERNEL_start", float("inf")
        )

        for section in elffile.iter_sections():
            if section.name == "initlevel":
                data = section.data()
                sh_addr = section["sh_addr"]
                for offset in range(0, len(data), 8):
                    entry_addr = sh_addr + offset
                    if entry_addr < post_kernel_start:
                        func_ptr = struct.unpack(
                            "<I", data[offset : offset + 4]
                        )[0]
                        func_addr = func_ptr & ~1
                        func_name = reverse_symbol_table.get(func_addr)
                        if func_name:
                            pre_kernel_roots.add(func_name)

            elif section.name == "zephyr_shim_hook_HOOK_INIT_area":
                data = section.data()
                for offset in range(0, len(data), 8):
                    func_ptr = struct.unpack("<I", data[offset : offset + 4])[0]
                    func_addr = func_ptr & ~1
                    func_name = reverse_symbol_table.get(func_addr)
                    if func_name:
                        ec_application_hooks.add(func_name)

        call_graph = parse_call_graph_pure_python(
            elffile, reverse_symbol_table, arch
        )
        isr_roots = find_isr_roots(elffile, symbol_table, reverse_symbol_table)
        resolve_driver_function_pointers(
            elffile, symbol_table, reverse_symbol_table, call_graph, isr_roots
        )

    violations = []
    violations.extend(
        evaluate_context_rules(
            isr_roots, "ISR", rules, call_graph, funcprops_db
        )
    )
    violations.extend(
        evaluate_context_rules(
            sorted(list(pre_kernel_roots)),
            "PRE_KERNEL",
            rules,
            call_graph,
            funcprops_db,
        )
    )

    return (
        violations,
        arch,
        len(symbol_table),
        len(call_graph),
        len(isr_roots),
        len(pre_kernel_roots),
        len(ec_application_hooks),
    )


def main():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    default_db = os.path.join(script_dir, "funcprops.json")
    default_rules = os.path.join(script_dir, "rules.json")

    parser = argparse.ArgumentParser(
        description="Declarative Static violation analyzer for @funcprops compliance."
    )
    parser.add_argument(
        "elf_files", nargs="+", help="Path to compiled .elf binary targets."
    )
    parser.add_argument(
        "--db",
        default=default_db,
        help=f"Path to funcprops.json file. Default: {default_db}",
    )
    parser.add_argument(
        "--rules",
        default=default_rules,
        help=f"Path to declarative rules.json config. Default: {default_rules}",
    )

    args = parser.parse_args()

    if not os.path.exists(args.db):
        print(f"[ERROR] Database file not found: {args.db}")
        print(
            "Please run extract_funcprops.py first to generate funcprops.json."
        )
        sys.exit(1)

    rules = load_rules(args.rules)
    with open(args.db, "r", encoding="utf-8") as f:
        funcprops_db = json.load(f)

    for elf in args.elf_files:
        print(f"==================================================")
        print(f"Firmware ELF Target: {elf}")
        (
            violations,
            arch,
            sym_count,
            func_count,
            isr_count,
            pre_count,
            hook_count,
        ) = analyze_elf(elf, rules, funcprops_db)
        print(
            f"Architecture: {arch.upper()} | Symbols: {sym_count} | Compiled Funcs: {func_count}"
        )
        print(
            f"Audited Entry Points: {isr_count} ISRs | {pre_count} Pre-Kernel Inits (`initlevel` section)"
        )
        print(f"Excluded EC App Hooks: {hook_count} (`HOOK_INIT` section)")
        print(f"Total Verified Violations: {len(violations)}\n")

        rule_groups = {}
        for v in violations:
            rule_groups[v["rule"]] = rule_groups.get(v["rule"], 0) + 1

        print("Violation Rule Summary:")
        for r, cnt in rule_groups.items():
            print(f"  - {r}: {cnt}")
        print()

        file_map = funcprops_db.get("_file_map", {})

        def get_rel_src_link(func_name):
            file_candidates = []
            entry = funcprops_db.get(func_name)
            if isinstance(entry, dict) and "file" in entry:
                file_candidates.append(entry["file"])

            raw_cand = file_map.get(func_name, [])
            if isinstance(raw_cand, list):
                file_candidates.extend(raw_cand)
            elif isinstance(raw_cand, str) and raw_cand:
                file_candidates.append(raw_cand)

            if not file_candidates:
                return " (file unknown)"

            best_file = file_candidates[0]
            elf_lower = elf.lower()

            def score_cand(cand):
                cand_l = cand.lower()
                score = 0
                if (
                    "ruby" in elf_lower
                    or "riven" in elf_lower
                    or "uldren" in elf_lower
                ):
                    if "npcx" in cand_l or "itim" in cand_l:
                        score += 10
                    elif "cortex_m" in cand_l or "arm" in cand_l:
                        score += 5
                elif "domika" in elf_lower:
                    if "it8xxx2" in cand_l or "ite" in cand_l:
                        score += 10
                    elif "riscv" in cand_l:
                        score += 5
                elif "lapis" in elf_lower:
                    if "rts5912" in cand_l or "rts" in cand_l:
                        score += 10
                    elif "cortex_m" in cand_l or "arm" in cand_l:
                        score += 5
                if (
                    "sim" in cand_l
                    or "native" in cand_l
                    or "arcv2" in cand_l
                    or "litex" in cand_l
                ):
                    score -= 5
                return score

            best_file = max(file_candidates, key=score_cand)

            idx = best_file.find("/src/")
            rel_path = (
                best_file[idx + 5 :]
                if idx != -1
                else os.path.basename(best_file)
            )
            return f" ({rel_path})"

        for idx, v in enumerate(violations, 1):
            norm_root = normalize_symbol_name(v["root"])

            print(f"Violation #{idx} [{v['category']} | `{v['rule']}`]:")
            print(
                f"  Root Entry Point: '{norm_root}'{get_rel_src_link(norm_root)}"
            )
            print(
                f"  Target Symbol: '{v['norm_func']}' [Props: {', '.join(v['props']) if v['props'] else 'NONE'}]{get_rel_src_link(v['norm_func'])}"
            )
            print(f"  Description: {v['description']}")
            print(f"  Compiled Call Stack Trace:")
            for depth, f_sym in enumerate(v["path"]):
                norm_sym = normalize_symbol_name(f_sym)
                print(f"    {depth}. {norm_sym}(){get_rel_src_link(norm_sym)}")
            print("-" * 50)


if __name__ == "__main__":
    main()
