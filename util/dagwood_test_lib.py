# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Library for running Dagwood tests.

This library provides shared utilities for parsing command-line arguments
and constructing twister arguments for running tests on Dagwood hardware,
supporting both host-direct and containerized execution.
"""

import argparse


def add_common_args(parser: argparse.ArgumentParser):
    """Add common arguments for Dagwood test runners.

    Args:
        parser: The ArgumentParser object to add arguments to.
    """
    parser.add_argument(
        "-p",
        "--platform",
        required=True,
        help="Platform type (e.g., npcx9/npcx9m7f, realtek/rts5912)",
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
    parser.add_argument(
        "-r",
        "--sram",
        action="store_true",
        help="Run tests from SRAM (adds -r to flash command).",
    )


def get_twister_args(args: argparse.Namespace) -> list:
    """Construct twister arguments from parsed arguments.

    Args:
        args: Parsed command-line arguments (Namespace).

    Returns:
        A list of string arguments to be passed to the twister command.
    """
    flash_cmd = "../dagwood/flash.py"
    if args.sram:
        flash_cmd += ",-r"

    twister_args = [
        "-ivc",
        "--toolchain=coreboot-sdk",
        "-p",
        args.platform,
        "--device-testing",
        "--device-serial",
        args.device_serial,
        "--flash-command",
        flash_cmd,
        "--device-flash-timeout",
        "60",
    ]

    if args.test_dir:
        for t_dir in args.test_dir:
            twister_args.extend(["-T", t_dir])

    if args.test_scenario:
        for scenario in args.test_scenario:
            twister_args.extend(["-s", scenario])

    return twister_args
