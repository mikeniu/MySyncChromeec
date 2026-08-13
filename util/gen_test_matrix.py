#!/usr/bin/env python3
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Generates test_matrix.json from LCOV tracefiles (lcov.info or coverage.tbz2).

This script parses an LCOV tracefile generated from a full postsubmit/CQ
coverage run (such as from firmware-zephyr-cov-cq), extracts the test suite
associations for each executed source line and file, and writes the resulting
test mapping to test_matrix.json for change-based test selection.
"""

import argparse
import collections
import json
import pathlib
import sys
import tarfile
import tempfile
from typing import Dict, List, Optional, Set, Tuple


def normalize_source_path(
    raw_path: str, repo_dir: pathlib.Path
) -> Optional[str]:
    """Normalizes an absolute or chroot path to a relative platform/ec repo path.

    Args:
        raw_path: The file path extracted from the SF: record in lcov.info.
        repo_dir: The resolved path to the src/platform/ec repository.

    Returns:
        The normalized relative path (e.g., 'common/crc.c'), or None if the
        file does not belong to platform/ec or is a generated/build artifact.
    """
    path_str = raw_path.strip()

    # Ignore build artifacts, twister output, and system headers
    if any(
        pattern in path_str
        for pattern in (
            "/build/",
            "/twister-out",
            "/usr/include",
            "/usr/lib",
            ".cache",
            "/third_party/zephyrproject",
            "/third_party/pigweed",
        )
    ):
        return None

    # Strip standard chroot prefixes
    prefixes = [
        "/mnt/host/source/src/platform/ec/",
        "/mnt/host/source/src/platform/ec-private/",
        str(repo_dir.resolve()) + "/",
    ]
    for prefix in prefixes:
        if path_str.startswith(prefix):
            path_str = path_str[len(prefix) :]
            break

    # If it is still absolute, try to make it relative if within repo_dir
    p = pathlib.Path(path_str)
    if p.is_absolute():
        try:
            p = p.relative_to(repo_dir)
            path_str = str(p)
        except ValueError:
            return None

    # Filter out test mock/framework files that shouldn't be mapped as targets
    if path_str.startswith(("test/mock/", "zephyr/mock/")):
        return None

    return path_str


def parse_lcov_tracefile(
    lcov_file: pathlib.Path, repo_dir: pathlib.Path
) -> Tuple[Dict[str, Dict[str, List[str]]], Dict[str, List[str]]]:
    """Parses an LCOV tracefile and builds line-level and file-level test maps.

    Args:
        lcov_file: Path to the uncompressed lcov.info file.
        repo_dir: Path to the platform/ec repository root.

    Returns:
        A tuple of (by_line, by_file) dictionaries:
        - by_line: {source_file: {line_num_str: [sorted_test_names]}}
        - by_file: {source_file: [sorted_test_names]}
    """
    # by_line_sets: source_file -> line_num -> set of test names
    by_line_sets: Dict[str, Dict[str, Set[str]]] = collections.defaultdict(
        lambda: collections.defaultdict(set)
    )
    by_file_sets: Dict[str, Set[str]] = collections.defaultdict(set)

    current_tn = ""
    current_sf: Optional[str] = None

    with open(lcov_file, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.rstrip("\r\n")
            if not line:
                continue

            if line.startswith("TN:"):
                current_tn = line[3:].strip()
            elif line.startswith("SF:"):
                raw_sf = line[3:].strip()
                current_sf = normalize_source_path(raw_sf, repo_dir)
            elif line.startswith("DA:"):
                if not current_sf or not current_tn:
                    continue
                parts = line[3:].split(",")
                if len(parts) >= 2:
                    line_num, hit_count = parts[0], parts[1]
                    try:
                        if int(hit_count) > 0:
                            by_line_sets[current_sf][line_num].add(current_tn)
                            by_file_sets[current_sf].add(current_tn)
                    except ValueError:
                        continue
            elif line == "end_of_record":
                current_sf = None

    # Convert sets to sorted lists for deterministic JSON output
    by_line: Dict[str, Dict[str, List[str]]] = {}
    for sf in sorted(by_line_sets.keys()):
        line_dict: Dict[str, List[str]] = {}
        for l_num in sorted(by_line_sets[sf].keys(), key=int):
            line_dict[l_num] = sorted(by_line_sets[sf][l_num])
        if line_dict:
            by_line[sf] = line_dict

    by_file: Dict[str, List[str]] = {}
    for sf in sorted(by_file_sets.keys()):
        if by_file_sets[sf]:
            by_file[sf] = sorted(by_file_sets[sf])

    return by_line, by_file


def generate_test_matrix(
    input_path: pathlib.Path,
    output_path: pathlib.Path,
    repo_dir: pathlib.Path,
) -> int:
    """Generates test_matrix.json from the specified input tracefile or archive.

    Args:
        input_path: Path to lcov.info or coverage.tbz2 / .tar.bz2.
        output_path: Path where test_matrix.json will be saved.
        repo_dir: Path to the platform/ec repository root.

    Returns:
        0 on success, non-zero on failure.
    """
    if not input_path.exists():
        print(f"Error: Input file '{input_path}' not found.", file=sys.stderr)
        return 1

    is_archive = tarfile.is_tarfile(input_path) or input_path.suffix in (
        ".tbz2",
        ".bz2",
        ".tar",
        ".gz",
    )

    if is_archive:
        with tempfile.TemporaryDirectory() as temp_dir_name:
            temp_path = pathlib.Path(temp_dir_name)
            with tarfile.open(input_path, "r:*") as tar:
                tar.extractall(path=temp_path)

            # Locate extracted lcov.info
            candidates = list(temp_path.glob("**/lcov.info")) + list(
                temp_path.glob("**/*.info")
            )
            if not candidates:
                print(
                    f"Error: No lcov.info found inside tarball '{input_path}'.",
                    file=sys.stderr,
                )
                return 1
            lcov_file = candidates[0]
            print(f"Parsing tracefile: {lcov_file}...")
            by_line, by_file = parse_lcov_tracefile(lcov_file, repo_dir)
    else:
        print(f"Parsing tracefile: {input_path}...")
        by_line, by_file = parse_lcov_tracefile(input_path, repo_dir)

    output_data = {
        "version": 1,
        "by_file": by_file,
        "by_line": by_line,
    }

    output_path.parent.mkdir(parents=True, exist_ok=True)
    with open(output_path, "w", encoding="utf-8") as f:
        json.dump(output_data, f, indent=2)

    print(
        f"Successfully generated {output_path} with {len(by_file)} source files."
    )
    return 0


def main(argv: Optional[List[str]] = None) -> int:
    """Main CLI entrypoint."""
    script_dir = pathlib.Path(__file__).resolve().parent
    default_repo_dir = script_dir.parent
    default_output = script_dir / "test_matrix.json"

    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument(
        "-i",
        "--input",
        required=True,
        type=pathlib.Path,
        help="Path to input lcov.info or coverage.tbz2 tarball.",
    )
    parser.add_argument(
        "-o",
        "--output",
        default=default_output,
        type=pathlib.Path,
        help=f"Path to output JSON file (default: {default_output}).",
    )
    parser.add_argument(
        "-r",
        "--repo-dir",
        default=default_repo_dir,
        type=pathlib.Path,
        help=f"Path to platform/ec repository root (default: {default_repo_dir}).",
    )

    args = parser.parse_args(argv)
    return generate_test_matrix(
        input_path=args.input.resolve(),
        output_path=args.output.resolve(),
        repo_dir=args.repo_dir.resolve(),
    )


if __name__ == "__main__":
    sys.exit(main())
