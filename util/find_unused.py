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
import ast
from concurrent.futures import as_completed
from concurrent.futures import ProcessPoolExecutor
import configparser
import fnmatch
import functools
import json
import os
from pathlib import Path
import re
import subprocess
import sys

# pylint: disable=import-error
from elftools.elf.elffile import ELFFile
from elftools.elf.sections import SymbolTableSection


MAX_WORKERS = os.cpu_count() or 4

DEFAULT_COPYBOT_PATTERNS = [
    "common/fpsensor/*",
    "common/mock/fpsensor*",
    "common/mock/rollback*",
    "common/rollback*",
    "driver/fingerprint/*",
    "include/ec_commands.h",
    "include/ec_cmd_api.h",
    "include/fpsensor/*",
    "test/run_device_tests.py",
    "util/flash_ec",
    "util/flash_jlink.py",
    "util/renode/*",
    "util/renode_ec_launch.py",
]

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
    r"(?:(?!return\b|else\b|case\b|goto\b)[a-zA-Z_][\w:]*[\s*]+)+"
    r"([\w.:]+)"
    r"\s*\([^;{}]*\)"
    r"(?:\s*(?:const\b|__attribute__\s*\(\((?:[^()]|\([^()]*\))*\)\)))*"
    r"\s*\{",
    re.DOTALL,
)


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
    config_pattern = r"\bCONFIG_[A-Z][A-Z0-9_]*[A-Z0-9]\b"
    ignored = {
        "CONFIG_IS_ENABLED",
        "CONFIG_H",
        "CONFIG_CHIP_H",
        "CONFIG_DIR",
        "CONFIG_FILE",
    }
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
            if c not in ignored and not c.endswith("_H_"):
                configs.add(c)
    return configs


