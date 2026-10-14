#!/usr/bin/env python3
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# pylint: disable=too-many-lines

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
from concurrent.futures import as_completed
from concurrent.futures import ProcessPoolExecutor
import functools
import os
from pathlib import Path
import re
import subprocess
import sys

# pylint: disable=import-error
from elftools.elf.elffile import ELFFile
from elftools.elf.sections import SymbolTableSection


MAX_WORKERS = os.cpu_count() or 4


@functools.lru_cache(maxsize=16384)
def normalize_path(
    ec_root: Path | None, zephyr_root: Path | None, path: Path
) -> Path:
    """Normalize path to be relative to ec_root and handle cros_sdk mounts."""
    path_str = os.path.normpath(str(path))

    if "/src/platform/ec/" in path_str:
        path_str = path_str.split("/src/platform/ec/", 1)[1]
    elif "/src/platform/ec-legacy/" in path_str:
        path_str = path_str.split("/src/platform/ec-legacy/", 1)[1]
    elif "/src/third_party/zephyr/main/" in path_str:
        path_str = (
            "zephyr-main/"
            + path_str.split("/src/third_party/zephyr/main/", 1)[1]
        )
    elif "/src/third_party/zephyrproject/zephyr/" in path_str:
        path_str = (
            "zephyr-base/"
            + path_str.split("/src/third_party/zephyrproject/zephyr/", 1)[1]
        )

    res_path = Path(path_str)

    if res_path.is_absolute():
        if ec_root and res_path.is_relative_to(ec_root):
            res_path = res_path.relative_to(ec_root)
        elif zephyr_root and res_path.is_relative_to(zephyr_root):
            res_path = Path("zephyr-base") / res_path.relative_to(zephyr_root)

    path_str = os.path.normpath(str(res_path))
    path_str = re.sub(r"^build/zephyr/[\w-]+/modules/ec/", "", path_str)
    path_str = re.sub(r"^build/zephyr/[\w-]+/build-r[ow]/", "", path_str)
    path_str = re.sub(r"^twister-out[^/]*/.*/modules/ec/", "", path_str)

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
        cmd = [
            "find",
            str(path),
            "(",
            "-name",
            "CMakeFiles",
            "-o",
            "-name",
            "modules",
            "-o",
            "-name",
            "output",
            "-o",
            "-name",
            "packer",
            ")",
            "-prune",
            "-o",
            "-type",
            "f",
        ]
        if filter_pattern:
            cmd.extend(["("] + filter_pattern + [")"])
        cmd.append("-print")
        result = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            check=False,
        )
        found_files = [Path(p) for p in result.stdout.splitlines()]
        sources.update(found_files)
    return sources


def _resolve_line_program_files(
    line_program, comp_dir: str, ec_root: Path | None, zephyr_root: Path | None
) -> tuple[list[Path | None], set[Path], set[Path]]:
    """Resolve file_entry table in a CU's line_program to normalized Paths."""
    cu_files: list[Path | None] = []
    sources: set[Path] = set()
    headers: set[Path] = set()

    if not line_program:
        return cu_files, sources, headers

    inc_dirs = line_program["include_directory"]
    is_dwarf5 = line_program.header.version >= 5
    for file_entry in line_program["file_entry"]:
        name = file_entry.name.decode("utf-8", errors="ignore")
        dir_index = file_entry.dir_index

        if is_dwarf5:
            if 0 <= dir_index < len(inc_dirs):
                dir_path = inc_dirs[dir_index].decode("utf-8", errors="ignore")
                if comp_dir and not Path(dir_path).is_absolute():
                    full_path = Path(comp_dir) / dir_path / name
                else:
                    full_path = Path(dir_path) / name
            elif comp_dir and not Path(name).is_absolute():
                full_path = Path(comp_dir) / name
            else:
                full_path = Path(name)
        elif 0 < dir_index <= len(inc_dirs):
            dir_path = inc_dirs[dir_index - 1].decode("utf-8", errors="ignore")
            if comp_dir and not Path(dir_path).is_absolute():
                full_path = Path(comp_dir) / dir_path / name
            else:
                full_path = Path(dir_path) / name
        else:
            if comp_dir and not Path(name).is_absolute():
                full_path = Path(comp_dir) / name
            else:
                full_path = Path(name)

        if full_path.name.startswith("<") and full_path.name.endswith(">"):
            cu_files.append(None)
        else:
            norm = normalize_path(ec_root, zephyr_root, full_path)
            cu_files.append(norm)
            if norm.suffix in [".h", ".inc", ".hpp"]:
                headers.add(norm)
            else:
                sources.add(norm)

    return cu_files, sources, headers


