# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Library for running Dagwood tests.

This library provides shared utilities for parsing command-line arguments
and constructing twister arguments for running tests on Dagwood hardware,
supporting both host-direct and containerized execution.
"""

import argparse
from pathlib import Path
import sys


DAGWOOD_DIR = Path(__file__).resolve().parents[2] / "dagwood"
if str(DAGWOOD_DIR) not in sys.path:
    sys.path.append(str(DAGWOOD_DIR))

# pylint: disable=import-error, wrong-import-position
import utils


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
        default=None,
        help="Device serial port (default: automatically detected from Dagwood board)",
    )
    parser.add_argument(
        "--board-id",
        type=str,
        required=False,
        help="Dagwood board serial number",
    )
    parser.add_argument(
        "-r",
        "--sram",
        action="store_true",
        help="Run tests from SRAM (adds -r to flash command).",
    )
    parser.add_argument(
        "-b",
        "--build-only",
        action="store_true",
        help=(
            "Only build the test binaries; do not execute on hardware "
            "or flash."
        ),
    )


def get_twister_args(
    args: argparse.Namespace, extra_args: list | None = None
) -> list:
    """Construct twister arguments from parsed arguments.

    Args:
        args: Parsed command-line arguments (Namespace).
        extra_args: Unparsed extra arguments to pass through to twister.

    Returns:
        A list of string arguments to be passed to the twister command.
    """
    flash_cmd = "../dagwood/flash.py"
    if getattr(args, "board_id", None):
        flash_cmd += f",--board-id,{args.board_id}"
    if args.sram:
        flash_cmd += ",-r"

    twister_args = [
        "-ivc",
        "--toolchain=coreboot-sdk",
        "-p",
        args.platform,
    ]

    if args.build_only:
        twister_args.append("-b")
    else:
        device_serial = args.device_serial
        if not device_serial:
            dev = utils.find_usb_device(getattr(args, "board_id", None))
            device_serial = utils.find_ec_port(dev)

        twister_args.extend(
            [
                "--device-testing",
                "--device-serial",
                device_serial,
                "--flash-command",
                flash_cmd,
                "--device-flash-timeout",
                "60",
            ]
        )

    if args.sram:
        twister_args.append("-x=SNIPPET=sram-only")

    if args.test_dir:
        for t_dir in args.test_dir:
            twister_args.extend(["-T", t_dir])

    if args.test_scenario:
        for scenario in args.test_scenario:
            twister_args.extend(["-s", scenario])

    if extra_args:
        twister_args.extend(extra_args)

    return twister_args
