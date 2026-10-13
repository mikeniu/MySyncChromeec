# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""OpenOCD JTAG EC flasher implementation."""

import logging
import os
import shlex
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
        for ctrl in ("jtag_buf_on_flex_en", "jtag_buf_en"):
            if self.servo.has_control(ctrl):
                self.servo.save_state(ctrl)
                self.servo.set(ctrl, "on")

    def cleanup_dut(self) -> None:
        """Restore JTAG buffer controls (handled by ServoDriver.restore_state)."""

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

        ocd_chip_cfg = getattr(self.args, "ocd_chip_cfg", None)
        if not ocd_chip_cfg:
            raise ValueError("--ocd_chip_cfg is required for OpenOCD flashing")
        ocd_cmds = getattr(self.args, "ocd_cmds", None)
        if not ocd_cmds:
            ocd_cmds = (
                "init; reset halt; "
                "flash write_image erase {image} {offset}; "
                "reset run; shutdown"
            )

        offset_val = getattr(self.args, "offset", None)
        offset_str = str(offset_val) if offset_val is not None else "0"
        image_arg = str(self.args.image)
        # The path is embedded in a Tcl command, where a double quote would
        # end the quoted word early.
        if '"' in image_arg:
            raise ValueError(
                f"Image path must not contain double quotes: {image_arg}"
            )
        if " " in image_arg:
            image_arg = f'"{image_arg}"'
        formatted_cmds = ocd_cmds.replace("{image}", image_arg).replace(
            "{offset}", offset_str
        )

        timeout_val = getattr(self.args, "timeout", None)
        timeout_str = str(timeout_val) if timeout_val is not None else "600"
        cmd = [
            "sudo",
            "timeout",
            "-k",
            "10",
            timeout_str,
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

        logger.info("Running command: %s", shlex.join(cmd))
        if not getattr(self.args, "dry_run", False):
            try:
                subprocess.run(cmd, check=True)
            except subprocess.CalledProcessError as err:
                raise RuntimeError(
                    f"OpenOCD flashing failed with exit code {err.returncode}"
                ) from err
