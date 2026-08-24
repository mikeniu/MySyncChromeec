#!/usr/bin/env vpython3
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
import subprocess
from .flasher_base import BaseEcFlasher

class NpcxSpiFlasher(BaseEcFlasher):
    """Nuvoton direct SPI Flasher implementation via flashrom."""
    
    def prepare_dut(self):
        self._logger.info("Setting up NPCX SPI buffers.")
        self.servo.set("cold_reset", "on")
        self.servo.set("spi2_vref", "pp3300")
        self.servo.set("spi2_buf_en", "on")

    def execute_flash(self):
        cmd = ["flashrom", "-p", "raiden_debug_spi:target=AP", "-w", self.args.image]
        if self.args.dry_run:
            self._logger.info(f"DRY RUN TOOL: {' '.join(cmd)}")
            return
            
        self._logger.info(f"Executing: {' '.join(cmd)}")
        subprocess.run(cmd, check=True)

    def cleanup_dut(self):
        self._logger.info("Cleaning up NPCX SPI state.")
        self.servo.set("spi2_buf_en", "off")
        self.servo.set("spi2_vref", "off")
        self.servo.set("cold_reset", "off")
