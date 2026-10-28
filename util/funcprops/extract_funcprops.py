#!/usr/bin/env python3
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Extract @funcprops documentation annotations from Zephyr and ChromeOS EC source trees
into JSON DB.

Supports:
  1. Fast Regex Scraper: Scans .h and .c files in platform/ec and zephyrproject/zephyr.
  2. Baseline Qualifier Definitions: Guarantees core Zephyr synchronization and
     blocking primitives have accurate function properties even when upstream headers
     lack complete doc qualifiers.
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

# Baseline qualifier definitions for core Zephyr kernel and driver primitives
# (from third_party/zephyrproject/zephyr). EC functions should be documented
# directly in platform/ec header files using Doxygen @<qualifier> tags.
# Note: Canonical qualifier sets omit 'reschedule' when 'sleep' is present
# because '@sleep' implies '@reschedule'.
BASELINE_FUNCPROPS: Dict[str, List[str]] = {
    # Unconditional sleep / thread-only blocking (["sleep"])
    "k_sleep": ["sleep"],
    "k_sleep_ticks": ["sleep"],
    "k_usleep": ["sleep"],
    "k_msleep": ["sleep"],
    "k_yield": ["sleep"],
    "k_work_cancel_sync": ["sleep"],
    "k_work_cancel_delayable_sync": ["sleep"],
    "k_work_flush": ["sleep"],
    "k_work_flush_delayable": ["sleep"],
    "k_thread_join": ["sleep"],
    "k_timer_status_sync": ["sleep"],
    "k_mutex_unlock": ["sleep"],
    "k_condvar_wait": ["sleep"],
    "i2c_transfer": ["sleep"],
    "spi_transceive": ["sleep"],
    "spi_transceive_dt": ["sleep"],
    "spi_transceive_cb": ["sleep"],
    "spi_read": ["sleep"],
    "spi_write": ["sleep"],
    "spi_release": ["sleep"],
    # Timeout-conditional sleep with non-blocking K_NO_WAIT path (["no_wait", "sleep"]).
    # Note: 'isr_ok' is intentionally omitted here so any direct call from an
    # ISR is flagged unless wrapped by an explicitly certified @isr_ok helper.
    "k_mutex_lock": ["no_wait", "sleep"],
    "k_poll": ["no_wait", "sleep"],
    "k_sem_take": ["no_wait", "sleep"],
    "k_event_wait": ["no_wait", "sleep"],
    "k_event_wait_all": ["no_wait", "sleep"],
    "k_event_wait_safe": ["no_wait", "sleep"],
    "k_msgq_get": ["no_wait", "sleep"],
    "k_msgq_put": ["no_wait", "sleep"],
    "k_msgq_put_front": ["no_wait", "sleep"],
    "k_queue_get": ["no_wait", "sleep"],
    "k_fifo_get": ["no_wait", "sleep"],
    "k_lifo_get": ["no_wait", "sleep"],
    "k_stack_pop": ["no_wait", "sleep"],
    "k_heap_alloc": ["no_wait", "sleep"],
    "k_mem_slab_alloc": ["no_wait", "sleep"],
    # ISR-safe primitives that may wake a higher-priority thread (["isr_ok", "reschedule"])
    "k_sem_give": ["isr_ok", "reschedule"],
    "k_event_post": ["isr_ok", "reschedule"],
    "k_event_set": ["isr_ok", "reschedule"],
    "k_event_set_masked": ["isr_ok", "reschedule"],
    "k_work_submit": ["isr_ok", "reschedule"],
    "k_work_submit_to_queue": ["isr_ok", "reschedule"],
    # ISR-safe non-rescheduling primitives (["isr_ok"])
    "k_event_clear": ["isr_ok"],
    "k_work_schedule": ["isr_ok"],
    "k_work_reschedule": ["isr_ok"],
    "k_work_schedule_for_queue": ["isr_ok"],
    "k_work_reschedule_for_queue": ["isr_ok"],
    "k_work_cancel": ["isr_ok"],
    "k_work_cancel_delayable": ["isr_ok"],
    # ISR-safe and pre-kernel-safe primitives (["isr_ok", "pre_kernel_ok"])
    "k_busy_wait": ["isr_ok", "pre_kernel_ok"],
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
    "__overridable",
    "__override",
    "__override_proto",
    "__test_only",
    "FUNC_NORETURN",
    "Z_DECL_ALIGN",
}

