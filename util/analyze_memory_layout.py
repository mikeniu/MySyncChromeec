#!/usr/bin/env vpython3
# Copyright 2025 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Analyze Zephyr RAM and Flash usage details."""

# [VPYTHON:BEGIN]
# wheel: <
#   name: "infra/python/wheels/pyelftools-py2_py3"
#   version: "version:0.29"
# >
# [VPYTHON:END]

import argparse
import json
from pathlib import Path
import sys
from typing import Any, Dict, List, NamedTuple, Optional

# pylint: disable=import-error
from elftools.elf.elffile import ELFFile
from elftools.elf.sections import SymbolTableSection


class Section(NamedTuple):
    name: str
    start: int
    end: int
    is_unused: bool


class Symbol(NamedTuple):
    name: str
    start: int
    size: int


class BuildInfo:
    """Extract relevant symbols from a Zephyr ELF file."""

    def __init__(self, elf_file: Path):
        self.syms = {}
        self.all_symbols: List[Symbol] = []
        self.path = elf_file
        self.section_data = []

        with open(elf_file, "rb") as file:
            elf = ELFFile(file)
            for section in elf.iter_sections():
                if (
                    section["sh_type"] == "SHT_PROGBITS"
                    and section["sh_size"] > 0
                ):
                    self.section_data.append(
                        (
                            section["sh_addr"],
                            section["sh_addr"] + section["sh_size"],
                            section.data(),
                        )
                    )
                if isinstance(section, SymbolTableSection):
                    for s in section.iter_symbols():
                        self.syms[s.name] = s.entry.st_value
                        if s.entry.st_size > 0:
                            self.all_symbols.append(
                                Symbol(
                                    s.name, s.entry.st_value, s.entry.st_size
                                )
                            )

    def get_symbol(self, sym, default=None):
        """Get a symbol from the ELF or default if it doesn't exist."""
        return self.syms.get(sym, default)

    def _process_gap(
        self, gap_start: int, gap_size: int, min_size: int
    ) -> List[Symbol]:
        gap_end = gap_start + gap_size
        candidates = []

        gap_data = None
        for sec_start, sec_end, data in self.section_data:
            if gap_start >= sec_start and gap_end <= sec_end:
                offset = gap_start - sec_start
                gap_data = data[offset : offset + gap_size]
                break

        if not gap_data:
            if gap_size >= min_size:
                candidates.append(Symbol("*anonymous*", gap_start, gap_size))
            return candidates

        current_offset = gap_start
        parts = gap_data.split(b"\x00")

        clump_start = None
        clump_size = 0
        clump_str_size = 0

        def flush_clump():
            nonlocal clump_size, clump_start, clump_str_size
            if clump_size > 0:
                if clump_size >= min_size:
                    name = (
                        "*strings (clustered)*"
                        if (clump_str_size / clump_size) > 0.5
                        else "*anonymous*"
                    )
                    candidates.append(Symbol(name, clump_start, clump_size))
                clump_size = 0
                clump_str_size = 0
                clump_start = None

        for i, p in enumerate(parts):
            plen = len(p)
            item_size = plen + 1 if i < len(parts) - 1 else plen

            is_str = False
            if plen >= 4:
                printable_chars = sum(
                    1 for c in p if 32 <= c <= 126 or c in (9, 10, 13)
                )
                if printable_chars / plen > 0.8:
                    is_str = True

            if is_str and item_size >= min_size:
                flush_clump()
                decoded = p.decode("utf-8", errors="ignore")
                display_str = repr(decoded[:40])
                if len(decoded) > 40:
                    display_str = repr(decoded[:37])[:-1] + "...'"
                candidates.append(
                    Symbol(
                        f"*string*: {display_str}", current_offset, item_size
                    )
                )
            else:
                if clump_start is None:
                    clump_start = current_offset
                clump_size += item_size
                if is_str:
                    clump_str_size += item_size

            current_offset += item_size

        flush_clump()
        return candidates

    def get_large_symbols_and_gaps(
        self, start_addr: int, end_addr: int, min_size: int = 256
    ) -> List[Symbol]:
        """Find largest symbols and gaps within a range."""
        in_range = [
            s
            for s in self.all_symbols
            if s.start >= start_addr and (s.start + s.size) <= end_addr
        ]
        in_range.sort(key=lambda x: x.start)

        candidates = []
        current_addr = start_addr
        for s in in_range:
            if s.start > current_addr:
                gap_size = s.start - current_addr
                candidates.extend(
                    self._process_gap(current_addr, gap_size, min_size)
                )
            if s.size >= min_size:
                candidates.append(s)
            current_addr = max(current_addr, s.start + s.size)

        if current_addr < end_addr:
            gap_size = end_addr - current_addr
            candidates.extend(
                self._process_gap(current_addr, gap_size, min_size)
            )

        candidates.sort(key=lambda x: x.size, reverse=True)
        return candidates

    def get_inlined_symbols(
        self, start_addr: int, size: int, min_size: int = 0
    ) -> List[Dict]:
        if not hasattr(self, "_aranges"):
            self._elf = ELFFile(open(self.path, "rb"))
            if not self._elf.has_dwarf_info():
                self._aranges = None
                self._dwarf = None
            else:
                self._dwarf = self._elf.get_dwarf_info()
                self._aranges = self._dwarf.get_aranges()
                self._cus = list(self._dwarf.iter_CUs())

        if not self._aranges:
            return []

        cu_offset = self._aranges.cu_offset_at_addr(start_addr)
        if cu_offset is None:
            return []

        target_cu = None
        for cu in self._cus:
            if cu.cu_offset == cu_offset:
                target_cu = cu
                break

        if not target_cu:
            return []

        end_addr = start_addr + size

        def get_die_name(die, dwarf):
            if "DW_AT_name" in die.attributes:
                return die.attributes["DW_AT_name"].value.decode()
            if "DW_AT_abstract_origin" in die.attributes:
                attr = die.attributes["DW_AT_abstract_origin"]
                offset = attr.value
                if attr.form == "DW_FORM_ref_addr":
                    for CU in self._cus:
                        if CU.cu_offset <= offset < CU.cu_offset + CU.size:
                            target_die = CU.get_DIE_from_refaddr(offset)
                            if (
                                target_die
                                and "DW_AT_name" in target_die.attributes
                            ):
                                return target_die.attributes[
                                    "DW_AT_name"
                                ].value.decode()
                else:
                    target_die = die.cu.get_DIE_from_refaddr(
                        offset + die.cu.cu_offset
                    )
                    if target_die and "DW_AT_name" in target_die.attributes:
                        return target_die.attributes[
                            "DW_AT_name"
                        ].value.decode()
            return "unknown"

        def get_ranges(die):
            ranges = []
            if "DW_AT_ranges" in die.attributes:
                offset = die.attributes["DW_AT_ranges"].value
                for r in self._dwarf.range_lists().get_range_list_at_offset(
                    offset, die.cu
                ):
                    if hasattr(r, "begin_offset"):
                        ranges.append((r.begin_offset, r.end_offset))
            elif (
                "DW_AT_low_pc" in die.attributes
                and "DW_AT_high_pc" in die.attributes
            ):
                low = die.attributes["DW_AT_low_pc"].value
                high_attr = die.attributes["DW_AT_high_pc"]
                high = (
                    high_attr.value
                    if high_attr.form == "DW_FORM_addr"
                    else low + high_attr.value
                )
                ranges.append((low, high))
            return ranges

        byte_owners = [[] for _ in range(size)]

        for die in target_cu.iter_DIEs():
            if die.tag == "DW_TAG_inlined_subroutine":
                name = get_die_name(die, self._dwarf)
                for r in get_ranges(die):
                    if r[0] < end_addr and r[1] > start_addr:
                        r_start = max(0, r[0] - start_addr)
                        r_end = min(size, r[1] - start_addr)
                        range_len = r[1] - r[0]
                        for i in range(r_start, r_end):
                            byte_owners[i].append((range_len, name))

        class Node:
            def __init__(self, name):
                self.name = name
                self.size = 0
                self.children = {}

            def add_path(self, path):
                self.size += 1
                if path:
                    child_name = path[0]
                    if child_name not in self.children:
                        self.children[child_name] = Node(child_name)
                    self.children[child_name].add_path(path[1:])

            def to_dict(self, min_size):
                kids = []
                for c in self.children.values():
                    if c.size >= min_size:
                        kids.append(c.to_dict(min_size))
                kids.sort(key=lambda x: x["size"], reverse=True)
                return {"name": self.name, "size": self.size, "children": kids}

        root = Node("root")
        for owners in byte_owners:
            if owners:
                owners.sort(key=lambda x: x[0], reverse=True)
                stack = [name for _, name in owners]
                root.add_path(stack)

        return root.to_dict(min_size)["children"]

    def has_stacks_in_range(self, start_addr: int, end_addr: int) -> bool:
        """Check if known stack symbols exist in this range."""
        stack_markers = [
            "z_interrupt_stacks",
            "z_main_stack",
            "_k_thread_stack",
        ]
        for s in self.all_symbols:
            if s.start >= start_addr and s.start < end_addr:
                for marker in stack_markers:
                    if marker in s.name:
                        return True
        return False


