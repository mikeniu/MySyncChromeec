#!/usr/bin/env python3
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Fixes header guard macro names in C/C++ header files to match cpplint style."""

import argparse
import os
from pathlib import Path
import re
import subprocess
import sys
from typing import Optional


DEFAULT_EXCLUDE_DIRS = {"build", "third_party", ".git"}


def find_git_root(path: Path) -> Path:
    """Finds the git root directory containing |path|.

    Args:
        path: A file or directory path within the repository.

    Returns:
        The absolute Path to the root directory of the git checkout.
        Falls back to path's parent or directory if not inside a git checkout.
    """
    try:
        res = subprocess.run(
            ["git", "rev-parse", "--show-toplevel"],
            cwd=path if path.is_dir() else path.parent,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            check=True,
            text=True,
        )
        return Path(res.stdout.strip()).resolve()
    except (subprocess.CalledProcessError, FileNotFoundError):
        return path.resolve() if path.is_dir() else path.parent.resolve()


def get_guard_variable(
    filepath: Path, root: Path, prefix: Optional[str] = None
) -> str:
    """Calculates the expected header guard macro variable name for |filepath|.

    The macro name is derived from the file path relative to |root|,
    converting all non-alphanumeric characters to underscores, uppercase,
    with a trailing underscore. If |prefix| is specified, it is prepended.

    Args:
        filepath: The path to the header file.
        root: The base directory from which the relative path is calculated.
        prefix: Optional prefix string to prepend (e.g. 'EC' or 'CROS_EC').

    Returns:
        The formatted macro variable string (e.g., 'EC_UTIL_COMM_HOST_H_').
    """
    try:
        rel_path = filepath.resolve().relative_to(root.resolve())
    except ValueError:
        rel_path = filepath.name
    guard = re.sub(r"[^a-zA-Z0-9]", "_", str(rel_path)).upper() + "_"
    if prefix:
        clean_prefix = re.sub(r"[^a-zA-Z0-9]", "_", prefix).upper().rstrip("_")
        if clean_prefix:
            guard = f"{clean_prefix}_{guard}"
    return guard


def strip_comments(line: str, in_block_comment: bool) -> tuple[str, bool]:
    """Strips comments and string literals from |line|.

    Args:
        line: The input source line.
        in_block_comment: Whether we are currently inside a multi-line /* ... */ block.

    Returns:
        A tuple (code, new_in_block_comment) where code is the non-comment text
        stripped of whitespace, and new_in_block_comment is the updated comment state.
    """
    i = 0
    n = len(line)
    code = []

    while i < n:
        if in_block_comment:
            end = line.find("*/", i)
            if end != -1:
                in_block_comment = False
                i = end + 2
            else:
                break
        else:
            if line[i : i + 2] == "//":
                break
            if line[i : i + 2] == "/*":
                in_block_comment = True
                i += 2
            elif line[i] in "\"'":
                quote = line[i]
                code.append(quote)
                i += 1
                while i < n and line[i] != quote:
                    if line[i] == "\\":
                        code.append(line[i : i + 2])
                        i += 2
                    else:
                        code.append(line[i])
                        i += 1
                if i < n:
                    code.append(line[i])
                    i += 1
            else:
                code.append(line[i])
                i += 1

    return "".join(code).strip(), in_block_comment


def has_non_comment_lines(lines: list[str], start: int, end: int) -> bool:
    """Returns True if there are any non-comment, non-blank lines in lines[start:end].

    Args:
        lines: List of source lines.
        start: Starting index to check (inclusive).
        end: Ending index to check (exclusive).

    Returns:
        True if at least one line in the range contains code / preprocessor text.
    """
    in_block = False
    for idx in range(start, end):
        if idx >= len(lines):
            break
        line = lines[idx]
        if idx == 0 and line.startswith("#!"):
            continue
        code, in_block = strip_comments(line, in_block)
        if code:
            return True
    return False


def is_precondition_check(lines: list[str], start: int, end: int) -> bool:
    """Returns True if lines[start:end] contains only preprocessor checks with #error.

    Allows inclusion / precondition checks (e.g. #error blocks) before #ifndef.
    """
    has_error = False
    in_block = False
    in_continuation = False
    for idx in range(start, end):
        if idx >= len(lines):
            break
        line = lines[idx]
        code, in_block = strip_comments(line, in_block)
        if not code:
            continue
        if in_continuation:
            in_continuation = code.endswith("\\")
            continue
        if not code.startswith("#"):
            return False
        if re.match(r"^#\s*(include|define)\b", code):
            return False
        if re.match(r"^#\s*error\b", code):
            has_error = True
        in_continuation = code.endswith("\\")
    return has_error


def is_plausible_header_guard(
    macro: Optional[str], expected_guard: str
) -> bool:
    """Checks if |macro| looks like a header guard macro rather than an internal config."""
    if not macro:
        return False
    m = macro.upper()
    if m.endswith(("_H", "_H_", "_H__", "_HH", "_HH_", "_HPP", "_HPP_")):
        return True
    clean_exp = expected_guard.strip("_").upper()
    clean_m = m.strip("_")
    return clean_exp in clean_m or clean_m in clean_exp


def find_guard_insertion_index(lines: list[str]) -> int:
    """Finds the line index where #ifndef / #define should be inserted.

    Skips shebangs, top-level license/copyright comments, and file descriptions.

    Args:
        lines: List of lines in the file.

    Returns:
        The line index where the header guard should be inserted.
    """
    idx = 0
    n = len(lines)

    # Skip shebang if present
    if idx < n and lines[idx].startswith("#!"):
        idx += 1

    in_block_comment = False
    last_comment_or_blank_idx = idx

    while idx < n:
        code, in_block_comment = strip_comments(lines[idx], in_block_comment)
        if code:
            break
        idx += 1
        last_comment_or_blank_idx = idx

    return last_comment_or_blank_idx


def build_endif_lines(
    expected_guard: str,
    use_c_comments: bool,
    newline: str,
    endif_cmd: str = "#endif",
    column_limit: int = 78,
) -> list[str]:
    """Builds the #endif line(s) for |expected_guard|.

    Args:
        expected_guard: The macro name for the header guard.
        use_c_comments: If True, uses /* ... */ comments. Otherwise uses // comments.
        newline: The newline character sequence ('\\n' or '\\r\\n').
        endif_cmd: The #endif string, preserving any indentation.
        column_limit: Column limit for line wrapping (default: 78).

    Returns:
        List of formatted lines representing the #endif block.
    """
    if use_c_comments:
        single_line = f"{endif_cmd} /* {expected_guard} */"
        if len(single_line) > column_limit:
            return [
                f"{endif_cmd} /* {expected_guard} \\{newline}",
                f"\t*/{newline}",
            ]
        return [f"{single_line}{newline}"]
    return [f"{endif_cmd}  // {expected_guard}{newline}"]


def find_header_guard_bounds(lines: list[str], expected_guard: str) -> tuple[
    Optional[int],
    Optional[int],
    Optional[str],
    Optional[int],
    Optional[int],
]:
    """Identifies the #ifndef, #define, and #endif lines belonging to the header guard.

    Uses preprocessor directive nesting to ensure that internal #if/#endif blocks
    (such as #ifdef __cplusplus) are never mistaken for header guards.

    Args:
        lines: List of source code lines.
        expected_guard: The expected header guard macro name.

    Returns:
        (ifndef_idx, define_idx, old_guard, endif_idx, endif_end_idx)
        Where any value may be None if not found or incomplete.
    """
    stack = []
    top_guard_macro = None
    ifndef_idx = None
    define_idx = None
    guard_pair_endif_idx = None
    guard_pair_endif_end_idx = None

    # Step 1: Scan for candidate header guard #ifndef and matching #define
    for idx, line in enumerate(lines):
        if ifndef_idx is None:
            m = re.match(r"^\s*#\s*ifndef\s+([A-Za-z0-9_]+)", line)
            if not m:
                m = re.match(r"^\s*#\s*if\s+!defined\(([A-Za-z0-9_]+)\)", line)
            if m:
                macro = m.group(1)
                if is_plausible_header_guard(macro, expected_guard):
                    for j in range(idx + 1, min(idx + 5, len(lines))):
                        m_def = re.match(
                            r"^\s*#\s*define\s+([A-Za-z0-9_]+)", lines[j]
                        )
                        if m_def and (
                            m_def.group(1) == macro
                            or m_def.group(1) == expected_guard
                        ):
                            define_idx = j
                            ifndef_idx = idx
                            top_guard_macro = macro
                            break
                if ifndef_idx is not None:
                    break

    # Step 2: Trace preprocessor nesting to find the matching #endif
    for idx, line in enumerate(lines):
        m_if = re.match(r"^\s*#\s*(ifndef|ifdef|if)\b\s*([A-Za-z0-9_]*)", line)
        if m_if:
            kind = m_if.group(1)
            macro = m_if.group(2) or None
            stack.append((kind, macro, idx))
            continue

        m_endif = re.match(r"^\s*#\s*endif\b", line)
        if m_endif:
            end_idx = idx
            if "/*" in line and "*/" not in line:
                for j in range(idx + 1, len(lines)):
                    end_idx = j
                    if "*/" in lines[j]:
                        break
            elif line.rstrip("\r\n").endswith("\\"):
                for j in range(idx + 1, len(lines)):
                    end_idx = j
                    if "*/" in lines[j] or not lines[j].rstrip("\r\n").endswith(
                        "\\"
                    ):
                        break
            while end_idx + 1 < len(lines) and re.match(
                r"^\s*\*/\s*$", lines[end_idx + 1]
            ):
                end_idx += 1

            if stack:
                kind, macro, o_idx = stack.pop()
                if ifndef_idx is not None and o_idx == ifndef_idx:
                    guard_pair_endif_idx = idx
                    guard_pair_endif_end_idx = end_idx

    # Fallback: if guard_pair_endif_idx is None, but top_guard_macro was found,
    # check if the last #endif explicitly has a comment matching the guard macro.
    if guard_pair_endif_idx is None and top_guard_macro is not None:
        clean_macro = top_guard_macro.strip("_")
        clean_exp = expected_guard.strip("_")
        for idx in range(len(lines) - 1, -1, -1):
            line = lines[idx]
            if re.match(r"^\s*#\s*endif\b", line):
                if clean_macro in line or clean_exp in line:
                    end_idx = idx
                    if "/*" in line and "*/" not in line:
                        for j in range(idx + 1, len(lines)):
                            end_idx = j
                            if "*/" in lines[j]:
                                break
                    elif line.rstrip("\r\n").endswith("\\"):
                        for j in range(idx + 1, len(lines)):
                            end_idx = j
                            if "*/" in lines[j] or not lines[j].rstrip(
                                "\r\n"
                            ).endswith("\\"):
                                break
                    while end_idx + 1 < len(lines) and re.match(
                        r"^\s*\*/\s*$", lines[end_idx + 1]
                    ):
                        end_idx += 1
                    guard_pair_endif_idx = idx
                    guard_pair_endif_end_idx = end_idx
                    break

    return (
        ifndef_idx,
        define_idx,
        top_guard_macro,
        guard_pair_endif_idx,
        guard_pair_endif_end_idx,
    )


def fix_header_guard(
    filepath: Path,
    root: Path,
    prefix: Optional[str] = None,
    use_c_comments: bool = False,
    dry_run: bool = False,
) -> bool:
    """Fixes header guard macro directives in |filepath|.

    Updates existing #ifndef, #define, and trailing #endif comment lines, or
    inserts new ones if they are missing or misplaced, ensuring that no
    non-comment lines exist before #ifndef or after #endif. Internal #if/#endif
    blocks (like #ifdef __cplusplus) are never altered or removed.

    Args:
        filepath: Path to the header file to examine and update.
        root: The base repository or project root for guard path derivation.
        prefix: Optional prefix string to prepend to the guard name.
        use_c_comments: If True, uses C-style /* ... */ comments on #endif.
            Otherwise uses C++ style // comments.
        dry_run: If True, reports planned modifications without writing to disk.

    Returns:
        True if the file was modified (or would be modified in dry-run mode);
        False otherwise.
    """
    try:
        content = filepath.read_text(encoding="utf-8")
    except UnicodeDecodeError:
        print(f"Skipping (not utf-8): {filepath}", file=sys.stderr)
        return False

    if "TODO(b/510249930): Switch to" in content:
        return False

    lines = content.splitlines(keepends=True)
    newline = "\r\n" if lines and lines[0].endswith("\r\n") else "\n"
    expected_guard = get_guard_variable(filepath, root, prefix=prefix)

    # Remove any #pragma once
    has_pragma = any(re.match(r"^\s*#\s*pragma\s+once\b", l) for l in lines)
    if has_pragma:
        lines = [
            l for l in lines if not re.match(r"^\s*#\s*pragma\s+once\b", l)
        ]

    (
        ifndef_idx,
        define_idx,
        _old_guard,
        endif_idx,
        endif_end_idx,
    ) = find_header_guard_bounds(lines, expected_guard)

    has_existing_top = ifndef_idx is not None and define_idx is not None
    has_existing_bottom = endif_idx is not None

    # Check that no non-comment lines exist before #ifndef or after #endif
    top_clean = has_existing_top and (
        not has_non_comment_lines(lines, 0, ifndef_idx)
        or is_precondition_check(lines, 0, ifndef_idx)
    )
    bottom_clean = (
        has_existing_bottom
        and endif_end_idx is not None
        and not has_non_comment_lines(lines, endif_end_idx + 1, len(lines))
    )

    if top_clean and bottom_clean and endif_idx is not None:
        # Both guards are in valid positions enclosing all code: update in place.
        endif_match = re.match(r"^(\s*#\s*endif)", lines[endif_idx])
        endif_cmd = endif_match.group(1) if endif_match else "#endif"
        new_endif_lines = build_endif_lines(
            expected_guard, use_c_comments, newline, endif_cmd=endif_cmd
        )

        existing_endif_text = "".join(lines[endif_idx : endif_end_idx + 1])
        target_endif_text = "".join(new_endif_lines)
        endif_matches = (
            existing_endif_text == target_endif_text
            or existing_endif_text.replace("\t", "        ")
            == target_endif_text.replace("\t", "        ")
        )
        if _old_guard == expected_guard and endif_matches:
            if not has_pragma:
                return False

        new_ifndef = re.sub(
            r"^(#\s*ifndef\s+)[A-Za-z0-9_]+",
            rf"\g<1>{expected_guard}",
            lines[ifndef_idx].rstrip("\r\n"),
        )
        new_define = re.sub(
            r"^(#\s*define\s+)[A-Za-z0-9_]+",
            rf"\g<1>{expected_guard}",
            lines[define_idx].rstrip("\r\n"),
        )

        lines[ifndef_idx] = new_ifndef + newline
        lines[define_idx] = new_define + newline
        lines[endif_idx : endif_end_idx + 1] = new_endif_lines

    else:
        # Guards are missing or misplaced (non-comment lines exist outside them).
        # 1. Handle top guard (#ifndef and #define)
        if top_clean and ifndef_idx is not None and define_idx is not None:
            new_ifndef = re.sub(
                r"^(#\s*ifndef\s+)[A-Za-z0-9_]+",
                rf"\g<1>{expected_guard}",
                lines[ifndef_idx].rstrip("\r\n"),
            )
            new_define = re.sub(
                r"^(#\s*define\s+)[A-Za-z0-9_]+",
                rf"\g<1>{expected_guard}",
                lines[define_idx].rstrip("\r\n"),
            )
            lines[ifndef_idx] = new_ifndef + newline
            lines[define_idx] = new_define + newline
        else:
            if (
                has_existing_top
                and ifndef_idx is not None
                and define_idx is not None
            ):
                del lines[define_idx]
                del lines[ifndef_idx]

            insert_idx = find_guard_insertion_index(lines)
            guard_top_lines = []
            if insert_idx > 0 and lines[insert_idx - 1].strip() != "":
                guard_top_lines.append(newline)
            guard_top_lines.append(f"#ifndef {expected_guard}{newline}")
            guard_top_lines.append(f"#define {expected_guard}{newline}")
            if insert_idx < len(lines) and lines[insert_idx].strip() != "":
                guard_top_lines.append(newline)
            lines[insert_idx:insert_idx] = guard_top_lines

        # 2. Handle bottom guard (#endif)
        if bottom_clean and endif_idx is not None:
            # Re-find cur_endif_idx in case lines shifted
            (
                _,
                _,
                _,
                cur_endif_idx,
                cur_end,
            ) = find_header_guard_bounds(lines, expected_guard)
            if cur_endif_idx is not None and cur_end is not None:
                endif_match = re.match(r"^(\s*#\s*endif)", lines[cur_endif_idx])
                endif_cmd = endif_match.group(1) if endif_match else "#endif"
                new_endif_lines = build_endif_lines(
                    expected_guard, use_c_comments, newline, endif_cmd=endif_cmd
                )
                lines[cur_endif_idx : cur_end + 1] = new_endif_lines
        else:
            if has_existing_bottom and endif_idx is not None:
                (
                    _,
                    _,
                    _,
                    del_endif_idx,
                    del_end_idx,
                ) = find_header_guard_bounds(lines, expected_guard)
                if del_endif_idx is not None and del_end_idx is not None:
                    del lines[del_endif_idx : del_end_idx + 1]

            # Append #endif after all code at the end of the file
            while lines and not lines[-1].strip():
                lines.pop()
            if lines and lines[-1].strip() != "":
                lines.append(newline)
            new_endif_lines = build_endif_lines(
                expected_guard, use_c_comments, newline
            )
            lines.extend(new_endif_lines)

    new_content = "".join(lines)
    if newline == "\r\n":
        new_content = re.sub(r"(\r\n){3,}", "\r\n\r\n", new_content)
    else:
        new_content = re.sub(r"\n{3,}", "\n\n", new_content)

    if new_content == content:
        return False

    if dry_run:
        print(f"[dry-run] Would update: {filepath} -> {expected_guard}")
    else:
        filepath.write_text(new_content, encoding="utf-8")
        print(f"Updated: {filepath} -> {expected_guard}")

    return True


def collect_header_files(
    paths: list[Path],
    exclude_dirs: Optional[set[str]] = None,
) -> list[Path]:
    """Collects all .h files from paths, searching directories recursively.

    Recursively traverses directories to locate header files (.h, .hh, .hpp)
    while pruning directories matching |exclude_dirs|.

    Args:
        paths: List of file and/or directory paths to inspect.
        exclude_dirs: Optional set of directory names to skip (defaults to
            DEFAULT_EXCLUDE_DIRS).

    Returns:
        A sorted list of unique header file Path objects.
    """
    if exclude_dirs is None:
        exclude_dirs = DEFAULT_EXCLUDE_DIRS

    def is_excluded(p: Path) -> bool:
        """Returns True if any path component matches the excluded directory names."""
        try:
            resolved_parts = p.resolve().parts
        except (OSError, RuntimeError):
            resolved_parts = ()
        return any(part in exclude_dirs for part in p.parts) or any(
            part in exclude_dirs for part in resolved_parts
        )

    headers = []
    for path in paths:
        if path.is_symlink():
            continue
        if path.is_file():
            if path.suffix in (".h", ".hh", ".hpp") and not is_excluded(path):
                headers.append(path)
        elif path.is_dir():
            if is_excluded(path):
                continue
            for root, dirs, files in os.walk(path):
                # Prune excluded directories in-place so os.walk does not descend into them
                dirs[:] = [d for d in dirs if d not in exclude_dirs]
                for file in files:
                    file_path = Path(root) / file
                    if file_path.is_symlink():
                        continue
                    if file_path.suffix in (
                        ".h",
                        ".hh",
                        ".hpp",
                    ) and not is_excluded(file_path):
                        headers.append(file_path)
    return sorted(set(headers))


def parse_args(argv: Optional[list[str]] = None) -> argparse.Namespace:
    """Parses command-line arguments.

    Args:
        argv: Optional list of command-line argument strings. If None,
            sys.argv[1:] is parsed.

    Returns:
        An argparse.Namespace containing the parsed argument values.
    """
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument(
        "paths",
        nargs="*",
        type=Path,
        default=[Path(".")],
        help="Header files or directories to process (default: current directory).",
    )
    parser.add_argument(
        "--root",
        type=Path,
        default=None,
        help="Root directory for relative guard paths (default: git repository root).",
    )
    parser.add_argument(
        "--prefix",
        type=str,
        default=None,
        help="Optional prefix to prepend to the header guard (e.g. 'EC' or 'CROS_EC').",
    )
    parser.add_argument(
        "--exclude-dir",
        action="append",
        default=None,
        help="Additional directory names to ignore (default: build, third_party, .git).",
    )
    parser.add_argument(
        "--use-c-comments",
        action="store_true",
        help="Use /* ... */ comments instead of // comments on #endif.",
    )
    parser.add_argument(
        "-n",
        "--dry-run",
        action="store_true",
        help="Show what would be changed without writing changes to disk.",
    )
    return parser.parse_args(argv)


def main(argv: Optional[list[str]] = None) -> int:
    """Main entry point for the header guard fixer script.

    Args:
        argv: Optional command-line arguments.

    Returns:
        0 on success, or non-zero on error.
    """
    args = parse_args(argv)

    root = args.root
    if root is None:
        first_path = args.paths[0] if args.paths else Path(".")
        root = find_git_root(first_path)

    exclude_dirs = set(DEFAULT_EXCLUDE_DIRS)
    if args.exclude_dir:
        exclude_dirs.update(args.exclude_dir)

    headers = collect_header_files(args.paths, exclude_dirs=exclude_dirs)
    if not headers:
        print("No header files found to process.")
        return 0

    modified_count = 0
    for header in headers:
        if fix_header_guard(
            header,
            root=root,
            prefix=args.prefix,
            use_c_comments=args.use_c_comments,
            dry_run=args.dry_run,
        ):
            modified_count += 1

    action = "Would update" if args.dry_run else "Updated"
    print(f"{action} {modified_count} of {len(headers)} header file(s).")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