def find_set_configs(source_paths, build_paths) -> set:
    """Find all enabled or defined configs in build artifacts and sources."""
    configs = {"CONFIG_ZEPHYR"}

    # 1. .config, ec.config, autoconf.h, and ec_config.h in build artifacts
    cfg_files = find_files(
        paths=build_paths,
        patterns=["*.config", ".config", "autoconf.h", "ec_config.h"],
    )
    for cf in cfg_files:
        try:
            txt = cf.read_text(errors="ignore")
            for m in re.finditer(r"^(CONFIG_[A-Z0-9_]+)=(.+)$", txt, re.M):
                if m.group(2) != "n":
                    configs.add(m.group(1))
            for m in re.finditer(
                r"^\s*#\s*define\s+(CONFIG_[A-Z0-9_]+)", txt, re.M
            ):
                configs.add(m.group(1))
        except Exception:  # pylint: disable=broad-exception-caught
            pass

    # 2. Map enabled CONFIG_PLATFORM_EC_* Kconfigs to legacy CONFIG_*
    for c in list(configs):
        if c.startswith("CONFIG_PLATFORM_EC_"):
            configs.add("CONFIG_" + c[len("CONFIG_PLATFORM_EC_") :])

    # 3. Evaluate unconditional and conditional #define CONFIG_ in headers
    def parse_if_groups(expr: str) -> list[list[str]]:
        # Strip negated terms (!defined(CONFIG_X) / !CONFIG_X) so default/exclusion
        # guards don't block positive requirements, while keeping pure #ifndef / #if !defined
        # guards handled when no positive CONFIG_ remains.
        pos_expr = re.sub(r"^#\s*(?:el)?if(?:n?def)?\b", "", expr)
        pos_expr = re.sub(
            r"!\s*(?:defined\s*\(\s*)?CONFIG_[A-Z0-9_]+\s*\)?",
            "__NEG_CFG__",
            pos_expr,
        )
        pos_expr = re.sub(
            r"\b(?:defined|IS_ENABLED|CONFIG_IS_ENABLED)\s*\(\s*(CONFIG_[A-Z0-9_]+)\s*\)",
            r"\1",
            pos_expr,
        )

        def _split_top(s: str, sep: str) -> list[str]:
            parts, depth, last, i = [], 0, 0, 0
            while i < len(s):
                if s[i] == "(":
                    depth += 1
                elif s[i] == ")":
                    depth = max(0, depth - 1)
                elif depth == 0 and s.startswith(sep, i):
                    parts.append(s[last:i])
                    i += len(sep)
                    last = i
                    continue
                i += 1
            parts.append(s[last:])
            return parts

        def _to_cnf(s: str) -> list[list[str]]:
            s = s.strip()
            or_parts = _split_top(s, "||")
            if len(or_parts) > 1:
                acc: list[list[str]] = [[]]
                for p in or_parts:
                    p_cnf = _to_cnf(p)
                    if not p_cnf:
                        return []
                    acc = [g1 + g2 for g1 in acc for g2 in p_cnf]
                return acc
            and_parts = _split_top(s, "&&")
            if len(and_parts) > 1:
                res: list[list[str]] = []
                for p in and_parts:
                    res.extend(_to_cnf(p))
                return res
            if s.startswith("(") and s.endswith(")"):
                depth = 0
                is_wrapped = True
                for ch in s[:-1]:
                    if ch == "(":
                        depth += 1
                    elif ch == ")":
                        depth -= 1
                        if depth == 0:
                            is_wrapped = False
                            break
                if is_wrapped:
                    return _to_cnf(s[1:-1])
            cfgs = re.findall(r"CONFIG_[A-Z0-9_]+", s)
            if cfgs:
                return [cfgs]
            return (
                []
                if ("__NEG_CFG__" in s or "DT_" in s)
                else [["__UNSATISFIED_NON_CONFIG__"]]
            )

        return _to_cnf(pos_expr)

    hdr_files = find_files(paths=source_paths, patterns=["*.h"])
    cond_rules = []
    for hf in hdr_files:
        if hf.name == "config.h":
            continue
        try:
            txt = hf.read_text(errors="ignore")
            txt = re.sub(r"\\\n", " ", txt)
            stack = []
            for line in txt.splitlines():
                s = line.strip()
                if re.match(
                    r"^#\s*(?:ifndef\s+\w+_H_?\b|ifdef\s+__cplusplus\b)", s
                ):
                    stack.append([])
                elif re.match(r"^#\s*ifndef\s+(CONFIG_[A-Z0-9_]+)\b", s):
                    # Default fallback pattern: #ifndef CONFIG_X / #define CONFIG_X
                    stack.append([])
                elif re.match(r"^#\s*if", s):
                    stack.append(parse_if_groups(s))
                elif re.match(r"^#\s*elif", s):
                    if stack:
                        stack[-1] = parse_if_groups(s)
                elif re.match(r"^#\s*else\b", s):
                    if stack:
                        stack[-1] = []
                elif re.match(r"^#\s*endif", s):
                    if stack:
                        stack.pop()
                else:
                    m = re.match(r"^#\s*define\s+(CONFIG_[A-Z0-9_]+)", s)
                    if (
                        m
                        and not m.group(1).endswith("_H")
                        and not m.group(1).endswith("_H_")
                    ):
                        cond_groups = [
                            grp for level in stack for grp in level if grp
                        ]
                        if not cond_groups:
                            configs.add(m.group(1))
                        else:
                            cond_rules.append((cond_groups, m.group(1)))
        except Exception:  # pylint: disable=broad-exception-caught
            pass

    changed = True
    while changed:
        changed = False
        for cond_groups, target in cond_rules:
            if target not in configs and all(
                any(c in configs for c in grp) for grp in cond_groups
            ):
                configs.add(target)
                changed = True

    return configs


def find_includes(paths) -> tuple[dict[str, set[str]], set[str]]:
    """Find all #include directives in source paths."""
    full_includes: dict[str, set[str]] = {}
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
                base_name = os.path.basename(inc_str)
                full_includes.setdefault(base_name, set()).add(inc_str)
                base_includes.add(base_name)

    return full_includes, base_includes


def is_header_included(
    header: Path, full_includes: dict[str, set[str]], base_includes: set[str]
) -> bool:
    """Check if header is referenced in includes."""
    h_name = header.name
    if h_name not in base_includes:
        return False

    h_str = str(header)
    has_plain_basename = False

    for inc in full_includes.get(h_name, ()):
        if "/" in inc:
            if h_str == inc or h_str.endswith("/" + inc):
                return True
        else:
            has_plain_basename = True

    return has_plain_basename


