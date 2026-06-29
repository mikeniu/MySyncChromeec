#!/usr/bin/env python3
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Helper script to run twister on-device tests on the dagwood tester."""

import argparse
import os
import sys

import dagwood_test_lib


def main():
    """Main function to parse arguments and run tests."""
    parser = argparse.ArgumentParser(
        description="Run twister device tests on host/chroot."
    )
    dagwood_test_lib.add_common_args(parser)
    args = parser.parse_args()

    twister_args = dagwood_test_lib.get_twister_args(args)

    cmd = ["./twister"]
    cmd.extend(twister_args)
    print(f"Running command: {' '.join(cmd)}")

    try:
        os.execvp(cmd[0], cmd)
    except OSError as e:
        print(f"Error executing {cmd[0]}: {e}", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
