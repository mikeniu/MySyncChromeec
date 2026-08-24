#!/usr/bin/env vpython3
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
import subprocess
from .flasher_base import BaseEcFlasher

class IteUartFlasher(BaseEcFlasher):
    """ITE DBGR UART Flasher implementation."""
    
    def prepare_dut(self):
        self._logger.info("Asserting resets for ITE UART flash.")
        self.servo.set("cold_reset", "on")
        self.servo.set("ec_uart_en", "on")
        
        # In reality, this checks args for overrides
        if self.args.baud:
            self._logger.info(f"Overriding baud rate to {self.args.baud}")
            self.servo.set("ec_uart_baudrate", self.args.baud)

        if "ccd" in self.servo.get("servo_type", ""):
            self._logger.info("Asserting sysrst to suppress AP I2C collision before DBGR mode...")
            try:
                self.servo.set("cr50_uart_cmd", "sysrst assert")
            except Exception:
                pass
            try:
                self.servo.set("gsc_uart_cmd", "sysrst assert")
            except Exception:
                pass
            import time
            time.sleep(1)
            
            self._logger.info("Asking CR50 to send the dbgr special waveform to ITE chip")
            time.sleep(2)
            try:
                self.servo.set("cr50_i2c_ctrl", "ite_debugger_mode")
            except Exception:
                pass
            time.sleep(8)

    def execute_flash(self):
        pty = self.servo.get("ec_uart_pty")
        baud = self.args.baud if self.args.baud else "115200"
        
        cmd = ["itecomdbgr", "-d", pty, "-f", self.args.image, "-b", baud]
        
        if self.args.dry_run:
            self._logger.info(f"DRY RUN TOOL: {' '.join(cmd)}")
            return
            
        self._logger.info(f"Executing: {' '.join(cmd)}")
        subprocess.run(cmd, check=True)

    def cleanup_dut(self):
        self._logger.info("Cleaning up ITE UART state.")
        self.servo.set("cold_reset", "off")
        
        if "ccd" in self.servo.get("servo_type", ""):
            self._logger.info("Deasserting sysrst after DBGR flash...")
            try:
                self.servo.set("cr50_uart_cmd", "sysrst deassert")
            except Exception:
                pass
            try:
                self.servo.set("gsc_uart_cmd", "sysrst deassert")
            except Exception:
                pass
            import time
            time.sleep(1)
