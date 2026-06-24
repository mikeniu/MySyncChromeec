#!/usr/bin/env python3
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Helper script to run Dagwood device tests inside the Docker container."""

import argparse
import os
from pathlib import Path
import sys


# Add EC root to path to allow importing util.dagwood_test_lib
SCRIPT_DIR = Path(__file__).resolve().parent
EC_ROOT = SCRIPT_DIR.parent
sys.path.append(str(EC_ROOT))

# pylint: disable=import-error, wrong-import-position
from util import dagwood_test_lib


def main():
    """Main function to parse arguments and run tests."""
    parser = argparse.ArgumentParser(
        description="Run Dagwood device tests inside the Docker container."
    )
    dagwood_test_lib.add_common_args(parser)
    args = parser.parse_args()

    twister_args = dagwood_test_lib.get_twister_args(args)

    run_docker_sh = str(SCRIPT_DIR / "run_docker.sh")

    # Join twister args into a space-separated string for bash -c
    twister_cmd = " ".join(twister_args)
    cmd = [
        run_docker_sh,
        "bash",
        "-c",
        f"cd /workspace/src/platform/ec && python3 ./twister {twister_cmd}",
    ]

    print(f"Running command: {' '.join(cmd)}")

    # Use execvp to replace the current process, matching the bash
    # behavior of 'exec'.
    # This ensures signals are forwarded correctly and we don't leave a
    # hanging python process.
    try:
        os.execvp(cmd[0], cmd)
    except OSError as e:
        print(f"Error executing {cmd[0]}: {e}", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
