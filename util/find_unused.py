#!/usr/bin/env python3
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Find unused sources, functions and configs by comparing sources to build artifacts.

This script relies on heuristics and DWARF info extraction using pyelftools.
Results must be interpreted by a developer.

Limitations:
- Cannot reliably detect unused header files (.h) without conservative heuristics.
- May miss files included via assembly .inc directives if not in symbol table.
- Heuristics for function definition detection may have false positives/negatives.
- Requires ELF files with debug symbols to be present in build/ directory.
"""

import argparse
from concurrent.futures import ProcessPoolExecutor
import functools
import json
import os
from pathlib import Path
import re
import subprocess
import sys

from elftools.elf.elffile import ELFFile  # pylint: disable=import-error


MAX_WORKERS = os.cpu_count() or 4

C_KEYWORDS = {
    "ARRAY_SIZE",
    "BIT",
    "BUILD_ASSERT",
    "CONFIG_IS_ENABLED",
    "GENMASK",
    "IS_ENABLED",
    "LOG_DBG",
    "LOG_ERR",
    "LOG_INF",
    "LOG_WRN",
    "STATIC_ASSERT",
    "alignof",
    "case",
    "do",
    "else",
    "enum",
    "for",
    "goto",
    "if",
    "return",
    "sizeof",
    "struct",
    "switch",
    "typedef",
    "typeof",
    "union",
    "while",
}

FUNC_DEF_PATTERN = re.compile(
    r"(?:^|\n)\s*"
    r"(?:[a-zA-Z_]\w*[\s*]+)+"
    r"([a-zA-Z_]\w*)"
    r"\s*\([^;{}]*?\)"
    r"(?:\s*__attribute__\s*\(\([^)]*\)\))*"
    r"\s*\{",
    re.DOTALL,
)


@functools.lru_cache(maxsize=16384)
def normalize_path(
    ec_root: Path | None, zephyr_root: Path | None, path: Path
) -> Path:
    """Normalize path to be relative to ec_root and handle cros_sdk mounts."""
    path_str = str(path)

    if "/mnt/host/source/src/platform/ec/" in path_str:
        path_str = path_str.split("/mnt/host/source/src/platform/ec/")[1]
    elif "/mnt/host/source/src/platform/ec-legacy/" in path_str:
        path_str = path_str.split("/mnt/host/source/src/platform/ec-legacy/")[1]
    elif "/mnt/host/source/src/third_party/zephyr/main/" in path_str:
        path_str = (
            "zephyr-main/"
            + path_str.split("/mnt/host/source/src/third_party/zephyr/main/")[1]
        )

    res_path = Path(path_str)

    if res_path.is_absolute():
        if ec_root and res_path.is_relative_to(ec_root):
            res_path = res_path.relative_to(ec_root)
        elif zephyr_root and res_path.is_relative_to(zephyr_root):
            res_path = Path("zephyr-base") / res_path.relative_to(zephyr_root)

    path_str = str(res_path)
    path_str = re.sub(r"^build/zephyr/[\w-]+/modules/ec/", "", path_str)
    path_str = re.sub(r"^build/zephyr/[\w-]+/build-r[ow]/", "", path_str)

    return Path(path_str)


def find_files(paths, patterns):
    """Find files using native find for efficiency."""
    sources = set()
    filter_pattern = []
    for pattern in patterns:
        if filter_pattern:
            filter_pattern.append("-o")
        filter_pattern.extend(["-name", pattern])
    for path in paths:
        if not path.exists():
            continue
        cmd = ["find", str(path), "-type", "f"] + filter_pattern
        result = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            check=False,
        )
        found_files = [Path(p) for p in result.stdout.splitlines()]
        sources.update(found_files)
    return sources


def extract_elf_info(args_tuple) -> tuple[set, set, list]:
    """Extract sources, headers, and functions from an ELF file in a single pass."""
    elf_path, ec_root, zephyr_root = args_tuple
    sources = set()
    headers = set()
    functions = []

    try:
        with open(elf_path, "rb") as f:
            elffile = ELFFile(f)
            if not elffile.has_dwarf_info():
                return sources, headers, functions

            dwarfinfo = elffile.get_dwarf_info()
            for cu in dwarfinfo.iter_CUs():
                top_die = cu.get_top_DIE()
                comp_dir = ""
                cu_name = ""

                if top_die.tag == "DW_TAG_compile_unit":
                    name_attr = top_die.attributes.get("DW_AT_name")
                    comp_dir_attr = top_die.attributes.get("DW_AT_comp_dir")
                    if name_attr:
                        cu_name = name_attr.value.decode("utf-8")
                    if comp_dir_attr:
                        comp_dir = comp_dir_attr.value.decode("utf-8")

                line_program = dwarfinfo.line_program_for_CU(cu)
                if line_program:
                    for file_entry in line_program["file_entry"]:
                        name = file_entry.name.decode("utf-8")
                        dir_index = file_entry.dir_index

                        if dir_index > 0:
                            dir_path = line_program["include_directory"][
                                dir_index - 1
                            ].decode("utf-8")
                            full_path = Path(dir_path) / name
                        else:
                            if comp_dir and not Path(name).is_absolute():
                                full_path = Path(comp_dir) / name
                            else:
                                full_path = Path(name)

                        if not (
                            full_path.name.startswith("<")
                            and full_path.name.endswith(">")
                        ):
                            norm = normalize_path(
                                ec_root, zephyr_root, full_path
                            )
                            if norm.suffix in [".h", ".inc", ".hpp"]:
                                headers.add(norm)
                            else:
                                sources.add(norm)

                for die in cu.iter_DIEs():
                    if die.tag == "DW_TAG_subprogram":
                        name_attr = die.attributes.get("DW_AT_name")
                        if name_attr:
                            func_name = name_attr.value.decode("utf-8")
                            if cu_name:
                                if not Path(cu_name).is_absolute() and comp_dir:
                                    full_path = Path(comp_dir) / cu_name
                                else:
                                    full_path = Path(cu_name)
                                norm = normalize_path(
                                    ec_root, zephyr_root, full_path
                                )
                                functions.append((norm, func_name))
    except Exception:  # pylint: disable=broad-exception-caught
        pass

    return sources, headers, functions


def find_functions_in_file(path):
    """Find all defined functions in a C file."""
    try:
        content = path.read_text(errors="ignore", encoding="utf-8")
    except Exception:  # pylint: disable=broad-exception-caught
        return []

    text = re.sub(r"/\*.*?\*/", "", content, flags=re.DOTALL)
    text = re.sub(r"//.*$", "", text, flags=re.MULTILINE)

    matches = FUNC_DEF_PATTERN.findall(text)
    functions = []
    for match in matches:
        if match not in C_KEYWORDS and len(match) > 1:
            functions.append((path, match))
    return functions


def find_all_defined_functions(paths) -> set:
    """Find all defined functions in all source paths."""
    functions = set()
    sources = find_files(
        paths=paths, patterns=["*.c", "*.h", "*.cc", "*.S", "*.inc"]
    )
    for source in sources:
        functions.update(find_functions_in_file(source))
    return functions


def find_configs(paths) -> set:
    """Find all configs referenced in the source code."""
    configs = set()
    config_pattern = "CONFIG_[A-Z0-9_]*[A-Z0-9]"
    ignored = {"CONFIG_IS_ENABLED", "CONFIG_H", "CONFIG_DIR", "CONFIG_FILE"}
    for path in paths:
        cmd = [
            "grep",
            "--recursive",
            "--no-filename",
            "--binary-files=without-match",
            "--only-matching",
            "--extended-regexp",
            config_pattern,
            str(path),
        ]
        result = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            check=False,
        )
        for c in result.stdout.splitlines():
            if c not in ignored:
                configs.add(c)
    return configs


def find_set_configs(source_paths, build_paths) -> set:
    """Find all enabled or defined configs in build artifacts and sources."""
    configs = set()

    # 1. .config files in build artifacts
    for path in build_paths:
        if not path.exists():
            continue
        for cf in path.rglob("*.config"):
            try:
                for line in cf.read_text(errors="ignore").splitlines():
                    line = line.strip()
                    if line.startswith("CONFIG_") and "=" in line:
                        k, v = line.split("=", 1)
                        if v != "n":
                            configs.add(k)
            except Exception:  # pylint: disable=broad-exception-caught
                pass

    # 2. autoconf.h / kconfig.h / generated headers in build artifacts
    for path in build_paths:
        if not path.exists():
            continue
        for hf in path.rglob("*.h"):
            try:
                for line in hf.read_text(errors="ignore").splitlines():
                    m = re.match(r"^\s*#\s*define\s+(CONFIG_[A-Z0-9_]+)", line)
                    if m:
                        configs.add(m.group(1))
            except Exception:  # pylint: disable=broad-exception-caught
                pass

    # 3. Kconfig definitions in source paths
    for path in source_paths:
        if not path.exists():
            continue
        for kf in path.rglob("Kconfig*"):
            try:
                for line in kf.read_text(errors="ignore").splitlines():
                    m = re.match(r"^\s*(?:menu)?config\s+([A-Z0-9_]+)", line)
                    if m:
                        configs.add("CONFIG_" + m.group(1))
            except Exception:  # pylint: disable=broad-exception-caught
                pass

    # 4. #define CONFIG_ in source headers
    for path in source_paths:
        if not path.exists():
            continue
        for hf in path.rglob("*.h"):
            try:
                for line in hf.read_text(errors="ignore").splitlines():
                    m = re.match(r"^\s*#\s*define\s+(CONFIG_[A-Z0-9_]+)", line)
                    if m:
                        configs.add(m.group(1))
            except Exception:  # pylint: disable=broad-exception-caught
                pass

    return configs


def find_includes(paths) -> tuple[set, set]:
    """Find all #include directives in source paths."""
    full_includes = set()
    base_includes = set()

    for path in paths:
        if not path.exists():
            continue
        cmd = [
            "grep",
            "--recursive",
            "--no-filename",
            "--binary-files=without-match",
            "--only-matching",
            "--extended-regexp",
            r'#include\s*[<"][^>"]+[>"]',
            str(path),
        ]
        result = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            check=False,
        )
        for line in result.stdout.splitlines():
            match = re.search(r'[<"]([^>"]+)[>"]', line)
            if match:
                inc_str = match.group(1).strip()
                full_includes.add(inc_str)
                base_includes.add(Path(inc_str).name)

    return full_includes, base_includes