def analyze_ram(
    build_info: BuildInfo, elf_path: Path
) -> Optional[Dict[str, Any]]:
    """Analyze RAM layout and return data structure."""
    # --- 1. RAM Configuration ---
    ram_base = build_info.get_symbol("CONFIG_SRAM_BASE_ADDRESS")
    ram_size_kb = build_info.get_symbol("CONFIG_SRAM_SIZE")

    if ram_base is not None and ram_size_kb is not None:
        ram_size = ram_size_kb * 1024
    else:
        ram_base = build_info.get_symbol("CONFIG_CROS_EC_RAM_BASE")
        ram_size = build_info.get_symbol("CONFIG_CROS_EC_RAM_SIZE")

    if ram_base is None or ram_size is None:
        # Silent return if RAM config not found (might be an RO image that doesn't define it fully or differently)
        # But usually RO still has RAM. Let's print error only if verbose or just return None.
        # For now, print error to stderr to aid debugging but allow continuation.
        print(
            f"Warning: Could not determine RAM configuration for {elf_path.name}",
            file=sys.stderr,
        )
        return None

    ram_end = ram_base + ram_size

    # --- 2. Section Markers ---
    sections = []

    # Text in RAM (ILM)
    ilm_start = build_info.get_symbol("__ilm_ram_start")
    ilm_end = build_info.get_symbol("__ilm_ram_end")
    if ilm_start and ilm_end:
        sections.append(Section("ilm", ilm_start, ilm_end, False))

    # BSS
    bss_start = build_info.get_symbol("__bss_start")
    bss_end = build_info.get_symbol("__bss_end")
    if bss_start and bss_end:
        sections.append(Section("bss", bss_start, bss_end, False))

    # Data
    data_start = build_info.get_symbol("__data_start")
    data_end = build_info.get_symbol("__data_end")
    if data_start and data_end:
        sections.append(Section("data", data_start, data_end, False))

    # NoInit Logic (Explicit)
    if bss_end and data_start and data_start > bss_end:
        sections.append(Section("noinit", bss_end, data_start, False))

    # H2RAM Pool
    h2ram_start = build_info.get_symbol("_h2ram_pool_start")
    h2ram_end = build_info.get_symbol("_h2ram_pool_end")
    if h2ram_start and h2ram_end:
        sections.append(Section("h2ram_pool", h2ram_start, h2ram_end, False))

    # NoInit End of RAM
    noinit_end_start = build_info.get_symbol("__noinit_end_of_ram_start")
    noinit_end_end = build_info.get_symbol("__noinit_end_of_ram_end")
    if noinit_end_start and noinit_end_end:
        sections.append(
            Section(
                "noinit_end_of_ram", noinit_end_start, noinit_end_end, False
            )
        )

    # Preserved RAM
    preserved_size = build_info.get_symbol(
        "CONFIG_PLATFORM_EC_PRESERVED_END_OF_RAM_SIZE", 0
    )
    preserved_start = ram_end - preserved_size
    if preserved_size > 0:
        sections.append(
            Section("preserved_end_of_ram", preserved_start, ram_end, False)
        )

    # Sort sections
    sections.sort(key=lambda x: x.start)

    # --- 3. Calculations ---
    total_used = 0
    image_end = ram_base
    for s in sections:
        if s.name != "preserved_end_of_ram":
            if s.end > image_end:
                image_end = s.end
            total_used += s.end - s.start

    # Calculate unused regions
    final_regions = []
    current_addr = ram_base

    # Check for gap at start
    if sections and sections[0].start > ram_base:
        final_regions.append(
            Section("<UNUSED>", ram_base, sections[0].start, True)
        )

    for i in range(len(sections)):
        s = sections[i]

        # Check gap before this section
        if s.start > current_addr:
            gap_start = current_addr
            gap_end = s.start
            gap_name = "<UNUSED>"
            is_unused = True

            # Heuristic: Check if this "unused" gap actually contains stacks
            if build_info.has_stacks_in_range(gap_start, gap_end):
                gap_name = "noinit (Stacks)"
                is_unused = False
                total_used += gap_end - gap_start

            final_regions.append(
                Section(gap_name, gap_start, gap_end, is_unused)
            )

        final_regions.append(s)
        current_addr = s.end

    # Check gap at end if we haven't reached RAM end
    if current_addr < ram_end:
        if not (sections and sections[-1].name == "preserved_end_of_ram"):
            final_regions.append(
                Section("<UNUSED>", current_addr, ram_end, True)
            )

    return {
        "title": f"RAM Analysis for: {elf_path.name}",
        "type": "RAM",
        "base_addr": ram_base,
        "total_size": ram_size,
        "total_used": total_used,
        "regions": final_regions,
    }


