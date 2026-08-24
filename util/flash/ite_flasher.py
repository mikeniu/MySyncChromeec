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
            self._logger.info(
                "Asserting sysrst to suppress AP I2C collision before DBGR mode..."
            )
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

            self._logger.info(
                "Asking CR50 to send the dbgr special waveform to ITE chip"
            )
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


class IteI2cFlasher(BaseEcFlasher):
    """ITE DBGR I2C Flasher implementation using iteflash."""

    def prepare_dut(self):
        self._logger.info("Preparing ITE I2C connections.")
        servo_type = self.servo.get("servo_type", "")
        if "c2d2" in servo_type:
            self.servo.set("gsc_ec_reset", "on")
            self.servo.set("i2c_ec_bus_speed", "400")
            self.servo.set("ec_flash_select", "on")

        if self.servo.get(
            "dut_i2c_mux", None
        ) is not None and "ERR" not in self.servo.get("dut_i2c_mux", ""):
            self._logger.info("Switching DUT I2C Mux")
            self.servo.set("dut_i2c_mux", "ec_prog")

        if self.servo.get(
            "dut_i2c_en", None
        ) is not None and "ERR" not in self.servo.get("dut_i2c_en", ""):
            self._logger.info("Enabling I2C programming")
            self.servo.set("dut_i2c_en", "on")

        if "c2d2" in servo_type:
            self.servo.set("gsc_ec_reset", "off")
        else:
            self.servo.set("cold_reset", "on")
            self.servo.set("cold_reset", "off")
        import time

        if "servo_v4" in servo_type and getattr(
            self.args, "usbc_charging", False
        ):

            self.servo.set("cold_reset", "on")

            self._logger.info("Disable CCD watchdog")

            self.servo.set("watchdog_remove", "ccd_cr50")

            self._logger.info("Switching servo_v4 to USB-C (non-PD) charging")

            self.servo.set("usbc_charging", "1A5")

            time.sleep(2)

            self._logger.info("Disconnect/reconnect CC lines")

            try:

                self.servo.set("servo_v4p1_uart_cmd", "cc off")

                self.servo.set("servo_v4p1_uart_cmd", "cc on")

            except Exception:

                pass

            self.servo.set("cold_reset", "off")

        import time

        # Implement ite_dfu_mode standard setup
        time.sleep(2)
        try:
            self.servo.set("cr50_i2c_ctrl", "ite_debugger_mode")
        except Exception:
            pass
        time.sleep(8)

        if "c2d2" in servo_type:
            try:
                self.servo.set("cr50_idle_level", "active")
            except Exception:
                pass

    def execute_flash(self):
        servo_type = self.servo.get("servo_type", "")

        cmd = ["iteflash"]

        if servo_type == "servo_v2":
            cmd.extend(["--send-waveform=1", "--i2c-interface=ftdi"])
        else:
            serial = self.servo.get("serialname", "")
            cmd.extend(
                ["--send-waveform=0", "--i2c-interface=usb", "-s", serial]
            )
            if "ccd" in servo_type or "c2d2" in servo_type:
                cmd.append("--block-write-size=256")

        if self.args.dry_run:
            self._logger.info(
                f"DRY RUN TOOL: {' '.join(cmd + ['--erase', '--write', self.args.image])}"
            )
            return

        import os
        import shutil
        import tempfile

        from util.flash.helpers import preserve_sections_with_cur_image

        with tempfile.TemporaryDirectory() as temp_dir:
            temp_img = self.args.image
            if getattr(
                self.args, "no_preserve", False
            ) is False and not getattr(self.args, "read", None):
                self._logger.info("Preserving sections via I2C...")
                img_cpy = os.path.join(temp_dir, "ec.new.cpy.bin")
                shutil.copy(self.args.image, img_cpy)
                temp_img = img_cpy

                cur_img = os.path.join(temp_dir, "ec.cur.bin")
                rd_cmd = cmd + [f"--read={cur_img}"]
                self._logger.info(
                    f"Reading current ITE image: {' '.join(rd_cmd)}"
                )
                subprocess.run(rd_cmd, check=True)

                preserve_sections_with_cur_image(cur_img, img_cpy, temp_dir)

            cmd.extend(["--erase", f"--write={temp_img}"])
            if not getattr(self.args, "verify", True):
                cmd.append("--noverify")

            self._logger.info(f"Executing: {' '.join(cmd)}")
            subprocess.run(cmd, check=True)

    def cleanup_dut(self):
        self._logger.info("Cleaning up ITE I2C state.")
        servo_type = self.servo.get("servo_type", "")
        if "c2d2" in servo_type:
            try:
                self.servo.set("ec_flash_select", "off")
            except Exception:
                pass
        if "servo_v4" in servo_type and getattr(
            self.args, "usbc_charging", False
        ):

            self._logger.info("Restoring servo_v4 to PD charging")

            self.servo.set("servo_pd_role", "src")

            self._logger.info("Disconnect/reconnect CC lines")

            try:

                self.servo.set("servo_v4p1_uart_cmd", "cc off")

                self.servo.set("servo_v4p1_uart_cmd", "cc on")

            except Exception:

                pass
