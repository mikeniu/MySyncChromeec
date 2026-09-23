#!/usr/bin/env python3
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Helper script to run twister on-device tests on the dagwood tester."""

import argparse
import os
from pathlib import Path
import shutil
import sys


SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))

# pylint: disable=wrong-import-position
import dagwood_test_lib


def main():
    """Main function to parse arguments and run tests."""
    parser = argparse.ArgumentParser(
        description="Run twister device tests on host/chroot."
    )
    dagwood_test_lib.add_common_args(parser)
    args, extra_args = parser.parse_known_args()

    twister_args = dagwood_test_lib.get_twister_args(args, extra_args)

    ec_root = SCRIPT_DIR.parent
    twister_script = ec_root / "twister"
    if not twister_script.is_file():
        twister_script = Path("./twister").resolve()

    if shutil.which("vpython3"):
        cmd = [str(twister_script)]
    else:
        # When vpython3 is not available (e.g., inside Docker container),
        # use Python directly.
        cmd = [sys.executable, str(twister_script)]

    cmd.extend(twister_args)
    print(f"Running command: {' '.join(cmd)}")

    try:
        os.execvp(cmd[0], cmd)
    except OSError as e:
        print(f"Error executing {cmd[0]}: {e}", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
