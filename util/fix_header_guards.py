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
from typing import List, Optional, Set


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


def get_guard_variable(filepath: Path, root: Path) -> str:
    """Calculates the expected header guard macro variable name for |filepath|.

    The macro name is derived from the file path relative to |root|,
    converting all non-alphanumeric characters to underscores, uppercase,
    with a trailing underscore.

    Args:
        filepath: The path to the header file.
        root: The base directory from which the relative path is calculated.

    Returns:
        The formatted macro variable string (e.g., 'UTIL_COMM_HOST_H_').
    """
    try:
        rel_path = filepath.resolve().relative_to(root.resolve())
    except ValueError:
        rel_path = filepath.name
    guard = re.sub(r"[^a-zA-Z0-9]", "_", str(rel_path)).upper() + "_"
    return guard


def fix_header_guard(
    filepath: Path,
    root: Path,
    use_c_comments: bool = False,
    dry_run: bool = False,
) -> bool:
    """Fixes header guard macro directives in |filepath|.

    Updates the #ifndef, #define, and trailing #endif comment lines to match
    the expected guard variable name computed from |filepath| and |root|.

    Args:
        filepath: Path to the header file to examine and update.
        root: The base repository or project root for guard path derivation.
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

    lines = content.splitlines(keepends=True)
    if not lines:
        return False

    expected_guard = get_guard_variable(filepath, root)

    ifndef_idx: Optional[int] = None
    define_idx: Optional[int] = None
    old_guard: Optional[str] = None

    for idx, line in enumerate(lines):
        if ifndef_idx is None:
            m = re.match(r"^\s*#\s*ifndef\s+([A-Za-z0-9_]+)", line)
            if m:
                ifndef_idx = idx
                old_guard = m.group(1)
                continue
        if ifndef_idx is not None and define_idx is None:
            m = re.match(r"^\s*#\s*define\s+([A-Za-z0-9_]+)", line)
            if m and (m.group(1) == old_guard or not old_guard):
                define_idx = idx
                break

    if ifndef_idx is None or define_idx is None:
        return False

    # Find the last #endif in the file
    endif_idx: Optional[int] = None
    for idx in range(len(lines) - 1, -1, -1):
        if re.match(r"^\s*#\s*endif\b", lines[idx]):
            endif_idx = idx
            break

    if endif_idx is None:
        return False

    comment = (
        f"/* {expected_guard} */" if use_c_comments else f"// {expected_guard}"
    )

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
    new_endif = f"#endif {comment}"

    # Preserve original line endings
    newline = "\r\n" if lines[0].endswith("\r\n") else "\n"
    lines[ifndef_idx] = new_ifndef + newline
    lines[define_idx] = new_define + newline
    lines[endif_idx] = new_endif + newline

    new_content = "".join(lines)
    if new_content == content:
        return False

    if dry_run:
        print(f"[dry-run] Would update: {filepath} -> {expected_guard}")
    else:
        filepath.write_text(new_content, encoding="utf-8")
        print(f"Updated: {filepath} -> {expected_guard}")

    return True


def collect_header_files(
    paths: List[Path],
    exclude_dirs: Optional[Set[str]] = None,
) -> List[Path]:
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
        return any(part in exclude_dirs for part in p.parts)

    headers = []
    for path in paths:
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
                    if file_path.suffix in (".h", ".hh", ".hpp"):
                        headers.append(file_path)
    return sorted(set(headers))


def parse_args(argv: Optional[List[str]] = None) -> argparse.Namespace:
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


def main(argv: Optional[List[str]] = None) -> int:
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
            use_c_comments=args.use_c_comments,
            dry_run=args.dry_run,
        ):
            modified_count += 1

    action = "Would update" if args.dry_run else "Updated"
    print(f"{action} {modified_count} of {len(headers)} header file(s).")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