def extract_elf_info(args_tuple) -> tuple[set, set, list]:
    """Extract sources, headers, and active functions from an ELF file in a single pass."""
    elf_path, ec_root, zephyr_root, need_functions = args_tuple
    sources = set()
    headers = set()
    functions = []

    try:
        with open(elf_path, "rb") as f:
            elffile = ELFFile(f)
            if not elffile.has_dwarf_info():
                return sources, headers, functions

            sym_funcs = set()
            if need_functions:
                for sec in elffile.iter_sections():
                    if isinstance(sec, SymbolTableSection):
                        for sym in sec.iter_symbols():
                            if (
                                sym["st_info"]["type"] == "STT_FUNC"
                                and sym["st_shndx"] != "SHN_UNDEF"
                                and sym["st_value"] > 0
                                and sym["st_size"] > 0
                            ):
                                sym_funcs.add(sym.name.split(".")[0])

            dwarfinfo = elffile.get_dwarf_info()
            subprogs = {}
            active_offsets = set()

            def walk_cu_dies(
                die,
                cu_offset,
                cu_norm,
                cu_files,
                is_dwarf5,
                is_cpp,
                scope: tuple[str, ...] = (),
            ):
                for child in die.iter_children():
                    if child.tag == "DW_TAG_subprogram":
                        name_attr = child.attributes.get("DW_AT_name")
                        func_name = (
                            name_attr.value.decode("utf-8", errors="ignore")
                            if name_attr
                            else None
                        )
                        is_decl = "DW_AT_declaration" in child.attributes

                        decl_norm = None
                        decl_attr = child.attributes.get("DW_AT_decl_file")
                        if decl_attr is not None and cu_files:
                            f_idx = (
                                decl_attr.value
                                if is_dwarf5
                                else decl_attr.value - 1
                            )
                            if 0 <= f_idx < len(cu_files):
                                decl_norm = cu_files[f_idx]

                        orig = child.attributes.get(
                            "DW_AT_abstract_origin"
                        ) or child.attributes.get("DW_AT_specification")
                        orig_off = None
                        if orig is not None:
                            orig_off = orig.value + (
                                cu_offset
                                if orig.form != "DW_FORM_ref_addr"
                                else 0
                            )

                        subprogs[child.offset] = (
                            func_name,
                            scope,
                            decl_norm,
                            cu_norm,
                            orig_off,
                        )

                        lpc = child.attributes.get("DW_AT_low_pc")
                        rng = child.attributes.get("DW_AT_ranges")
                        has_code = (lpc is not None and lpc.value > 0) or (
                            rng is not None and lpc is None
                        )

                        if has_code or (not is_decl and func_name in sym_funcs):
                            active_offsets.add(child.offset)
                            if orig_off is not None:
                                active_offsets.add(orig_off)

                        if has_code and child.has_children:
                            stack = [child]
                            while stack:
                                curr = stack.pop()
                                for c in curr.iter_children():
                                    if c.tag in (
                                        "DW_TAG_inlined_subroutine",
                                        "DW_TAG_subprogram",
                                    ):
                                        io = c.attributes.get(
                                            "DW_AT_abstract_origin"
                                        ) or c.attributes.get(
                                            "DW_AT_specification"
                                        )
                                        if io is not None:
                                            active_offsets.add(
                                                io.value
                                                + (
                                                    cu_offset
                                                    if io.form
                                                    != "DW_FORM_ref_addr"
                                                    else 0
                                                )
                                            )
                                    if c.has_children:
                                        stack.append(c)
                    elif is_cpp and child.tag in (
                        "DW_TAG_namespace",
                        "DW_TAG_class_type",
                        "DW_TAG_structure_type",
                        "DW_TAG_union_type",
                    ):
                        s_attr = child.attributes.get("DW_AT_name")
                        s_name = (
                            s_attr.value.decode("utf-8", errors="ignore")
                            if s_attr
                            else None
                        )
                        next_scope = scope + (s_name,) if s_name else scope
                        walk_cu_dies(
                            child,
                            cu_offset,
                            cu_norm,
                            cu_files,
                            is_dwarf5,
                            is_cpp,
                            next_scope,
                        )

            for cu in dwarfinfo.iter_CUs():
                top_die = cu.get_top_DIE()
                comp_dir = ""
                cu_name = ""

                if top_die.tag == "DW_TAG_compile_unit":
                    name_attr = top_die.attributes.get("DW_AT_name")
                    comp_dir_attr = top_die.attributes.get("DW_AT_comp_dir")
                    if name_attr:
                        cu_name = name_attr.value.decode(
                            "utf-8", errors="ignore"
                        )
                    if comp_dir_attr:
                        comp_dir = comp_dir_attr.value.decode(
                            "utf-8", errors="ignore"
                        )

                line_program = dwarfinfo.line_program_for_CU(cu)
                cu_files, cu_srcs, cu_hdrs = _resolve_line_program_files(
                    line_program, comp_dir, ec_root, zephyr_root
                )
                sources.update(cu_srcs)
                headers.update(cu_hdrs)

                if need_functions:
                    cu_norm = None
                    if cu_name and not (
                        cu_name.startswith("<") and cu_name.endswith(">")
                    ):
                        if not Path(cu_name).is_absolute() and comp_dir:
                            full_cu = Path(comp_dir) / cu_name
                        else:
                            full_cu = Path(cu_name)
                        cu_norm = normalize_path(ec_root, zephyr_root, full_cu)

                    is_dwarf5 = (
                        line_program.header.version >= 5
                        if line_program
                        else False
                    )
                    lang_attr = top_die.attributes.get("DW_AT_language")
                    is_cpp = cu_name.endswith((".cc", ".cpp", ".cxx")) or (
                        lang_attr is not None
                        and lang_attr.value in (4, 25, 26, 33, 42, 43)
                    )
                    walk_cu_dies(
                        top_die,
                        cu.cu_offset,
                        cu_norm,
                        cu_files,
                        is_dwarf5,
                        is_cpp,
                    )

            if need_functions:
                changed = True
                while changed:
                    changed = False
                    for off in list(active_offsets):
                        info = subprogs.get(off)
                        if info is not None:
                            _, _, _, _, orig_off = info
                            if (
                                orig_off is not None
                                and orig_off not in active_offsets
                            ):
                                active_offsets.add(orig_off)
                                changed = True

                for off in active_offsets:
                    info = subprogs.get(off)
                    if info is None:
                        continue
                    func_name, scope, decl_norm, cu_norm, orig_off = info
                    visited = {off}
                    curr_orig = orig_off
                    while curr_orig is not None and curr_orig not in visited:
                        visited.add(curr_orig)
                        orig_info = subprogs.get(curr_orig)
                        if orig_info is None:
                            break
                        o_name, o_scope, o_decl, o_cu, next_orig = orig_info
                        if not func_name and o_name:
                            func_name = o_name
                        if not scope and o_scope:
                            scope = o_scope
                        if decl_norm is None and o_decl is not None:
                            decl_norm = o_decl
                        if cu_norm is None and o_cu is not None:
                            cu_norm = o_cu
                        curr_orig = next_orig

                    if func_name:
                        for i in range(len(scope) + 1):
                            cand = "::".join(scope[i:] + (func_name,))
                            if decl_norm is not None:
                                functions.append((decl_norm, cand))
                            if cu_norm is not None and cu_norm != decl_norm:
                                functions.append((cu_norm, cand))
    except Exception:  # pylint: disable=broad-exception-caught
        pass

    return sources, headers, functions