def is_header_included(
    header: Path, full_includes: set, base_includes: set
) -> bool:
    """Check if header is referenced in includes."""
    h_name = header.name
    if h_name not in base_includes:
        return False

    h_str = str(header)
    has_plain_basename = False

    for inc in full_includes:
        if Path(inc).name == h_name:
            if "/" in inc:
                if h_str.endswith(inc):
                    return True
            else:
                has_plain_basename = True

    return has_plain_basename


def scan_elfs(all_elfs, ec_root, zephyr_root):
    """Scan all ELF files in parallel and extract sources, headers, and functions."""
    used_sources = set()
    used_headers_dwarf = set()
    used_functions = set()

    count = 0
    tasks = [(elf, ec_root, zephyr_root) for elf in all_elfs]
    with ProcessPoolExecutor(max_workers=MAX_WORKERS) as executor:
        futures = [executor.submit(extract_elf_info, t) for t in tasks]
        for future in futures:
            count += 1
            elf_srcs, elf_hdrs, elf_funcs = future.result()
            used_sources.update(elf_srcs)
            used_headers_dwarf.update(elf_hdrs)
            used_functions.update(elf_funcs)
            if sys.stdout.isatty():
                print(f"{count}/{len(all_elfs)}", end="\r", flush=True)

    if sys.stdout.isatty():
        print()

    return used_sources, used_headers_dwarf, used_functions