def analyze_flash(
    build_info: BuildInfo, elf_path: Path
) -> Optional[Dict[str, Any]]:
    """Analyze Flash layout and return data structure."""
    # --- 1. Flash Configuration ---
    flash_base = build_info.get_symbol("__rom_region_start")

    # Determine usable flash size (partition size)
    # Prefer explicit CROS_EC_R*_SIZE symbols if they match the image type.
    is_ro = build_info.get_symbol("CONFIG_CROS_EC_RO") == 1
    is_rw = build_info.get_symbol("CONFIG_CROS_EC_RW") == 1

    total_flash_size = None
    if is_rw:
        total_flash_size = build_info.get_symbol("CONFIG_CROS_EC_RW_SIZE")
    elif is_ro:
        total_flash_size = build_info.get_symbol("CONFIG_CROS_EC_RO_SIZE")

    if total_flash_size is None:
        # Fallback to CONFIG_FLASH_SIZE if available
        flash_size_kb = build_info.get_symbol("CONFIG_FLASH_SIZE")
        if flash_size_kb:
            # Heuristic: if we don't have explicit partition sizes, assume
            # flash is split into RO/RW slots, so usable size is half.
            total_flash_size = (flash_size_kb * 1024) // 2

    if total_flash_size is None:
        # Fallback to image size if total not known
        flash_end_image = build_info.get_symbol("__rom_region_end")
        if flash_base is None or flash_end_image is None:
            print(
                f"Warning: Could not determine Flash configuration for {elf_path.name}",
                file=sys.stderr,
            )
            return None
        total_flash_size = flash_end_image - flash_base

    flash_end_chip = flash_base + total_flash_size

    # --- 2. Section Markers ---
    sections = []

    # Text Section
    text_start = build_info.get_symbol("__text_region_start")
    text_end = build_info.get_symbol("__text_region_end")
    if text_start and text_end:
        sections.append(Section("text", text_start, text_end, False))

    # RO Data Section
    rodata_start = build_info.get_symbol("__rodata_region_start")
    rodata_end = build_info.get_symbol("__rodata_region_end")
    if rodata_start and rodata_end:
        sections.append(Section("rodata", rodata_start, rodata_end, False))

    # Data LMA (Load Memory Address)
    data_lma_start = build_info.get_symbol("__data_region_load_start")
    data_size = build_info.get_symbol("__data_size")
    if data_lma_start and data_size:
        data_lma_end = data_lma_start + data_size
        sections.append(
            Section("data (LMA)", data_lma_start, data_lma_end, False)
        )

    sections.sort(key=lambda x: x.start)

    # --- 3. Calculations ---
    total_used = 0
    final_regions = []
    current_addr = flash_base

    # Filter out sections that are outside of the reported region (sanity check)
    # or handle the fact that sections might not start at 0 offset if they are offset in flash.

    for i in range(len(sections)):
        s = sections[i]

        # Check gap before this section
        if s.start > current_addr:
            final_regions.append(
                Section("<UNUSED>", current_addr, s.start, True)
            )

        final_regions.append(s)
        total_used += s.end - s.start
        current_addr = s.end

    # Check gap at end (Tail Free)
    if current_addr < flash_end_chip:
        final_regions.append(
            Section("<UNUSED>", current_addr, flash_end_chip, True)
        )

    return {
        "title": f"Flash Analysis for: {elf_path.name}",
        "type": "Flash",
        "base_addr": flash_base,
        "total_size": total_flash_size,
        "total_used": total_used,
        "regions": final_regions,
    }


