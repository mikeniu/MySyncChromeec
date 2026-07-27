# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Module to analyze binary differences between two Zephyr EC builds."""

import argparse
import os
import pathlib
import re

from elftools.elf.constants import SH_FLAGS  # pylint: disable=import-error
from elftools.elf.elffile import ELFFile  # pylint: disable=import-error


def is_ignored_section_or_symbol(name):
    """Check if a section or symbol is build metadata or non-code."""
    ignored = [
        "build_opt",
        "build_date",
        "build_user",
        "build_host",
        "version",
        "cros_ec_version",
        "app_version_str",
    ]
    return name in ignored or name.startswith(
        (".debug", ".pw_tokenizer", "_pw_tokenizer_string_entry")
    )


def find_build_files(path):
    """Locate ec.bin, zephyr.elf, and zephyr.map from a build directory or file."""
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
    """Find exact byte differences and contiguous byte ranges between two binaries."""
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


def get_elf_sections(elf_path):
    """Get section sizes and flags using pyelftools."""
    if not elf_path:
        return {}
    if not os.path.exists(elf_path):
        raise FileNotFoundError(f"ELF file not found: {elf_path}")

    sections = {}
    with open(elf_path, "rb") as f:
        elf = ELFFile(f)
        load_segments = [
            s for s in elf.iter_segments() if s.header["p_type"] == "PT_LOAD"
        ]

        for sec in elf.iter_sections():
            name = sec.name
            if not name:
                continue

            sh = sec.header
            vma = sh["sh_addr"]
            size = sh["sh_size"]
            flags = sh["sh_flags"]
            alloc = bool(flags & SH_FLAGS.SHF_ALLOC)
            readonly = not bool(flags & SH_FLAGS.SHF_WRITE)

            lma = vma
            is_load = alloc and (sh["sh_type"] != "SHT_NOBITS")
            if alloc:
                for seg in load_segments:
                    p_vaddr = seg.header["p_vaddr"]
                    p_memsz = seg.header["p_memsz"]
                    if p_vaddr <= vma < (p_vaddr + p_memsz):
                        lma = seg.header["p_paddr"] + (vma - p_vaddr)
                        break

            sections[name] = {
                "size": size,
                "vma": vma,
                "lma": lma,
                "alloc": alloc,
                "load": is_load,
                "readonly": readonly,
            }

    return sections


def parse_map_memory_regions(map_path):
    """Parse memory regions (Origin, Length) from a zephyr.map file."""
    if not map_path:
        return {}
    if not os.path.exists(map_path):
        raise FileNotFoundError(f"Map file not found: {map_path}")

    regions = {}
    with open(map_path, "r", encoding="utf-8") as f:
        content = f.read()

    match = re.search(
        r"Memory Configuration\s+Name\s+Origin\s+Length"
        r"\s+Attributes(?P<block>.*?)(?:\r?\n\r?\n|\Z)",
        content,
        re.DOTALL,
    )
    if not match:
        return {}

    # Example line: "  FLASH            0x00080000         0x00080000         xr"
    region_pattern = re.compile(
        r"^\s*(?P<name>[A-Za-z0-9_]+)\s+(?P<origin>0x[0-9a-fA-F]+)\s+(?P<length>0x[0-9a-fA-F]+)"
    )
    block = match.group(1)
    for line in block.splitlines():
        m = region_pattern.match(line)
        if m:
            name = m.group("name")
            origin = int(m.group("origin"), 16)
            length = int(m.group("length"), 16)
            regions[name] = {
                "origin": origin,
                "length": length,
            }
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
    """Format byte counts cleanly."""
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


def _compare_single_image(
    image_type,
    elf1,
    elf2,
    map1=None,
    map2=None,
    output_fn=print,
    sections=False,
):
    """Compare memory and section footprint for a single image."""
    is_metadata_only = True
    sec_diff = compare_elf_sections(elf1, elf2, map1=map1, map2=map2)
    rom_delta, ram_delta = 0, 0

    if sec_diff:
        rom1, rom2, rom_delta = sec_diff["rom"]
        ram1, ram2, ram_delta = sec_diff["ram"]

        if sections:
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

    return {
        "is_metadata_only": is_metadata_only,
        "mem_delta": (rom_delta, ram_delta),
    }


def analyze_build_diff(
    target1,
    target2,
    project_name=None,
    sections=False,
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
        for target in (target2, target1):
            p = pathlib.Path(target)
            if p.is_file():
                if p.parent.name == "output":
                    project_name = p.parent.parent.name
                else:
                    project_name = p.parent.name
            elif p.name:
                project_name = p.name
            if project_name:
                break

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
            )
            if not res["is_metadata_only"]:
                is_metadata_only = False
            mem_deltas[image_type] = res["mem_delta"]

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

    if mem_deltas:
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
            if image_type in mem_deltas:
                rom_d, ram_d = mem_deltas[image_type]
                rom_d_str = format_bytes(rom_d, show_exact=False, signed=True)
                ram_d_str = format_bytes(ram_d, show_exact=False, signed=True)
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

    args = parser.parse_args()
    analyze_build_diff(
        args.target1,
        args.target2,
        sections=args.sections,
        output_fn=print,
    )


if __name__ == "__main__":
    main()
