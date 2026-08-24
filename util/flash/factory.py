#!/usr/bin/env vpython3

"""Factory module for instantiating correct EC flasher classes."""

# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
import logging
import os
import re

from util.flash.board_map import get_chip_from_board
from util.flash.flasher_base import BaseEcFlasher
from util.flash.flashrom_flasher import FlashromFlasher
from util.flash.ite_flasher import IteI2cFlasher
from util.flash.ite_flasher import IteUartFlasher
from util.flash.mchp_flasher import MchpFlasher
from util.flash.npcx_flasher import NpcxJtagFlasher
from util.flash.npcx_flasher import NpcxSpiFlasher
from util.flash.npcx_flasher import NpcxUutFlasher
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
        with open(build_py, "r", encoding="utf-8") as f:
            content = f.read()
            chunks = content.split("register_")
            for chunk in chunks[1:]:
                proj_match = re.search(
                    r'project_name\s*=\s*["\']([^"\']+)["\']', chunk
                )
                if not proj_match or proj_match.group(1) != board_name:
                    continue

                logger.debug(
                    "Matches dynamically localized Zephyr config for %s.",
                    board_name,
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
    """Factory for instantiating correct EC Flasher instance."""

    @staticmethod
    def create(servo, args) -> BaseEcFlasher:
        # pylint: disable=too-many-return-statements
        """Instantiates the correct EC Flasher based on CLI params and Servod probes."""
        logger = logging.getLogger("FlasherFactory")

        # 1. Start with Override Chip
        chip = args.chip if hasattr(args, "chip") and args.chip else ""

        # 2. Try Servod Detection if missing
        if not chip:
            try:
                chip = servo.get("ec_chip")
                if chip and chip != "unknown":
                    logger.info("EC Chip detected via servo: %s", chip)
            except Exception:  # pylint: disable=broad-exception-caught
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
                    "EC Chip deduced from ec_source tree: %s",
                    dynamic_chip,
                )
                chip = dynamic_chip

        # 4. Try hardcoded cache as final fallback (board_map.py)
        if not chip or chip == "unknown":
            logger.info("Falling back to static board mapping cache...")
            chip = get_chip_from_board(args.board)
            if chip:
                logger.info(
                    "EC Chip inferred from hardcoded board map: %s",
                    chip,
                )

        if not chip:
            logger.error(
                "Unable to determine EC chip. Please specify using --chip."
            )
            raise ValueError("Undefined EC Chip.")

        # Normalize common chips safely without stripping crucial topology
        chip = chip.lower()
        if chip in ["ite", "it8320"]:
            chip = "it8320"

        args.chip = chip

        # --- ROUTING LOGIC based on 100% parity map ---

        # Interface overrides
        if args.interface == "dfu":
            return Stm32DfuFlasher(servo, args)
        if args.interface == "uart" and "it8" in chip:
            return IteUartFlasher(servo, args)
        if args.interface == "spi":
            if "npcx" in chip:
                return NpcxSpiFlasher(servo, args)
            return FlashromFlasher(servo, args)

        # Standard Routing
        if chip in ["it8xxx2", "it83xx"]:
            servo_type = servo.get("servo_type", "") if servo else ""
            if "servo_micro" in servo_type or "servo_v2" in servo_type:
                return FlashromFlasher(servo, args)
            return IteI2cFlasher(servo, args)

        if chip in ["ite_i2c", "ite_spi_ccd_i2c"]:
            return IteI2cFlasher(servo, args)
        if "it8320" in chip or "ite_uart" in chip or "ite_spi_ccd_uart" in chip:
            return IteUartFlasher(servo, args)
        if "stm32_dfu" in chip:
            return Stm32DfuFlasher(servo, args)
        if "stm32" in chip or "google_" in chip:
            return Stm32UartFlasher(servo, args)
        if "npcx_uut" in chip:
            return NpcxUutFlasher(servo, args)
        if "npcx_jtag" in chip or "5m5g_jtag" in chip:
            return NpcxJtagFlasher(servo, args)
        if "npcx" in chip:
            return NpcxSpiFlasher(servo, args)
        if "openocd" in chip:
            return OpenOcdFlasher(servo, args)
        if "mchp" in chip or "mec" in chip:
            return MchpFlasher(servo, args)
        if "rtk" in chip or "realtek" in chip:
            return RtkFlasher(servo, args)
        if "spi" in chip or chip in ["raiden"]:
            # Fallback to flashrom for SPI interfaces not specifically handled
            return FlashromFlasher(servo, args)

        raise ValueError(
            f"Flashing strategy for chip={chip} (interface={args.interface}) "
            "is not valid or supported."
        )
