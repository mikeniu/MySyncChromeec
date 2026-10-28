#!/usr/bin/env python3
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Static violation analyzer for Zephyr system workqueue (k_sys_work_q) handlers.

Audits compiled firmware ELF binaries for blocking calls and synchronous I/O inside
system workqueue handlers:
  - DECLARE_HOOK routines that execute on k_sys_work_q (excluding boot/task-local hooks).
  - DECLARE_DEFERRED routines (*_work_data).
  - Statically defined k_work / k_work_delayable handlers (K_WORK_DEFINE / K_WORK_DELAYABLE_DEFINE).

Classifies violations into @funcprops-aligned rules:
  - SLEEP_IN_SYSWORKQ: Unconditional @sleep calls (without @no_wait).
  - IO_IN_SYSWORKQ: Synchronous bus/flash I/O (I2C, SPI, flash write/erase).
  - NO_WAIT_IN_SYSWORKQ: @sleep + @no_wait synchronization primitives
    (disable via --exclude-no-wait).
"""

import argparse
import json
import os
import re
import struct
import sys
from typing import Any, Dict, List, Optional, Set, Tuple


# Ensure current directory is in path for module imports
_SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
if _SCRIPT_DIR not in sys.path:
    sys.path.insert(0, _SCRIPT_DIR)

# pylint: disable=wrong-import-position,import-error
from analyze_funcprops import _ARM_THUMB_BIT_MASK
from analyze_funcprops import _find_section_for_vaddr
from analyze_funcprops import AnalysisRoot
from analyze_funcprops import ContextViolation
from analyze_funcprops import evaluate_violations
from analyze_funcprops import format_and_print_report
from analyze_funcprops import get_elf_arch
from analyze_funcprops import load_elf_symbols
from analyze_funcprops import parse_call_graph_pure_python
from analyze_funcprops import resolve_driver_function_pointers
from elftools.elf.elffile import ELFFile


# pylint: enable=wrong-import-position,import-error


_HOOK_START_RE = re.compile(r"^_zephyr_shim_hook_(HOOK_[A-Z0-9_]+)_list_start$")

# Hook types whose DECLARE_HOOK lists execute on k_sys_work_q.
# Excluded hook types:
#   - HOOK_INIT, HOOK_INIT_EARLY: run synchronously on main thread during boot.
#   - HOOK_SYSJUMP: runs in jump_to_image() with interrupts locked.
#   - HOOK_USB_PD_CONNECT, HOOK_USB_PD_DISCONNECT: run on dedicated PD_C* threads.
#   - HOOK_CHIPSET_PRE_INIT, STARTUP, RESUME, SUSPEND, RESUME_INIT, SUSPEND_COMPLETE,
#     SHUTDOWN, SHUTDOWN_COMPLETE, HARD_OFF: run on dedicated CHIPSET / ap_pwrseq thread.
_SYSWORKQ_HOOK_TYPES: Set[str] = {
    "HOOK_TICK",
    "HOOK_SECOND",
    "HOOK_AC_CHANGE",
    "HOOK_LID_CHANGE",
    "HOOK_TABLET_MODE_CHANGE",
    "HOOK_BASE_ATTACHED_CHANGE",
    "HOOK_POWER_BUTTON_CHANGE",
    "HOOK_BODY_DETECT_CHANGE",
    "HOOK_POWER_SUPPLY_CHANGE",
    "HOOK_CHIPSET_RESET",
    "HOOK_USB_PM_CHANGE",
}

# Functions ignored specifically during SYSWORKQ traversal:
#   - hook_notify: dispatches hook lists; SYSWORKQ hooks are audited directly as roots.
#   - k_mutex_unlock: releasing a mutex never blocks the calling workqueue item.
SYSWORKQ_DEFAULT_ALLOWLIST: Set[str] = {
    "hook_notify",
    "k_mutex_unlock",
}


def discover_sysworkq_roots(
    elffile: Any,
    symbol_table: Dict[str, int],
    reverse_symbol_table: Dict[int, str],
    symbol_sizes: Optional[Dict[str, int]] = None,
) -> List[AnalysisRoot]:
    """Discovers hooks, DECLARE_DEFERRED routines, and k_work handlers running on k_sys_work_q."""
    roots: List[AnalysisRoot] = []
    seen: Set[str] = set()

    is_64bit = getattr(elffile, "elfclass", 32) == 64
    ptr_size = 8 if is_64bit else 4
    ptr_fmt = "<Q" if is_64bit else "<I"
    hook_info_size = 16 if is_64bit else 8

    # HOOK_BATTERY_SOC_CHANGE runs on charger_task when CONFIG_PLATFORM_EC_CHARGER=y
    # (charger_task present), and via HOOK_SECOND on k_sys_work_q when charger_task is absent.
    active_hook_types = set(_SYSWORKQ_HOOK_TYPES)
    if "charger_task" not in symbol_table:
        active_hook_types.add("HOOK_BATTERY_SOC_CHANGE")

    # 1. Extract SYSWORKQ hooks from _zephyr_shim_hook_<HOOK_TYPE>_list_start .. _end
    for sym_name, start_addr in list(symbol_table.items()):
        m = _HOOK_START_RE.match(sym_name)
        if not m:
            continue
        hook_type = m.group(1)
        if hook_type not in active_hook_types:
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
    for sym_name in symbol_table:
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

    # 3. Extract statically initialized K_WORK_DEFINE / K_WORK_DELAYABLE_DEFINE objects.
    # In Zephyr, `struct k_work` starts with `sys_dnode_t node` (2 * ptr_size bytes),
    # followed immediately at offset `2 * ptr_size` by `k_work_handler_t handler`.
    # `struct k_work_delayable` embeds `struct k_work work` at offset 0.
    min_work_size = 4 * ptr_size
    handler_offset = 2 * ptr_size

    for sym_name, sym_addr in symbol_table.items():
        if sym_name.endswith("_work_data") or sym_addr in reverse_symbol_table:
            continue
        sym_lower = sym_name.lower()
        if "work" not in sym_lower and "dwork" not in sym_lower:
            continue
        if symbol_sizes is not None:
            sz = symbol_sizes.get(sym_name, 0)
            if sz != 0 and sz < min_work_size:
                continue

        sec = _find_section_for_vaddr(elffile, sym_addr)
        if not sec:
            continue
        sec_name = getattr(sec, "name", "")
        if "bss" in sec_name:
            continue
        sec_start = (
            sec.header["sh_addr"] if hasattr(sec, "header") else sec["sh_addr"]
        )
        sec_data = sec.data()
        off = sym_addr - sec_start + handler_offset
        if off < 0 or off + ptr_size > len(sec_data):
            continue

        handler_ptr = struct.unpack_from(ptr_fmt, sec_data, off)[0]
        norm_ptr = (
            handler_ptr & _ARM_THUMB_BIT_MASK if not is_64bit else handler_ptr
        )
        fn_name = reverse_symbol_table.get(
            norm_ptr
        ) or reverse_symbol_table.get(handler_ptr)
        if fn_name and fn_name not in seen and fn_name != sym_name:
            seen.add(fn_name)
            roots.append(
                AnalysisRoot(
                    context="SYSWORKQ",
                    kind="k_work",
                    name=fn_name,
                    source=f"k_work:{sym_name}",
                    address=norm_ptr,
                )
            )

    return roots


def analyze_sysworkq_elf(
    elf_path: str,
    rules: List[dict],
    funcprops_db: dict,
    allowlist: Set[str],
    include_no_wait: bool = True,
) -> Tuple[List[ContextViolation], List[AnalysisRoot], str, int]:
    """Audits a compiled ELF binary for SYSWORKQ context violations."""
    with open(elf_path, "rb") as f:
        elffile = ELFFile(f)
        arch = get_elf_arch(elffile)
        symbol_table, reverse_symbol_table, symbol_sizes = load_elf_symbols(
            elffile, arch
        )

        roots = discover_sysworkq_roots(
            elffile, symbol_table, reverse_symbol_table, symbol_sizes
        )

        call_graph = parse_call_graph_pure_python(
            elffile, reverse_symbol_table, arch
        )

        resolve_driver_function_pointers(
            elffile,
            symbol_table,
            reverse_symbol_table,
            call_graph,
            set(),
        )

        effective_allowlist = set(SYSWORKQ_DEFAULT_ALLOWLIST) | allowlist
        enabled_flags: Set[str] = (
            {"include_no_wait"} if include_no_wait else set()
        )

        violations = evaluate_violations(
            roots=roots,
            call_graph=call_graph,
            rules=rules,
            funcprops_db=funcprops_db,
            allowlist=effective_allowlist,
            enabled_flags=enabled_flags,
        )

        return violations, roots, arch, len(symbol_table)


def main(argv=None):
    """Main CLI entry point for analyze_sysworkq."""
    default_db = os.path.join(_SCRIPT_DIR, "funcprops.json")
    default_rules = os.path.join(_SCRIPT_DIR, "sysworkq_rules.json")

    parser = argparse.ArgumentParser(
        description=(
            "Static violation analyzer for Zephyr system workqueue "
            "(k_sys_work_q) handlers."
        )
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
        help=f"Path to declarative sysworkq_rules.json config. Default: {default_rules}",
    )
    parser.add_argument(
        "--exclude-no-wait",
        action="store_false",
        dest="include_no_wait",
        default=True,
        help=(
            "Exclude @sleep + @no_wait synchronization primitives "
            "(NO_WAIT_IN_SYSWORKQ) such as k_mutex_lock and k_sem_take."
        ),
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

    all_reports: Dict[str, Any] = {}
    total_violations = 0

    for elf in args.elf_files:
        if not os.path.isfile(elf):
            print(f"[ERROR] ELF file not found: {elf}", file=sys.stderr)
            return 2

        violations, roots, arch, sym_count = analyze_sysworkq_elf(
            elf_path=elf,
            rules=rules,
            funcprops_db=funcprops_db,
            allowlist=allow_set,
            include_no_wait=args.include_no_wait,
        )

        total_violations += len(violations)
        all_reports[elf] = format_and_print_report(
            elf=elf,
            arch=arch,
            sym_count=sym_count,
            roots=roots,
            violations=violations,
            target_contexts={"SYSWORKQ"},
            funcprops_db=funcprops_db,
            json_mode=args.json,
        )

    if args.json:
        print(json.dumps(all_reports, indent=2))

    if total_violations > 0 and not args.warn_only:
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