def print_text_report(
    data: Dict[str, Any], build_info: BuildInfo, threshold: int
):
    """Print the formatted analysis report in text."""
    title = data["title"]
    base_addr = data["base_addr"]
    total_size = data["total_size"]
    total_used = data["total_used"]
    regions = data["regions"]

    end_addr = base_addr + total_size

    print(title)
    print("=" * 80)
    print(f"{'Name':<40} {'Address':<12} {'Size (B)':<10} {'% Usage':<8}")
    print("=" * 80)

    total_avail_growth = 0
    regions.sort(key=lambda x: x.start)

    for r in regions:
        size = r.end - r.start
        pct = (size / total_size) * 100
        name = r.name

        if r.is_unused:
            total_avail_growth += size

        print(f"{name:<40} {hex(r.start):<12} {size:<10} {pct:5.1f}%")

        # Drill down if it's a used section and threshold is not disabled
        if not r.is_unused and size > 0 and threshold >= 0:
            top_items = build_info.get_large_symbols_and_gaps(
                r.start, r.end, min_size=threshold
            )
            if top_items:
                for item in top_items:
                    item_pct = (item.size / total_size) * 100
                    addr_str = (
                        hex(item.start) if item.name == "*anonymous*" else ""
                    )
                    print(
                        f"  {item.name[:35]:<38} {addr_str:<12} {item.size:<10} {item_pct:5.1f}%"
                    )

                    if not item.name.startswith("*"):
                        inlines = build_info.get_inlined_symbols(
                            item.start, item.size, min_size=threshold
                        )

                        def print_inline_tree(nodes, depth=1):
                            indent = "  " * depth
                            for node in nodes:
                                name_str = f"{indent}{node['name']}"[:38]
                                print(
                                    f"  {name_str:<38} {'':<12} {node['size']:<10}"
                                )
                                print_inline_tree(node["children"], depth + 1)

                        print_inline_tree(inlines)
                print("")

    print("=" * 80)

    used_pct = (total_used / total_size) * 100
    avail_pct = (total_avail_growth / total_size) * 100

    print(f"{'Total':<40} {hex(base_addr):<12} {total_size:<10} 100.0%")
    print(f"{'Total Used':<40} {'':<12} {total_used:<10} {used_pct:5.1f}%")
    print(
        f"{'Total Available':<40} {'':<12} {total_avail_growth:<10} {avail_pct:5.1f}%"
    )


