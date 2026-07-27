# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Analyze binary differences between two EC builds.

This module compares two EC binary images (or build output directories) and
identifies the exact ELF sections, symbols, and source files responsible for
any differences.
"""

import argparse
import os
import pathlib
import re
import subprocess


IGNORED_SECTION_PREFIXES = (
    ".debug",
    ".pw_tokenizer",
    "_pw_tokenizer_string_entry",
)


def is_ignored_section_or_symbol(name):
    """Check if a section or symbol name should be ignored for footprint calculations."""
    return name.startswith(IGNORED_SECTION_PREFIXES)


def find_build_files(path):
    """Find ec.bin, zephyr.elf, and zephyr.map from a path or directory."""
    if os.path.isfile(path):
        build_dir = os.path.dirname(os.path.dirname(path))
        bin_path = path
    else:
        build_dir = path
        bin_path = os.path.join(build_dir, "output", "ec.bin")
        if not os.path.exists(bin_path):
            bin_path = os.path.join(build_dir, "ec.bin")

    ro_elf = os.path.join(build_dir, "build-ro", "zephyr", "zephyr.elf")
    rw_elf = os.path.join(build_dir, "build-rw", "zephyr", "zephyr.elf")
    single_elf = os.path.join(
        build_dir, "build-singleimage", "zephyr", "zephyr.elf"
    )

    if not os.path.exists(ro_elf) and os.path.exists(single_elf):
        ro_elf = single_elf
        rw_elf = None

    ro_map = os.path.join(build_dir, "build-ro", "zephyr", "zephyr.map")
    rw_map = os.path.join(build_dir, "build-rw", "zephyr", "zephyr.map")
    single_map = os.path.join(
        build_dir, "build-singleimage", "zephyr", "zephyr.map"
    )

    if not os.path.exists(ro_map) and os.path.exists(single_map):
        ro_map = single_map
        rw_map = None

    return {
        "bin": bin_path if os.path.exists(bin_path) else None,
        "ro_elf": ro_elf if (ro_elf and os.path.exists(ro_elf)) else None,
        "rw_elf": rw_elf if (rw_elf and os.path.exists(rw_elf)) else None,
        "ro_map": ro_map if (ro_map and os.path.exists(ro_map)) else None,
        "rw_map": rw_map if (rw_map and os.path.exists(rw_map)) else None,
    }


def analyze_bin_diffs(bin1_path, bin2_path):
    """Compare two binary files and return contiguous diff byte ranges."""
    with open(bin1_path, "rb") as f1, open(bin2_path, "rb") as f2:
        b1 = f1.read()
        b2 = f2.read()

    size1, size2 = len(b1), len(b2)
    min_len = min(size1, size2)

    diff_indices = [i for i in range(min_len) if b1[i] != b2[i]]
    total_diff_bytes = len(diff_indices) + abs(size1 - size2)

    ranges = []
    if diff_indices:
        start = diff_indices[0]
        prev = diff_indices[0]
        for d in diff_indices[1:]:
            if d > prev + 1:
                ranges.append((start, prev))
                start = d
            prev = d
        ranges.append((start, prev))

    return {
        "size1": size1,
        "size2": size2,
        "total_diff_bytes": total_diff_bytes,
        "ranges": ranges,
    }


def parse_map_symbols(map_file_path):
    """Parse symbol locations and sizes from a Zephyr/GNU ld map file."""
    if not map_file_path or not os.path.exists(map_file_path):
        return []

    symbols = []
    pattern = re.compile(
        r"^\s*(0x[0-9a-fA-F]+)\s+(0x[0-9a-fA-F]+)\s+(.*\.o(?:bj)?(?:\(.*\))?)$"
    )
    sym_pattern = re.compile(r"^\s*(0x[0-9a-fA-F]+)\s+([a-zA-Z0-9_]+)$")

    with open(map_file_path, "r", encoding="utf-8", errors="ignore") as f:
        for line in f:
            m = pattern.match(line)
            if m:
                addr = int(m.group(1), 16)
                size = int(m.group(2), 16)
                target = m.group(3)
                symbols.append(
                    {
                        "type": "object",
                        "addr": addr,
                        "size": size,
                        "name": target,
                    }
                )
                continue

            sm = sym_pattern.match(line)
            if sm:
                addr = int(sm.group(1), 16)
                name = sm.group(2)
                symbols.append({"type": "symbol", "addr": addr, "name": name})

    return sorted(symbols, key=lambda x: x["addr"])


def get_elf_symbols(elf_path):
    """Get symbol table using nm."""
    if not elf_path or not os.path.exists(elf_path):
        return {}
    try:
        out = subprocess.check_output(
            ["nm", "-S", elf_path], stderr=subprocess.DEVNULL
        ).decode()
    except (subprocess.CalledProcessError, OSError, ValueError):
        return {}

    syms = {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) >= 3:
            name = parts[-1]
            if is_ignored_section_or_symbol(name):
                continue
            try:
                addr = int(parts[0], 16)
                size = int(parts[1], 16) if len(parts) == 4 else 0
                syms[name] = (addr, size)
            except ValueError:
                pass
    return syms


def compare_elf_symbols(elf1, elf2):
    """Compare symbol tables between two ELF files."""
    s1 = get_elf_symbols(elf1)
    s2 = get_elf_symbols(elf2)

    if not s1 or not s2:
        return None

    added = {k: s2[k][1] for k in (set(s2.keys()) - set(s1.keys()))}
    removed = {k: s1[k][1] for k in (set(s1.keys()) - set(s2.keys()))}
    size_changed = {
        k: (s1[k][1], s2[k][1])
        for k in (set(s1.keys()) & set(s2.keys()))
        if s1[k][1] != s2[k][1]
    }

    added_size = sum(added.values())
    removed_size = sum(removed.values())
    size_changed_delta = sum(s2[k][1] - s1[k][1] for k in size_changed)
    net_size_change = added_size - removed_size + size_changed_delta

    return {
        "added": added,
        "removed": removed,
        "size_changed": size_changed,
        "net_size_change": net_size_change,
    }


def map_offset_to_symbols(offset, map_symbols, header_offset=0x400):
    """Map binary offset to ELF VMA address and find corresponding symbol/object."""
    vma = offset - header_offset if offset >= header_offset else offset
    matching = []

    for item in map_symbols:
        if item["type"] == "object":
            addr = item["addr"]
            size = item.get("size", 0)
            if size > 0 and addr <= vma < (addr + size):
                matching.append(
                    f"{item['name']} (addr 0x{addr:x}, size 0x{size:x})"
                )
        elif item["type"] == "symbol":
            addr = item["addr"]
            if abs(addr - vma) < 0x20:
                matching.append(f"symbol: {item['name']} (near 0x{addr:x})")

    return matching


def get_elf_sections(elf_path):
    """Get section sizes and flags using objdump -h."""
    if not elf_path or not os.path.exists(elf_path):
        return {}
    try:
        out = subprocess.check_output(
            ["objdump", "-h", elf_path], stderr=subprocess.DEVNULL
        ).decode()
    except (subprocess.CalledProcessError, OSError, ValueError):
        return {}

    sections = {}
    lines = out.splitlines()
    for i, line in enumerate(lines):
        parts = line.split()
        if len(parts) >= 6 and parts[0].isdigit():
            name = parts[1]
            try:
                size = int(parts[2], 16)
                vma = int(parts[3], 16)
                lma = int(parts[4], 16)
                flags = lines[i + 1] if (i + 1 < len(lines)) else ""
                sections[name] = {
                    "size": size,
                    "vma": vma,
                    "lma": lma,
                    "alloc": "ALLOC" in flags,
                    "load": "LOAD" in flags,
                    "readonly": "READONLY" in flags,
                }
            except ValueError:
                pass
    return sections


def parse_map_memory_regions(map_path):
    """Parse memory regions (Origin, Length) from a zephyr.map file."""
    if not map_path or not os.path.exists(map_path):
        return {}
    regions = {}
    try:
        with open(map_path, "r", encoding="utf-8") as f:
            content = f.read()
    except OSError:
        return {}

    match = re.search(
        r"Memory Configuration\s+Name\s+Origin\s+Length\s+Attributes(.*?)(?:\r?\n\r?\n|\Z)",
        content,
        re.DOTALL,
    )
    if not match:
        return {}

    block = match.group(1)
    for line in block.splitlines():
        parts = line.split()
        if len(parts) >= 3:
            name = parts[0]
            try:
                origin = int(parts[1], 16)
                length = int(parts[2], 16)
                regions[name] = {
                    "origin": origin,
                    "length": length,
                }
            except ValueError:
                pass
    return regions


def get_region_type(name):
    """Classify a memory region name as ROM or RAM."""
    name_upper = name.upper()
    if any(
        k in name_upper
        for k in ["FLASH", "ROM", "IMEM", "ILM", "CODE", "PROGRAM"]
    ):
        return "ROM"
    if any(k in name_upper for k in ["RAM", "SRAM", "DMEM", "DLM", "DATA"]):
        return "RAM"
    return None


def compare_elf_sections(elf1, elf2, map1=None, map2=None):
    """Compare section and memory region sizes between two ELF files."""
    sec1 = get_elf_sections(elf1)
    sec2 = get_elf_sections(elf2)

    if not sec1 or not sec2:
        return None

    all_keys = set(sec1.keys()) | set(sec2.keys())
    changed = {}
    for k in sorted(all_keys):
        s1 = sec1.get(k, {}).get("size", 0)
        s2 = sec2.get(k, {}).get("size", 0)
        if s1 != s2:
            changed[k] = (s1, s2, s2 - s1)

    def get_rom_ram(sections, map_path):
        regions = parse_map_memory_regions(map_path)
        if not regions:
            rom = sum(
                v["size"]
                for k, v in sections.items()
                if v.get("alloc")
                and v.get("load")
                and not is_ignored_section_or_symbol(k)
            )
            ram = sum(
                v["size"]
                for k, v in sections.items()
                if v.get("alloc")
                and not v.get("readonly")
                and not is_ignored_section_or_symbol(k)
            )
            return rom, ram

        rom_ranges = []
        ram_ranges = []
        for name, r in regions.items():
            if "UNPADDED" in name.upper():
                continue
            rtype = get_region_type(name)
            if rtype == "ROM":
                rom_ranges.append((r["origin"], r["origin"] + r["length"]))
            elif rtype == "RAM":
                ram_ranges.append((r["origin"], r["origin"] + r["length"]))

        def in_ranges(addr, ranges):
            return any(start <= addr < end for start, end in ranges)

        rom_secs = []
        ram_secs = []
        for k, v in sections.items():
            if not v.get("alloc") or is_ignored_section_or_symbol(k):
                continue
            vma = v.get("vma", 0)
            lma = v.get("lma", 0)

            if rom_ranges and in_ranges(lma, rom_ranges) and v.get("load"):
                rom_secs.append((lma, v["size"]))
            if ram_ranges and in_ranges(vma, ram_ranges):
                ram_secs.append((vma, v["size"]))

        if rom_secs:
            min_rom = min(addr for addr, _ in rom_secs)
            max_rom = max(addr + size for addr, size in rom_secs)
            rom = max_rom - min_rom
        else:
            rom = 0

        if ram_secs:
            min_ram = min(addr for addr, _ in ram_secs)
            max_ram = max(addr + size for addr, size in ram_secs)
            ram = max_ram - min_ram
        else:
            ram = 0

        return rom, ram

    rom1, ram1 = get_rom_ram(sec1, map1)
    rom2, ram2 = get_rom_ram(sec2, map2)

    return {
        "changed": changed,
        "rom": (rom1, rom2, rom2 - rom1),
        "ram": (ram1, ram2, ram2 - ram1),
    }


def format_bytes(num_bytes, show_exact=True, signed=False):
    """Format byte count to human-readable string (e.g. +128.8 KB (+128,800 B))."""
    abs_bytes = abs(num_bytes)
    sign = (
        "+"
        if (signed and num_bytes > 0)
        else ("-" if num_bytes < 0 else ("+" if signed else ""))
    )

    if abs_bytes >= 1024 * 1024:
        formatted = f"{sign}{abs_bytes / (1024 * 1024):.2f} MB"
    elif abs_bytes >= 1024:
        formatted = f"{sign}{abs_bytes / 1024:.1f} KB"
    else:
        formatted = f"{sign}{abs_bytes} B"

    if show_exact and abs_bytes >= 1024:
        return f"{formatted} ({sign}{abs_bytes:,} B)"
    return formatted


def demangle_symbols(symbols):
    """Demangle a list or dict of symbol names using c++filt if available."""
    if not symbols:
        return {} if isinstance(symbols, dict) else []

    sym_list = (
        list(symbols.keys()) if isinstance(symbols, dict) else list(symbols)
    )
    mangled = [s for s in sym_list if s.startswith("_Z")]
    if not mangled:
        if isinstance(symbols, dict):
            return dict(symbols)
        return list(sym_list)

    try:
        with subprocess.Popen(
            ["c++filt"],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            text=True,
        ) as proc:
            out, _ = proc.communicate(input="\n".join(mangled))
        demangled_lines = out.splitlines()
        demangled_map = dict(zip(mangled, demangled_lines))
    except (subprocess.CalledProcessError, OSError, ValueError):
        demangled_map = {}

    def get_demangled(name):
        return demangled_map.get(name, name)

    if isinstance(symbols, dict):
        return {get_demangled(k): v for k, v in symbols.items()}
    return [get_demangled(s) for s in sym_list]


def _compare_single_image(
    image_type,
    elf1,
    elf2,
    map1=None,
    map2=None,
    output_fn=print,
    sections=False,
    symbols=False,
    verbose=False,
):
    """Compare memory, section, and symbol footprint for a single image."""
    is_metadata_only = True
    sec_diff = compare_elf_sections(elf1, elf2, map1=map1, map2=map2)
    rom_delta, ram_delta = 0, 0

    if sec_diff:
        rom1, rom2, rom_delta = sec_diff["rom"]
        ram1, ram2, ram_delta = sec_diff["ram"]

        if sections or verbose:
            output_fn(
                "\n---------------------------------------------------------"
            )
            output_fn(f"--- {image_type} Memory & Section Comparison ---")
            rom1_str = format_bytes(rom1, show_exact=False)
            rom2_str = format_bytes(rom2, show_exact=False)
            rom_d_str = format_bytes(rom_delta, show_exact=True, signed=True)
            output_fn(
                f"  ROM (.text + .rodata + .data): {rom1_str} -> {rom2_str} ({rom_d_str})"
            )

            ram1_str = format_bytes(ram1, show_exact=False)
            ram2_str = format_bytes(ram2, show_exact=False)
            ram_d_str = format_bytes(ram_delta, show_exact=True, signed=True)
            output_fn(
                f"  RAM (.data + .bss): {ram1_str} -> {ram2_str} ({ram_d_str})"
            )

            if sec_diff["changed"]:
                output_fn("  Changed Sections:")
                for k, (s1, s2, d) in sec_diff["changed"].items():
                    s1_str = format_bytes(s1, show_exact=False)
                    s2_str = format_bytes(s2, show_exact=False)
                    d_str = format_bytes(d, show_exact=False, signed=True)
                    output_fn(
                        f"    - {k:<20}: {s1_str:>7} -> {s2_str:>7}  ({d_str})"
                    )

        if sec_diff["changed"]:
            for k in sec_diff["changed"]:
                if k in [".text", ".rodata", ".data", ".bss"]:
                    is_metadata_only = False

    sym_diff = compare_elf_symbols(elf1, elf2)
    net_change = 0

    if sym_diff:
        net_change = sym_diff["net_size_change"]
        if symbols or verbose:
            output_fn(f"\n--- {image_type} Symbol Table Comparison (nm) ---")
            added = sym_diff["added"]
            demangled_added = demangle_symbols(added)
            output_fn(f"  Added Symbols: {len(added)}")
            if demangled_added:
                sorted_added = sorted(
                    demangled_added.items(), key=lambda x: x[1], reverse=True
                )
                show_added = (
                    len(sorted_added) if verbose else min(10, len(sorted_added))
                )
                if not verbose and len(sorted_added) > 10:
                    output_fn(
                        f"    (Showing top 10 of {len(sorted_added)} added "
                        "symbols by size. Use -v / --verbose to list all)"
                    )
                for k, size in sorted_added[:show_added]:
                    sz_str = format_bytes(size, show_exact=False)
                    output_fn(f"    + {k:<55} ({sz_str})")

            removed = sym_diff["removed"]
            demangled_removed = demangle_symbols(removed)
            output_fn(f"  Removed Symbols: {len(removed)}")
            if demangled_removed:
                sorted_removed = sorted(
                    demangled_removed.items(), key=lambda x: x[1], reverse=True
                )
                show_removed = (
                    len(sorted_removed)
                    if verbose
                    else min(10, len(sorted_removed))
                )
                if not verbose and len(sorted_removed) > 10:
                    output_fn(
                        f"    (Showing top 10 of {len(sorted_removed)} removed "
                        "symbols by size. Use -v / --verbose to list all)"
                    )
                for k, size in sorted_removed[:show_removed]:
                    sz_str = format_bytes(size, show_exact=False)
                    output_fn(f"    - {k:<55} ({sz_str})")

            output_fn(f"  Changed Size: {len(sym_diff['size_changed'])}")
            demangled_changed = demangle_symbols(sym_diff["size_changed"])
            if demangled_changed:
                sorted_changed = sorted(
                    demangled_changed.items(),
                    key=lambda x: abs(x[1][1] - x[1][0]),
                    reverse=True,
                )
                show_count = (
                    len(sorted_changed)
                    if verbose
                    else min(10, len(sorted_changed))
                )
                if not verbose and len(sorted_changed) > 10:
                    output_fn(
                        f"    (Showing top 10 of {len(sorted_changed)} changed-"
                        "size symbols by absolute delta. Use -v / --verbose to list all)"
                    )
                for k, (s1, s2) in sorted_changed[:show_count]:
                    delta = s2 - s1
                    s1_str = format_bytes(s1, show_exact=False)
                    s2_str = format_bytes(s2, show_exact=False)
                    d_str = format_bytes(delta, show_exact=False, signed=True)
                    output_fn(
                        f"    - {k:<55}: {s1_str:>7} -> {s2_str:>7}  ({d_str})"
                    )

            net_str = format_bytes(net_change, show_exact=True, signed=True)
            output_fn(f"  Net Symbol Size Change: {net_str}")

        if sym_diff["added"] or sym_diff["removed"] or sym_diff["size_changed"]:
            is_metadata_only = False

    return {
        "is_metadata_only": is_metadata_only,
        "mem_delta": (rom_delta, ram_delta),
        "net_delta": net_change,
    }


def analyze_build_diff(
    target1,
    target2,
    project_name=None,
    sections=False,
    symbols=False,
    verbose=False,
    exact_bytes=False,
    output_fn=print,
):
    """Analyze binary differences between two EC builds."""
    files1 = find_build_files(target1)
    files2 = find_build_files(target2)

    if not files1["bin"] or not os.path.exists(files1["bin"]):
        output_fn(f"Error: Could not find ec.bin in {target1}")
        return False

    if not files2["bin"] or not os.path.exists(files2["bin"]):
        output_fn(f"Error: Could not find ec.bin in {target2}")
        return False

    if not project_name:
        p1 = pathlib.Path(target1)
        if p1.is_dir():
            project_name = p1.name

    header_title = (
        f"EC BUILD DIFFERENCE ANALYSIS ({project_name})"
        if project_name
        else "EC BUILD DIFFERENCE ANALYSIS"
    )

    output_fn("=========================================================")
    output_fn(f"           {header_title}")
    output_fn("=========================================================")
    output_fn(f" Target 1: {files1['bin']}")
    output_fn(f" Target 2: {files2['bin']}")
    output_fn("---------------------------------------------------------")

    diff_info = analyze_bin_diffs(files1["bin"], files2["bin"])
    total_diff_str = format_bytes(
        diff_info["total_diff_bytes"], show_exact=True
    )

    if diff_info["size1"] != diff_info["size2"]:
        s1_str = format_bytes(diff_info["size1"], show_exact=False)
        s2_str = format_bytes(diff_info["size2"], show_exact=False)
        delta_str = format_bytes(
            diff_info["size2"] - diff_info["size1"],
            show_exact=True,
            signed=True,
        )
        output_fn(f"Binary Size Delta: {s1_str} -> {s2_str} ({delta_str})")

    output_fn(f"Total Diff Bytes: {total_diff_str}")

    if diff_info["total_diff_bytes"] == 0:
        output_fn("\n=> RESULT: Binaries are 100% IDENTICAL!")
        return True

    output_fn(f"Total Contiguous Diff Ranges: {len(diff_info['ranges'])}\n")

    if verbose and diff_info["ranges"]:
        ro_symbols = parse_map_symbols(files1["ro_map"])
        rw_symbols = parse_map_symbols(files1["rw_map"])

        output_fn("--- Contiguous Diff Byte Ranges ---")
        for idx, (s, e) in enumerate(diff_info["ranges"], 1):
            length = e - s + 1
            range_matches = []
            if ro_symbols:
                matches_start = map_offset_to_symbols(s, ro_symbols)
                matches_end = map_offset_to_symbols(e, ro_symbols)
                ro_matches = sorted(list(set(matches_start + matches_end)))
                for m in ro_matches:
                    range_matches.append(f"[RO] {m}")

            if rw_symbols:
                matches_start = map_offset_to_symbols(s, rw_symbols)
                matches_end = map_offset_to_symbols(e, rw_symbols)
                rw_matches = sorted(list(set(matches_start + matches_end)))
                for m in rw_matches:
                    range_matches.append(f"[RW] {m}")

            len_str = format_bytes(length, show_exact=False)
            output_fn(f"\n[Range {idx}] Offset: 0x{s:x} .. 0x{e:x} ({len_str})")
            if range_matches:
                output_fn("  Mapped Objects / Symbols:")
                for m in sorted(list(set(range_matches))):
                    output_fn(f"    - {m}")

    net_deltas = {}
    mem_deltas = {}
    is_metadata_only = True
    for image_type, elf_key, map_key in [
        ("RO", "ro_elf", "ro_map"),
        ("RW", "rw_elf", "rw_map"),
    ]:
        elf1 = files1[elf_key]
        elf2 = files2[elf_key]
        map1 = files1[map_key]
        map2 = files2[map_key]
        if elf1 and elf2:
            res = _compare_single_image(
                image_type,
                elf1,
                elf2,
                map1=map1,
                map2=map2,
                output_fn=output_fn,
                sections=sections,
                symbols=symbols,
                verbose=verbose,
            )
            if not res["is_metadata_only"]:
                is_metadata_only = False
            mem_deltas[image_type] = res["mem_delta"]
            net_deltas[image_type] = res["net_delta"]

    bin_size_delta = diff_info["size2"] - diff_info["size1"]

    verdict_title = (
        f"ANALYSIS VERDICT ({project_name})"
        if project_name
        else "ANALYSIS VERDICT"
    )

    output_fn("\n=========================================================")
    output_fn(f"                   {verdict_title}")
    output_fn("=========================================================")
    if is_metadata_only:
        output_fn("=> Difference is LIMITED TO VERSION / BUILD METADATA!")
        output_fn(
            "   Executable code (.text) and data (.data/.bss) are 100% IDENTICAL."
        )
    else:
        output_fn("=> Binary contains EXECUTABLE CODE OR DATA DIFFERENCES.")

    if mem_deltas or net_deltas:
        if symbols:
            summary_title = (
                f"Memory & Symbol Footprint Summary ({project_name}):"
                if project_name
                else "Memory & Symbol Footprint Summary:"
            )
            border = (
                "  +-------+--------------------+"
                "--------------------+--------------------+"
            )
            header = (
                "  | Image |     ROM Delta      |"
                "     RAM Delta      |  Net Symbol Delta  |"
            )
        else:
            summary_title = (
                f"Memory Footprint Summary ({project_name}):"
                if project_name
                else "Memory Footprint Summary:"
            )
            border = "  +-------+--------------------+--------------------+"
            header = "  | Image |     ROM Delta      |     RAM Delta      |"

        output_fn(f"\n  {summary_title}")
        output_fn(border)
        output_fn(header)
        output_fn(border)
        for image_type in ["RO", "RW"]:
            rom_d_str = "-"
            ram_d_str = "-"
            net_d_str = "-"
            if image_type in mem_deltas:
                rom_d, ram_d = mem_deltas[image_type]
                rom_d_str = format_bytes(
                    rom_d, show_exact=exact_bytes, signed=True
                )
                ram_d_str = format_bytes(
                    ram_d, show_exact=exact_bytes, signed=True
                )
            if symbols:
                if image_type in net_deltas:
                    net_d_str = format_bytes(
                        net_deltas[image_type],
                        show_exact=exact_bytes,
                        signed=True,
                    )
                output_fn(
                    f"  |  {image_type:<4} | {rom_d_str:>18} | "
                    f"{ram_d_str:>18} | {net_d_str:>18} |"
                )
            else:
                output_fn(
                    f"  |  {image_type:<4} | {rom_d_str:>18} | "
                    f"{ram_d_str:>18} |"
                )
        output_fn(border)

    if bin_size_delta != 0:
        bin_size_str = format_bytes(
            bin_size_delta, show_exact=True, signed=True
        )
        output_fn(f"\n   Overall ec.bin File Size Delta: {bin_size_str}")
    output_fn("=========================================================\n")

    return not is_metadata_only


def main():
    """Main entry point for analyze_build_diff CLI."""
    parser = argparse.ArgumentParser(
        description="Analyze binary differences between two EC builds."
    )
    parser.add_argument("target1", help="First build directory or ec.bin file")
    parser.add_argument("target2", help="Second build directory or ec.bin file")
    parser.add_argument(
        "-s",
        "--sections",
        action="store_true",
        help="Print detailed section size comparison and changed sections",
    )
    parser.add_argument(
        "-S",
        "--symbols",
        action="store_true",
        help="Print detailed symbol table comparison (added/removed/changed symbols)",
    )
    parser.add_argument(
        "-v",
        "--verbose",
        action="store_true",
        help="Print all detailed section, symbol, and contiguous diff range details",
    )
    parser.add_argument(
        "-b",
        "--bytes",
        action="store_true",
        help="Show exact byte values in memory summary tables",
    )

    args = parser.parse_args()
    analyze_build_diff(
        args.target1,
        args.target2,
        sections=args.sections,
        symbols=args.symbols,
        verbose=args.verbose,
        exact_bytes=args.bytes,
        output_fn=print,
    )


if __name__ == "__main__":
    main()
