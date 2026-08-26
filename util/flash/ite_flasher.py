#!/usr/bin/env vpython3

"""ITE flasher implementations."""

# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
import os
import shutil
import subprocess
import tempfile
import time

from util.flash.helpers import locate_tool
from util.flash.helpers import preserve_sections_with_cur_image
from util.flash.helpers import dump_fmap

from .flasher_base import BaseEcFlasher


class IteUartFlasher(BaseEcFlasher):
    """ITE DBGR UART Flasher implementation."""

    def prepare_dut(self):
        self._logger.info("Asserting resets for ITE UART flash.")
        try:
            self.servo.set("fw_up", "on")
        except Exception:
            pass
        self.servo.set("cold_reset", "on")
        try:
            self.servo.set("ec_uart_en", "on")
        except Exception:
            pass

        time.sleep(0.5)
        self.servo.set("cold_reset", "off")
        try:
            self.servo.set("fw_up", "off")
        except Exception:
            pass

        # In reality, this checks args for overrides
        baud = getattr(self.args, "baud", None)
        if not baud:
            baud = "115200"
        self._logger.info("Setting baud rate to %s", baud)
        try:
            self.servo.set("ec_uart_baudrate", baud)
        except Exception:
            pass

        if "ccd" in self.servo.get("servo_type", ""):
            self._logger.info("Sending GSC sysrst assert to suppress AP")
            try:
                self.servo.set("cr50_uart_cmd", "sysrst assert")
            except Exception:
                pass
            try:
                self.servo.set("gsc_uart_cmd", "sysrst assert")
            except Exception:
                pass
            time.sleep(1)
            self._logger.info(
                "Asking CR50 to send the dbgr special waveform to ITE chip"
            )
            time.sleep(2)
            try:
                self.servo.set("cr50_i2c_ctrl", "ite_debugger_mode")
            except Exception:  # pylint: disable=broad-exception-caught
                pass
            time.sleep(8)

    def execute_flash(self):
        pty = self.servo.get("ec_uart_pty", "") if self.servo else ""
        baud = getattr(self.args, "baud", None)
        if not baud:
            baud = "115200"

        read_path = getattr(self.args, "read", None)

        if read_path:
            itecomdbgr_path = locate_tool("itecomdbgr")
            cmd = [itecomdbgr_path, "-d", pty, "-r", read_path, "-b", baud]
            if self.args.dry_run:
                self._logger.info("DRY RUN TOOL: %s", " ".join(cmd))
                return
            self._logger.info("Executing read: %s", " ".join(cmd))
            subprocess.run(cmd, check=True)
            return

        itecomdbgr_path = locate_tool("itecomdbgr")
        
        with tempfile.TemporaryDirectory() as temp_dir:
            temp_img = self.args.image
            if getattr(self.args, "no_preserve", False) is False:
                self._logger.info("Preserving sections via UART (chunked)...")
                img_cpy = os.path.join(temp_dir, "ec.new.cpy.bin")
                shutil.copy(self.args.image, img_cpy)
                temp_img = img_cpy

                ro_only = getattr(self.args, "ro", False)
                sections_to_preserve = []

                fmap_p = dump_fmap(temp_img, "-p")
                
                if ro_only:
                    if fmap_p:
                        for line in fmap_p.splitlines():
                            parts = line.split()
                            if parts:
                                region = parts[0]
                                if region != "EC_RO" and not region.startswith("FMAP"):
                                    sections_to_preserve.append(region)
                else:
                    fmap_e = dump_fmap(temp_img, "-e")
                    if fmap_e and " preserve" in fmap_e:
                        for line in fmap_e.splitlines():
                            if " preserve" in line:
                                sections_to_preserve.append(line.split()[0])

                if sections_to_preserve:
                    fmap_p = dump_fmap(temp_img, "-p")
                    pos_dict = {}
                    if fmap_p:
                        for line in fmap_p.splitlines():
                            parts = line.split()
                            if len(parts) >= 3:
                                pos_dict[parts[0]] = (parts[1], parts[2])

                    for sec in sections_to_preserve:
                        if sec in pos_dict:
                            offset, size = pos_dict[sec]
                            sec_img = os.path.join(temp_dir, f"ec.cur.{sec}.bin")
                            rd_cmd = [
                                itecomdbgr_path, "-d", pty, "-b", baud,
                                "-r", sec_img, "-R", str(offset), str(size)
                            ]
                            self._logger.info("Reading UART section %s: %s", sec, " ".join(rd_cmd))
                            subprocess.run(rd_cmd, check=True)

                            try:
                                subprocess.run(
                                    ["futility", "load_fmap", temp_img, f"{sec}:{sec_img}"],
                                    check=True, cwd=temp_dir, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL
                                )
                            except subprocess.CalledProcessError as e:
                                self._logger.error("Failed to inject %s: %s", sec, e)
                                raise
                else:
                    self._logger.info("No sections are being preserved")

            cmd = [itecomdbgr_path, "-d", pty, "-f", temp_img, "-b", baud]

            if self.args.dry_run:
                self._logger.info("DRY RUN TOOL: %s", " ".join(cmd))
                return

            self._logger.info("Executing: %s", " ".join(cmd))
            subprocess.run(cmd, check=True)

    def cleanup_dut(self):
        self._logger.info("Cleaning up ITE UART state.")
        try:
            self.servo.set("cr50_uart_cmd", "sysrst deassert")
        except Exception:
            pass
        try:
            self.servo.set("gsc_uart_cmd", "sysrst deassert")
        except Exception:
            pass
        self.servo.set("cold_reset", "off")