_PAREN_ATTR_MACROS = (
    "__printf_like",
    "__scanf_like",
    "__aligned",
    "Z_DECL_ALIGN",
)

INIT_MACRO_PATTERN = re.compile(
    r"(?:SYS_INIT|DEVICE_DT_DEFINE|DEVICE_DT_INST_DEFINE|DECLARE_HOOK)\s*\(\s*([A-Za-z0-9_]+)",
    re.MULTILINE,
)


def extract_qualifiers_from_text(text: str) -> List[str]:
    """Extract canonical @<qualifier> tags from a Doxygen comment block.

    Returns sorted canonical qualifiers. Since '@sleep' implies '@reschedule',
    'reschedule' is omitted when 'sleep' is present.
    """
    found: Set[str] = set()

    for m in re.finditer(r"@([A-Za-z0-9_]+)\b", text):
        tag = m.group(1).lower()
        if tag in CANONICAL_QUALIFIERS:
            found.add(tag)

    if "sleep" in found:
        found.discard("reschedule")

    return sorted(list(found))


def _strip_balanced_parens(text: str, macro_name: str) -> str:
    """Strips occurrences of `macro_name(...)` with balanced parentheses."""
    idx = text.find(macro_name)
    while idx != -1:
        p_start = text.find("(", idx + len(macro_name))
        if p_start == -1 or text[idx + len(macro_name) : p_start].strip() != "":
            idx = text.find(macro_name, idx + len(macro_name))
            continue
        depth = 1
        p_end = p_start + 1
        while p_end < len(text) and depth > 0:
            if text[p_end] == "(":
                depth += 1
            elif text[p_end] == ")":
                depth -= 1
            p_end += 1
        text = text[:idx] + text[p_end:]
        idx = text.find(macro_name)
    return text


# pylint: disable=too-many-return-statements
def parse_immediate_declaration(text_after: str) -> Optional[str]:
    """Parses the function or macro declaration immediately following a Doxygen comment.

    Guarantees:
      - Handles pointer return types (void *, char *, struct foo *).
      - Handles __syscall, static inline, ALWAYS_INLINE, __must_check, etc.
      - Handles __attribute__((...)) and __printf_like(...) without capturing the attribute.
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

    # Strip __attribute__((...)) and parenthesized attribute macros
    block_no_attr = _strip_balanced_parens(block, "__attribute__")
    for macro_name in _PAREN_ATTR_MACROS:
        block_no_attr = _strip_balanced_parens(block_no_attr, macro_name)

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


_EXCLUDED_DIR_PREFIXES = (
    "twister-out",
    "build",
    ".git",
    "out",
    "test",
    "tests",
    "samples",
    "boards",
    "doc",
)


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
                    d == ex or d.startswith(f"{ex}-") or d.startswith(f"{ex}_")
                    for ex in _EXCLUDED_DIR_PREFIXES
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
                        if func_name in funcprops_db:
                            merged_set = set(
                                funcprops_db[func_name]["props"]
                            ) | set(props)
                            if "sleep" in merged_set:
                                merged_set.discard("reschedule")
                            funcprops_db[func_name]["props"] = sorted(
                                list(merged_set)
                            )
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

    # Authoritative baseline overrides for core Zephyr kernel/driver primitives
    # whose upstream headers may have incomplete qualifier sets.
    for fn, baseline_props in BASELINE_FUNCPROPS.items():
        existing_file = (
            funcprops_db[fn]["file"]
            if fn in funcprops_db
            else "zephyr/kernel.h"
        )
        funcprops_db[fn] = {
            "props": sorted(list(baseline_props)),
            "file": existing_file,
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
