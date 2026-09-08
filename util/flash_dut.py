#!/usr/bin/env python3
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Helper script to flash EC images directly on remote CrOS and ALOS DUTs.

This script leverages the DutHandler implementations in util/dut_handlers
to flash EC RW (via AP Software Sync) or RO (via flashrom/futility) over SSH or ADB,
without requiring a physical Servo connection.
"""

import argparse
import os
import sys
import time
from typing import Optional


# Ensure util/ directory is on sys.path so dut_handlers package can be imported
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)
# pylint: disable=wrong-import-position
from dut_handlers import create_dut_handler
from dut_handlers import DutOsType


# pylint: enable=wrong-import-position


def parse_dut_type(val: str) -> DutOsType:
    """Normalize and validate the DUT OS type string."""
    normalized = val.strip().lower()
    if normalized in ("cros", "chromeos", "cr"):
        return DutOsType.CROS
    if normalized in ("alos", "android", "al"):
        return DutOsType.ANDROID
    raise argparse.ArgumentTypeError(
        f"Invalid DUT type: '{val}'. Expected 'cros' or 'alos'."
    )


def parse_args() -> argparse.Namespace:
    """Parse command-line arguments."""
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )

    # Mandatory arguments
    mandatory_group = parser.add_argument_group("Mandatory Arguments")
    mandatory_group.add_argument(
        "-m",
        "--model",
        required=True,
        help="DUT model/project name (e.g. brya, skyrim, redrix).",
    )
    mandatory_group.add_argument(
        "-H",
        "--hostname",
        "--host",
        "--ip",
        "--dut",
        dest="hostname",
        required=True,
        help="DUT hostname or IP address (e.g. al-brya-ip6, 192.168.1.50).",
    )
    mandatory_group.add_argument(
        "-t",
        "--type",
        "--dut-type",
        "--os-type",
        dest="dut_type",
        required=True,
        type=parse_dut_type,
        help="DUT OS type: 'cros' or 'alos'.",
    )

    # Optional image path arguments
    image_group = parser.add_argument_group("Firmware Images")
    image_group.add_argument(
        "-i",
        "--image",
        "--ec-rw-bin",
        dest="ec_rw_bin",
        help=(
            "Path to custom EC RW binary to flash. Defaults to: "
            "<ec_dir>/build/zephyr/<model>/output/ec.bin"
        ),
    )
    image_group.add_argument(
        "--ec-ro-bin",
        "--ro",
        dest="ec_ro_bin",
        help="Path to custom EC RO binary to flash (optional).",
    )
    image_group.add_argument(
        "--skip-rw",
        action="store_true",
        help="Skip flashing the EC RW binary (useful when only flashing RO).",
    )

    # Additional options
    options_group = parser.add_argument_group("Additional Options")
    options_group.add_argument(
        "--board",
        help="DUT board name if different from model (default: same as model).",
    )
    options_group.add_argument(
        "--skip-verify",
        action="store_true",
        help="Skip waiting for DUT AP to reboot and verifying EC version.",
    )
    options_group.add_argument(
        "--timeout",
        type=int,
        default=300,
        help="Timeout in seconds to wait for AP to boot up (default: 300s).",
    )
    options_group.add_argument(
        "--android-dir",
        default=os.environ.get(
            "ANDROID_BUILD_TOP",
            os.path.expanduser("~/alos"),
        ),
        help="Path to Android root directory (for ALOS, default: $ANDROID_BUILD_TOP or ~/alos).",
    )

    return parser.parse_args()


def resolve_ec_rw_path(args: argparse.Namespace, ec_dir: str) -> Optional[str]:
    """Resolve and validate the EC RW binary path."""
    if args.skip_rw:
        return None

    if args.ec_rw_bin:
        path = os.path.abspath(args.ec_rw_bin)
    else:
        path = os.path.join(
            ec_dir, "build", "zephyr", args.model, "output", "ec.bin"
        )

    if not os.path.exists(path):
        if not args.ec_ro_bin:
            raise FileNotFoundError(
                f"EC RW binary not found at '{path}'. "
                "Please build the project first (e.g. zmake build <model>) "
                "or specify an explicit image using --image."
            )
        print(f"Note: EC RW binary not found at '{path}'. Skipping RW flash.")
        return None

    return path


def resolve_ec_ro_path(args: argparse.Namespace) -> Optional[str]:
    """Resolve and validate the EC RO binary path if requested."""
    if not args.ec_ro_bin:
        return None

    path = os.path.abspath(args.ec_ro_bin)
    if not os.path.exists(path):
        raise FileNotFoundError(f"EC RO binary not found at '{path}'.")
    return path


def main() -> None:
    """Flash EC image on target DUT and optionally verify reboot."""
    args = parse_args()
    ec_dir = os.path.dirname(SCRIPT_DIR)

    try:
        ec_rw_bin_path = resolve_ec_rw_path(args, ec_dir)
        ec_ro_bin_path = resolve_ec_ro_path(args)
    except FileNotFoundError as e:
        print(f"Error: {e}", file=sys.stderr)
        sys.exit(1)

    if not ec_rw_bin_path and not ec_ro_bin_path:
        print(
            "Error: Nothing to flash. Neither RW nor RO binary available.",
            file=sys.stderr,
        )
        sys.exit(1)

    details = {
        "dut_hostname": args.hostname,
        "model": args.model,
        "board": args.board or args.model,
        "os_type": args.dut_type,
    }

    print("========================================")
    print(f"Target DUT   : {args.hostname}")
    print(f"DUT Type     : {args.dut_type}")
    print(f"Model / Board: {args.model} / {details['board']}")
    if ec_ro_bin_path:
        print(f"EC RO Image  : {ec_ro_bin_path}")
    if ec_rw_bin_path:
        print(f"EC RW Image  : {ec_rw_bin_path}")
    print("========================================")

    handler = create_dut_handler(details, args, ec_dir)

    start_time = time.time()
    try:
        if ec_ro_bin_path:
            print(f"\n--- Flashing EC RO on {args.hostname} ---")
            handler.flash_ec_ro(ec_ro_bin_path)

        if ec_rw_bin_path:
            print(f"\n--- Flashing EC RW on {args.hostname} ---")
            handler.flash_ec_rw(ec_rw_bin_path)

        if not args.skip_verify:
            print("\n--- Verifying AP & EC Status ---")
            handler.verify_ap_up(timeout_secs=args.timeout)

        elapsed = time.time() - start_time
        print(f"\nFlashing completed successfully in {elapsed:.1f} seconds.")

    except KeyboardInterrupt:
        print("\nInterrupted by user.", file=sys.stderr)
        sys.exit(1)
    except Exception as e:  # pylint: disable=broad-exception-caught
        print(f"\nFlashing failed: {e}", file=sys.stderr)
        sys.exit(1)
    finally:
        handler.cleanup()


if __name__ == "__main__":
    main()
