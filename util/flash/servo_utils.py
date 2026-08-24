#!/usr/bin/env vpython3
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
import logging
import subprocess


class ServoDriver:
    """Wrapper for servod API and dut-control execution."""

    def __init__(self, port="9999", dut_ip="", dry_run=False):
        self.port = port
        self.dut_ip = dut_ip
        self.dry_run = dry_run
        self._logger = logging.getLogger(self.__class__.__name__)
        self._restore_states = {}

    def _run_cmd(self, args):
        if self.dry_run:
            self._logger.info(f"DRY RUN: {' '.join(args)}")
            return "dry_run_output"

        try:
            result = subprocess.run(
                args,
                check=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
            )
            return result.stdout.strip()
        except subprocess.CalledProcessError as e:
            self._logger.error(
                f"dut-control command failed: {' '.join(args)}\nError: {e.stderr}"
            )
            raise

    def get(self, control, default=None):
        cmd = ["dut-control", "--", "-p", str(self.port)]
        if self.dut_ip:
            cmd.extend(["--host", self.dut_ip])
        cmd.append(control)
        try:
            out = self._run_cmd(cmd)
            if self.dry_run:
                return "unknown"
            # dut-control output is typically 'control:value'
            if ":" in out:
                return out.split(":", 1)[1].strip()
            return out
        except Exception:
            if default is not None:
                return default
            raise

    def set(self, control, value):
        cmd = ["dut-control", "--", "-p", str(self.port)]
        if self.dut_ip:
            cmd.extend(["--host", self.dut_ip])
        cmd.append(f"{control}:{value}")
        self._run_cmd(cmd)

    def has_control(self, control):
        return "ERR" not in self.get(control, "ERR")

    def is_ccd(self):
        return "ccd" in self.get("servo_type", "")

    def hard_reset(self, mcu="ec", board=""):
        servo_type = self.get("servo_type", "")
        if mcu == "ec":
            if "c2d2" in servo_type:
                self.set("gsc_ec_reset", "on")
                self.set("gsc_ec_reset", "off")
            elif self.has_control("cold_reset"):
                self.set("cold_reset", "on")
                self.set("cold_reset", "off")
        elif mcu == "fpmcu":
            import time

            self.set("fpmcu_reset", "on")
            time.sleep(0.5)
            self.set("fpmcu_reset", "off")
            if "dartmonkey" in board and "servo_micro" in servo_type:
                self._logger.warning(
                    "Workaround for dartmonkey + servo_micro: repeat FPMCU reset"
                )
                self.set("fpmcu_reset", "on")
                time.sleep(0.5)
                self.set("fpmcu_reset", "off")
        elif mcu == "usbpd":
            import time

            self.set("usbpd_reset", "on")
            time.sleep(0.5)
            self.set("usbpd_reset", "off")
        elif mcu == "sh":
            self.set("sh_reset", "on")
            self.set("sh_reset", "off")

    def set_boot0(
        self, mcu="ec", on_value="on", boot_mode="uut", rx_rework=False
    ):
        servo_type = self.get("servo_type", "")
        stype = servo_type
        if self.is_ccd():
            stype = "ccd"
        elif "servo_micro" in servo_type:
            stype = "servo_micro"
        elif "servo" in servo_type:
            stype = "servo"

        if mcu == "ec":
            if stype == "ccd":
                if on_value == "on" and boot_mode == "uut":
                    self.set("cold_reset", "on")
                self.set(f"ccd_ec_boot_mode_{boot_mode}", on_value)
            elif stype == "servo_micro":
                if rx_rework:
                    self.set("tp1", on_value)
                    self.set(f"servo_micro_ec_boot_mode_{boot_mode}", on_value)
                else:
                    self.set("ec_boot_mode", on_value)
            elif stype == "c2d2":
                self.set("ec_boot_mode_uut", on_value)
            else:
                self.set("ec_boot_mode", on_value)
        else:
            # Fallback for fpmcu, usbpd, sh
            self.set(f"{mcu}_boot_mode", on_value)

    def save_add(self, control, default_val=""):
        if control not in self._restore_states:
            try:
                current = self.get(control)
                self._restore_states[control] = current
            except Exception:
                self._restore_states[control] = default_val

    def restore_all(self):
        for control, val in reversed(list(self._restore_states.items())):
            if val:
                self._logger.info(f"Restoring {control}:{val}")
                try:
                    self.set(control, val)
                except Exception:
                    pass
        self._restore_states.clear()

    def wait_for_c2d2_h1(self):
        if not self.has_control("spi1_buf_en"):
            return

        import time

        if "off" in self.get("h1_vref_present", "off"):
            self._logger.info("Please attach C2D2 to DUT and power DUT now!")
            for _ in range(100):
                if "on" in self.get("h1_vref_present", "off"):
                    break
                time.sleep(0.1)

        self.set("spi1_buf_en", "on")
        idle_lvl = self.get("cr50_idle_level", "")
        while "sleep" in idle_lvl:
            time.sleep(0.1)
            idle_lvl = self.get("cr50_idle_level", "")
