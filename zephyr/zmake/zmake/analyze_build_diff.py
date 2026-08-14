# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Module to analyze binary differences between two Zephyr EC builds."""

import argparse
import os
import pathlib


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


def analyze_build_diff(
    target1,
    target2,
    project_name=None,
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

    bin_size_delta = diff_info["size2"] - diff_info["size1"]

    verdict_title = (
        f"ANALYSIS VERDICT ({project_name})"
        if project_name
        else "ANALYSIS VERDICT"
    )

    output_fn("\n=========================================================")
    output_fn(f"                   {verdict_title}")
    output_fn("=========================================================")
    output_fn("=> Binary contains EXECUTABLE CODE OR DATA DIFFERENCES.")

    if bin_size_delta != 0:
        bin_size_str = format_bytes(
            bin_size_delta, show_exact=True, signed=True
        )
        output_fn(f"\n   Overall ec.bin File Size Delta: {bin_size_str}")
    output_fn("=========================================================\n")

    return False


def main():
    """Main entry point for analyze_build_diff CLI."""
    parser = argparse.ArgumentParser(
        description="Analyze binary differences between two EC builds."
    )
    parser.add_argument("target1", help="First build directory or ec.bin file")
    parser.add_argument("target2", help="Second build directory or ec.bin file")

    args = parser.parse_args()
    analyze_build_diff(
        args.target1,
        args.target2,
        output_fn=print,
    )


if __name__ == "__main__":
    main()
