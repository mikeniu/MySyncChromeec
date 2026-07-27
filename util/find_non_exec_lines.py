#!/usr/bin/env python3
# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Read lcov files, and find lines that are probably not executable.

Example run:
zmake build --coverage -a && \
./util/find_non_exec_lines.py build/zephyr/all_builds.info && \
echo SUCCESS
"""

import os
import re
import sys
from typing import List, Tuple


BAD_COVERAGE = re.compile(
    r"^\s*$|"  # Blank line
    r"\*/\s*$|"  # End of multiline comment
    r"^\s*//|"  # Single line comment //
    r"^\s*/\*.*\*/\s*$|"  # Single line comment /* ... */
    r"^\s*#\s*(endif|else|elif|ifdef|ifndef)\b|"  # Preprocessor directive
    r"^\s*\}\s*;?\s*$"  # Standalone closing brace
)


def is_ignored_path(active_name: str) -> bool:
    """Returns True if active_name is a known third_party/test/build file."""
    return (
        "src/third_party/zephyrproject/zephyr/arch/arm/core/aarch32/mpu/arm_mpu.c"
        in active_name
        or "src/third_party/zephyrproject/zephyr/drivers/clock_control/clock_control_mchp_xec.c"
        in active_name
        or "src/third_party/zephyrproject/zephyr/include/zephyr/sys/byteorder.h"
        in active_name
        or "src/third_party/zephyrproject/zephyr/lib/libc/minimal/include/"
        in active_name
        or "src/third_party/zephyrproject/zephyr/subsys/testsuite/ztest/"
        in active_name
        or "platform/ec/zephyr/test/" in active_name
        or "platform/ec/build/" in active_name
    )


def find_non_exec_in_file(input_file: str) -> List[Tuple[str, int, str]]:
    """Scans LCOV file and returns (filename, line_num, text) for non-exec lines."""
    results = []
    with open(input_file, encoding="utf-8") as lcov:
        active_file = None
        active_line = 0
        active_name = ""
        for line in lcov:
            line = line.strip()
            if line.startswith("SF:"):
                if active_file:
                    active_file.close()
                    active_file = None
                active_line = 0
                active_name = line[3:]
                if not is_ignored_path(active_name) and os.path.exists(
                    active_name
                ):
                    try:
                        active_file = open(  # pylint: disable=R1732
                            active_name, encoding="utf-8", errors="replace"
                        )
                    except OSError:
                        active_file = None
            if active_file and line.startswith("DA:"):
                target_line = int(line[3:].split(",", 1)[0])
                target = "NO SUCH LINE\n"
                while target and target_line > active_line:
                    target = active_file.readline()
                    active_line += 1
                if target and target_line == active_line:
                    target = target.strip()
                    if BAD_COVERAGE.match(target):
                        results.append((active_name, active_line, target))
        if active_file:
            active_file.close()

    return results


def main() -> int:
    """Read lcov files, and find lines that are probably not executable."""
    exit_code = 0
    for input_file in sys.argv[1:]:
        bad_lines = find_non_exec_in_file(input_file)
        for name, line_num, text in bad_lines:
            print(f"{name}:{line_num}={text}")
            exit_code = 1

    return exit_code


if __name__ == "__main__":
    sys.exit(main())