def load_copybot_patterns(
    ec_root: Path, copybot_config: Path | None = None
) -> list[str]:
    """Load include-downstream globs from the ec-legacy copybot config."""
    candidates = []
    if copybot_config:
        candidates.append(copybot_config.resolve())
    else:
        if len(ec_root.parents) >= 3:
            candidates.append(
                ec_root.parents[2]
                / "infra"
                / "copybot"
                / "config"
                / "fingerprint"
                / "ec-legacy.ini"
            )
        candidates.append(
            Path(
                "/mnt/host/source/infra/copybot/config/fingerprint/ec-legacy.ini"
            )
        )

    for cfg_path in candidates:
        if cfg_path.exists():
            try:
                cp = configparser.ConfigParser()
                cp.read(cfg_path, encoding="utf-8")
                if cp.has_option("copybot", "include-downstream"):
                    raw = cp.get("copybot", "include-downstream")
                    parsed = ast.literal_eval(raw)
                    if isinstance(parsed, list):
                        return [str(x) for x in parsed]
            except (
                OSError,
                configparser.Error,
                ValueError,
                SyntaxError,
            ):
                pass

    return list(DEFAULT_COPYBOT_PATTERNS)


def is_copybot_synced(rel_path: Path | str, patterns: list[str]) -> bool:
    """Return True if rel_path matches any copybot include-downstream glob."""
    rel_str = str(rel_path)
    return any(fnmatch.fnmatch(rel_str, pat) for pat in patterns)


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