def main():
    """Main entry point."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "command", choices=["sources", "configs", "functions", "headers", "all"]
    )
    parser.add_argument("--ec-root", type=Path, help="Path to EC root")
    parser.add_argument("--zephyr-root", type=Path, help="Path to Zephyr root")
    parser.add_argument("--board", help="Filter by board name (e.g. 'taniks')")
    parser.add_argument(
        "--include-tests", action="store_true", help="Include test directories"
    )
    parser.add_argument(
        "--json", action="store_true", help="Output results in JSON format"
    )
    parser.add_argument(
        "--output", "-o", type=Path, help="Write output to specified file"
    )
    parser.add_argument(
        "--summary",
        action="store_true",
        help="Print summary metrics and statistics",
    )
    args = parser.parse_args()

    if args.ec_root:
        ec_root = args.ec_root.resolve()
    else:
        ec_root = Path(__file__).resolve().parent.parent

    if args.zephyr_root:
        zephyr_root = args.zephyr_root.resolve()
    else:
        zephyr_root = (
            ec_root.parent / "third_party" / "zephyrproject" / "zephyr"
        )
        if not zephyr_root.exists():
            zephyr_root = Path("/mnt/host/source/src/third_party/zephyr/main/")

    ec_zephyr_root = ec_root / "zephyr"
    source_paths = [
        ec_root / "baseboard",
        ec_root / "board",
        ec_root / "builtin",
        ec_root / "chip",
        ec_root / "common",
        ec_root / "core",
        ec_root / "crypto",
        ec_root / "driver",
        ec_root / "extra",
        ec_root / "include",
        ec_root / "libc",
        ec_root / "power",
        ec_root / "fuzz",
        ec_zephyr_root / "app",
        ec_zephyr_root / "boards",
        ec_zephyr_root / "chip",
        ec_zephyr_root / "drivers",
        ec_zephyr_root / "dts",
        ec_zephyr_root / "include",
        ec_zephyr_root / "lib",
        ec_zephyr_root / "libc",
        ec_zephyr_root / "program",
        ec_zephyr_root / "shim",
        ec_zephyr_root / "subsys",
    ]

    test_source_paths = [
        ec_root / "test",
        ec_zephyr_root / "test",
        ec_zephyr_root / "fake",
        ec_zephyr_root / "emul",
    ]

    if args.include_tests:
        source_paths.extend(test_source_paths)

    source_paths = [p for p in source_paths if p.exists()]

    if args.board:
        build_paths = [
            ec_root / "build" / args.board,
            ec_root / "build" / "zephyr" / args.board,
        ]
    else:
        build_paths = [ec_root / "build"]

    build_paths = [p for p in build_paths if p.exists()]

    if not source_paths:
        print(f"Error: No source paths found in {ec_root}", file=sys.stderr)
        sys.exit(1)

    results = {}
    summary = {}

    need_elfs = args.command in ["sources", "functions", "headers", "all"]
    all_elfs = []
    used_sources = set()
    used_headers_dwarf = set()
    used_functions = set()

    if need_elfs:
        all_elfs = list(
            find_files(paths=build_paths, patterns=["*.elf", "*.exe"])
        )
        if not args.include_tests:
            test_patterns = [
                "minimal-posix",
                "native_sim",
                "test",
                "fake",
                "emul",
                "ztest",
                "hwtest",
            ]
            all_elfs = [
                e
                for e in all_elfs
                if not any(tp in str(e).lower() for tp in test_patterns)
            ]
        if not args.json:
            print(f"Finding elf files in {len(build_paths)} paths...")
            print(f"* Found {len(all_elfs)} elf files")
            print("Extracting info from elf files (sources, headers, DWARF)...")
        used_sources, used_headers_dwarf, used_functions = scan_elfs(
            all_elfs, ec_root, zephyr_root
        )

    # 1. SOURCES
    if args.command in ["sources", "all"]:
        all_sources = find_files(
            paths=source_paths, patterns=["*.c", "*.h", "*.cc", "*.S", "*.inc"]
        )
        all_sources = {
            normalize_path(ec_root, zephyr_root, s) for s in all_sources
        }
        unused_sources = [
            str(s)
            for s in sorted(list(all_sources))
            if s.suffix not in [".h"] and s not in used_sources
        ]
        results["unused_sources"] = unused_sources
        summary["total_sources"] = len(all_sources)
        summary["unused_sources"] = len(unused_sources)

    # 2. HEADERS
    if args.command in ["headers", "all"]:
        all_headers = find_files(paths=source_paths, patterns=["*.h", "*.inc"])
        all_headers = {
            normalize_path(ec_root, zephyr_root, s) for s in all_headers
        }
        full_inc, base_inc = find_includes(source_paths)
        unused_headers = []
        for h in sorted(list(all_headers)):
            if h in used_headers_dwarf:
                continue
            if is_header_included(h, full_inc, base_inc):
                continue
            unused_headers.append(str(h))
        results["unused_headers"] = unused_headers
        summary["total_headers"] = len(all_headers)
        summary["unused_headers"] = len(unused_headers)

    # 3. FUNCTIONS
    if args.command in ["functions", "all"]:
        all_functions = find_all_defined_functions(paths=source_paths)
        all_functions = {
            (normalize_path(ec_root, zephyr_root, f[0]), f[1])
            for f in all_functions
        }
        unused_functions = [
            {"path": str(p), "name": name}
            for p, name in sorted(list(all_functions - used_functions))
        ]
        results["unused_functions"] = unused_functions
        summary["total_functions"] = len(all_functions)
        summary["unused_functions"] = len(unused_functions)

    # 4. CONFIGS
    if args.command in ["configs", "all"]:
        all_configs = find_configs(source_paths)
        set_configs = find_set_configs(source_paths, build_paths)
        never_set = sorted(list(all_configs - set_configs))
        results["never_set_configs"] = never_set
        summary["total_configs"] = len(all_configs)
        summary["never_set_configs"] = len(never_set)

    # OUTPUT FORMATTING
    output_str = ""
    if args.json:
        out_obj = {"command": args.command, "summary": summary, **results}
        output_str = json.dumps(out_obj, indent=2)
    else:
        out_lines = []
        if args.summary or args.command == "all":
            out_lines.append("=== SUMMARY METRICS ===")
            for k, v in summary.items():
                out_lines.append(f"  {k}: {v}")
            out_lines.append("")

        if "unused_sources" in results:
            out_lines.append(f"UNUSED SOURCES({summary['unused_sources']}):\n")
            for s in results["unused_sources"]:
                out_lines.append(s)
            out_lines.append("")

        if "unused_headers" in results:
            out_lines.append(f"UNUSED HEADERS({summary['unused_headers']}):\n")
            for h in results["unused_headers"]:
                out_lines.append(h)
            out_lines.append("")

        if "unused_functions" in results:
            out_lines.append(
                f"UNUSED FUNCTIONS({summary['unused_functions']}):\n"
            )
            for f in results["unused_functions"]:
                out_lines.append(f"{f['path']}: {f['name']}")
            out_lines.append("")

        if "never_set_configs" in results:
            out_lines.append(
                f"NEVER SET CONFIGS({summary['never_set_configs']}):\n"
            )
            for c in results["never_set_configs"]:
                out_lines.append(c)
            out_lines.append("")

        output_str = "\n".join(out_lines).strip()

    if args.output:
        args.output.write_text(output_str + "\n", encoding="utf-8")
        print(f"Results written to {args.output}")
    else:
        print(output_str)


if __name__ == "__main__":
    main()
