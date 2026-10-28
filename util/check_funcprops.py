#!/usr/bin/env vpython3
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

r"""Checks Doxygen function property qualifiers in C sources.

Zephyr documents a function's calling context and scheduling behavior with
Doxygen qualifiers. Definitions follow the upstream API terminology:
https://docs.zephyrproject.org/latest/develop/api/terminology.html#api-terminology

Qualifiers:
- @reschedule: May reach a reschedule point. If a higher-priority thread
  becomes ready, a preemptible caller may be preempted. Callable from ISRs.
- @sleep: May cause the calling thread to sleep (wait on a kernel object or
  timeout, or yield). Implies @reschedule. Callable only from thread context
  unless @no_wait, @isr_ok, or @pre_kernel_ok says otherwise.
- @no_wait: Requires @sleep. A parameter (typically K_NO_WAIT) selects a path
  that never sleeps. On that path the function is also callable from ISR and
  pre-kernel contexts.
- @isr_ok: Works as documented when called from an ISR, for any arguments
  (e.g. detects ISR context and returns -EWOULDBLOCK instead of sleeping).
- @pre_kernel_ok: Works as documented before the kernel main thread starts
  (PRE_KERNEL_1 and PRE_KERNEL_2 init).
- @async: May return before the operation it starts completes. Independent
  of @sleep.
- @supervisor: Cannot be called from user mode.

Unannotated functions:
A function with no qualifiers is synchronous, never sleeps or reschedules,
and is callable from ISRs and user mode. Any function without @sleep is
implicitly @isr_ok. No function is implicitly @pre_kernel_ok.

Annotating callers:
Qualifiers describe a function as its callers see it. Derive them from how
the function calls into the kernel, not by copying a callee's qualifiers:
- Never sleeps or reschedules (k_uptime_get, k_busy_wait):
  no qualifiers.
- Wakes threads but never sleeps (k_sem_give, k_work_submit):
  @reschedule.
- Always may sleep (k_msleep, k_sem_take(&s, K_FOREVER)):
  @sleep.
- Passes a caller-supplied timeout to a sleeping call
  (k_sem_take(&s, timeout)):
  @sleep and @no_wait.
- Sleeps only in thread context and skips the sleeping call when
  k_is_in_isr() (or !k_can_yield()):
  @sleep and @isr_ok, plus @pre_kernel_ok if the call is also skipped
  pre-kernel.
- Always passes K_NO_WAIT to a sleeping call (k_sem_take(&s, K_NO_WAIT)):
  no @sleep or @no_wait.

Thread-only functions:
There is no thread-only qualifier; the terminology assumes that any function
that cannot sleep is ISR-safe. Some primitives are thread-only even when they
cannot sleep. For example, k_mutex_lock() binds ownership to the calling
thread, so it is invalid in an ISR even with K_NO_WAIT. Annotate such
functions with @sleep, omit @no_wait and @isr_ok, and state the restriction
in the description.

Rules enforced:
1. Use standalone @<qualifier> tags, not the deprecated @funcprops wrapper
   or backslash (\qualifier) syntax.
2. Use underscores, not hyphens (@isr_ok, @no_wait, @pre_kernel_ok).
3. Separate qualifier tags from following Doxygen tags (@param, @return,
   @retval, @note, etc.) with a blank comment line (' *'). Adjacent
   qualifier tags need no blank line between them.
4. @no_wait requires @sleep.
5. Do not combine @reschedule with @sleep (@sleep implies @reschedule).

Example:
/**
 * Wait for data to arrive.
 *
 * @sleep
 * @no_wait
 *
 * @param timeout Maximum time to wait, or K_NO_WAIT to not wait.
 * @return 0 on success, -EAGAIN if no data arrived before the timeout.
 */
int foo_wait_data(k_timeout_t timeout);
"""

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
                f"{prefix}{line_no}: error: backslash qualifier "
                f"'{m.group(0)}'; use canonical '@{m.group(1).lower()}'."
            )

        # 3. Hyphenated typos
        for m in HYPHEN_TYPO_RE.finditer(block):
            line_no = text[: start_pos + m.start()].count("\n") + 1
            canonical = m.group(1).lower().replace("-", "_")
            errors.append(
                f"{prefix}{line_no}: error: hyphenated qualifier "
                f"'{m.group(0)}'; use canonical '@{canonical}'."
            )

        # Collect canonical qualifiers in this block for semantic checks
        block_quals = {
            q.lower()
            for q in QUAL_TAG_RE.findall(block)
            if q.lower() in CANONICAL_QUALIFIERS
        }

        # Semantic check 4: @no_wait requires @sleep
        if "no_wait" in block_quals and "sleep" not in block_quals:
            m_nw = re.search(r"@no_wait\b", block)
            line_no = (
                text[: start_pos + m_nw.start()].count("\n") + 1
                if m_nw
                else text[:start_pos].count("\n") + 1
            )
            errors.append(
                f"{prefix}{line_no}: error: '@no_wait' is only valid on "
                "functions that also specify '@sleep'."
            )

        # Semantic check 5: @reschedule is redundant when @sleep is specified
        if "sleep" in block_quals and "reschedule" in block_quals:
            m_resched = re.search(r"@reschedule\b", block)
            line_no = (
                text[: start_pos + m_resched.start()].count("\n") + 1
                if m_resched
                else text[:start_pos].count("\n") + 1
            )
            errors.append(
                f"{prefix}{line_no}: error: '@reschedule' is redundant "
                "when '@sleep' is specified ('@sleep' implies '@reschedule')."
            )

        # 6. Missing blank comment line between qualifier and subsequent
        # Doxygen tags
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
