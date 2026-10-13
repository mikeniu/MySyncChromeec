# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""OpenOCD JTAG EC flasher implementation."""

import logging
import os
import subprocess

from flash.flasher_base import BaseEcFlasher
from flash.helpers import EC_DIR


logger = logging.getLogger(__name__)


class OpenOcdFlasher(BaseEcFlasher):
    """Flasher for ECs over JTAG using OpenOCD."""

    def prepare_dut(self) -> None:
        """Enable JTAG buffers prior to OpenOCD flashing."""
        if not self.servo:
            return
        logger.info("Preparing DUT for OpenOCD flashing...")
        self.servo.save_state("jtag_buf_on_flex_en")
        self.servo.save_state("jtag_buf_en")
        self.servo.set("jtag_buf_on_flex_en", "on")
        self.servo.set("jtag_buf_en", "on")

    def cleanup_dut(self) -> None:
        """Disable JTAG buffers after OpenOCD flashing."""
        if not self.servo:
            return
        logger.info("Cleaning up DUT after OpenOCD flashing...")
        self.servo.set("jtag_buf_on_flex_en", "off")
        self.servo.set("jtag_buf_en", "off")

    def execute_flash(self) -> None:
        """Execute OpenOCD to write the EC firmware image."""
        if self.args.read:
            raise NotImplementedError(
                "Reading is not supported over OpenOCD JTAG."
            )

        if not self.args.image or not os.path.exists(self.args.image):
            raise FileNotFoundError(
                f"Firmware image not found: {self.args.image}"
            )

        logger.info("Flashing %s via OpenOCD...", self.args.image)

        ocd_path = os.path.join(EC_DIR, "util", "openocd")
        if not os.path.exists(ocd_path):
            ocd_path = "/usr/share/ec-devutils"
            if not os.path.exists(ocd_path):
                raise FileNotFoundError(
                    "Cannot find openocd configuration files in "
                    "util/openocd or /usr/share/ec-devutils"
                )

        ocd_chip_cfg = (
            getattr(self.args, "ocd_chip_cfg", None) or "stm32f1x.cfg"
        )
        ocd_cmds = getattr(self.args, "ocd_cmds", None)
        if not ocd_cmds:
            ocd_cmds = (
                "init; reset halt; "
                "flash write_image erase {image} {offset}; "
                "reset run; shutdown"
            )

        offset = getattr(self.args, "offset", "0")
        image = getattr(self.args, "image", "")
        formatted_cmds = ocd_cmds.replace("{image}", str(image)).replace(
            "{offset}", str(offset)
        )

        cmd = [
            "sudo",
            "timeout",
            "-k",
            "10",
            "-s",
            "9",
            str(getattr(self.args, "timeout", 600)),
            "openocd",
            "-s",
            ocd_path,
            "-f",
            "servo.cfg",
            "-f",
            ocd_chip_cfg,
            "-c",
            formatted_cmds,
        ]

        logger.info("Running command: %s", " ".join(cmd))
        if not getattr(self.args, "dry_run", False):
            try:
                subprocess.run(cmd, check=True)
            except subprocess.CalledProcessError as err:
                raise RuntimeError(
                    f"OpenOCD flashing failed with exit code {err.returncode}"
                ) from err
