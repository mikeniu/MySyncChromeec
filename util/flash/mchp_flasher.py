#!/usr/bin/env vpython3

"""Module docstring."""

# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
import os
import subprocess
import time

from util.flash.flasher_base import BaseEcFlasher
from util.flash.helpers import EC_DIR


class MchpFlasher(BaseEcFlasher):
    """Docstring."""

    def __init__(self, servo, args):
        super().__init__(servo, args)
        self.ec_uart_prefix = "ec"

    def prepare_dut(self):
        self._logger.info("Preparing DUT for MCHP UART flashing")
        try:
            self.ec_uart_prefix = self.servo.get("ec_uart_prefix", "ec")
            self.servo.set("ec_ec3po_interp_connect", "off")
            self.servo.set("cold_reset", "on")
            self.servo.set("fw_up", "on")
            self.servo.set("cold_reset", "off")
            time.sleep(1)
        except Exception as e:  # pylint: disable=broad-exception-caught
            self._logger.warning("Error during MCHP preparation: %s", e)

    def execute_flash(self):
        img_path = getattr(self.args, "image", None)
        if not img_path or not os.path.exists(img_path):
            raise FileNotFoundError(f"Image not found: {img_path}")

        mchp_loader = getattr(self.args, "mchp_loader", None)
        if not mchp_loader or not os.path.exists(mchp_loader):
            ec_dir = EC_DIR
            board = getattr(self.args, "board", "")

            search_dirs = [
                os.path.dirname(img_path),
                os.path.join(ec_dir, "build", board, "output"),
                os.path.join(ec_dir, "build", "zephyr", board, "output"),
            ]
            for d in search_dirs:
                pot = os.path.join(d, "second_loader_fw.bin")
                if os.path.exists(pot):
                    mchp_loader = pot
                    break

        if not mchp_loader or not os.path.exists(mchp_loader):
            raise FileNotFoundError("MCHP second loader image not found!")

        crisis_tool = os.path.abspath(
            os.path.join(
                EC_DIR,
                "util",
                "mchp",
                "crisis_rcvry_util.py",
            )
        )

        ec_uart = (
            self.servo.get("ec_uart_pty") if self.servo else "/dev/ttyUSB0"
        )

        try:
            sec_load_baudrate = 9600
            self._logger.info(  # pylint: disable=logging-fstring-interpolation
                f"Using {sec_load_baudrate} baudrate for second loader"
            )
            if self.servo:
                self.servo.set(
                    f"{self.ec_uart_prefix}_baudrate", str(sec_load_baudrate)
                )
                self.servo.set(f"{self.ec_uart_prefix}_parity", "none")

            subprocess.run(
                ["python3", crisis_tool, "-s", mchp_loader, "-p", ec_uart],
                check=True,
            )

            image_load_baudrate = 57600
            self._logger.info(  # pylint: disable=logging-fstring-interpolation
                f"Using {image_load_baudrate} baudrate for image load"
            )
            if self.servo:
                self.servo.set(
                    f"{self.ec_uart_prefix}_baudrate", str(image_load_baudrate)
                )

            subprocess.run(
                ["python3", crisis_tool, "-e", img_path, "-p", ec_uart],
                check=True,
            )

        finally:
            if self.servo:
                self.servo.set(f"{self.ec_uart_prefix}_baudrate", "115200")

    def cleanup_dut(self):
        self._logger.info("Cleaning up DUT after MCHP")
        try:
            self.servo.set("cold_reset", "on")
            self.servo.set("fw_up", "off")
            self.servo.set("cold_reset", "off")
            self.servo.set("ec_ec3po_interp_connect", "on")
        except Exception as e:  # pylint: disable=broad-exception-caught
            self._logger.warning("Error during MCHP cleanup: %s", e)