def scan_elfs(all_elfs, ec_root, zephyr_root, need_functions: bool = True):
    """Scan all ELF files in parallel and extract sources, headers, and functions."""
    used_sources = set()
    used_headers_dwarf = set()
    used_functions = set()

    count = 0
    tasks = [(elf, ec_root, zephyr_root, need_functions) for elf in all_elfs]
    with ProcessPoolExecutor(max_workers=MAX_WORKERS) as executor:
        futures = [executor.submit(extract_elf_info, t) for t in tasks]
        for future in as_completed(futures):
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


def find_configs(paths) -> set:
    """Find all configs referenced in the source code."""
    configs = set()
    config_pattern = "CONFIG_[A-Z0-9_]*[A-Z0-9]"
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
        configs.update(result.stdout.splitlines())
    return configs


def find_set_configs(paths) -> set:
    """Find all enabled configs in .config build artifacts."""
    configs = set()
    config_set_pattern = "^CONFIG_[A-Z0-9_]*[A-Z0-9]+=.*"
    for path in paths:
        cmd = [
            "grep",
            "--recursive",
            "--no-filename",
            "--binary-files=without-match",
            "--extended-regexp",
            "--include=ec.config",
            "--include=.config",
            config_set_pattern,
            str(path),
        ]
        result = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            check=False,
        )
        configs.update({c.split("=")[0] for c in result.stdout.splitlines()})
    return configs


