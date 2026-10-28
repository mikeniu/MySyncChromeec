#!/usr/bin/env vpython3
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

r"""Checks files for valid Doxygen function property qualifiers.

Overview:
Zephyr provides standard Doxygen function property qualifiers to document
the execution context and execution behavior of kernel and driver APIs:
- Default (unannotated): Functions without @sleep or @reschedule never sleep
  or reach a reschedule point and are implicitly safe in ISR and pre-kernel
  contexts (isr-ok), unless they call thread-only APIs.
- @reschedule: Reaches a reschedule point that can preempt the current thread
  if a higher-priority thread becomes ready (e.g. k_sem_give, k_event_post,
  k_work_submit, k_mutex_unlock). Allowed in ISRs unless restricted by a
  thread-only primitive (e.g. mutexes).
- @sleep: Can cause the invoking thread to sleep, block, or yield (or
  busy-wait for hardware FIFO drain like cflush()). Implicitly includes
  @reschedule (do not specify both @sleep and @reschedule). Prohibited in ISR,
  pre-kernel, and k_sys_work_q contexts unless mediated by @no_wait or
  @isr_ok / @pre_kernel_ok.
- @no_wait: Used ONLY alongside @sleep to indicate that a parameter to the
  function (such as K_NO_WAIT or timeout_us == 0) forces a non-sleeping
  execution path. Do NOT add @no_wait to functions that never sleep.
- @isr_ok: Safe to call from both interrupt and thread contexts. Used on:
  (1) @sleep functions that detect ISR context at runtime (e.g. via
  k_is_in_isr() or !k_can_yield()) and bypass blocking or return -EWOULDBLOCK;
  (2) @sleep + @no_wait functions whose non-waiting path is legal in an ISR
  (k_sem_take with K_NO_WAIT is @isr_ok; k_mutex_lock is NOT @isr_ok because
  mutexes assert !arch_is_in_isr() even with K_NO_WAIT);
  (3) core non-sleeping APIs explicitly intended for ISR callers
  (task_wake, hook_call_deferred, get_time).
- @pre_kernel_ok: Safe to call during PRE_KERNEL_1 / PRE_KERNEL_2 init
  before the scheduler starts (typically checks k_is_pre_kernel() or
  !k_can_yield()).
- @async: Returns before the initiated operation completes (orthogonal
  to @sleep).
- @supervisor: Requires supervisor privileges.

Upstream documentation:
https://docs.zephyrproject.org/latest/develop/api/terminology.html#api-terminology

Decision Guide for Common Patterns:
- Pure computation / register read (get_time): No @sleep/@reschedule
  (@isr_ok / @pre_kernel_ok optional to document ISR/early-boot contract).
- Unconditional sleep or blocking bus/FIFO wait (crec_usleep, i2c_xfer,
  cflush): @sleep.
- Blocking API with a timeout parameter supporting non-blocking calls
  (task_wait_event, k_sem_take, k_mutex_lock): @sleep and @no_wait (plus
  @isr_ok only if valid in an ISR with K_NO_WAIT).
- Function that calls k_mutex_lock(&m, K_NO_WAIT) + k_mutex_unlock(&m)
  internally: @reschedule (unlocking can reschedule a waiter; NOT @sleep,
  NOT @no_wait, and NOT @isr_ok because mutexes assert !arch_is_in_isr()).
- Wakes a task or submits work without sleeping (task_wake, task_set_event,
  hook_call_deferred): @isr_ok and @reschedule (NOT @no_wait).
- Locks a mutex in thread context, but skips it in ISR/pre-kernel via
  !k_can_yield() (cprints, cprintf, cputs): @sleep, @isr_ok, @pre_kernel_ok.

Formatting & Semantic Rules Enforced:
1. Use standalone @<qualifier> tags rather than the deprecated @funcprops
   wrapper or backslash (\qualifier) syntax.
2. Use underscores rather than hyphens (@isr_ok, @no_wait, @pre_kernel_ok).
3. Separate @<qualifier> tags from subsequent Doxygen tags (@param, @return,
   @retval, @note, etc.) with a blank comment line (' *'). Consecutive
   qualifier tags on adjacent lines do not require intervening blank lines.
4. @no_wait is only valid on functions that also specify @sleep.
5. @reschedule is redundant when @sleep is specified (@sleep implies
   @reschedule).

Example:
/**
 * Short function description.
 *
 * @sleep
 * @no_wait
 * @isr_ok
 *
 * @param timeout_us Maximum wait time (0 for non-blocking).
 * @return 0 on success, negative error code on failure.
 */
int example_function(int timeout_us);
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
