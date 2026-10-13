# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Factory for instantiating EC flasher implementations."""

import argparse
import logging
import os
import re
from typing import Optional

from flash.board_map import get_chip_from_board
from flash.flasher_base import BaseEcFlasher
from flash.servo_utils import ServoDriver
from flash.servo_utils import ServoError


logger = logging.getLogger(__name__)


def query_ec_source_tree(board: str, ec_dir: str) -> Optional[str]:
    """Scan zephyr/program/*/BUILD.py to deduce the chip for a board."""
    prog_dir = os.path.join(ec_dir, "zephyr", "program")
    if not os.path.isdir(prog_dir):
        return None

    proj_regex = re.compile(
        r"register_.*?_project\s*\([^)]*?project_name\s*=\s*['\"]"
        + re.escape(board)
        + r"['\"][^)]*?\)",
        re.DOTALL,
    )
    zboard_regex = re.compile(r"zephyr_board\s*=\s*['\"]([^'\"]+)['\"]")
    chip_regex = re.compile(r"chip\s*=\s*['\"]([^'\"]+)['\"]")

    for root_dir in sorted(os.listdir(prog_dir)):
        build_py = os.path.join(prog_dir, root_dir, "BUILD.py")
        if not os.path.exists(build_py):
            continue
        try:
            with open(build_py, "r", encoding="utf-8") as file_obj:
                content = file_obj.read()
                match = proj_regex.search(content)
                if match:
                    block = match.group(0)
                    zb_match = zboard_regex.search(block)
                    if zb_match:
                        return zb_match.group(1).split("/")[0]
                    c_match = chip_regex.search(block)
                    if c_match:
                        return c_match.group(1).split("/")[0]
        except OSError:
            pass
    return None


class FlasherFactory:
    """Factory to instantiate the appropriate BaseEcFlasher."""

    @staticmethod
    def create(
        servo: Optional[ServoDriver], args: argparse.Namespace
    ) -> BaseEcFlasher:
        """Create a BaseEcFlasher matching the target chip and interface."""
        chip = args.chip
        interface = args.interface

        if not chip and servo:
            try:
                chip = servo.get("ec_chip")
                logger.info("Detected EC chip from servo: %s", chip)
            except ServoError as err:
                logger.debug(
                    "Could not dynamically query ec_chip from servo: %s",
                    err,
                )

        if (
            (not chip or "unknown" in chip.lower())
            and args.board
            and getattr(args, "ec_source", None)
        ):
            chip = query_ec_source_tree(args.board, args.ec_source)
            if chip:
                logger.info(
                    "Deduced EC chip '%s' for board '%s' via EC source tree.",
                    chip,
                    args.board,
                )

        if (not chip or "unknown" in chip.lower()) and args.board:
            chip = get_chip_from_board(args.board)
            if chip:
                logger.info(
                    "Deduced legacy EC chip '%s' from board '%s'",
                    chip,
                    args.board,
                )

        if not chip or "unknown" in chip.lower():
            raise ValueError(
                f"Could not determine EC chip for board '{args.board}'. "
                "Ensure servod is running or explicitly specify --chip."
            )

        chip = chip.lower()
        args.chip = chip
        raise NotImplementedError(
            f"Flasher for chip '{chip}' (interface='{interface}') "
            "is not yet implemented."
        )