def main():
    """Main entry point."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "command", choices=["sources", "configs", "functions", "headers", "all"]
    )
    parser.add_argument("--ec-root", type=Path, help="Path to EC root")
    parser.add_argument("--zephyr-root", type=Path, help="Path to Zephyr root")
    parser.add_argument(
        "--build-dir",
        type=Path,
        action="append",
        help="Path to build directory (can be specified multiple times)",
    )
    parser.add_argument("--board", help="Filter by board name (e.g. 'taniks')")
    parser.add_argument(
        "--include-tests", action="store_true", help="Include test directories"
    )
    parser.add_argument(
        "--ignore-copybot",
        action="store_true",
        help="Ignore files synced to platform/ec-legacy via copybot",
    )
    parser.add_argument(
        "--copybot-config",
        type=Path,
        help="Path to copybot ec-legacy.ini config file",
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
        ec_root / "builtin",
        ec_root / "common",
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
        ec_zephyr_root / "mock",
    ]

    if args.include_tests:
        source_paths.extend(test_source_paths)

    source_paths = [p for p in source_paths if p.exists()]

    if args.build_dir:
        build_paths = [p.resolve() for p in args.build_dir]
    elif args.board:
        build_paths = [
            ec_root / "build" / args.board,
            ec_root / "build" / "zephyr" / args.board,
        ]
    else:
        build_paths = [ec_root / "build"]
        if args.include_tests:
            build_paths.extend(
                p
                for p in sorted(ec_root.glob("twister-out*"))
                if not re.match(r"^twister-out\.\d+$", p.name)
                and "old" not in p.name
            )

    build_paths = list(dict.fromkeys(p for p in build_paths if p.exists()))

    if not source_paths:
        print(f"Error: No source paths found in {ec_root}", file=sys.stderr)
        sys.exit(1)

    results = {}
    summary = {}
    copybot_patterns = load_copybot_patterns(ec_root, args.copybot_config)

    need_elfs = args.command in ["sources", "functions", "headers", "all"]
    need_functions = args.command in ["functions", "all"]
    all_elfs = []
    used_sources = set()
    used_headers_dwarf = set()
    used_functions = set()

    if need_elfs:
        raw_elfs = find_files(paths=build_paths, patterns=["*.elf", "*.exe"])
        raw_elf_set = set(raw_elfs)
        all_elfs = [
            e
            for e in sorted(raw_elfs)
            if e.name != "zephyr_pre0.elf"
            and not (
                e.name == "zephyr.exe"
                and (e.parent / "zephyr.elf") in raw_elf_set
            )
        ]
        if not args.include_tests:
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
                    tp
                    in str(e)
                    .rsplit("/src/platform/ec/", maxsplit=1)[-1]
                    .lower()
                    for tp in test_patterns
                )
            ]
        if not args.json:
            print(f"Finding elf files in {len(build_paths)} paths...")
            print(f"* Found {len(all_elfs)} elf files")
            print("Extracting info from elf files (sources, headers, DWARF)...")
        used_sources, used_headers_dwarf, used_functions = scan_elfs(
            all_elfs, ec_root, zephyr_root, need_functions=need_functions
        )

    # 1. SOURCES
    if args.command in ["sources", "all"]:
        all_sources = find_files(
            paths=source_paths, patterns=["*.c", "*.cc", "*.S"]
        )
        all_sources = {
            normalize_path(ec_root, zephyr_root, s) for s in all_sources
        }
        if args.ignore_copybot:
            all_sources = {
                s
                for s in all_sources
                if not is_copybot_synced(s, copybot_patterns)
            }
        unused_sources = [
            str(s) for s in sorted(list(all_sources)) if s not in used_sources
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
        if args.ignore_copybot:
            all_headers = {
                h
                for h in all_headers
                if not is_copybot_synced(h, copybot_patterns)
            }
        inc_search_paths = list(source_paths) + [ec_root / "util"]
        if zephyr_root:
            inc_search_paths.extend(
                [
                    zephyr_root / "include",
                    zephyr_root / "subsys" / "testsuite",
                ]
            )
        full_inc, base_inc = find_includes(inc_search_paths)
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
        if args.ignore_copybot:
            all_functions = {
                (p, name)
                for p, name in all_functions
                if not is_copybot_synced(p, copybot_patterns)
            }
        unused_functions = []
        for p, name in sorted(list(all_functions - used_functions)):
            entry = {"path": str(p), "name": name}
            if is_copybot_synced(p, copybot_patterns):
                entry["copybot_synced"] = True
            unused_functions.append(entry)
        results["unused_functions"] = unused_functions
        summary["total_functions"] = len(all_functions)
        summary["unused_functions"] = len(unused_functions)

    # 4. CONFIGS
    if args.command in ["configs", "all"]:
        all_configs = find_configs(source_paths + [ec_root / "util"])
        set_configs = find_set_configs(source_paths, build_paths)
        never_set = sorted(list(all_configs - set_configs))
        results["never_set_configs"] = never_set
        summary["total_configs"] = len(all_configs)
        summary["never_set_configs"] = len(never_set)

    copybot_synced_set = set()
    if not args.ignore_copybot:
        copybot_synced_set = {
            p
            for p in (
                results.get("unused_sources", [])
                + results.get("unused_headers", [])
                + [f["path"] for f in results.get("unused_functions", [])]
            )
            if is_copybot_synced(p, copybot_patterns)
        }
        if copybot_synced_set:
            copybot_synced_files = sorted(copybot_synced_set)
            results["copybot_synced_files"] = copybot_synced_files
            summary["copybot_synced_files"] = len(copybot_synced_files)

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
                tag = " [copybot: ec-legacy]" if s in copybot_synced_set else ""
                out_lines.append(f"{s}{tag}")
            out_lines.append("")

        if "unused_headers" in results:
            out_lines.append(f"UNUSED HEADERS({summary['unused_headers']}):\n")
            for h in results["unused_headers"]:
                tag = " [copybot: ec-legacy]" if h in copybot_synced_set else ""
                out_lines.append(f"{h}{tag}")
            out_lines.append("")

        if "unused_functions" in results:
            out_lines.append(
                f"UNUSED FUNCTIONS({summary['unused_functions']}):\n"
            )
            for f in results["unused_functions"]:
                tag = " [copybot: ec-legacy]" if f.get("copybot_synced") else ""
                out_lines.append(f"{f['path']}: {f['name']}{tag}")
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
