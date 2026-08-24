#!/usr/bin/env vpython3
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
import os
import subprocess

from .flasher_base import BaseEcFlasher


class OpenOcdFlasher(BaseEcFlasher):
    """
    Flasher implementation utilizing OpenOCD interface.
    """

    def prepare_dut(self):
        """Set up JTAG buffers for OpenOCD flashing."""
        self._logger.info("Enabling JTAG buffers for OpenOCD...")
        self.servo.set("jtag_buf_on_flex_en", "on")
        self.servo.set("jtag_buf_en", "on")

    def execute_flash(self):
        """Invoke openocd to flash the device."""
        self._logger.info(f"Flashing via OpenOCD with image {self.args.image}")

        ocd_cfg = "servo.cfg"
        # Calculate EC directory
        ec_dir = os.path.abspath(
            os.path.join(os.path.dirname(__file__), "..", "..")
        )
        ocd_path = os.path.join(ec_dir, "util", "openocd")

        if not os.path.exists(ocd_path):
            if os.path.exists("/usr/share/ec-devutils"):
                ocd_path = "/usr/share/ec-devutils"
            else:
                raise FileNotFoundError(
                    f"Cannot locate openocd configs at {ocd_path} or /usr/share/ec-devutils"
                )

        ocd_chip_cfg = getattr(self.args, "ocd_chip_cfg", "stm32f1x.cfg")
        timeout_sec = getattr(self.args, "timeout", 600)

        # Allow passing custom openocd commands or construct default
        ocd_cmds = getattr(self.args, "ocd_cmds", None)
        if not ocd_cmds:
            offset = getattr(self.args, "offset", "0")
            ocd_cmds = f"init; reset halt; flash write_image erase {self.args.image} {offset}; reset run; shutdown"
        else:
            ocd_cmds = ocd_cmds.replace("{image}", self.args.image).replace(
                "{offset}", str(getattr(self.args, "offset", "0"))
            )

        cmd = [
            "sudo",
            "timeout",
            "-k",
            "10",
            "-s",
            "9",
            str(timeout_sec),
            "openocd",
            "-s",
            ocd_path,
            "-f",
            ocd_cfg,
            "-f",
            ocd_chip_cfg,
            "-c",
            ocd_cmds,
        ]

        if getattr(self.args, "dry_run", False):
            self._logger.info(f"DRY RUN: {' '.join(cmd)}")
        else:
            try:
                subprocess.run(cmd, check=True)
            except subprocess.CalledProcessError as e:
                self._logger.error(f"OpenOCD programming failed: {e}")
                raise RuntimeError(
                    f"Failed to program {self.args.image}"
                ) from e

    def cleanup_dut(self):
        """Restore default JTAG buffer states."""
        self._logger.info("Restoring JTAG buffers...")
        self.servo.set("jtag_buf_on_flex_en", "off")
        self.servo.set("jtag_buf_en", "off")
