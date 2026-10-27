#!/usr/bin/env vpython3
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Checks files for valid Doxygen function property qualifiers."""

import re
import sys
from typing import List, Optional

import preupload.lib


CANONICAL_QUALIFIERS = {
    "sleep",
    "reschedule",
    "no_wait",
    "isr_ok",
    "pre_kernel_ok",
    "async",
    "supervisor",
}

DOXYGEN_BLOCK_RE = re.compile(r"/\*\*(.*?)\*/", re.DOTALL)
DEPRECATED_FUNCPROPS_RE = re.compile(r"[@\\]funcprops\b")
BACKSLASH_QUAL_RE = re.compile(
    r"\\(sleep|reschedule|no_wait|isr_ok|pre_kernel_ok|async|supervisor)\b",
    re.IGNORECASE,
)
HYPHEN_TYPO_RE = re.compile(
    r"[@\\](isr-ok|no-wait|pre-kernel-ok)\b", re.IGNORECASE
)
QUAL_TAG_RE = re.compile(r"@([A-Za-z0-9_]+)\b")
NEXT_TAG_RE = re.compile(r"^@[A-Za-z0-9_]+(?::|\b)")


def validate_doxygen_qualifiers(text: str, filename: str = "") -> List[str]:
    """Validate Doxygen comment blocks in source text."""
    errors = []
    prefix = f"{filename}:" if filename else ""

    for match in DOXYGEN_BLOCK_RE.finditer(text):
        block = match.group(1)
        start_pos = match.start()

        # 1. Deprecated @funcprops or \funcprops
        for m in DEPRECATED_FUNCPROPS_RE.finditer(block):
            line_no = text[: start_pos + m.start()].count("\n") + 1
            errors.append(
                f"{prefix}{line_no}: error: deprecated '{m.group(0)}' tag; "
                "use standalone qualifiers (@sleep, @isr_ok, etc.)."
            )

        # 2. Backslash-prefixed qualifiers
        for m in BACKSLASH_QUAL_RE.finditer(block):
            line_no = text[: start_pos + m.start()].count("\n") + 1
            errors.append(
                f"{prefix}{line_no}: error: backslash qualifier '{m.group(0)}'; "
                f"use canonical '@{m.group(1).lower()}'."
            )

        # 3. Hyphenated typos
        for m in HYPHEN_TYPO_RE.finditer(block):
            line_no = text[: start_pos + m.start()].count("\n") + 1
            canonical = m.group(1).lower().replace("-", "_")
            errors.append(
                f"{prefix}{line_no}: error: hyphenated qualifier '{m.group(0)}'; "
                f"use canonical '@{canonical}'."
            )

        # 4. Missing blank comment line between qualifier and subsequent Doxygen tags
        lines = block.splitlines()
        for idx, line in enumerate(lines):
            s_line = line.strip()
            if s_line.startswith("*"):
                s_line = s_line.lstrip("*").strip()

            quals = [
                q
                for q in QUAL_TAG_RE.findall(s_line)
                if q.lower() in CANONICAL_QUALIFIERS
            ]
            if not quals:
                continue

            if idx + 1 < len(lines):
                next_line = lines[idx + 1].strip()
                if next_line.startswith("*"):
                    next_content = next_line.lstrip("*").strip()
                else:
                    next_content = next_line

                if not next_content or next_content.startswith("*/"):
                    continue

                next_quals = QUAL_TAG_RE.findall(next_content)
                if next_quals and next_quals[0].lower() in CANONICAL_QUALIFIERS:
                    continue

                if NEXT_TAG_RE.match(next_content):
                    line_no = (
                        text[:start_pos].count("\n")
                        + block[: block.find(line)].count("\n")
                        + 1
                    )
                    next_tag = next_content.split()[0]
                    errors.append(
                        f"{prefix}{line_no}: error: missing blank comment line "
                        f"(' *') between '@{quals[-1]}' and following "
                        f"tag '{next_tag}'."
                    )

    return errors


def main(argv: Optional[List[str]] = None) -> Optional[int]:
    """Validate Doxygen qualifiers on all files passed in on commandline."""
    return_code = 0
    args = preupload.lib.parse_args(argv)
    for filename in args.filename:
        if filename.is_file() and filename.suffix in [
            ".c",
            ".h",
            ".cc",
            ".inc",
        ]:
            content = preupload.lib.cat_file(args, filename)
            errors = validate_doxygen_qualifiers(content, str(filename))
            for err in errors:
                print(err, file=sys.stderr)
                return_code = 1

    return return_code


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