class IteI2cFlasher(BaseEcFlasher):
    """ITE DBGR I2C Flasher implementation using iteflash."""

    def prepare_dut(self):
        self._logger.info("Preparing ITE I2C connections.")
        servo_type = self.servo.get("servo_type", "")
        if "c2d2" in servo_type:
            self.servo.set("gsc_ec_reset", "on")
            self.servo.set("i2c_ec_bus_speed", "400")
            self.servo.set("ec_flash_select", "on")

        if self.servo.has_control("dut_i2c_mux"):
            self._logger.info("Switching DUT I2C Mux")
            self.servo.set("dut_i2c_mux", "ec_prog")

        if self.servo.has_control("dut_i2c_en"):
            self._logger.info("Enabling I2C programming")
            self.servo.set("dut_i2c_en", "on")

        if "c2d2" in servo_type:
            self.servo.set("gsc_ec_reset", "off")
        else:
            self.servo.set("cold_reset", "on")
            self.servo.set("cold_reset", "off")

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

            except (
                Exception
            ):  # pylint: disable=broad-exception-caught,unused-variable

                pass

            self.servo.set("cold_reset", "off")

        # True ite_dfu_mode hardware switching
        if "ccd" in servo_type:
            i2c_cap = self.servo.get("ccd_i2c_en", "")
            if i2c_cap.lower() != "always":
                self._logger.warning("Setting CCD I2C to always")
                self.servo.set("ccd_i2c_en", "always")
            # Suppress the AP using sysrst assert! This is REQUIRED for I2C DBGR since it is a hardware interface
            try:
                self._logger.info("Sending sysrst assert to suppress AP")
                self.servo.set("cr50_uart_cmd", "sysrst assert")
                self.servo.set("gsc_uart_cmd", "sysrst assert")
                import time
                time.sleep(1)
            except Exception as e:
                self._logger.warning("Could not assert sysrst: %s", e)

            self._logger.info("Asking CR50 to send the dbgr special waveform to ITE chip")
            time.sleep(2)
            try:
                self.servo.set("cr50_i2c_ctrl", "ite_debugger_mode")
            except Exception:
                pass
            time.sleep(3)
        elif "servo_v2" in servo_type:
            self._logger.info("Closing servod connection to ftdi_i2c interface")
            try:
                self.servo.set("ftdii2c_cmd", "close")
            except Exception:  # pylint: disable=broad-exception-caught
                pass

        if "c2d2" in servo_type:
            try:
                self.servo.set("cr50_idle_level", "active")
            except Exception:  # pylint: disable=broad-exception-caught
                pass

    def execute_flash(self):
        servo_type = self.servo.get("servo_type", "")

        iteflash_path = locate_tool("iteflash")
        cmd = [iteflash_path]

        if servo_type == "servo_v2":
            cmd.extend(["--send-waveform=1", "--i2c-interface=ftdi"])
        else:
            serial = ""
            if "ccd" in servo_type:
                serial = self.servo.get("ccd_serialname", "")
            elif "servo_micro" in servo_type:
                serial = self.servo.get("servo_micro_serialname", "")
            elif "c2d2" in servo_type:
                serial = self.servo.get("c2d2_serialname", "")
            if not serial or "Problem with" in serial:
                serial = self.servo.get("serialname", "")
            
            cmd.extend(["--send-waveform=0", "--i2c-interface=usb"])
            if serial and "Problem with" not in serial:
                cmd.extend(["-s", serial])
            if "ccd" in servo_type or "c2d2" in servo_type:
                cmd.append("--block-write-size=256")

        read_path = getattr(self.args, "read", None)
        if read_path:
            rd_cmd = cmd + [f"--read={read_path}"]
            if self.args.dry_run:
                self._logger.info("DRY RUN TOOL: %s", " ".join(rd_cmd))
                return
            self._logger.info("Reading ITE image: %s", " ".join(rd_cmd))
            subprocess.run(rd_cmd, check=True)
            return

        if self.args.dry_run:
            self._logger.info(
                "DRY RUN TOOL: %s",
                " ".join(cmd + ["--erase", f"--write={self.args.image}"]),
            )
            return

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
                    "Reading current ITE image: %s", " ".join(rd_cmd)
                )
                subprocess.run(rd_cmd, check=True)

                preserve_sections_with_cur_image(cur_img, img_cpy, temp_dir, ro_only=getattr(self.args, "ro", False))

            cmd.extend(["--erase", f"--write={temp_img}"])
            if not getattr(self.args, "verify", True):
                cmd.append("--noverify")

            self._logger.info("Executing: %s", " ".join(cmd))
            subprocess.run(cmd, check=True)

    def cleanup_dut(self):
        self._logger.info("Cleaning up ITE I2C state.")
        try:
            self.servo.set("cr50_uart_cmd", "sysrst deassert")
        except Exception:
            pass
        try:
            self.servo.set("gsc_uart_cmd", "sysrst deassert")
        except Exception:
            pass
        servo_type = self.servo.get("servo_type", "")
        if "c2d2" in servo_type:
            try:
                self.servo.set("ec_flash_select", "off")
            except Exception:  # pylint: disable=broad-exception-caught
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

            except Exception:  # pylint: disable=broad-exception-caught

                pass
