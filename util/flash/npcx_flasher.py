#!/usr/bin/env vpython3

"""NPCX flasher implementations."""

# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
import os
import subprocess
import time

from util.flash.helpers import EC_DIR
from util.flash.helpers import locate_tool

from .flasher_base import BaseEcFlasher


class NpcxSpiFlasher(BaseEcFlasher):
    """Nuvoton direct SPI Flasher implementation via flashrom."""

    def prepare_dut(self):
        self._logger.info("Setting up NPCX SPI buffers.")
        self.servo.set("cold_reset", "on")
        self.servo.set("spi2_vref", "pp3300")
        self.servo.set("spi2_buf_en", "on")

    def execute_flash(self):
        cmd = [
            "flashrom",
            "-p",
            "raiden_debug_spi:target=AP",
            "-w",
            self.args.image,
        ]
        if self.args.dry_run:
            self._logger.info("DRY RUN TOOL: %s", " ".join(cmd))
            return

        self._logger.info("Executing: %s", " ".join(cmd))
        subprocess.run(cmd, check=True)

    def cleanup_dut(self):
        self._logger.info("Cleaning up NPCX SPI state.")
        self.servo.set("spi2_buf_en", "off")
        self.servo.set("spi2_vref", "off")
        self.servo.set("cold_reset", "off")


class NpcxUutFlasher(BaseEcFlasher):
    """NPCX UART Update Tool Flasher implementation."""

    def prepare_dut(self):
        self._logger.info("Setting up NPCX UUT state.")
        servo_type = self.servo.get("servo_type", "")

        if "ccd_cr50" in servo_type:
            try:
                self.servo.set("ccd_keepalive_en", "on")
            except Exception:  # pylint: disable=broad-exception-caught
                pass

        if "c2d2" in servo_type:
            self.servo.set("gsc_ec_reset", "on")
            self.servo.set("ec_boot_mode", "uut")
            self.servo.set("gsc_ec_reset", "off")
            self.servo.set("ec_boot_mode", "normal")
        else:
            self.servo.set("ec_boot_mode", "uut")
            self.servo.set("cold_reset", "on")
            self.servo.set("cold_reset", "off")
            time.sleep(0.1)
            self.servo.set("ec_boot_mode", "normal")
            time.sleep(0.1)

    def execute_flash(self):
        uut_tool = locate_tool("uartupdatetool")
        if not uut_tool:
            raise FileNotFoundError("no NPCX UART Update Tool found.")

        ec_uart = self.servo.get("ec_uart_pty", "")
        if ec_uart.startswith("/dev/"):
            ec_uart = ec_uart[5:]

        board = getattr(self.args, "board", "")

        mon_paths = [
            os.path.join(
                os.path.dirname(self.args.image),
                "chip/npcx/spiflashfw/npcx_monitor.bin",
            ),
            os.path.join(os.path.dirname(self.args.image), "npcx_monitor.bin"),
            os.path.join(
                EC_DIR, "build", board, "chip/npcx/spiflashfw/npcx_monitor.bin"
            ),
            "/usr/share/ec-devutils/npcx_monitor.bin",
        ]

        mon_bin = None
        for p in mon_paths:
            if os.path.exists(p):
                mon_bin = p
                break

        if not mon_bin:
            raise FileNotFoundError("Failed to find npcx_monitor.bin")

        self._logger.info("Using NPCX monitor image: %s", mon_bin)

        base_cmd = [uut_tool, f"--port={ec_uart}", "--baudrate=115200"]

        if self.args.dry_run:
            self._logger.info("DRY RUN TOOL: %s", " ".join(base_cmd))
            return

        if not getattr(self.args, "read", None):
            self._logger.info("Loading monitor binary.")
            mon_cmd = base_cmd + [
                "--opr=wr",
                "--addr=0x200C3020",
                f"--file={mon_bin}",
            ]
            subprocess.run(mon_cmd, check=True)

            # TODO: Add section preservation via preserve_sections if no_preserve=False

            wr_cmd = base_cmd + [
                "--auto",
                f"--offset={getattr(self.args, 'offset', '0')}",
                f"--file={self.args.image}",
            ]
            self._logger.info("Programming EC firmware image.")
            subprocess.run(wr_cmd, check=True)
        else:
            rd_cmd = base_cmd + ["--read-flash", f"--file={self.args.read}"]
            self._logger.info("Reading EC firmware image.")
            subprocess.run(rd_cmd, check=True)

    def cleanup_dut(self):
        pass


class NpcxJtagFlasher(BaseEcFlasher):
    """NPCX JTAG Flasher via OpenOCD."""

    def prepare_dut(self):
        self._logger.info("Setting up NPCX JTAG OpenOCD buffers.")
        self.servo.set("jtag_buf_on_flex_en", "on")
        self.servo.set("jtag_buf_en", "on")

    def execute_flash(self):
        self._logger.info("Executing NPCX JTAG OpenOCD sequence")
        ocd_path = "/usr/share/ec-devutils"
        if not os.path.exists(ocd_path):
            ocd_path = os.path.join(EC_DIR, "util", "openocd")

        cmd = [
            "openocd",
            "-s",
            ocd_path,
            "-f",
            "cros_ec_jtag.cfg",
            "-c",
            f"npcx_jtag_setup {self.args.image}",
        ]

        if self.args.dry_run:
            self._logger.info("DRY RUN TOOL: %s", " ".join(cmd))
            return

        subprocess.run(cmd, check=True)

    def cleanup_dut(self):
        self._logger.info("Restoring JTAG buffers.")
        self.servo.set("jtag_buf_on_flex_en", "off")
        self.servo.set("jtag_buf_en", "off")
