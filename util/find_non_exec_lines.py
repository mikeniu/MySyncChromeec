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

import re
import sys


BAD_COVERAGE = re.compile(r"^$|\*/\s*$")


def main() -> int:
    """Read lcov files, and find lines that are probably not executable."""
    exit_code = 0
    for input_file in sys.argv:
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
                    # There are several files in zephyr that have odd coverage
                    # but it seems consistent.
                    # Also ignore test dirs that don't affect coverage numbers
                    if (  # pylint: disable=too-many-boolean-expressions
                        not (
<<<<<<< HEAD   (d6c32f2441663510e20ae2d25d5edeb8956421b9 program,zmake: drop cmsis references)
                            "src/third_party/zephyrproject/modules/hal/"
                            "cmsis/CMSIS/Core/Include/core_cm4.h"
||||||| BASE   (4f79cf3e850f16ecfb53fcd1c296c608faa9aef0 fpsensor: Add fingerprint_auth_enabled())
                        in active_name
=======
                            "src/third_party/zephyrproject/zephyr/"
                            "arch/arm/core/aarch32/mpu/arm_mpu.c"
>>>>>>> BRANCH (a2cd4a0be01528f87cbc369a0dffbfc2fe4673c1 imvp/rt3645: Remove gpio state check)
                        )
                        in active_name
                        and not (
<<<<<<< HEAD   (d6c32f2441663510e20ae2d25d5edeb8956421b9 program,zmake: drop cmsis references)
                            "src/third_party/zephyrproject/zephyr/"
                            "arch/arm/core/aarch32/mpu/arm_mpu.c"
                        )
                        in active_name
                        and not (
||||||| BASE   (4f79cf3e850f16ecfb53fcd1c296c608faa9aef0 fpsensor: Add fingerprint_auth_enabled())
=======
>>>>>>> BRANCH (a2cd4a0be01528f87cbc369a0dffbfc2fe4673c1 imvp/rt3645: Remove gpio state check)
                            "src/third_party/zephyrproject/zephyr/drivers/"
                            "clock_control/clock_control_mchp_xec.c"
                            in active_name
                        )
                        and not "src/third_party/zephyrproject/zephyr/lib/libc/minimal/include/"
                        in active_name
                        and not "src/third_party/zephyrproject/zephyr/subsys/testsuite/ztest/"
                        in active_name
                        and not "platform/ec/zephyr/test/" in active_name
                        and not "platform/ec/build/" in active_name
                    ):
                        active_file = open(  # pylint: disable=R1732
                            active_name, encoding="utf-8"
                        )
                if active_file and line.startswith("DA:"):
                    target_line = int(line[3:].split(",", 1)[0])
                    target = "NO SUCH LINE\n"
                    while target and target_line > active_line:
                        target = active_file.readline()
                        active_line += 1
                    if target and target_line == active_line:
                        target = target.strip()
                        if BAD_COVERAGE.match(target):
                            print(f"{active_name}:{active_line}={target}")
                            exit_code = 1
            if active_file:
                active_file.close()
                active_file = None

    return exit_code


if __name__ == "__main__":
    sys.exit(main())  # next section explains the use of sys.exit
