#!/usr/bin/env python3
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Unified declarative static violation analyzer for @funcprops compliance.

Audits compiled firmware ELF binaries across execution contexts:
  - ISR Context: hardware interrupts (_sw_isr_table) and fault vectors.
  - Pre-Kernel Context: early boot initialization hooks (.initlevel / PRE_KERNEL_1 / PRE_KERNEL_2).
  - Sysworkq Context: system workqueue hooks (DECLARE_HOOK) and deferred tasks (DECLARE_DEFERRED).

Operates with ZERO external CLI dependencies using a pure-Python instruction disassembler
(ARM Thumb/Thumb-2, RISC-V RV32IC, and x86).
"""

import argparse
from collections import deque
from dataclasses import asdict
from dataclasses import dataclass
import json
import os
import re
import struct
import sys
from typing import Any, Dict, List, Optional, Set, Tuple

# pylint: disable=import-error
from elftools.elf.elffile import ELFFile
from elftools.elf.sections import SymbolTableSection


# pylint: enable=import-error


_ARM_THUMB_BIT_MASK = ~1

# Audit/assertion/panic helpers that reference blocking functions internally and should
# not themselves be traversed as blocking violations.
DEFAULT_IGNORED_FUNCTIONS: Set[str] = {
    "sysworkq_audit_check_non_blocking",
    "report_violation",
    "assert_post_action",
    "zassert_fail",
    "zassert_post_action",
    "z_zassert",
    "__assert_fail",
    "k_sys_fatal_error_handler",
    "command_crash",
    "cpanic",
    "panic",
    "panic_assert_fail",
    "software_panic",
    "arch_system_halt",
    "z_arm_fatal_error",
    "z_riscv_fatal_error",
}

_HOOK_START_RE = re.compile(r"^_zephyr_shim_hook_(HOOK_[A-Z0-9_]+)_list_start$")


def get_elf_arch(elffile: ELFFile) -> str:
    """Detects CPU architecture (arm, riscv, x86) from ELF header."""
    mach = elffile.header.get("e_machine")
    if isinstance(mach, str):
        if "ARM" in mach:
            return "arm"
        if "RISCV" in mach:
            return "riscv"
        if "386" in mach or "X86" in mach:
            return "x86"
    return "unknown"


def strip_lto_mangling(name: str) -> str:
    """Strips compiler/LTO mangling prefixes and suffixes."""
    name = re.sub(
        r"\.(?:constprop|isra|part|lto_priv|cold|hot)(?:\.[0-9a-zA-Z_]+)*",
        "",
        name,
    )
    name = re.sub(r"__device_dts_ord_\d+_", "__device_", name)
    return name


def normalize_symbol_name(name: str) -> str:
    """Normalizes symbol names by stripping leading underscores, LTO tags, and syscall wrappers."""
    stripped = strip_lto_mangling(name.lstrip("_"))
    return re.sub(r"^z_(?:impl|vrfy)_", "", stripped)


def get_symbol_props(sym_name: str, funcprops_db: dict) -> List[str]:
    """Retrieves function properties from funcprops_db, supporting alias unwrapping."""
    norm = strip_lto_mangling(sym_name)
    if norm in funcprops_db:
        return funcprops_db[norm].get("props", [])

    # Strip z_impl_ or z_vrfy_ prefix
    unwrapped = re.sub(r"^z_(?:impl|vrfy)_", "", norm)
    if unwrapped in funcprops_db:
        return funcprops_db[unwrapped].get("props", [])

    # Also try stripped leading underscores
    bare = norm.lstrip("_")
    if bare in funcprops_db:
        return funcprops_db[bare].get("props", [])

    return []


# ==============================================================================
# Instruction Decoders (ARM Thumb-2, RISC-V RV32IC, x86)
# ==============================================================================


def decode_arm_thumb2_instruction(
    curr_pc: int,
    hw1: int,
    hw2: Optional[int],
    reverse_symbol_table: Dict[int, str],
) -> Tuple[int, Optional[str]]:
    """Decodes ARM Thumb/Thumb-2 instruction and returns (width, target_symbol).

    Width is 4 for 32-bit Thumb instructions, 2 for 16-bit instructions.
    Decodes BL, BLX, B.W (tail call), and 16-bit B (tail call).
    """
    is_32bit = (hw1 & 0xE000) == 0xE000 and (hw1 & 0x1800) != 0

    if is_32bit:
        width = 4
        if hw2 is None:
            return width, None

        # 32-bit BL ((hw1 & 0xF800) == 0xF000 and (hw2 & 0xD000) == 0xD000)
        # 32-bit BLX ((hw1 & 0xF800) == 0xF000 and (hw2 & 0xD000) == 0xC000)
        # 32-bit B.W ((hw1 & 0xF800) == 0xF000 and (hw2 & 0xD000) == 0x9000)
        if (hw1 & 0xF800) == 0xF000 and (hw2 & 0x8000) == 0x8000:
            sub_op = hw2 & 0xD000
            if sub_op in (0xD000, 0xC000, 0x9000):
                s = (hw1 >> 10) & 1
                j1 = (hw2 >> 13) & 1
                j2 = (hw2 >> 11) & 1
                imm10 = hw1 & 0x3FF
                imm11 = hw2 & 0x7FF

                i1 = ~(j1 ^ s) & 1
                i2 = ~(j2 ^ s) & 1

                offset = (
                    (s << 24)
                    | (i1 << 23)
                    | (i2 << 22)
                    | (imm10 << 12)
                    | (imm11 << 1)
                )
                if s:
                    offset -= 1 << 25

                target_pc = curr_pc + 4 + offset
                if sub_op == 0xC000:  # BLX to ARM clears bit 1
                    target_pc &= ~3

                target = reverse_symbol_table.get(target_pc & ~1)
                return width, target

        return width, None

    # 16-bit Thumb instructions
    width = 2
    # 16-bit unconditional branch B: 1110 0 imm11
    if (hw1 & 0xF800) == 0xE000:
        imm11 = hw1 & 0x7FF
        if imm11 & 0x400:
            offset = (imm11 << 1) - (1 << 12)
        else:
            offset = imm11 << 1

        target_pc = curr_pc + 4 + offset
        target = reverse_symbol_table.get(target_pc & ~1)
        return width, target

    return width, None


def decode_riscv_instruction(
    curr_pc: int,
    raw_inst: int,
    hw1: int,
    reverse_symbol_table: Dict[int, str],
) -> Tuple[int, Optional[str]]:
    """Decodes RISC-V RV32IC instruction and returns (width, target_symbol).

    Width is 4 for standard 32-bit instructions ((hw1 & 3) == 3), else 2.
    Decodes 32-bit JAL (both ra calls and x0 tail jumps) and 16-bit C.JAL / C.J.
    """
    is_32bit = (hw1 & 0x3) == 0x3

    if is_32bit:
        width = 4
        opcode = raw_inst & 0x7F
        # 32-bit JAL (opcode 0x6F)
        if opcode == 0x6F:
            imm20 = (raw_inst >> 31) & 1
            imm10_1 = (raw_inst >> 21) & 0x3FF
            imm11 = (raw_inst >> 20) & 1
            imm19_12 = (raw_inst >> 12) & 0xFF

            offset = (
                (imm20 << 20)
                | (imm19_12 << 12)
                | (imm11 << 11)
                | (imm10_1 << 1)
            )
            if imm20:
                offset -= 1 << 21

            target_pc = curr_pc + offset
            target = reverse_symbol_table.get(target_pc)
            return width, target

        return width, None

    # 16-bit RV32C compressed instructions
    width = 2
    # C.JAL: 001 imm 01 (RV32C only)
    # C.J:   101 imm 01
    op_quad = hw1 & 0xE003
    if op_quad in (0x2001, 0xA001):
        imm11 = (hw1 >> 12) & 1
        imm4 = (hw1 >> 11) & 1
        imm9_8 = (hw1 >> 9) & 0x3
        imm10 = (hw1 >> 8) & 1
        imm6 = (hw1 >> 7) & 1
        imm7 = (hw1 >> 6) & 1
        imm3_1 = (hw1 >> 3) & 0x7
        imm5 = (hw1 >> 2) & 1

        offset = (
            (imm11 << 11)
            | (imm10 << 10)
            | (imm9_8 << 8)
            | (imm7 << 7)
            | (imm6 << 6)
            | (imm5 << 5)
            | (imm4 << 4)
            | (imm3_1 << 1)
        )
        if imm11:
            offset -= 1 << 12

        target_pc = curr_pc + offset
        target = reverse_symbol_table.get(target_pc)
        return width, target

    return width, None


def decode_x86_calls(
    elffile: Any,
    reverse_symbol_table: Dict[int, str],
) -> Dict[str, Set[str]]:
    """Disassembles x86/x86_64 binary and decodes CALL (0xE8) and JMP (0xE9)."""
    call_graph: Dict[str, Set[str]] = {}
    symtab = elffile.get_section_by_name(".symtab")
    if not symtab:
        return call_graph

    for sym in symtab.iter_symbols():
        if sym.entry["st_info"]["type"] != "STT_FUNC":
            continue
        fn_name = strip_lto_mangling(sym.name)
        vaddr = sym.entry.st_value
        size = sym.entry.st_size
        if not fn_name or vaddr == 0:
            continue

        sec = None
        for s in elffile.iter_sections():
            if (
                s.header["sh_addr"]
                <= vaddr
                < s.header["sh_addr"] + s.header["sh_size"]
            ):
                sec = s
                break
        if not sec:
            continue

        sec_start = sec.header["sh_addr"]
        data = sec.data()
        start_off = vaddr - sec_start
        end_off = (
            min(start_off + size, len(data))
            if size > 0
            else min(start_off + 256, len(data))
        )
        fn_bytes = data[start_off:end_off]

        for idx in range(0, max(0, len(fn_bytes) - 4)):
            op = fn_bytes[idx]
            if op in (0xE8, 0xE9):
                rel32 = struct.unpack_from("<i", fn_bytes, idx + 1)[0]
                target_addr = vaddr + idx + 5 + rel32
                target_fn = reverse_symbol_table.get(target_addr)
                if target_fn and target_fn != fn_name:
                    call_graph.setdefault(fn_name, set()).add(target_fn)

    return call_graph


def parse_call_graph_pure_python(
    elffile: Any,
    reverse_symbol_table: Dict[int, str],
    arch: str,
) -> Dict[str, Set[str]]:
    """Disassembles binary text section in pure Python and builds static call graph."""
    if arch == "x86":
        return decode_x86_calls(elffile, reverse_symbol_table)

    call_graph: Dict[str, Set[str]] = {}
    text_sec = elffile.get_section_by_name(
        ".text"
    ) or elffile.get_section_by_name("text")
    if not text_sec:
        return call_graph

    text_data = text_sec.data()
    text_addr = text_sec["sh_addr"]
    size = len(text_data)

    current_func: Optional[str] = None
    pc_idx = 0

    while pc_idx < size - 2:
        curr_pc = text_addr + pc_idx

        # Check if a new function symbol begins at this address
        lookup_addr = (
            curr_pc & _ARM_THUMB_BIT_MASK if arch == "arm" else curr_pc
        )
        if lookup_addr in reverse_symbol_table:
            current_func = reverse_symbol_table[lookup_addr]

        if arch == "riscv":
            hw1 = struct.unpack_from("<H", text_data, pc_idx)[0]
            raw_inst = (
                struct.unpack_from("<I", text_data, pc_idx)[0]
                if pc_idx + 4 <= size
                else hw1
            )
            width, target = decode_riscv_instruction(
                curr_pc, raw_inst, hw1, reverse_symbol_table
            )
            if current_func and target and target != current_func:
                call_graph.setdefault(current_func, set()).add(target)
            pc_idx += width

        elif arch == "arm":
            hw1 = struct.unpack_from("<H", text_data, pc_idx)[0]
            hw2 = (
                struct.unpack_from("<H", text_data, pc_idx + 2)[0]
                if pc_idx + 4 <= size
                else None
            )
            width, target = decode_arm_thumb2_instruction(
                curr_pc, hw1, hw2, reverse_symbol_table
            )
            if current_func and target and target != current_func:
                call_graph.setdefault(current_func, set()).add(target)
            pc_idx += width

        else:
            pc_idx += 2

    return call_graph


def resolve_driver_function_pointers(
    elffile: Any,
    symbol_table: Dict[str, int],
    reverse_symbol_table: Dict[int, str],
    call_graph: Dict[str, Set[str]],
    isr_roots: Set[str],
) -> None:
    """Scans data/rodata to resolve function pointers inside driver descriptors
    and inject virtual call edges.
    """
    driver_ptrs_by_type: Dict[str, List[str]] = {}

    for section in elffile.iter_sections():
        sec_name = getattr(section, "name", "")
        if sec_name in [
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
                                struct.unpack("<I", sec_data[i : i + 4])[0]
                                & _ARM_THUMB_BIT_MASK
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
                    call_graph.setdefault(dispatcher, set()).update(fn_list)
        elif "gpio" in norm_drv:
            for dispatcher in ["gpio_init", "gpio_set_level", "gpio_get_level"]:
                if dispatcher in symbol_table:
                    call_graph.setdefault(dispatcher, set()).update(fn_list)
        elif "i2c" in norm_drv:
            for dispatcher in ["i2c_write", "i2c_read", "i2c_transceive"]:
                if dispatcher in symbol_table:
                    call_graph.setdefault(dispatcher, set()).update(fn_list)

    # Dynamic runtime callback structures (gpio_callback, uart_irq_callback)
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
                        val = (
                            struct.unpack("<I", sec_data[i : i + 4])[0]
                            & _ARM_THUMB_BIT_MASK
                        )
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
                                    call_graph.setdefault(isr, set()).add(
                                        fn_name
                                    )


# ==============================================================================
# Unified Root Discovery (ISR, PRE_KERNEL, SYSWORKQ)
# ==============================================================================


@dataclass(frozen=True)
class AnalysisRoot:
    """Represents a root execution entry point."""

    context: str  # "ISR", "PRE_KERNEL", or "SYSWORKQ"
    kind: str  # e.g. "hardware_isr", "pre_kernel_init", "hook", "deferred"
    name: str  # Function symbol name
    source: str  # Description or hook type
    address: int


def discover_isr_roots(
    elffile: Any,
    symbol_table: Dict[str, int],
    reverse_symbol_table: Dict[int, str],
) -> List[AnalysisRoot]:
    """Finds ground truth registered hardware ISR vectors from _sw_isr_table and fault vectors."""
    roots: List[AnalysisRoot] = []
    seen: Set[str] = set()

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
                        & _ARM_THUMB_BIT_MASK
                    )
                    if isr_ptr and isr_ptr != symbol_table.get(
                        "z_irq_spurious", 0
                    ):
                        isr_name = reverse_symbol_table.get(isr_ptr)
                        if isr_name and isr_name not in seen:
                            seen.add(isr_name)
                            roots.append(
                                AnalysisRoot(
                                    context="ISR",
                                    kind="hardware_isr",
                                    name=isr_name,
                                    source=f"_sw_isr_table[{i}]",
                                    address=isr_ptr,
                                )
                            )

    # Core fault and exception handlers
    for sym_name, addr in symbol_table.items():
        norm = normalize_symbol_name(sym_name)
        if any(
            f in norm.lower()
            for f in [
                "fault_handler",
                "hard_fault",
                "_fault",
                "_isr_wrapper",
                "nmi_handler",
            ]
        ):
            if (
                sym_name not in seen
                and not sym_name.startswith("z_vrfy_")
                and not sym_name.startswith("z_impl_")
            ):
                seen.add(sym_name)
                roots.append(
                    AnalysisRoot(
                        context="ISR",
                        kind="exception_handler",
                        name=sym_name,
                        source="vector_table",
                        address=addr,
                    )
                )

    return roots


def discover_pre_kernel_roots(
    elffile: Any,
    symbol_table: Dict[str, int],
    reverse_symbol_table: Dict[int, str],
) -> List[AnalysisRoot]:
    """Finds early boot initialization hooks (.initlevel / PRE_KERNEL_1 / PRE_KERNEL_2)."""
    roots: List[AnalysisRoot] = []
    seen: Set[str] = set()

    post_kernel_start = symbol_table.get("__init_POST_KERNEL_start")
    if not post_kernel_start:
        return roots

    for sec in elffile.iter_sections():
        sec_name = getattr(sec, "name", "")
        if "initlevel" in sec_name:
            sec_data = sec.data()
            sh_addr = sec.header["sh_addr"]

            for offset in range(0, len(sec_data) - 4, 8):
                curr_addr = sh_addr + offset
                if curr_addr >= post_kernel_start:
                    break
                val = (
                    struct.unpack("<I", sec_data[offset : offset + 4])[0]
                    & _ARM_THUMB_BIT_MASK
                )
                if val in reverse_symbol_table:
                    fn_name = reverse_symbol_table[val]
                    if (
                        fn_name not in seen
                        and not fn_name.endswith(".c")
                        and not fn_name.endswith(".obj")
                    ):
                        seen.add(fn_name)
                        roots.append(
                            AnalysisRoot(
                                context="PRE_KERNEL",
                                kind="pre_kernel_init",
                                name=fn_name,
                                source=f"{sec_name}+0x{offset:x}",
                                address=val,
                            )
                        )

    return roots


def get_rel_src_link(func_name: str, elf_path: str, funcprops_db: dict) -> str:
    """Returns relative source file path for a function from funcprops_db."""
    file_map = funcprops_db.get("_file_map", {})
    file_candidates = []

    entry = funcprops_db.get(func_name)
    if isinstance(entry, dict) and "file" in entry:
        file_candidates.append(entry["file"])

    raw_cand = file_map.get(func_name, [])
    if isinstance(raw_cand, list):
        file_candidates.extend(raw_cand)
    elif isinstance(raw_cand, str) and raw_cand:
        file_candidates.append(raw_cand)

    # Try unwrapped / bare name if no candidate found
    if not file_candidates:
        unwrapped = re.sub(r"^z_(?:impl|vrfy)_", "", func_name)
        if unwrapped != func_name:
            entry = funcprops_db.get(unwrapped)
            if isinstance(entry, dict) and "file" in entry:
                file_candidates.append(entry["file"])
            raw_cand = file_map.get(unwrapped, [])
            if isinstance(raw_cand, list):
                file_candidates.extend(raw_cand)
            elif isinstance(raw_cand, str) and raw_cand:
                file_candidates.append(raw_cand)

    if not file_candidates:
        return ""

    best_file = file_candidates[0]
    elf_lower = elf_path.lower()

    def score_cand(cand: str) -> int:
        cand_l = cand.lower()
        score = 0
        if any(board in elf_lower for board in ["ruby", "riven", "uldren"]):
            if "npcx" in cand_l or "itim" in cand_l:
                score += 10
            elif "cortex_m" in cand_l or "arm" in cand_l:
                score += 5
        elif any(
            board in elf_lower for board in ["domika", "veluza", "corsola"]
        ):
            if "it8xxx2" in cand_l or "ite" in cand_l:
                score += 10
            elif "riscv" in cand_l:
                score += 5
        elif "lapis" in elf_lower or "jubilant" in elf_lower:
            if "rts5912" in cand_l or "rts" in cand_l:
                score += 10
            elif "cortex_m" in cand_l or "arm" in cand_l:
                score += 5

        if any(bad in cand_l for bad in ["sim", "native", "arcv2", "litex"]):
            score -= 5
        return score

    best_file = max(file_candidates, key=score_cand)

    idx = best_file.find("/src/")
    rel_path = (
        best_file[idx + 5 :] if idx != -1 else os.path.basename(best_file)
    )
    return rel_path


def _find_section_for_vaddr(elffile: Any, vaddr: int) -> Optional[Any]:
    """Finds section containing the virtual address."""
    for s in elffile.iter_sections():
        sh_addr = s.header["sh_addr"] if hasattr(s, "header") else s["sh_addr"]
        sh_size = s.header["sh_size"] if hasattr(s, "header") else s["sh_size"]
        if sh_addr <= vaddr < sh_addr + sh_size:
            return s
    return None


def discover_sysworkq_roots(
    elffile: Any,
    symbol_table: Dict[str, int],
    reverse_symbol_table: Dict[int, str],
) -> List[AnalysisRoot]:
    """Finds hooks (DECLARE_HOOK) and deferred tasks (DECLARE_DEFERRED).

    Excludes HOOK_INIT because it executes during synchronous boot initialization,
    not on k_sys_work_q.
    """
    roots: List[AnalysisRoot] = []
    seen: Set[str] = set()

    is_64bit = getattr(elffile, "elfclass", 32) == 64
    ptr_size = 8 if is_64bit else 4
    ptr_fmt = "<Q" if is_64bit else "<I"
    hook_info_size = 16 if is_64bit else 8

    # 1. Extract hooks from _zephyr_shim_hook_<HOOK_TYPE>_list_start .. _end
    for sym_name, start_addr in list(symbol_table.items()):
        m = _HOOK_START_RE.match(sym_name)
        if not m:
            continue
        hook_type = m.group(1)
        if hook_type == "HOOK_INIT":
            continue
        end_sym = f"_zephyr_shim_hook_{hook_type}_list_end"
        end_addr = symbol_table.get(end_sym)
        if end_addr is None or end_addr <= start_addr:
            continue

        sec = _find_section_for_vaddr(elffile, start_addr)
        if not sec:
            continue
        sec_start = (
            sec.header["sh_addr"] if hasattr(sec, "header") else sec["sh_addr"]
        )
        sec_data = sec.data()

        for addr in range(start_addr, end_addr, hook_info_size):
            offset = addr - sec_start
            if offset < 0 or offset + ptr_size > len(sec_data):
                break
            routine_ptr = struct.unpack_from(ptr_fmt, sec_data, offset)[0]
            norm_ptr = (
                routine_ptr & _ARM_THUMB_BIT_MASK
                if not is_64bit
                else routine_ptr
            )
            fn_name = reverse_symbol_table.get(
                norm_ptr
            ) or reverse_symbol_table.get(routine_ptr)
            if fn_name and fn_name not in seen:
                seen.add(fn_name)
                roots.append(
                    AnalysisRoot(
                        context="SYSWORKQ",
                        kind="hook",
                        name=fn_name,
                        source=hook_type,
                        address=norm_ptr,
                    )
                )

    # 2. Extract DECLARE_DEFERRED routines (*_work_data)
    for sym_name, addr in symbol_table.items():
        if sym_name.endswith("_work_data"):
            routine = sym_name[: -len("_work_data")]
            if routine not in seen and routine in symbol_table:
                seen.add(routine)
                roots.append(
                    AnalysisRoot(
                        context="SYSWORKQ",
                        kind="deferred",
                        name=routine,
                        source="DECLARE_DEFERRED",
                        address=symbol_table[routine],
                    )
                )

    return roots


# ==============================================================================
# Violation Evaluation Engine
# ==============================================================================


@dataclass(frozen=True)
class ContextViolation:
    """Represents a verified rule violation along a static call graph path."""

    rule_id: str
    category: str
    context: str
    root: AnalysisRoot
    target: str
    target_props: List[str]
    call_chain: List[str]
    description: str


def check_path_matchers(path: List[str], matchers: List[str]) -> bool:
    """Checks if any function along the call path matches specified keyword matchers."""
    if not matchers:
        return True
    for fn in path:
        fn_lower = fn.lower()
        if any(m.lower() in fn_lower for m in matchers):
            return True
    return False


def evaluate_violations(
    roots: List[AnalysisRoot],
    call_graph: Dict[str, Set[str]],
    rules: List[dict],
    funcprops_db: dict,
    allowlist: Set[str],
    include_timeout_primitives: bool = True,
) -> List[ContextViolation]:
    """Traverses call graph from each root and checks declarative context rules."""
    violations: List[ContextViolation] = []
    seen_violation_keys: Set[Tuple[str, str, str]] = set()

    for root in roots:
        root_norm = strip_lto_mangling(root.name)
        if root_norm in allowlist or root_norm in DEFAULT_IGNORED_FUNCTIONS:
            continue

        # BFS Queue holds (current_fn, call_path)
        queue = deque([(root_norm, [root_norm])])
        visited = {root_norm}

        while queue:
            curr_fn, path = queue.popleft()
            callees = call_graph.get(curr_fn, set())

            for callee in callees:
                norm_callee = strip_lto_mangling(callee)
                if (
                    norm_callee in allowlist
                    or norm_callee in DEFAULT_IGNORED_FUNCTIONS
                ):
                    continue

                callee_props = get_symbol_props(norm_callee, funcprops_db)
                new_path = path + [norm_callee]
                callee_violated = False

                # Evaluate all matching rules against this callee
                for rule in rules:
                    if root.context not in rule.get("contexts", []):
                        continue

                    # Check required CLI flags
                    req_flag = rule.get("requires_flag")
                    if (
                        req_flag == "include_timeout_primitives"
                        and not include_timeout_primitives
                    ):
                        continue

                    # Check exemption properties: if callee has an exemption, rule does not fire
                    exemption_props = rule.get("exemption_props", [])
                    if any(ex in callee_props for ex in exemption_props):
                        continue

                    # Check prohibited properties: must all be present
                    prohibited_props = rule.get("prohibited_props", [])
                    if prohibited_props and not all(
                        p in callee_props for p in prohibited_props
                    ):
                        continue

                    # Check required absent properties: none may be present
                    absent_props = rule.get("required_absent_props", [])
                    if any(ab in callee_props for ab in absent_props):
                        continue

                    # Check path matchers (e.g. mutex_lock keywords or I2C keywords)
                    matchers = rule.get("path_matchers", [])
                    if matchers:
                        rule_id = rule.get("rule_id", "")
                        # Rules targeting specific functions (MUTEX_IN_ISR,
                        # MUTEX_IN_PRE_KERNEL, I2C_IN_ISR) or when match_target is
                        # True or prohibited_props is empty: only match against
                        # norm_callee (the target symbol being called).
                        # Only SLEEP_IN_CRITICAL_SECTION matches against the ancestor call path.
                        if (
                            rule.get("match_target", False)
                            or rule_id
                            in (
                                "MUTEX_IN_ISR",
                                "MUTEX_IN_PRE_KERNEL",
                                "I2C_IN_ISR",
                            )
                            or not prohibited_props
                        ):
                            if not any(
                                m.lower() in norm_callee.lower()
                                for m in matchers
                            ):
                                continue
                        else:
                            if not check_path_matchers(path, matchers):
                                continue

                    # Violation detected!
                    callee_violated = True
                    v_key = (rule["rule_id"], root.name, norm_callee)
                    if v_key not in seen_violation_keys:
                        seen_violation_keys.add(v_key)
                        violations.append(
                            ContextViolation(
                                rule_id=rule["rule_id"],
                                category=rule["category"],
                                context=root.context,
                                root=root,
                                target=norm_callee,
                                target_props=callee_props,
                                call_chain=new_path,
                                description=rule["description"],
                            )
                        )

                # Stop expanding inside a violating sink (e.g. crec_usleep -> k_usleep,
                # k_msleep -> k_sleep)
                if callee_violated:
                    continue

                # Context-adaptive boundary pruning: do not traverse into internal
                # callees of functions certified safe for the current execution context.
                if root.context == "ISR" and "isr_ok" in callee_props:
                    continue
                if (
                    root.context == "PRE_KERNEL"
                    and "pre_kernel_ok" in callee_props
                ):
                    continue

                if norm_callee not in visited:
                    visited.add(norm_callee)
                    queue.append((norm_callee, new_path))

    return violations


def analyze_elf(
    elf_path: str,
    rules: List[dict],
    funcprops_db: dict,
    contexts: Set[str],
    allowlist: Set[str],
    include_timeout_primitives: bool = True,
) -> Tuple[List[ContextViolation], List[AnalysisRoot], str, int]:
    """Audits a compiled ELF binary for context violations."""
    with open(elf_path, "rb") as f:
        elffile = ELFFile(f)
        arch = get_elf_arch(elffile)

        # Build symbol tables
        symbol_table: Dict[str, int] = {}
        reverse_symbol_table: Dict[int, str] = {}
        is_func_sym: Dict[int, bool] = {}

        symtab = elffile.get_section_by_name(".symtab")
        if symtab and isinstance(symtab, SymbolTableSection):
            for sym in symtab.iter_symbols():
                sym_name = sym.name
                if (
                    not sym_name
                    or sym_name.startswith("$")
                    or sym_name.startswith(".")
                    or sym_name.endswith(".c")
                    or sym_name.endswith(".obj")
                ):
                    continue
                addr = sym.entry["st_value"]
                if addr == 0:
                    continue

                st_info_type = sym.entry["st_info"]["type"]
                is_func = st_info_type == "STT_FUNC"

                # Clear Thumb bit for reverse address lookups
                clean_addr = (
                    addr & _ARM_THUMB_BIT_MASK if arch == "arm" else addr
                )
                clean_name = strip_lto_mangling(sym_name)

                symbol_table[clean_name] = clean_addr
                if sym_name != clean_name:
                    symbol_table[sym_name] = clean_addr

                # Prefer STT_FUNC symbols; never let non-func overwrite STT_FUNC
                if clean_addr not in reverse_symbol_table:
                    reverse_symbol_table[clean_addr] = clean_name
                    is_func_sym[clean_addr] = is_func
                elif is_func and not is_func_sym.get(clean_addr, False):
                    reverse_symbol_table[clean_addr] = clean_name
                    is_func_sym[clean_addr] = True

        # Discover roots across requested contexts
        roots: List[AnalysisRoot] = []
        isr_root_names: Set[str] = set()

        if "ISR" in contexts:
            isr_roots = discover_isr_roots(
                elffile, symbol_table, reverse_symbol_table
            )
            roots.extend(isr_roots)
            isr_root_names = {r.name for r in isr_roots}

        if "PRE_KERNEL" in contexts:
            roots.extend(
                discover_pre_kernel_roots(
                    elffile, symbol_table, reverse_symbol_table
                )
            )

        if "SYSWORKQ" in contexts:
            roots.extend(
                discover_sysworkq_roots(
                    elffile, symbol_table, reverse_symbol_table
                )
            )

        # Build call graph using pure-Python disassembler
        call_graph = parse_call_graph_pure_python(
            elffile, reverse_symbol_table, arch
        )

        # Resolve indirect function pointers in driver tables
        resolve_driver_function_pointers(
            elffile,
            symbol_table,
            reverse_symbol_table,
            call_graph,
            isr_root_names,
        )

        # Evaluate context violations
        violations = evaluate_violations(
            roots=roots,
            call_graph=call_graph,
            rules=rules,
            funcprops_db=funcprops_db,
            allowlist=allowlist,
            include_timeout_primitives=include_timeout_primitives,
        )

        return violations, roots, arch, len(symbol_table)


def main(argv=None):
    """Main CLI entry point for analyze_funcprops."""
    script_dir = os.path.dirname(os.path.abspath(__file__))
    default_db = os.path.join(script_dir, "funcprops.json")
    default_rules = os.path.join(script_dir, "rules.json")

    parser = argparse.ArgumentParser(
        description="Unified declarative static violation analyzer for @funcprops compliance."
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
    parser.add_argument(
        "--contexts",
        default="isr,pre_kernel,sysworkq",
        help=(
            "Comma-separated execution contexts to audit "
            "(isr,pre_kernel,sysworkq). Default: isr,pre_kernel,sysworkq"
        ),
    )
    parser.add_argument(
        "--exclude-timeout-primitives",
        action="store_false",
        dest="include_timeout_primitives",
        default=True,
        help="Exclude timeout-conditional sleep primitives (k_mutex_lock, k_sem_take) on sysworkq.",
    )
    parser.add_argument(
        "--allowlist",
        action="append",
        default=[],
        help="Comma-separated function symbols to exclude from violation reporting.",
    )
    parser.add_argument(
        "--json",
        action="store_true",
        help="Emit structured JSON violation report.",
    )
    parser.add_argument(
        "--warn-only",
        action="store_true",
        help="Exit with status 0 even if context violations are detected.",
    )

    args = parser.parse_args(argv)

    if not os.path.exists(args.db):
        # pylint: disable=import-outside-toplevel,import-error
        import extract_funcprops

        extract_funcprops.scan_headers_and_sources(
            extract_funcprops.get_default_paths(), args.db
        )

    with open(args.rules, "r", encoding="utf-8") as f:
        rules = json.load(f)

    with open(args.db, "r", encoding="utf-8") as f:
        funcprops_db = json.load(f)

    allow_set: Set[str] = set()
    for item in args.allowlist:
        for fn in item.split(","):
            fn = fn.strip()
            if fn:
                allow_set.add(fn)

    target_contexts = {
        c.strip().upper() for c in args.contexts.split(",") if c.strip()
    }

    all_reports: Dict[str, Any] = {}
    total_violations = 0

    for elf in args.elf_files:
        if not os.path.isfile(elf):
            print(f"[ERROR] ELF file not found: {elf}", file=sys.stderr)
            return 2

        violations, roots, arch, sym_count = analyze_elf(
            elf_path=elf,
            rules=rules,
            funcprops_db=funcprops_db,
            contexts=target_contexts,
            allowlist=allow_set,
            include_timeout_primitives=args.include_timeout_primitives,
        )

        total_violations += len(violations)
        all_reports[elf] = {
            "arch": arch,
            "symbols_count": sym_count,
            "roots_analyzed": len(roots),
            "violations_count": len(violations),
            "violations": [
                {
                    "rule_id": v.rule_id,
                    "category": v.category,
                    "context": v.context,
                    "root": asdict(v.root),
                    "root_file": get_rel_src_link(
                        v.root.name, elf, funcprops_db
                    ),
                    "target": v.target,
                    "target_props": v.target_props,
                    "target_file": get_rel_src_link(
                        v.target, elf, funcprops_db
                    ),
                    "call_chain": v.call_chain,
                    "call_chain_files": [
                        get_rel_src_link(fn, elf, funcprops_db)
                        for fn in v.call_chain
                    ],
                    "description": v.description,
                }
                for v in violations
            ],
        }

        if not args.json:
            print("==================================================")
            print(f"Firmware ELF Target: {elf}")
            print(f"Architecture: {arch.upper()} | Symbols: {sym_count}")
            ctx_str = ", ".join(sorted(target_contexts))
            print(f"Audited Entry Points: {len(roots)} roots across {ctx_str}")
            print(f"Total Verified Violations: {len(violations)}\n")

            for idx, v in enumerate(violations, 1):
                root_src = get_rel_src_link(v.root.name, elf, funcprops_db)
                root_src_str = f" ({root_src})" if root_src else ""
                target_src = get_rel_src_link(v.target, elf, funcprops_db)
                target_src_str = f" ({target_src})" if target_src else ""

                props_str = (
                    ", ".join(v.target_props) if v.target_props else "NONE"
                )
                print(f"Violation #{idx} [{v.category} | `{v.rule_id}`]:")
                print(f"  Context: {v.context}")
                root_desc = f"'{v.root.name}' ({v.root.kind}: {v.root.source})"
                print(f"  Root Entry Point: {root_desc}{root_src_str}")
                print(
                    f"  Target Symbol: '{v.target}' [Props: {props_str}]{target_src_str}"
                )
                print(f"  Description: {v.description}")
                print("  Compiled Call Stack Trace:")
                for depth, fn in enumerate(v.call_chain):
                    fn_src = get_rel_src_link(fn, elf, funcprops_db)
                    fn_src_str = f" ({fn_src})" if fn_src else ""
                    print(f"    {depth}. {fn}(){fn_src_str}")
                print("-" * 50)

    if args.json:
        print(json.dumps(all_reports, indent=2))

    if total_violations > 0 and not args.warn_only:
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
