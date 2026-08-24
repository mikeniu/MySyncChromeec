#!/usr/bin/env vpython3

"""Module docstring."""

# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
import subprocess

from util.flash.flasher_base import BaseEcFlasher


class Stm32UartFlasher(BaseEcFlasher):
    """STM32 UART Flasher using stm32mon."""

    def prepare_dut(self):
        self._logger.info("Preparing STM32 DUT (Cold Reset + Boot0 asserts).")
        if "c2d2" in self.servo.get("servo_type"):
            self.servo.set("gsc_ec_reset", "on")
            self.servo.set("ec_boot_mode", "uut")
            self.servo.set("gsc_ec_reset", "off")
        else:
            self.servo.set("ec_boot_mode", "uut")
            self.servo.set("cold_reset", "on")
            self.servo.set("cold_reset", "off")

    def execute_flash(self):
        pty = self.servo.get("ec_uart_pty")

        read_path = getattr(self.args, "read", None)
        if read_path:
            cmd = ["stm32mon", "-d", pty, "-U", "-r", read_path]
            if self.args.dry_run:
                self._logger.info("DRY RUN TOOL: %s", " ".join(cmd))
                return
            subprocess.run(cmd, check=True)
            return

        cmd = ["stm32mon", "-d", pty, "-U", "-u", "-e", "-w", self.args.image]
        if self.args.dry_run:
            self._logger.info("DRY RUN TOOL: %s", " ".join(cmd))
            return

        subprocess.run(cmd, check=True)

    def cleanup_dut(self):
        self._logger.info("Cleaning up STM32 DUT state.")
        self.servo.set("ec_boot_mode", "normal")
        self.servo.set("cold_reset", "on")
        self.servo.set("cold_reset", "off")


class Stm32DfuFlasher(BaseEcFlasher):
    """STM32 DFU Flasher using dfu-util."""

    def prepare_dut(self):
        self._logger.info(
            "Executing DFU transition for STM32 (Not implemented precisely)."
        )

    def execute_flash(self):
        cmd = [
            "dfu-util",
            "-a",
            "0",
            "-s",
            "0x08000000:leave",
            "-D",
            self.args.image,
        ]
        if self.args.dry_run:
            self._logger.info("DRY RUN TOOL: %s", " ".join(cmd))
            return
        subprocess.run(cmd, check=True)

    def cleanup_dut(self):
        pass
