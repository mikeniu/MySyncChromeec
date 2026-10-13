# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Factory for instantiating EC flasher implementations."""

import argparse
import logging
import os
import re
from typing import Optional, Type

from flash.board_map import get_chip_from_board
from flash.flasher_base import BaseEcFlasher
from flash.flashrom_flasher import FlashromFlasher
from flash.ite_flasher import IteI2cFlasher
from flash.ite_flasher import IteUartFlasher
from flash.mchp_flasher import MchpFlasher
from flash.npcx_flasher import NpcxJtagFlasher
from flash.npcx_flasher import NpcxSpiFlasher
from flash.npcx_flasher import NpcxUutFlasher
from flash.servo_utils import ServoDriver
from flash.servo_utils import ServoError
from flash.stm32_flasher import Stm32DfuFlasher
from flash.stm32_flasher import Stm32UartFlasher


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
                    for regex in (zboard_regex, chip_regex):
                        sub_match = regex.search(block)
                        if sub_match:
                            return sub_match.group(1).split("/")[0]
        except OSError:
            pass
    return None


class FlasherFactory:
    """Factory to instantiate the appropriate BaseEcFlasher."""

    @staticmethod
    def _resolve_chip(
        servo: Optional[ServoDriver], args: argparse.Namespace
    ) -> str:
        """Resolve the lowercase EC chip name from args, servo, or board."""
        chip = args.chip

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
        return chip

    @staticmethod
    def create(
        servo: Optional[ServoDriver], args: argparse.Namespace
    ) -> BaseEcFlasher:
        """Create a BaseEcFlasher matching the target chip and interface."""
        chip = FlasherFactory._resolve_chip(servo, args)
        interface = args.interface
        flasher_cls: Optional[Type[BaseEcFlasher]] = None

        if interface == "dfu" or chip == "stm32_dfu":
            flasher_cls = Stm32DfuFlasher
        elif interface == "uart" and chip.startswith("it8"):
            flasher_cls = IteUartFlasher
        elif interface == "spi":
            if chip.startswith("npcx"):
                flasher_cls = NpcxSpiFlasher
            else:
                flasher_cls = FlashromFlasher
        elif chip in ("ite_spi", "stm32_spi", "mchp_int_spi"):
            flasher_cls = FlashromFlasher
        elif chip in ("it8xxx2", "ite_spi_ccd_i2c", "ite_spi_ccd_uart"):
            servo_type = servo.get("servo_type", default="") if servo else ""
            if "servo_micro" in servo_type or "c2d2" in servo_type:
                flasher_cls = FlashromFlasher
            elif chip == "ite_spi_ccd_uart":
                flasher_cls = IteUartFlasher
            else:
                flasher_cls = IteI2cFlasher
        elif chip in ("it83xx", "ite_i2c", "ite_i2c_usbc_charging"):
            flasher_cls = IteI2cFlasher
        elif chip in ("ite_uart", "ite_uart_3m", "it8320"):
            flasher_cls = IteUartFlasher
        elif chip == "npcx_uut":
            flasher_cls = NpcxUutFlasher
        elif chip in ("npcx_jtag", "npcx_5m5g_jtag", "5m5g_jtag"):
            flasher_cls = NpcxJtagFlasher
        elif chip.startswith("npcx"):
            flasher_cls = NpcxSpiFlasher
        elif chip.startswith("stm32") or chip.startswith("google_"):
            flasher_cls = Stm32UartFlasher
        elif chip.startswith("mchp") or chip.startswith("mec"):
            flasher_cls = MchpFlasher

        if flasher_cls is not None:
            return flasher_cls(servo, args)

        raise NotImplementedError(
            f"Flasher for chip '{chip}' (interface='{interface}') "
            "is not yet implemented."
        )