def find_functions_in_file(path):
    """Find all defined functions in a file."""
    functions = []
    try:
        with open(path, "r", errors="ignore", encoding="utf-8") as f:
            content = f.read()
    except Exception:  # pylint: disable=broad-exception-caught
        return []
    matches = re.findall(
        r"^(\w+[\w:*\s]+\s\**([\w.:]+)\([\w\s,*]*\))\s*{", content, re.MULTILINE
    )
    for match in matches:
        functions.append((path, match[1]))
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


def _collect_elfs(build_paths, include_tests: bool = False):
    """Find and filter ELF files in build_paths."""
    raw_elfs = find_files(paths=build_paths, patterns=["*.elf", "*.exe"])
    raw_elf_set = set(raw_elfs)
    all_elfs = [
        e
        for e in sorted(raw_elfs)
        if e.name != "zephyr_pre0.elf"
        and not (
            e.name == "zephyr.exe" and (e.parent / "zephyr.elf") in raw_elf_set
        )
    ]
    if not include_tests:
        test_patterns = [
            "build/host/",
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
            if not any(
                tp in str(e).rsplit("/src/platform/ec/", maxsplit=1)[-1].lower()
                for tp in test_patterns
            )
        ]
    return all_elfs


def find_unused_functions(
    ec_root, zephyr_root, source_paths, build_paths, include_tests=False
):
    """Find functions that are defined but never appear in ELFs."""
    print(f"Finding all defined functions in {len(source_paths)} paths...")
    all_functions = find_all_defined_functions(paths=source_paths)
    all_functions = {
        (normalize_path(ec_root, zephyr_root, f[0]), f[1])
        for f in all_functions
    }
    print(f"* Found {len(all_functions)} defined functions")

    print(f"Finding elf files in {len(build_paths)} paths...")
    all_elfs = _collect_elfs(build_paths, include_tests=include_tests)
    print(f"* Found {len(all_elfs)} elf files")

    print("Extracting functions from elf files...")
    _, _, used_functions = scan_elfs(
        all_elfs, ec_root, zephyr_root, need_functions=True
    )
    print(f"\n* Extracted {len(used_functions)} unique functions from elfs")

    unused_functions = all_functions - used_functions
    print(f"\nUNUSED FUNCTIONS({len(unused_functions)}):\n")
    for path, function_name in sorted(list(unused_functions)):
        print(f"{str(path)}: {function_name}")


def find_unused_configs(source_paths, build_paths):
    """Find configs that are referenced in source but never set in builds."""
    print("Finding all referenced configs...")
    all_configs = find_configs(source_paths)
    print(f"* Found {len(all_configs)} configs")

    print("Finding all set configs...")
    set_configs = find_set_configs(build_paths)
    print(f"* Found {len(set_configs)} set configs")

    never_set_configs = all_configs - set_configs
    print(f"\nNEVER SET CONFIGS({len(never_set_configs)}):\n")
    for config in sorted(list(never_set_configs)):
        print(config)


