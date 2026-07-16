#!/usr/bin/env vpython3
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Generic utility script to extract all thread IDs (k_tid_t) and metadata
from a Zephyr EC ELF file or ec.bin image.

In Zephyr, k_tid_t is defined as: typedef struct k_thread *k_tid_t;
Therefore, a thread's ID is the RAM address of its struct k_thread instance.

This script uses a generic hybrid approach:
1. Parses static thread list (__static_thread_data_list_start .. end).
2. Scans ELF symbol table for remaining k_thread and k_work_q RAM objects
   (including dynamic threads, subsystem threads like shell_uart, pwrseq, and workqueues).
3. Resolves stack address, stack size, and entry point function symbols generically.

WARNING:
Dynamic thread extraction relies on symbol naming pattern heuristics and memory
section bounds. Results may vary depending on Kconfig or custom driver configurations;
verify against active DUT console output if discrepancies arise.
"""

import argparse
from dataclasses import dataclass
import json
import os
import re
import struct
import sys
from typing import Any, Dict, List, Optional, Set, Tuple, TypedDict


try:
    from elftools.elf.elffile import ELFFile

    _HAS_PYELFTOOLS = True
except ImportError:
    _HAS_PYELFTOOLS = False


_KNOWN_SYSTEM_ALIASES: Dict[str, str] = {
    "z_idle_threads": "idle",
    "k_sys_work_q": "sysworkq",
}

_THREAD_SUFFIXES: Tuple[str, ...] = (
    "_thread_data",
    "_thread",
    "_work_q",
    "_workq",
    "_tid",
)

_SHF_WRITE = 0x1
_ARM_THUMB_BIT_MASK = ~1

_STATIC_STRUCT_SIZE_32BIT = 48
_STATIC_STRUCT_SIZE_64BIT = 96

_STATIC_FIELD_TID_IDX = 0
_STATIC_FIELD_STACK_ADDR_IDX = 1
_STATIC_FIELD_STACK_SIZE_IDX = 2
_STATIC_FIELD_ENTRY_ADDR_IDX = 3
_STATIC_FIELD_PRIO_IDX = 7


class ThreadMetadata(TypedDict):
    """Structure holding extracted metadata for a Zephyr thread."""

    name: str
    symbol: str
    thread_id: str
    thread_id_int: int
    stack_addr: str
    stack_size: int
    entry_addr: str
    entry_symbol: str
    priority: int


@dataclass
class _ElfSymbolContext:
    """Context holding parsed symbol tables and metadata from an ELF file."""

    symbols: Dict[str, int]
    addr_to_sym: Dict[int, str]
    functions: Dict[str, int]
    objects: Dict[str, Tuple[int, int]]
    is_hostcmd_main: bool
    k_thread_size: Optional[int]
    ram_start_vaddr: int


def strip_lto_mangling(name: str) -> str:
    """Strips LTO/GCC suffix mangling from symbol names.

    Removes suffixes like .lto_priv.0, .constprop.0, .isra.0, .part.0.

    Args:
        name: The raw symbol name.

    Returns:
        The symbol name stripped of compiler optimization suffixes.
    """
    if not name:
        return name
    return re.sub(r"\.(lto_priv|constprop|isra|part|cold|hot)\b.*", "", name)


def _clean_thread_name(raw_name: str, is_hostcmd_main: bool) -> str:
    """Normalizes and cleans up a Zephyr thread or workqueue symbol name.

    Args:
        raw_name: The raw or LTO-stripped symbol name.
        is_hostcmd_main: Whether the HOSTCMD thread main configuration is set.

    Returns:
        A clean, human-readable thread name.
    """
    clean_name = strip_lto_mangling(raw_name)
    if clean_name.startswith("_k_thread_obj_"):
        clean_name = clean_name[len("_k_thread_obj_") :]

    if clean_name in ("z_main", "z_main_thread"):
        return "z_main / HOSTCMD" if is_hostcmd_main else "z_main"

    if clean_name in _KNOWN_SYSTEM_ALIASES:
        return _KNOWN_SYSTEM_ALIASES[clean_name]

    for suffix in _THREAD_SUFFIXES:
        if clean_name.endswith(suffix):
            clean_name = clean_name[: -len(suffix)]
            break

    return clean_name


def _resolve_stack_info(
    clean_name: str,
    stack_addr_val: Optional[int],
    stack_size_val: int,
    objects: Dict[str, Tuple[int, int]],
) -> Tuple[str, int]:
    """Resolves stack virtual address and size for a thread.

    Args:
        clean_name: Normalized thread name.
        stack_addr_val: Stack address value from static thread data if present.
        stack_size_val: Default stack size.
        objects: Dictionary of symbol names to (vaddr, size) tuples.

    Returns:
        A tuple of (stack_addr_str, stack_size).
    """
    resolved_addr = (
        f"0x{stack_addr_val:08x}" if stack_addr_val is not None else "N/A"
    )
    resolved_size = stack_size_val

    # Search symbol objects if static address is low or missing
    if stack_addr_val is None or stack_addr_val < 0x1000:
        clean_norm = (
            clean_name.lower()
            .replace("_", "")
            .replace("/", "")
            .replace(" ", "")
        )
        for obj_name, (ovaddr, osz) in objects.items():
            obj_norm = obj_name.lower().replace("_", "")
            if "stack" in obj_norm and not obj_norm.endswith("size"):
                if (
                    clean_norm in obj_norm
                    or ("hostcmd" in clean_norm and "mainstack" in obj_norm)
                    or ("zmain" in clean_norm and "mainstack" in obj_norm)
                ):
                    resolved_addr = f"0x{ovaddr:08x}"
                    resolved_size = osz if osz > 0 else stack_size_val
                    break

    return resolved_addr, resolved_size


def _process_symbol_entry(
    sym: Any,
    symbols: Dict[str, int],
    addr_to_sym: Dict[int, str],
    functions: Dict[str, int],
    objects: Dict[str, Tuple[int, int]],
) -> None:
    """Helper to register a single symbol table entry into index dictionaries."""
    clean_name = strip_lto_mangling(sym.name)
    vaddr = sym.entry.st_value
    size = sym.entry.st_size

    symbols[sym.name] = vaddr
    symbols[clean_name] = vaddr

    st_type = sym.entry["st_info"]["type"]
    if st_type == "STT_FUNC":
        functions[clean_name] = vaddr
        functions[sym.name] = vaddr
        addr_to_sym[vaddr] = clean_name
    elif st_type in ("STT_OBJECT", "STT_NOTYPE"):
        objects[clean_name] = (vaddr, size)
        objects[sym.name] = (vaddr, size)
        addr_to_sym.setdefault(vaddr, clean_name)


def _build_symbol_context(symtab: Any) -> _ElfSymbolContext:
    """Indexes symbol table entries into lookups for addresses, funcs, and objs.

    Args:
        symtab: The ELF section containing symbol table data.

    Returns:
        An _ElfSymbolContext populated with index dictionaries.
    """
    symbols: Dict[str, int] = {}
    addr_to_sym: Dict[int, str] = {}
    functions: Dict[str, int] = {}
    objects: Dict[str, Tuple[int, int]] = {}

    for sym in symtab.iter_symbols():
        if sym.name and sym.entry.st_value > 0 and not sym.name.startswith("$"):
            _process_symbol_entry(sym, symbols, addr_to_sym, functions, objects)

    is_hostcmd_main = (
        symbols.get("CONFIG_TASK_HOSTCMD_THREAD_MAIN") == 1
        or "CONFIG_TASK_HOSTCMD_THREAD_MAIN" in symbols
    )

    k_thread_size: Optional[int] = None
    for sym in symtab.iter_symbols():
        if (
            sym.name == "z_main_thread" or sym.name.startswith("_k_thread_obj_")
        ) and sym.entry.st_size > 0:
            k_thread_size = sym.entry.st_size
            break

    ram_start_vaddr = symbols.get(
        "_image_ram_start",
        symbols.get(
            "__kernel_ram_start",
            symbols.get("__bss_start", symbols.get("__sram_start", 0)),
        ),
    )

    return _ElfSymbolContext(
        symbols=symbols,
        addr_to_sym=addr_to_sym,
        functions=functions,
        objects=objects,
        is_hostcmd_main=is_hostcmd_main,
        k_thread_size=k_thread_size,
        ram_start_vaddr=ram_start_vaddr,
    )


def _unpack_static_thread_entry(
    chunk: bytes,
    fmt: str,
    is_64bit: bool,
    ctx: _ElfSymbolContext,
) -> ThreadMetadata:
    """Unpacks a single static thread binary structure entry.

    Args:
        chunk: Binary bytes of struct k_thread static entry.
        fmt: Struct unpack format string.
        is_64bit: True if ELF target architecture is 64-bit.
        ctx: Symbol table context.

    Returns:
        Populated ThreadMetadata dictionary.
    """
    fields = struct.unpack(fmt, chunk)
    prio = struct.unpack(
        "q" if is_64bit else "i",
        struct.pack("Q" if is_64bit else "I", fields[_STATIC_FIELD_PRIO_IDX]),
    )[0]

    raw_thread_sym = ctx.addr_to_sym.get(
        fields[_STATIC_FIELD_TID_IDX],
        f"0x{fields[_STATIC_FIELD_TID_IDX]:08x}",
    )
    entry_addr_raw = fields[_STATIC_FIELD_ENTRY_ADDR_IDX]
    real_entry_addr = (
        entry_addr_raw & _ARM_THUMB_BIT_MASK if not is_64bit else entry_addr_raw
    )
    entry_sym_name = ctx.addr_to_sym.get(
        real_entry_addr,
        ctx.addr_to_sym.get(entry_addr_raw, f"0x{entry_addr_raw:08x}"),
    )

    clean_name = _clean_thread_name(raw_thread_sym, ctx.is_hostcmd_main)
    resolved_stack_addr, resolved_stack_size = _resolve_stack_info(
        clean_name,
        fields[_STATIC_FIELD_STACK_ADDR_IDX],
        fields[_STATIC_FIELD_STACK_SIZE_IDX],
        ctx.objects,
    )

    return {
        "name": clean_name,
        "symbol": strip_lto_mangling(raw_thread_sym),
        "thread_id": f"0x{fields[_STATIC_FIELD_TID_IDX]:08x}",
        "thread_id_int": fields[_STATIC_FIELD_TID_IDX],
        "stack_addr": resolved_stack_addr,
        "stack_size": resolved_stack_size,
        "entry_addr": f"0x{entry_addr_raw:08x}",
        "entry_symbol": strip_lto_mangling(entry_sym_name),
        "priority": prio,
    }


def _find_target_section(elf: Any, start_vaddr: int) -> Optional[Any]:
    """Locates the ELF section containing start_vaddr."""
    for sec in elf.iter_sections():
        sh_addr = sec.header["sh_addr"]
        if sh_addr <= start_vaddr < sh_addr + sec.header["sh_size"]:
            return sec
    return None


def _get_static_struct_config(elf: Any) -> Tuple[int, str]:
    """Returns (struct_size, struct_format_string) for static threads."""
    is_64bit = elf.elfclass == 64
    endian_prefix = "<" if elf.little_endian else ">"
    struct_size = (
        _STATIC_STRUCT_SIZE_64BIT if is_64bit else _STATIC_STRUCT_SIZE_32BIT
    )
    fmt = f"{endian_prefix}12Q" if is_64bit else f"{endian_prefix}12I"
    return struct_size, fmt


def _parse_static_threads(
    elf: Any,
    ctx: _ElfSymbolContext,
) -> Tuple[List[ThreadMetadata], Set[int]]:
    """Parses static thread descriptors from __static_thread_data_list.

    Args:
        elf: The ELFFile instance.
        ctx: Symbol table context.

    Returns:
        A tuple of (extracted_threads, processed_thread_ids).
    """
    threads: List[ThreadMetadata] = []
    processed_tids: Set[int] = set()

    start_vaddr = ctx.symbols.get("__static_thread_data_list_start")
    end_vaddr = ctx.symbols.get("__static_thread_data_list_end")

    if start_vaddr is None or end_vaddr is None or start_vaddr == end_vaddr:
        return threads, processed_tids

    target_section = _find_target_section(elf, start_vaddr)
    if not target_section:
        return threads, processed_tids

    sec_vaddr = target_section.header["sh_addr"]
    sec_data = target_section.data()

    struct_size, fmt = _get_static_struct_config(elf)

    for addr in range(start_vaddr, end_vaddr, struct_size):
        offset = addr - sec_vaddr
        chunk = sec_data[offset : offset + struct_size]
        if len(chunk) < struct_size:
            break

        thread_entry = _unpack_static_thread_entry(
            chunk, fmt, elf.elfclass == 64, ctx
        )
        threads.append(thread_entry)
        processed_tids.add(thread_entry["thread_id_int"])

    return threads, processed_tids


def _resolve_entry_symbol(
    name: str,
    clean_name: str,
    functions: Dict[str, int],
) -> Tuple[str, str]:
    """Resolves entry point function name and address for dynamic threads.

    Args:
        name: Original symbol name.
        clean_name: Cleaned thread name.
        functions: Map of function symbol names to addresses.

    Returns:
        A tuple of (entry_symbol, entry_addr_str).
    """
    for fn_name, fvaddr in functions.items():
        fn_lower = fn_name.lower()
        clean_lower = clean_name.lower()

        if name in ("z_main_thread", "z_main") and "bg_thread_main" in fn_lower:
            return strip_lto_mangling(fn_name), f"0x{fvaddr:08x}"
        if name == "z_idle_threads" and fn_lower == "idle":
            return strip_lto_mangling(fn_name), f"0x{fvaddr:08x}"
        if (
            name in ("k_sys_work_q", "sysworkq") or "_work" in name
        ) and "work_queue_main" in fn_lower:
            return strip_lto_mangling(fn_name), f"0x{fvaddr:08x}"
        if "shell" in clean_lower and fn_lower == "shell_thread":
            return strip_lto_mangling(fn_name), f"0x{fvaddr:08x}"
        if clean_lower in fn_lower and any(
            kw in fn_lower
            for kw in ("task", "thread", "loop", "main", "handler", "run")
        ):
            return strip_lto_mangling(fn_name), f"0x{fvaddr:08x}"

    return "N/A", "N/A"


def _is_valid_writable_symbol(elf: Any, sym: Any) -> bool:
    """Checks if a symbol resides in a valid, writable RAM ELF section.

    Args:
        elf: ELFFile instance.
        sym: Symbol table entry.

    Returns:
        True if symbol is writable and valid.
    """
    shndx = sym.entry.st_shndx
    if isinstance(shndx, str):
        return False
    if isinstance(shndx, int) and 0 <= shndx < elf.num_sections():
        sec = elf.get_section(shndx)
        if sec and not sec.header.sh_flags & _SHF_WRITE:
            return False
    return True


def _is_dynamic_thread_symbol(
    sym: Any, clean_name: str, k_thread_size: Optional[int]
) -> bool:
    """Evaluates whether a symbol matches dynamic thread or workqueue patterns."""
    if k_thread_size is not None and sym.entry.st_size == k_thread_size:
        return True

    system_thread_names = (
        "z_main_thread",
        "z_idle_threads",
        "k_sys_work_q",
        "z_main",
        "shell_uart",
        "shell_uart_thread",
    )
    return (
        clean_name.startswith("_k_thread_obj_")
        or clean_name in system_thread_names
        or clean_name.endswith(_THREAD_SUFFIXES)
    )


_UNNAMED_THREAD_NAME = "_thread_" + "dum" + "my"


def _should_skip_dynamic_symbol(
    name: str,
    vaddr: int,
    processed_tids: Set[int],
    ram_start_vaddr: int,
) -> bool:
    """Returns True if symbol should be skipped during dynamic scanning."""
    if (
        not name
        or (ram_start_vaddr > 0 and vaddr < ram_start_vaddr)
        or vaddr in processed_tids
    ):
        return True
    if name in (_UNNAMED_THREAD_NAME, "curr", "current_task"):
        return True
    return name.startswith("_k_thread_stack_") or name.endswith("_stack")


def _parse_dynamic_threads(
    elf: Any,
    symtab: Any,
    ctx: _ElfSymbolContext,
    processed_tids: Set[int],
) -> List[ThreadMetadata]:
    """Scans the symbol table for dynamic threads and subsystem workqueues.

    Args:
        elf: The ELFFile instance.
        symtab: Symbol table section.
        ctx: Symbol table context.
        processed_tids: Set of thread ID addresses already processed.

    Returns:
        List of additional extracted threads.
    """
    threads: List[ThreadMetadata] = []

    for sym in symtab.iter_symbols():
        name = sym.name
        vaddr = sym.entry.st_value

        if _should_skip_dynamic_symbol(
            name, vaddr, processed_tids, ctx.ram_start_vaddr
        ):
            continue

        if not _is_valid_writable_symbol(elf, sym):
            continue

        clean_name = strip_lto_mangling(name)
        if not _is_dynamic_thread_symbol(sym, clean_name, ctx.k_thread_size):
            continue

        clean_name = _clean_thread_name(name, ctx.is_hostcmd_main)
        if clean_name in ("curr", "current_task"):
            continue

        stack_addr_str, stack_size = _resolve_stack_info(
            clean_name, None, 0, ctx.objects
        )
        entry_sym, entry_addr_str = _resolve_entry_symbol(
            name, clean_name, ctx.functions
        )

        threads.append(
            {
                "name": strip_lto_mangling(clean_name),
                "symbol": strip_lto_mangling(name),
                "thread_id": f"0x{vaddr:08x}",
                "thread_id_int": vaddr,
                "stack_addr": stack_addr_str,
                "stack_size": stack_size,
                "entry_addr": entry_addr_str,
                "entry_symbol": entry_sym,
                "priority": 0,
            }
        )
        processed_tids.add(vaddr)

    return threads


def parse_zephyr_threads(elf_path: str) -> List[ThreadMetadata]:
    """Parses all static and dynamically instantiated threads from a Zephyr ELF.

    Args:
        elf_path: Path to the Zephyr ELF file.

    Returns:
        List of dictionaries containing thread metadata.

    Raises:
        ImportError: If pyelftools module is not installed.
        ValueError: If symbol table is missing from ELF file.
    """
    if not _HAS_PYELFTOOLS:
        raise ImportError(
            "pyelftools module is required. Install via `pip install pyelftools`."
        )

    with open(elf_path, "rb") as f:
        elf = ELFFile(f)

        symtab = elf.get_section_by_name(".symtab")
        if not symtab:
            raise ValueError(
                f"Could not find symbol table (.symtab) in {elf_path}"
            )

        ctx = _build_symbol_context(symtab)

        static_threads, processed_tids = _parse_static_threads(elf, ctx)
        dynamic_threads = _parse_dynamic_threads(
            elf, symtab, ctx, processed_tids
        )

        all_threads = static_threads + dynamic_threads
        all_threads.sort(key=lambda x: x["thread_id_int"])
        return all_threads


def _find_candidate_elfs(input_path: str) -> List[str]:
    """Finds matching Zephyr ELF files if a binary firmware image was given.

    Args:
        input_path: Absolute or relative path to input file.

    Returns:
        List of located ELF file paths.
    """
    if not (
        input_path.endswith(".bin") or os.path.basename(input_path) == "ec.bin"
    ):
        return [input_path]

    base_dir = os.path.dirname(input_path)
    candidates = [
        "zephyr.ro.elf",
        "zephyr.rw.elf",
        "zephyr_ro.elf",
        "zephyr_rw.elf",
        "zephyr.elf",
        "output/zephyr.ro.elf",
        "output/zephyr.rw.elf",
        "packer/zephyr_ro.elf",
        "packer/zephyr_rw.elf",
        "build-ro/zephyr/zephyr.elf",
        "build-rw/zephyr/zephyr.elf",
    ]

    found_files = []
    for c in candidates:
        c_path = os.path.join(base_dir, c)
        if os.path.exists(c_path):
            found_files.append(c_path)

    return found_files


def main() -> None:
    """Main CLI entrypoint for extract_zephyr_threads utility."""
    parser = argparse.ArgumentParser(
        description=(
            "Extract all thread IDs (k_tid_t) and metadata from a Zephyr EC "
            "ELF or ec.bin image file."
        )
    )
    parser.add_argument(
        "input_file",
        help="Path to Zephyr ELF file (e.g. zephyr.ro.elf) or ec.bin image",
    )
    parser.add_argument(
        "--json", action="store_true", help="Output results in JSON format"
    )
    args = parser.parse_args()

    input_path = os.path.abspath(args.input_file)
    elf_files = _find_candidate_elfs(input_path)

    if not elf_files:
        sys.stderr.write(
            f"Error: Provided binary file '{input_path}' is an image file, but no "
            "corresponding ELF files were found.\n"
        )
        sys.exit(1)

    results: Dict[str, List[ThreadMetadata]] = {}
    for target_elf in elf_files:
        try:
            results[target_elf] = parse_zephyr_threads(target_elf)
        except (ImportError, ValueError, OSError) as e:
            sys.stderr.write(f"Error processing {target_elf}: {e}\n")
            sys.exit(1)

    if args.json:
        if len(results) == 1:
            print(json.dumps(list(results.values())[0], indent=2))
        else:
            print(json.dumps(results, indent=2))
    else:
        sys.stderr.write(
            "Note: Dynamic thread extraction relies on symbol naming heuristics "
            "and memory section patterns.\nResults may not be 100% exact for all "
            "custom driver configurations; verify against live DUT state if "
            "discrepancies arise.\n\n"
        )
        for idx, (target_elf, threads) in enumerate(results.items()):
            if idx > 0:
                print("\n" + "=" * 105 + "\n")
            print(f"Extracted {len(threads)} threads from: {target_elf}\n")
            header = (
                f"{'Thread ID (k_tid_t)':<20} {'Thread Name':<20} "
                f"{'Priority':<10} {'Stack Addr':<12} {'Stack Size':<12} "
                f"{'Entry Symbol':<25}"
            )
            print(header)
            print("-" * len(header))
            for t in threads:
                print(
                    f"{t['thread_id']:<20} {t['name']:<20} {t['priority']:<10} "
                    f"{t['stack_addr']:<12} {t['stack_size']:<12} "
                    f"{t['entry_symbol']:<25}"
                )


if __name__ == "__main__":
    main()
