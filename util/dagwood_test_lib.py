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
import dagwood_utils


def add_common_args(parser: argparse.ArgumentParser):
    """Add common arguments for Dagwood test runners.

    Args:
        parser: The ArgumentParser object to add arguments to.
    """
    parser.add_argument(
        "-p",
        "--platform",
        action="append",
        help=(
            "Platform type (e.g., npcx9/npcx9m7f, realtek/rts5912). "
            "Can be specified multiple times. Optional when --hardware-map is used."
        ),
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
    device_group = parser.add_mutually_exclusive_group()
    device_group.add_argument(
        "-d",
        "--device-serial",
        default=None,
        help="Device serial port (default: automatically detected from Dagwood board)",
    )
    device_group.add_argument(
        "--hardware-map",
        default=None,
        help="Load hardware map from a file.",
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
    hardware_map = getattr(args, "hardware_map", None)
    if not hardware_map and extra_args:
        for idx, arg in enumerate(extra_args):
            if arg == "--hardware-map" and idx + 1 < len(extra_args):
                hardware_map = extra_args[idx + 1]
                break
            if arg.startswith("--hardware-map="):
                hardware_map = arg.split("=", 1)[1]
                break

    platform = getattr(args, "platform", None)
    if not hardware_map and not platform:
        sys.exit(
            "Error: -p/--platform is required when --hardware-map is not specified."
        )

    flash_cmd = "../dagwood/flash.py"
    if getattr(args, "board_id", None):
        flash_cmd += f",--board-id,{args.board_id}"
    if getattr(args, "sram", False):
        flash_cmd += ",-r"

    twister_args = [
        "-ivc",
        "--toolchain=coreboot-sdk",
    ]
    if isinstance(platform, list):
        for p in platform:
            twister_args.extend(["-p", p])
    elif platform:
        twister_args.extend(["-p", platform])

    if getattr(args, "build_only", False):
        twister_args.append("-b")
    elif hardware_map:
        twister_args.extend(
            [
                "--device-testing",
                "--hardware-map",
                hardware_map,
                "--flash-command",
                flash_cmd,
                "--device-flash-timeout",
                "60",
            ]
        )
        if getattr(args, "board_id", None):
            twister_args.append(f"--pytest-args=--board-id={args.board_id}")
    else:
        board_id = getattr(args, "board_id", None)
        device_serial = getattr(args, "device_serial", None)
        if not device_serial:
            dev = dagwood_utils.find_usb_device(board_id)
            device_serial = dagwood_utils.find_ec_port(dev)
            if not board_id:
                board_id = dev.serial_number

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

        if board_id:
            twister_args.append(f"--pytest-args=--board-id={board_id}")

    if getattr(args, "sram", False):
        twister_args.append("-x=SNIPPET=sram-only")

    test_dir = getattr(args, "test_dir", None)
    if test_dir:
        for t_dir in test_dir:
            twister_args.extend(["-T", t_dir])

    test_scenario = getattr(args, "test_scenario", None)
    if test_scenario:
        for scenario in test_scenario:
            twister_args.extend(["-s", scenario])

    if extra_args:
        cleaned_extra_args = []
        skip_next = False
        for arg in extra_args:
            if skip_next:
                skip_next = False
                continue
            if arg == "--hardware-map":
                skip_next = True
                continue
            if arg.startswith("--hardware-map="):
                continue
            cleaned_extra_args.append(arg)
        twister_args.extend(cleaned_extra_args)

    return twister_args
