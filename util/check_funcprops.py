#!/usr/bin/env vpython3
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

r"""Checks Doxygen function property qualifiers in C sources.

Zephyr documents a function's calling context and scheduling behavior with
Doxygen qualifiers. Definitions follow the upstream API terminology:
https://docs.zephyrproject.org/latest/develop/api/terminology.html

Qualifiers:
- @reschedule: May reach a reschedule point. If a higher-priority thread
  becomes ready, a preemptible caller may be preempted (a cooperative caller
  is not). Without @sleep, the function is callable from ISRs.
- @sleep: May cause the calling thread to sleep (wait on a kernel object or
  timeout, or call k_yield()). Implies @reschedule. Callable only from thread
  context unless combined with @no_wait (on the no-wait path), @isr_ok (from
  ISRs), or @pre_kernel_ok (pre-kernel).
- @no_wait: Used with @sleep when a parameter (typically K_NO_WAIT) selects a
  non-sleeping path that is also safe in ISR and pre-kernel contexts.
- @isr_ok: Safe and behaves as documented in both ISR and thread contexts. On
  a @sleep function, indicates the implementation handles ISR context itself
  (e.g. defers work or returns -EWOULDBLOCK) without requiring a no-wait
  argument. Implicit (optional) on functions without @sleep.
- @pre_kernel_ok: Safe and behaves as documented before the kernel main thread
  starts (PRE_KERNEL_1 and PRE_KERNEL_2 init).
- @async: May return before the initiated operation completes. Independent of
  @sleep and @no_wait.
- @supervisor: Requires supervisor privileges; cannot be called from user mode.

Unannotated functions:
A function with no qualifiers is implicitly synchronous (not @async), never
sleeps or reschedules (not @sleep, not @reschedule), is callable from ISRs
(implicitly @isr_ok) and user mode (not @supervisor), and is not assumed safe
before the kernel starts (not @pre_kernel_ok).

Annotating callers:
Qualifiers describe a function's contract with its callers. Derive them from
what the function can execute across all callees, not by copying a callee's
tags verbatim:
- Never sleeps or reschedules, and is ISR-safe (k_uptime_get, k_busy_wait):
  no qualifiers (add @pre_kernel_ok if also valid pre-kernel).
- Reaches a reschedule point without sleeping, and is ISR-safe (k_sem_give,
  k_event_post, k_work_submit):
  @reschedule.
- Can sleep or yield (k_msleep, k_yield, k_sem_take(&s, K_FOREVER)):
  @sleep.
- Forwards a timeout parameter to a sleeping call (k_sem_take(&s, timeout)),
  and the K_NO_WAIT path is safe in ISR and pre-kernel contexts:
  @sleep and @no_wait. If the K_NO_WAIT path is not ISR- and pre-kernel-safe
  (e.g. k_mutex_lock(&m, timeout)), use @sleep alone.
- Sleeps in thread context, but skips sleeping in non-thread contexts:
  @sleep, plus @isr_ok if the ISR path is concurrency-safe and fulfills the
  documented contract, and @pre_kernel_ok only if everything used is also
  initialized pre-kernel. (k_can_yield() is false in both ISRs and pre-kernel,
  whereas k_is_in_isr() is false pre-kernel.)
- Always passes K_NO_WAIT to a @no_wait call (k_sem_take(&s, K_NO_WAIT), not
  thread-only calls like k_mutex_lock):
  no @sleep or @no_wait.

Thread-only functions:
Zephyr has no @thread_only qualifier; any function without @sleep is
implicitly @isr_ok (including functions marked @reschedule). However, some
operations are forbidden in ISRs even when they never sleep; for example,
k_mutex_lock(&m, K_NO_WAIT) and k_mutex_unlock(&m) assert !arch_is_in_isr()
because mutex ownership is bound to a thread. By convention, mark such
thread-only functions @sleep (without @no_wait or @isr_ok) and note the
restriction in the Doxygen description.

Rules enforced by this script:
1. Use standalone @<qualifier> tags, not the deprecated @funcprops wrapper
   or backslash (\qualifier) syntax.
2. Use underscores, not hyphens (@isr_ok, @no_wait, @pre_kernel_ok).
3. Leave a blank comment line (' *') between qualifier tags and any following
   Doxygen @tag (@param, @return, @retval, @note, etc.). Multiple qualifiers
   may share a line or appear on adjacent lines.
4. @no_wait requires @sleep.
5. Do not combine @reschedule with @sleep (@sleep implies @reschedule).

Example:
/**
 * Read the next item from the queue.
 *
 * @sleep
 * @no_wait
 *
 * @param item Output buffer for the dequeued item.
 * @param timeout Maximum time to wait, or K_NO_WAIT to return immediately.
 * @retval 0 Item read.
 * @retval -ENOMSG Queue is empty and timeout was K_NO_WAIT.
 * @retval -EAGAIN Timed out waiting for an item.
 */
int foo_dequeue(struct foo_item *item, k_timeout_t timeout);
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