def find_unused_sources(
    ec_root, zephyr_root, source_paths, build_paths, include_tests=False
):
    """Find source files that are never compiled into any ELF."""
    print(f"Finding all source files in {len(source_paths)} paths...")
    all_sources = find_files(
        paths=source_paths, patterns=["*.c", "*.h", "*.cc", "*.S", "*.inc"]
    )
    all_sources = {normalize_path(ec_root, zephyr_root, s) for s in all_sources}
    print(f"* Found {len(all_sources)} source files")

    print(f"Finding elf files in {len(build_paths)} paths...")
    all_elfs = _collect_elfs(build_paths, include_tests=include_tests)
    print(f"* Found {len(all_elfs)} elf files")

    print("Extracting sources from elf files...")
    used_sources, used_headers_dwarf, _ = scan_elfs(
        all_elfs, ec_root, zephyr_root, need_functions=False
    )
    used_sources.update(used_headers_dwarf)
    print(f"\n* Extracted {len(used_sources)} unique source files from elfs")

    unused_sources = []
    for source in sorted(list(all_sources)):
        # Can't reliably detect unused header files using this method
        if source.suffix in [".h"]:
            continue
        if source not in used_sources:
            unused_sources.append(source)

    print(f"\nUNUSED SOURCES({len(unused_sources)}):\n")
    for source in unused_sources:
        print(source)


def find_unused_headers(
    ec_root, zephyr_root, source_paths, build_paths, include_tests=False
):
    """Find header files that are never included anywhere."""
    print(f"Finding all header files in {len(source_paths)} paths...")
    all_headers = find_files(paths=source_paths, patterns=["*.h", "*.inc"])
    all_headers = {normalize_path(ec_root, zephyr_root, s) for s in all_headers}
    print(f"* Found {len(all_headers)} header files")

    print(f"Finding elf files in {len(build_paths)} paths...")
    all_elfs = _collect_elfs(build_paths, include_tests=include_tests)
    print(f"* Found {len(all_elfs)} elf files")

    print("Extracting headers from elf files (DWARF heuristic)...")
    _, used_headers_dwarf, _ = scan_elfs(
        all_elfs, ec_root, zephyr_root, need_functions=False
    )
    print(f"\n* DWARF info reported {len(used_headers_dwarf)} headers as used")

    print("Searching for #include directives (Grep heuristic)...")
    used_headers_grep = set()
    for path in source_paths:
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
                include_path = match.group(1)
                used_headers_grep.add(Path(include_path).name)

    print(f"* Found {len(used_headers_grep)} unique filenames in includes")

    unused_headers = []
    for header in sorted(list(all_headers)):
        if header in used_headers_dwarf:
            continue
        if header.name in used_headers_grep:
            continue
        unused_headers.append(header)

    print(f"\nUNUSED HEADERS({len(unused_headers)}):\n")
    for header in unused_headers:
        print(header)


def main():
    """Main entry point."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "command", choices=["sources", "configs", "functions", "headers"]
    )
    parser.add_argument("--ec-root", type=Path, help="Path to EC root")
    parser.add_argument("--zephyr-root", type=Path, help="Path to Zephyr root")
    parser.add_argument("--board", help="Filter by board name (e.g. 'taniks')")
    parser.add_argument(
        "--include-tests", action="store_true", help="Include test directories"
    )
    args = parser.parse_args()

    if args.ec_root:
        ec_root = args.ec_root.resolve()
    else:
        ec_root = Path(__file__).resolve().parent.parent

    # Try to find zephyr root if not provided
    if args.zephyr_root:
        zephyr_root = args.zephyr_root.resolve()
    else:
        # Heuristic for Zephyr root in chromiumos tree
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
        build_paths = [ec_root / "build" / args.board]
    else:
        build_paths = [ec_root / "build"]

    build_paths = [p for p in build_paths if p.exists()]

    if not source_paths:
        print(f"Error: No source paths found in {ec_root}", file=sys.stderr)
        sys.exit(1)

    if args.command == "sources":
        find_unused_sources(
            ec_root,
            zephyr_root,
            source_paths,
            build_paths,
            include_tests=args.include_tests,
        )
    elif args.command == "configs":
        find_unused_configs(source_paths, build_paths)
    elif args.command == "functions":
        find_unused_functions(
            ec_root,
            zephyr_root,
            source_paths,
            build_paths,
            include_tests=args.include_tests,
        )
    elif args.command == "headers":
        find_unused_headers(
            ec_root,
            zephyr_root,
            source_paths,
            build_paths,
            include_tests=args.include_tests,
        )


if __name__ == "__main__":
    main()