def get_json_data(
    analysis_results: List[Dict[str, Any]],
    build_info_map: Dict[Path, BuildInfo],
    threshold: int,
) -> Dict[str, Any]:
    """Construct a dictionary for JSON output."""
    output = {"analyses": []}

    for data in analysis_results:
        # We need to map back to the build info for this specific analysis if we want symbols
        # But wait, we simplified and are passing list of results.
        # We need the build_info associated with the elf that produced this result to look up symbols.
        # I'll update the structure passed to this function or just lookup.
        # For simplicity, let's assume we can attach the symbols directly in analyze_* or handle it here.
        # Actually, analyze_* returns regions without symbols. print_text_report calls get_large_symbols.
        # I should probably include the elf path in the data dict so I can look up the correct build_info.

        elf_path = data.get("elf_path")
        build_info = build_info_map.get(elf_path)

        total_size = data["total_size"]
        analysis_out = {
            "type": data["type"],
            "title": data["title"],
            "base_address": hex(data["base_addr"]),
            "total_size_bytes": total_size,
            "total_used_bytes": data["total_used"],
            "sections": [],
        }

        for r in data["regions"]:
            sec = {
                "name": r.name,
                "start_address": hex(r.start),
                "end_address": hex(r.end),
                "size_bytes": r.end - r.start,
                "percent_usage": round(
                    ((r.end - r.start) / total_size) * 100, 2
                ),
                "is_unused": r.is_unused,
                "symbols": [],
            }

            if not r.is_unused and threshold >= 0 and build_info:
                top_items = build_info.get_large_symbols_and_gaps(
                    r.start, r.end, min_size=threshold
                )
                for item in top_items:
                    sym_dict = {
                        "name": item.name,
                        "start_address": hex(item.start),
                        "size_bytes": item.size,
                        "percent_usage": round(
                            (item.size / total_size) * 100, 2
                        ),
                        "is_gap": item.name == "*anonymous*",
                    }
                    if not item.name.startswith("*"):
                        inlines = build_info.get_inlined_symbols(
                            item.start, item.size, min_size=threshold
                        )
                        if inlines:

                            def serialize_inline_tree(nodes):
                                result = []
                                for node in nodes:
                                    result.append(
                                        {
                                            "name": node["name"],
                                            "size_bytes": node["size"],
                                            "percent_usage": round(
                                                (node["size"] / item.size)
                                                * 100,
                                                2,
                                            ),
                                            "children": serialize_inline_tree(
                                                node["children"]
                                            ),
                                        }
                                    )
                                return result

                            sym_dict["inlines"] = serialize_inline_tree(inlines)
                    sec["symbols"].append(sym_dict)
            analysis_out["sections"].append(sec)

        output["analyses"].append(analysis_out)

    return output


