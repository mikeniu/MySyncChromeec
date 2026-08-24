#!/usr/bin/env vpython3

"""RTK UART flasher implementations."""

# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
import os
import shutil
import subprocess
import tempfile
import time

from util.flash.flasher_base import BaseEcFlasher
from util.flash.helpers import EC_DIR
from util.flash.helpers import locate_tool
from util.flash.helpers import preserve_sections_with_cur_image


class RtkFlasher(BaseEcFlasher):
    """RTK Flasher implementation."""

    def prepare_dut(self):
        self._logger.info("Preparing DUT for RTK UART flashing")

        servo_type = self.servo.get("servo_type", "") if self.servo else ""
        if "ccd_cr50" in servo_type:
            try:
                self.servo.set("ccd_keepalive_en", "on")
            except Exception:  # pylint: disable=broad-exception-caught
                pass

        try:
            self.servo.set("gsc_ec_reset", "on")
            time.sleep(0.1)

            # ec_enable_boot0 "uut"
            try:
                self.servo.set("ccd_ec_boot_mode_uut", "on")
            except Exception:  # pylint: disable=broad-exception-caught
                self.servo.set("fw_up", "on")

            time.sleep(1)
            self.servo.set("gsc_ec_reset", "off")
            time.sleep(1)

            # ec_disable_boot0 "uut"
            try:
                self.servo.set("ccd_ec_boot_mode_uut", "off")
            except Exception:  # pylint: disable=broad-exception-caught
                self.servo.set("fw_up", "off")

            time.sleep(1)
        except Exception:  # pylint: disable=broad-exception-caught
            self._logger.warning("Error during RTK preparation")

    def execute_flash(self):
        img_path = getattr(self.args, "image", None)
        read_path = getattr(self.args, "read", None)
        if not read_path and not os.path.exists(img_path):
            raise FileNotFoundError(f"Image not found: {img_path}")
        rtk_uut = locate_tool("uartupdatetool")
        ec_uart = (
            self.servo.get("ec_uart_pty") if self.servo else "/dev/ttyUSB0"
        )

        # Remove "/dev/" because uartupdatetool will add it.
        ec_uart_clean = ec_uart[5:] if ec_uart.startswith("/dev/") else ec_uart

        # WP disable logic
        uut_wp = [
            rtk_uut,
            "--method=wp",
            "--protect=0",
            f"--uart_device={ec_uart_clean}",
        ]
        if getattr(self.args, "dry_run", False):
            self._logger.info("DRY RUN TOOL: %s", " ".join(uut_wp))
            return

        self._logger.info("Disabling write protect: %s", " ".join(uut_wp))

        # Write protect twice as in bash script
        subprocess.run(uut_wp)  # pylint: disable=subprocess-run-check
        subprocess.run(uut_wp)  # pylint: disable=subprocess-run-check

        # Look for rts5915_flash_upload.bin
        mon = ""

        ec_dir = EC_DIR

        search_dirs = [
            os.path.dirname(img_path) if img_path else "",
            os.path.join(
                ec_dir, "build", "zephyr", "rtk_flame", "build-singleimage"
            ),
            "/usr/share/ec-devutils",
        ]
        for d in search_dirs:
            pot = os.path.join(d, "rts5915_flash_upload.bin")
            if os.path.exists(pot):
                mon = pot
                break

        if not mon:
            raise FileNotFoundError("Failed to find rts5915_flash_upload.bin")

        self._logger.info("Using RTK monitor image : %s", mon)

        uut_mon = [
            rtk_uut,
            "--method=frame",
            f"--file={mon}",
            f"--uart_device={ec_uart_clean}",
        ]
        self._logger.info("Loading monitor binary: %s", " ".join(uut_mon))
        subprocess.run(uut_mon, check=True)

        if read_path:
            self._logger.info("Reading RTK image: %s", read_path)
            uut_rd = [
                rtk_uut,
                "--method=read_bin",
                "--spi_start=0x00000000",
                f"--file={read_path}",
                f"--uart_device={ec_uart_clean}",
                "--bin_length=0x100000",
            ]
            subprocess.run(uut_rd, check=True)
            return

        with tempfile.TemporaryDirectory() as temp_dir:
            temp_img = img_path
            if not getattr(self.args, "no_preserve", False):
                self._logger.info("Preserving sections...")
                img_cpy = os.path.join(temp_dir, "ec.new.cpy.bin")
                shutil.copy(img_path, img_cpy)
                temp_img = img_cpy

                cur_img = os.path.join(temp_dir, "ec.cur.bin")
                uut_rd = [
                    rtk_uut,
                    "--method=read_bin",
                    "--spi_start=0x0",
                    f"--file={cur_img}",
                    "--bin_length=0x100000",
                    f"--uart_device={ec_uart_clean}",
                ]
                self._logger.info("Reading RTK image: %s", " ".join(uut_rd))
                subprocess.run(uut_rd, check=True)

                preserve_sections_with_cur_image(cur_img, img_cpy, temp_dir)

            uut_wr = [
                rtk_uut,
                "--method=flash",
                "--spi_start=0x00000000",
                f"--file={temp_img}",
                f"--uart_device={ec_uart_clean}",
            ]

            self._logger.info("Writing RTK image: %s", " ".join(uut_wr))
            subprocess.run(uut_wr, check=True)

    def cleanup_dut(self):
        self._logger.info("Cleaning up DUT after RTK")
