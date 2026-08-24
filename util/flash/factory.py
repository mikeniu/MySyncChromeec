#!/usr/bin/env vpython3
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
import logging
import os
import re

from util.flash.board_map import get_chip_from_board
from util.flash.flasher_base import BaseEcFlasher
from util.flash.flashrom_flasher import FlashromFlasher
from util.flash.ite_flasher import IteUartFlasher
from util.flash.mchp_flasher import MchpFlasher
from util.flash.npcx_flasher import NpcxSpiFlasher
from util.flash.openocd_flasher import OpenOcdFlasher
from util.flash.rtk_flasher import RtkFlasher
from util.flash.stm32_flasher import Stm32DfuFlasher
from util.flash.stm32_flasher import Stm32UartFlasher


def query_ec_source_tree(board_name, ec_source_path):
    """Dynamically parses zephyr BUILD.py files looking for the hardware chip."""
    logger = logging.getLogger("FlasherFactory")
    prog_dir = os.path.join(ec_source_path, "zephyr", "program")
    if not os.path.isdir(prog_dir):
        return None

    for prog in os.listdir(prog_dir):
        build_py = os.path.join(prog_dir, prog, "BUILD.py")
        if not os.path.exists(build_py):
            continue
        with open(build_py, "r") as f:
            content = f.read()
            chunks = content.split("register_")
            for chunk in chunks[1:]:
                proj_match = re.search(
                    r'project_name\s*=\s*["\']([^"\']+)["\']', chunk
                )
                if not proj_match or proj_match.group(1) != board_name:
                    continue

                logger.debug(
                    f"Matches dynamically localized Zephyr config for {board_name}."
                )
                chip = None
                zb_match = re.search(
                    r'zephyr_board\s*=\s*["\']([^"\']+)["\']', chunk
                )
                if zb_match:
                    chip = zb_match.group(1).split("/")[0]
                if not chip:
                    c_match = re.search(
                        r'chip\s*=\s*["\']([^"\']+)["\']', chunk
                    )
                    if c_match:
                        chip = c_match.group(1).split("/")[0]
                if chip:
                    return chip
    return None


class FlasherFactory:
    """Instantiates the correct EC Flasher based on CLI params and Servod probes."""

    @staticmethod
    def create(servo, args) -> BaseEcFlasher:
        logger = logging.getLogger("FlasherFactory")

        # 1. Start with Override Chip
        chip = args.chip if hasattr(args, "chip") and args.chip else ""

        # 2. Try Servod Detection if missing
        if not chip:
            try:
                chip = servo.get("ec_chip")
                if chip and chip != "unknown":
                    logger.info(f"EC Chip detected via servo: {chip}")
            except Exception:
                pass

        # 3. Dynamic Repository Crawl (Source of Truth)
        ec_source = getattr(args, "ec_source", None)
        if (not chip or chip == "unknown") and ec_source:
            logger.info(
                "Attempting to deduce chip from EC source repository..."
            )
            dynamic_chip = query_ec_source_tree(args.board, ec_source)
            if dynamic_chip:
                logger.info(
                    f"EC Chip deduced from ec_source tree: {dynamic_chip}"
                )
                chip = dynamic_chip

        # 4. Try hardcoded cache as final fallback (board_map.py)
        if not chip or chip == "unknown":
            logger.info("Falling back to static board mapping cache...")
            chip = get_chip_from_board(args.board)
            if chip:
                logger.info(
                    f"EC Chip inferred from hardcoded board map: {chip}"
                )

        if not chip:
            logger.error(
                "Unable to determine EC chip. Please specify using --chip."
            )
            raise ValueError("Undefined EC Chip.")

        # Normalize common chips
        chip_lower = chip.lower()
        if "it8" in chip_lower or "ite" in chip_lower:
            chip = "it8320"
        elif "npcx" in chip_lower:
            chip = "npcx"

        # --- ROUTING LOGIC based on 100% parity map ---

        # Interface overrides
        if args.interface == "dfu":
            return Stm32DfuFlasher(servo, args)
        if args.interface == "uart" and "it8" in chip:
            return IteUartFlasher(servo, args)
        if args.interface == "spi" and "npcx" in chip:
            return NpcxSpiFlasher(servo, args)

        # Standard Routing
        if "it8320" in chip:
            return IteUartFlasher(servo, args)
        elif "stm32_dfu" in chip:
            return Stm32DfuFlasher(servo, args)
        elif "stm32" in chip:
            return Stm32UartFlasher(servo, args)
        elif "npcx" in chip:
            return NpcxSpiFlasher(servo, args)
        elif "openocd" in chip:
            return OpenOcdFlasher(servo, args)
        elif "mchp" in chip:
            return MchpFlasher(servo, args)
        elif "rtk" in chip:
            return RtkFlasher(servo, args)
        elif "spi" in chip or args.interface == "spi":
            # Fallback to flashrom for SPI interfaces not specifically handled
            return FlashromFlasher(servo, args)

        raise ValueError(
            f"Flashing strategy for chip={chip} (interface={args.interface}) is not valid or supported."
        )
