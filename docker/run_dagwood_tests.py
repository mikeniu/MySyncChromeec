#!/usr/bin/env python3
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Helper script to run Dagwood device tests inside the Docker container."""

import argparse
import os
import sys


def main():
    """Main function to parse arguments and run tests."""
    parser = argparse.ArgumentParser(
        description="Run Dagwood device tests inside the Docker container."
    )
    parser.add_argument(
        "-p",
        "--platform",
        required=True,
        help="Platform type (e.g., realtek/rts5912)",
    )
    parser.add_argument(
        "-T",
        "--test-dir",
        action="append",
        help=(
            "Test directory to search (e.g., zephyr/test/ec-aic). "
            "Can be specified multiple times."
        ),
    )
    parser.add_argument(
        "-s",
        "--test-scenario",
        action="append",
        help="Specific test scenario to run. Can be specified multiple times.",
    )
    parser.add_argument(
        "-d",
        "--device-serial",
        default="/dev/ttyACM1",
        help="Device serial port (default: /dev/ttyACM1)",
    )

    args = parser.parse_args()

    # Construct the twister command
    twister_args = [
        "-ivc",
        "--toolchain=coreboot-sdk",
        "-p",
        args.platform,
        "--device-testing",
        "--device-serial",
        args.device_serial,
        "--flash-command",
        "../dagwood/flash.py",
        "--device-flash-timeout",
        "60",
    ]

    if args.test_dir:
        for t_dir in args.test_dir:
            twister_args.extend(["-T", t_dir])

    if args.test_scenario:
        for scenario in args.test_scenario:
            twister_args.extend(["-s", scenario])

    # If neither test-dir nor test-scenario is specified, default to ec-aic
    if not args.test_dir and not args.test_scenario:
        print(
            "Neither -T nor -s specified. Defaulting to -T zephyr/test/ec-aic"
        )
        twister_args.extend(["-T", "zephyr/test/ec-aic"])

    script_dir = os.path.dirname(os.path.realpath(__file__))
    run_docker_sh = os.path.join(script_dir, "run_docker.sh")

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
