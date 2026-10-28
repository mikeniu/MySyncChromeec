#!/usr/bin/env python3
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Extract @funcprops documentation annotations from Zephyr and ChromeOS EC source trees
into JSON DB.

Supports:
  1. Fast Regex Scraper: Scans .h and .c files in platform/ec and zephyrproject/zephyr.
  2. Baseline Qualifier Definitions: Guarantees core Zephyr and EC synchronization and
     blocking primitives have accurate function properties even when headers lack doc qualifiers.
"""

import argparse
import json
import os
import re
from typing import Dict, List, Optional, Set


# Single Source of Truth for Supported Qualifiers
RAW_QUALIFIERS = [
    "reschedule",
    "sleep",
    "no_wait",
    "isr_ok",
    "pre_kernel_ok",
    "async",
    "supervisor",
]

# Canonical supported qualifier set
CANONICAL_QUALIFIERS = set(RAW_QUALIFIERS)
NORMALIZED_QUALIFIERS = CANONICAL_QUALIFIERS

# Baseline qualifier definitions for core Zephyr kernel primitives
# (from third_party/zephyrproject/zephyr). EC functions should be documented
# directly in platform/ec header files using Doxygen @qualifier tags.
BASELINE_FUNCPROPS: Dict[str, List[str]] = {
    # Unconditional sleep (["reschedule", "sleep"])
    "k_sleep": ["reschedule", "sleep"],
    "k_usleep": ["reschedule", "sleep"],
    "k_msleep": ["reschedule", "sleep"],
    "k_work_cancel_sync": ["reschedule", "sleep"],
    "k_work_cancel_delayable_sync": ["reschedule", "sleep"],
    "k_work_flush": ["reschedule", "sleep"],
    "k_work_flush_delayable": ["reschedule", "sleep"],
    "k_thread_join": ["reschedule", "sleep"],
    # Pure reschedule (["reschedule"])
    "k_yield": ["reschedule"],
    "k_mutex_unlock": ["reschedule"],
    # Timeout-conditional sleep, NOT ISR-safe (["no_wait", "reschedule", "sleep"])
    "k_mutex_lock": ["no_wait", "reschedule", "sleep"],
    "k_condvar_wait": ["no_wait", "reschedule", "sleep"],
    "k_event_wait": ["no_wait", "reschedule", "sleep"],
    "k_event_wait_all": ["no_wait", "reschedule", "sleep"],
    "k_poll": ["no_wait", "reschedule", "sleep"],
    # Timeout-conditional sleep, ISR-safe with K_NO_WAIT
    # (["isr_ok", "no_wait", "reschedule", "sleep"])
    "k_sem_take": ["isr_ok", "no_wait", "reschedule", "sleep"],
    "k_msgq_get": ["isr_ok", "no_wait", "reschedule", "sleep"],
    "k_msgq_put": ["isr_ok", "no_wait", "reschedule", "sleep"],
    "k_queue_get": ["isr_ok", "no_wait", "reschedule", "sleep"],
    "k_fifo_get": ["isr_ok", "no_wait", "reschedule", "sleep"],
    "k_lifo_get": ["isr_ok", "no_wait", "reschedule", "sleep"],
    "k_heap_alloc": ["isr_ok", "no_wait", "reschedule", "sleep"],
    "k_mem_slab_alloc": ["isr_ok", "no_wait", "reschedule", "sleep"],
    # ISR-safe non-sleeping (["isr_ok"])
    "k_sem_give": ["isr_ok"],
    "k_event_post": ["isr_ok"],
    "k_event_set": ["isr_ok"],
    "k_busy_wait": ["isr_ok", "pre_kernel_ok"],
    "k_work_submit": ["isr_ok"],
    "k_work_submit_to_queue": ["isr_ok"],
    "k_work_schedule": ["isr_ok"],
    "k_work_reschedule": ["isr_ok"],
}

_C_CONTROL_KEYWORDS = {
    "if",
    "while",
    "for",
    "switch",
    "return",
    "sizeof",
    "typeof",
    "__typeof__",
    "case",
    "goto",
    "break",
    "continue",
}

_C_TYPE_SPECIFIERS = {
    "void",
    "char",
    "short",
    "int",
    "long",
    "float",
    "double",
    "signed",
    "unsigned",
    "bool",
    "_Bool",
    "size_t",
    "ssize_t",
    "off_t",
    "int8_t",
    "int16_t",
    "int32_t",
    "int64_t",
    "uint8_t",
    "uint16_t",
    "uint32_t",
    "uint64_t",
    "uintptr_t",
    "intptr_t",
    "struct",
    "union",
    "enum",
    "static",
    "extern",
    "inline",
    "const",
    "volatile",
    "ALWAYS_INLINE",
    "__syscall",
    "__must_check",
    "__deprecated",
    "FUNC_NORETURN",
    "Z_DECL_ALIGN",
}

INIT_MACRO_PATTERN = re.compile(
    r"(?:SYS_INIT|DEVICE_DT_DEFINE|DEVICE_DT_INST_DEFINE|DECLARE_HOOK)\s*\(\s*([A-Za-z0-9_]+)",
    re.MULTILINE,
)


def extract_qualifiers_from_text(text: str) -> List[str]:
    """DRY Helper: Extract and normalize canonical @<qualifier> tags from Doxygen comments.

    Handles canonical @qualifier (e.g. @isr_ok, @sleep, @pre_kernel_ok).
    Applies Zephyr's implication rule: 'sleep' implies 'reschedule'.
    """
    found: Set[str] = set()

    for m in re.finditer(r"@([A-Za-z0-9_]+)", text):
        tag = m.group(1).lower()
        if tag in CANONICAL_QUALIFIERS:
            found.add(tag)

    if "sleep" in found and "reschedule" not in found:
        found.add("reschedule")

    return sorted(list(found))


def validate_doxygen_qualifiers(text: str, filepath: str = "") -> List[str]:
    """Validates that Doxygen comments use canonical @<qualifier> tags without syntax errors.

    Checks for:
      a. Deprecated @funcprops or \\funcprops tags.
      b. Backslash-prefixed qualifier tags (\\sleep, \\isr_ok, etc.).
      c. Hyphenated qualifier typos (@isr-ok, @no-wait, etc.).
      d. Orphaned qualifier tags on blocks not immediately followed by a C declaration.
    """
    errors: List[str] = []
    prefix = f"{filepath}:" if filepath else ""
    doxygen_re = re.compile(r"/\*\*([\s\S]*?)\*/")

    for m in doxygen_re.finditer(text):
        block = m.group(1)
        start_pos = m.start()
        end_pos = m.end()
        lineno = text[:start_pos].count("\n") + 1

        # a. Deprecated @funcprops or \funcprops
        if re.search(r"[@\\]funcprops\b", block):
            errors.append(
                f"{prefix}{lineno}: error: deprecated '@funcprops' tag used. "
                "Use canonical '@<qualifier>' tags directly."
            )

        # b. Backslash-prefixed qualifier tags
        bs_match = re.search(
            r"\\(sleep|reschedule|no_wait|isr_ok|pre_kernel_ok|async|supervisor)\b",
            block,
        )
        if bs_match:
            errors.append(
                f"{prefix}{lineno}: error: backslash qualifier '\\{bs_match.group(1)}' used. "
                f"Use '@{bs_match.group(1)}' instead."
            )

        # c. Hyphenated qualifier typos
        hyphen_match = re.search(
            r"[@\\](isr-ok|no-wait|pre-kernel-ok)\b",
            block,
        )
        if hyphen_match:
            typo = hyphen_match.group(1)
            canonical = typo.replace("-", "_")
            errors.append(
                f"{prefix}{lineno}: error: hyphenated qualifier '{hyphen_match.group(0)}' used. "
                f"Use canonical '@{canonical}' instead."
            )

        # d. Orphaned qualifier tags
        qualifiers = extract_qualifiers_from_text(block)
        if qualifiers:
            text_after = text[end_pos : end_pos + 600]
            decl = parse_immediate_declaration(text_after)
            if decl is None:
                errors.append(
                    f"{prefix}{lineno}: error: orphaned qualifier tag(s) {qualifiers} "
                    "found on comment block not followed by a C function declaration."
                )

        # e. Check for missing blank comment line between @<qualifier> and other tags
        lines = block.splitlines()
        for idx, line in enumerate(lines):
            s_line = line.strip()
            if s_line.startswith("*"):
                s_line = s_line.lstrip("*").strip()

            qual_matches = re.findall(r"@([A-Za-z0-9_]+)\b", s_line)
            canonical_quals = [
                q for q in qual_matches if q.lower() in CANONICAL_QUALIFIERS
            ]
            if not canonical_quals:
                continue

            if idx + 1 < len(lines):
                next_line = lines[idx + 1].strip()
                if next_line.startswith("*"):
                    next_line_content = next_line.lstrip("*").strip()
                else:
                    next_line_content = next_line

                if not next_line_content or next_line_content.startswith("*/"):
                    continue

                next_quals = re.findall(
                    r"^@([A-Za-z0-9_]+)\b", next_line_content
                )
                if next_quals and next_quals[0].lower() in CANONICAL_QUALIFIERS:
                    continue

                if re.match(r"^@[A-Za-z0-9_]+(?::|\b)", next_line_content):
                    line_no = (
                        text[:start_pos].count("\n")
                        + block[: block.find(line)].count("\n")
                        + 1
                    )
                    next_tag = next_line_content.split()[0]
                    errors.append(
                        f"{prefix}{line_no}: error: missing blank comment line "
                        f"(' *') between '@{canonical_quals[-1]}' and following "
                        f"tag '{next_tag}'."
                    )

    return errors


# pylint: disable=too-many-return-statements
def parse_immediate_declaration(text_after: str) -> Optional[str]:
    """Parses the function or macro declaration immediately following a Doxygen comment.

    Guarantees:
      - Handles pointer return types (void *, char *, struct foo *).
      - Handles __syscall, static inline, ALWAYS_INLINE, __must_check, etc.
      - Handles __attribute__((...)) without capturing __attribute__ as function name.
      - Handles parameter lists with nested callback function pointers: void (*cb)(...).
      - Does NOT skip forward to subsequent functions: stops immediately on
        struct/union/enum/typedef, non-function-like #define, or if no declaration is found.
    """
    lines = text_after.splitlines()
    cleaned_lines = []

    for line in lines:
        sline = line.strip()
        if not sline:
            if cleaned_lines:
                # Stop if an empty line follows an incomplete or comment-terminated block
                cleaned_lines.append(line)
            continue

        # Skip preprocessor conditionals (#if, #ifdef, #ifndef, #else, #endif)
        if sline.startswith("#") and not sline.startswith("#define"):
            continue

        # If a new doc comment begins, stop immediately
        if sline.startswith("/*") or sline.startswith("/**"):
            break

        cleaned_lines.append(line)
        if ";" in line or "{" in line:
            break
        # Guard against scanning arbitrarily far down a file
        if len(cleaned_lines) > 25:
            break

    block = "\n".join(cleaned_lines).strip()
    if not block:
        return None

    # Handle function-like macro definition: #define foo(x, y) ...
    macro_match = re.match(r"^#define\s+([A-Za-z0-9_]+)\s*\(", block)
    if macro_match:
        fn = macro_match.group(1)
        return fn if fn not in _C_CONTROL_KEYWORDS else None

    # If it starts with non-function constructs like struct/union/enum/typedef without func decl
    if re.match(r"^(?:struct|union|enum|typedef)\b", block) and not re.search(
        r"\([^)]*\)\s*(?:;|\{)", block
    ):
        return None
    if block.startswith("#define"):
        return None

    # Strip __attribute__((...)) compiler attributes (single or multiline, supporting nested parens)
    idx = block.find("__attribute__")
    while idx != -1:
        p_start = block.find("((", idx)
        if p_start == -1:
            break
        depth = 2
        p_end = p_start + 2
        while p_end < len(block) and depth > 0:
            if block[p_end] == "(":
                depth += 1
            elif block[p_end] == ")":
                depth -= 1
            p_end += 1
        block = block[:idx] + block[p_end:]
        idx = block.find("__attribute__")
    block_no_attr = block

    # Cut declaration at the terminating ';' or '{'
    term_idx = -1
    semi = block_no_attr.find(";")
    brace = block_no_attr.find("{")
    if semi != -1 and brace != -1:
        term_idx = min(semi, brace)
    elif semi != -1:
        term_idx = semi
    elif brace != -1:
        term_idx = brace

    decl = block_no_attr[:term_idx] if term_idx != -1 else block_no_attr

    # The function name is the identifier immediately before the outermost '('
    # Matching: [return type/pointers/qualifiers] [fn_name] ( [params] )
    m = re.search(r"([A-Za-z0-9_]+)\s*\(", decl)
    if not m:
        return None

    func_name = m.group(1)
    if func_name in _C_CONTROL_KEYWORDS:
        return None

    prefix = decl[: m.start(1)].strip()
    if not prefix:
        return None

    # Reject assignments or statements containing '=', ';', '{', '}', '(', ')', or control operators
    if any(
        ch in prefix
        for ch in ("=", ";", "{", "}", "(", ")", "!", "?", ":", "+")
    ):
        return None

    tokens = [t.strip("*") for t in prefix.split() if t.strip("*")]
    if not tokens:
        return None

    # Reject if any token in prefix is a control keyword (e.g. return, case, etc.)
    if any(tok in _C_CONTROL_KEYWORDS for tok in tokens):
        return None

    # Must contain at least one known C type specifier/qualifier, a typedef
    # ending in '_t', or a pointer '*'
    has_type = (
        any(tok in _C_TYPE_SPECIFIERS or tok.endswith("_t") for tok in tokens)
        or "*" in prefix
    )
    if not has_type:
        return None

    return func_name


def get_default_paths() -> List[str]:
    """Auto-detect workspace root directories relative to this script."""
    script_dir = os.path.dirname(os.path.abspath(__file__))
    ec_root = os.path.abspath(os.path.join(script_dir, "../.."))
    workspace_root = os.path.abspath(os.path.join(ec_root, "../../.."))
    zephyr_root = os.path.join(
        workspace_root, "src/third_party/zephyrproject/zephyr"
    )
    return [ec_root, zephyr_root]


def scan_headers_and_sources(
    search_paths: List[str], output_json: str
) -> Dict[str, dict]:
    """Fast regex scraper parsing Doxygen blocks directly preceding declarations/definitions."""
    funcprops_db: Dict[str, dict] = {}
    file_map: Dict[str, List[str]] = {}
    files_to_scan: List[str] = []

    for root_dir in search_paths:
        if not os.path.exists(root_dir):
            print(f"[WARNING] Search path does not exist: {root_dir}")
            continue
        for root, dirs, files in os.walk(root_dir):
            dirs[:] = [
                d
                for d in dirs
                if not any(
                    d.startswith(ex)
                    for ex in ["twister-out", "build", ".git", "out"]
                )
            ]
            for file in files:
                if file.endswith(".h") or file.endswith(".c"):
                    files_to_scan.append(os.path.join(root, file))

    doxygen_block_re = re.compile(r"/\*\*([\s\S]*?)\*/")

    for file_path in files_to_scan:
        try:
            with open(file_path, "r", encoding="utf-8", errors="ignore") as f:
                content = f.read()

            for match in doxygen_block_re.finditer(content):
                comment = match.group(1)
                props = extract_qualifiers_from_text(comment)
                text_after = content[match.end() : match.end() + 600]
                func_name = parse_immediate_declaration(text_after)

                if func_name:
                    if props:
                        # If already recorded, merge qualifiers
                        if func_name in funcprops_db:
                            merged = sorted(
                                list(
                                    set(funcprops_db[func_name]["props"])
                                    | set(props)
                                )
                            )
                            if "sleep" in merged and "reschedule" not in merged:
                                merged.append("reschedule")
                                merged.sort()
                            funcprops_db[func_name]["props"] = merged
                        else:
                            funcprops_db[func_name] = {
                                "props": props,
                                "file": file_path,
                            }
                    if file_path not in file_map.setdefault(func_name, []):
                        file_map[func_name].append(file_path)

            macro_matches = INIT_MACRO_PATTERN.findall(content)
            for func_name in macro_matches:
                if file_path not in file_map.setdefault(func_name, []):
                    file_map[func_name].append(file_path)
        # pylint: disable=broad-exception-caught
        except Exception:
            pass

    # Overlay baseline function properties
    for fn, baseline_props in BASELINE_FUNCPROPS.items():
        if fn in funcprops_db:
            merged = sorted(
                list(set(funcprops_db[fn]["props"]) | set(baseline_props))
            )
            if "sleep" in merged and "reschedule" not in merged:
                merged.append("reschedule")
                merged.sort()
            funcprops_db[fn]["props"] = merged
        else:
            funcprops_db[fn] = {
                "props": sorted(list(baseline_props)),
                "file": "zephyr/kernel.h",
            }

    funcprops_db["_file_map"] = file_map

    output_dir = os.path.dirname(os.path.abspath(output_json))
    if output_dir:
        os.makedirs(output_dir, exist_ok=True)

    with open(output_json, "w", encoding="utf-8") as f:
        json.dump(funcprops_db, f, indent=2)

    annotated_count = len([k for k in funcprops_db if not k.startswith("_")])
    print(
        f"Extracted {annotated_count} annotated functions across "
        f"{len(files_to_scan)} scanned files."
    )
    print(f"Database saved to: {output_json}")
    return funcprops_db


def main():
    """Main CLI entry point for extract_funcprops."""
    default_search_paths = get_default_paths()
    script_dir = os.path.dirname(os.path.abspath(__file__))
    default_output = os.path.join(script_dir, "funcprops.json")

    parser = argparse.ArgumentParser(
        description="Extract @funcprops annotations into JSON database."
    )
    parser.add_argument(
        "--search-paths",
        nargs="+",
        default=default_search_paths,
        help=f"Source/header directories to scan. Default: {default_search_paths}",
    )
    parser.add_argument(
        "--output",
        default=default_output,
        help=f"Output JSON database file path. Default: {default_output}",
    )
    args = parser.parse_args()

    scan_headers_and_sources(args.search_paths, args.output)


if __name__ == "__main__":
    main()
