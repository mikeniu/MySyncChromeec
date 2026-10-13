#!/usr/bin/env vpython3
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Python CLI for flashing ChromiumOS EC firmware over Servo."""

import argparse
import logging
import os
import subprocess
import sys
from typing import List, Optional

from flash.factory import FlasherFactory
from flash.servo_utils import ServoDriver
from flash.servo_utils import ServoError


def setup_logging(verbose: bool) -> None:
    """Configure root logger for the flash utility."""
    level = logging.DEBUG if verbose else logging.INFO
    logging.basicConfig(
        level=level,
        format="%(levelname)s: %(message)s",
    )


def parse_args(argv: Optional[List[str]] = None) -> argparse.Namespace:
    """Parse command-line arguments."""
    parser = argparse.ArgumentParser(description="Flash EC firmware via Servo.")

    parser.add_argument(
        "--board",
        "-b",
        type=str,
        default=os.environ.get("BOARD", ""),
        help="Target EC board name (defaults to $BOARD)",
    )
    parser.add_argument(
        "--image",
        "-i",
        type=str,
        help="Path to the firmware image file to write (required unless "
        "--read is given)",
    )
    parser.add_argument(
        "--read",
        "-r",
        type=str,
        help="Read current EC firmware into the specified file path",
    )
    parser.add_argument(
        "--port",
        "-p",
        type=int,
        default=9999,
        help="Servod port (default: 9999)",
    )
    parser.add_argument(
        "--host",
        type=str,
        default="localhost",
        help="Servod host address (default: localhost)",
    )
    parser.add_argument(
        "--chip",
        "-c",
        type=str,
        help="Override the target EC chip type (e.g., it83xx, npcx_uut, stm32)",
    )
    parser.add_argument(
        "--interface",
        type=str,
        choices=["spi", "i2c", "uart", "dfu", "jtag"],
        help="Explicitly force a specific programming transport interface",
    )
    parser.add_argument(
        "--ec_source",
        type=str,
        default=os.path.abspath(
            os.path.join(os.path.dirname(__file__), "..", "..")
        ),
        help="Path to the EC source tree root for dynamic board lookups",
    )

    parser.add_argument(
        "--verify",
        action="store_true",
        default=True,
        help="Verify firmware after writing (default: True)",
    )
    parser.add_argument(
        "--no_verify",
        "--noverify",
        action="store_false",
        dest="verify",
        help="Skip verification after writing",
    )
    parser.add_argument(
        "--no_preserve",
        action="store_true",
        default=False,
        help="Do not preserve FMAP sections across flashes",
    )
    parser.add_argument(
        "--dry_run",
        "-n",
        action="store_true",
        default=False,
        help="Print commands without mutating hardware states",
    )
    parser.add_argument(
        "--verbose",
        "-v",
        action="store_true",
        default=False,
        help="Enable verbose debug logging",
    )
    parser.add_argument(
        "--timeout",
        type=int,
        default=600,
        help="Timeout in seconds for individual flash tool invocations",
    )

    parser.add_argument(
        "--raiden",
        action="store_true",
        default=False,
        help="Use Raiden USB SPI interface directly instead of FTDI",
    )
    parser.add_argument(
        "--baud",
        type=int,
        help="Override serial baudrate for UART-based flashers",
    )
    parser.add_argument(
        "--offset",
        type=int,
        default=0,
        help="Byte offset for flashing",
    )
    parser.add_argument(
        "--unprotect",
        action="store_true",
        default=False,
        help="Remove flash read/write protection before programming",
    )
    parser.add_argument(
        "--bitbang",
        action="store_true",
        default=False,
        help="Enable UART bitbang mode",
    )
    parser.add_argument(
        "--try_apshutdown",
        action="store_true",
        default=False,
        help=(
            "Attempt to run 'apshutdown' on the EC console before and after "
            "flashing"
        ),
    )
    parser.add_argument(
        "--try_apshutdown_count",
        type=int,
        default=16,
        help="Number of times to send 'apshutdown' if --try_apshutdown is set",
    )
    parser.add_argument(
        "--mchp_loader",
        type=str,
        help="Path to the Microchip second_loader_fw.bin",
    )
    parser.add_argument(
        "--ocd_chip_cfg",
        type=str,
        help="OpenOCD target chip configuration filename",
    )
    parser.add_argument(
        "--ocd_cmds",
        type=str,
        help="Custom OpenOCD command sequence",
    )

    return parser.parse_args(argv)


def main(argv: Optional[List[str]] = None) -> int:
    """Main entry point for the Python EC flasher."""
    args = parse_args(argv)
    setup_logging(args.verbose)

    try:
        if not args.read and not args.image:
            raise ValueError("--image is required unless --read is given")

        need_servo = True
        if args.chip == "stm32_dfu" or args.interface == "dfu":
            need_servo = False

        servo = None
        if need_servo:
            servo = ServoDriver(
                host=args.host, port=args.port, dry_run=args.dry_run
            )

        flasher = FlasherFactory.create(servo, args)
        flasher.run()
        return 0
    except (
        ServoError,
        RuntimeError,
        OSError,
        ValueError,
        NotImplementedError,
        subprocess.SubprocessError,
    ) as err:
        logging.error("Flash operation failed: %s", err)
        if args.verbose:
            logging.exception("Detailed traceback:")
        return 1


if __name__ == "__main__":
    sys.exit(main())