def process_elf(
    elf_path: Path,
    analysis_type: str,
    threshold: int,
    results: List[Dict[str, Any]],
    build_info_map: Dict[Path, BuildInfo],
):
    """Process a single ELF file."""
    if not elf_path.exists():
        # Only print error if it was the *primary* file passed by user, handled in main.
        # But here we might be checking siblings.
        return

    build_info = BuildInfo(elf_path)
    build_info_map[elf_path] = build_info

    if analysis_type == "ram" or analysis_type == "both":
        data = analyze_ram(build_info, elf_path)
        if data:
            data["elf_path"] = elf_path  # Tag for lookup
            results.append(data)

    if analysis_type == "flash" or analysis_type == "both":
        data = analyze_flash(build_info, elf_path)
        if data:
            data["elf_path"] = elf_path
            results.append(data)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Analyze Zephyr memory/flash layout"
    )
    parser.add_argument(
        "elf", type=Path, help="Path to Zephyr ELF file or packed bin"
    )
    parser.add_argument(
        "--type",
        choices=["ram", "flash", "both"],
        default="both",
        help="Type of analysis to perform (default: both)",
    )
    parser.add_argument(
        "--threshold",
        type=int,
        default=256,
        help="Minimum size in bytes to display a symbol in breakdown (default: 256). Set to -1 to disable symbol output.",
    )
    parser.add_argument(
        "--json", action="store_true", help="Output in JSON format"
    )
    args = parser.parse_args()

    results = []
    build_info_map = {}

    target_files = []

    if args.elf.suffix == ".bin":
        # Packed binary mode: look for siblings
        base_dir = args.elf.parent
        ro_elf = base_dir / "zephyr.ro.elf"
        rw_elf = base_dir / "zephyr.rw.elf"

        if ro_elf.exists():
            target_files.append(ro_elf)
        if rw_elf.exists():
            target_files.append(rw_elf)

        if not target_files:
            print(
                f"Error: Packed binary detected but could not find 'zephyr.ro.elf' or 'zephyr.rw.elf' in {base_dir}",
                file=sys.stderr,
            )
            sys.exit(1)

        # For packed binaries, we usually want to see both RO and RW breakdowns
        # The user's requested type still applies (e.g. if they only want RAM usage for both)
    else:
        # Single ELF mode
        if not args.elf.exists():
            print(f"Error: File {args.elf} not found.", file=sys.stderr)
            sys.exit(1)
        target_files.append(args.elf)

    for elf_file in target_files:
        process_elf(
            elf_file, args.type, args.threshold, results, build_info_map
        )

    if args.json:
        json_out = get_json_data(results, build_info_map, args.threshold)
        print(json.dumps(json_out, indent=2))
    else:
        for i, data in enumerate(results):
            # Lookup correct build_info
            elf_path = data.get("elf_path")
            build_info = build_info_map.get(elf_path)

            print_text_report(data, build_info, args.threshold)
            if i < len(results) - 1:
                print()
