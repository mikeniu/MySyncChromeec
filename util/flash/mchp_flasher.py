# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Microchip MEC UART crisis recovery EC flasher implementation."""

import logging
import os
import subprocess
import sys

from flash.flasher_base import BaseEcFlasher
from flash.helpers import EC_DIR
from flash.servo_utils import ServoError


logger = logging.getLogger(__name__)


class MchpFlasher(BaseEcFlasher):
    """Flasher for Microchip MEC chips over UART crisis recovery."""

    def _is_ccd(self) -> bool:
        """Return True if the active servo interface is CCD."""
        if not self.servo:
            return False
        return "ccd" in self.servo.get("servo_type", default="")

    def _find_loader_image(self) -> str:
        """Locate the Microchip second_loader_fw.bin image."""
        if getattr(self.args, "mchp_loader", None):
            if os.path.exists(self.args.mchp_loader):
                return self.args.mchp_loader
            raise FileNotFoundError(
                f"Specified MCHP loader image not found: {self.args.mchp_loader}"
            )

        board = getattr(self.args, "board", "")
        img_dir = (
            os.path.dirname(os.path.abspath(self.args.image))
            if self.args.image
            else ""
        )

        search_dirs = [img_dir]
        if board:
            search_dirs.extend(
                [
                    os.path.join(EC_DIR, "build", board, "output"),
                    os.path.join(EC_DIR, "build", "zephyr", board, "output"),
                    f"/build/{board}/firmware",
                ]
            )

        for directory in search_dirs:
            if not directory:
                continue
            cand = os.path.join(directory, "second_loader_fw.bin")
            if os.path.isfile(cand):
                logger.info("Using MCHP loader: %s", cand)
                return cand

        raise FileNotFoundError(
            "No MCHP loader image (second_loader_fw.bin) found. "
            "Build one or specify --mchp_loader."
        )

    def prepare_dut(self) -> None:
        """Enter MCHP UART bootloader mode and save UART settings."""
        if not self.servo:
            return
        logger.info("Using MCHP serial bootloader")
        try:
            self.servo.save_state("ec_ec3po_interp_connect")
            self.servo.set("ec_ec3po_interp_connect", "off")
        except ServoError:
            pass

        self.servo.save_state("ec_uart_baudrate")
        self.servo.save_state("ec_uart_parity")
        self.servo.set("ec_uart_parity", "none")

        self.servo.set_many(
            [("cold_reset", "on"), ("fw_up", "on"), ("cold_reset", "off")]
        )

    def cleanup_dut(self) -> None:
        """Release fw_up and pulse cold reset to boot the updated firmware."""
        if not self.servo:
            return
        self.servo.set_many(
            [("cold_reset", "on"), ("fw_up", "off"), ("cold_reset", "off")]
        )

    def execute_flash(self) -> None:
        """Execute crisis_rcvry_util.py in two stages to program the MCHP EC."""
        if not self.servo:
            raise ValueError("Servo required for MCHP UART")

        if self.args.read:
            raise NotImplementedError(
                "Reading is not supported over MCHP UART."
            )

        if not self.args.image or not os.path.exists(self.args.image):
            raise FileNotFoundError(
                f"Firmware image not found: {self.args.image}"
            )

        loader = self._find_loader_image()
        pty = self.servo.get("raw_ec_uart_pty", default="")
        if not pty or "not_applicable" in pty:
            pty = self.servo.get("ec_uart_pty")

        crisis_tool = os.path.join(
            EC_DIR, "util", "mchp", "crisis_rcvry_util.py"
        )
        if not os.path.exists(crisis_tool):
            raise FileNotFoundError(
                f"MCHP crisis recovery tool not found at {crisis_tool}"
            )

        self.servo.set("ec_uart_baudrate", "9600")
        if self._is_ccd():
            self.servo.save_state("ec_uart_bitbang_en")
            self.servo.set("ec_uart_bitbang_en", "on")

        loader_cmd = [sys.executable, crisis_tool, "-s", loader, "-p", pty]
        logger.info(
            "Loading MCHP second loader into SRAM: %s",
            " ".join(loader_cmd),
        )
        if not self.args.dry_run:
            subprocess.run(loader_cmd, check=True)

        self.servo.set("ec_uart_baudrate", "57600")
        if self._is_ccd():
            # Changing ec_uart_baudrate on CCD resets the bitbang state.
            self.servo.set("ec_uart_bitbang_en", "on")

        flash_cmd = [
            sys.executable,
            crisis_tool,
            "-e",
            self.args.image,
            "-p",
            pty,
        ]
        logger.info("Programming MCHP EC: %s", " ".join(flash_cmd))
        if not self.args.dry_run:
            subprocess.run(flash_cmd, check=True)
