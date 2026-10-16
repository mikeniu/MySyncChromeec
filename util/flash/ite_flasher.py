# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""ITE (it83xx / it8xxx2) I2C and UART EC flasher implementations."""

import logging
import os
import shutil
import subprocess
import tempfile
import time

from flash.flasher_base import BaseEcFlasher
from flash.helpers import get_preserve_ranges
from flash.helpers import locate_tool
from flash.helpers import parse_fmap_areas
from flash.helpers import preserve_sections_with_cur_image
from flash.servo_utils import ServoError


logger = logging.getLogger(__name__)


class _IteBaseFlasher(BaseEcFlasher):
    """Shared base class for ITE EC flashers."""

    def _get_controller(self) -> str:
        """Return the active servo controller ('servo_micro', 'c2d2', 'ccd', or raw type)."""
        if not self.servo:
            return ""
        ctrl = self.servo.get("active_dut_controller", default="")
        if not ctrl:
            ctrl = self.servo.get("servo_type", default="")
        if "servo_micro" in ctrl:
            return "servo_micro"
        if "c2d2" in ctrl:
            return "c2d2"
        if "ccd" in ctrl:
            return "ccd"
        return ctrl

    def _is_ccd(self) -> bool:
        """Return True if the active servo interface is CCD."""
        return self._get_controller() == "ccd"


class IteI2cFlasher(_IteBaseFlasher):
    """Flasher implementation for ITE ECs over I2C using iteflash."""

    def _get_serial(self) -> str:
        """Return the servo serial number for USB I2C transport."""
        if not self.servo:
            return ""
        controller = self._get_controller()
        if controller == "servo_micro":
            return self.servo.get("servo_micro_serialname", default="")
        if controller == "c2d2":
            return self.servo.get("c2d2_serialname", default="")
        if controller == "ccd":
            return self.servo.get("ccd_serialname", default="")
        return self.servo.get("serialname", default="")

    def _enter_dfu_mode(self) -> None:
        """Reset EC and send the ITE special debugger waveform."""
        if not self.servo:
            return
        controller = self._get_controller()

        if controller == "c2d2":
            self.servo.set("gsc_ec_reset", "off")
        elif self.servo.has_control("cold_reset"):
            self.servo.set_many([("cold_reset", "on"), ("cold_reset", "off")])

        if controller in ("servo_micro", "c2d2"):
            # Get-only control: reading it sends the waveform.
            if not self.args.dry_run:
                logger.info("Asking servo to send the dbgr special waveform...")
                time.sleep(3)
                self.servo.get("enable_ite_dfu")
        elif controller == "ccd":
            logger.info("Asking GSC to send the dbgr special waveform...")
            time.sleep(2)
            self.servo.set("gsc_i2c_ctrl", "ite_debugger_mode")
            time.sleep(3)
            # Cr50 warm-reboots after sending ite_debugger_mode, which clears
            # sysrst and allows the AP to wake mid-erase unless re-asserted.
            try:
                self.servo.set("gsc_uart_cmd", "sysrst assert")
            except ServoError:
                pass
        else:
            raise ValueError(
                f"Servo controller not supported for ITE I2C: {controller}"
            )

        if controller == "c2d2":
            self.servo.set("gsc_idle_level", "active")

    def prepare_dut(self) -> None:
        """Configure I2C muxes, EC reset, and ITE DFU/debugger waveform."""
        if not self.servo:
            return

        controller = self._get_controller()

        if controller == "c2d2":
            self.servo.set("gsc_ec_reset", "on")
            self.servo.save_state("i2c_ec_bus_speed")
            self.servo.set("i2c_ec_bus_speed", "400")
            self.servo.save_state("ec_flash_select", "off")
            self.servo.set("ec_flash_select", "on")

        if self.servo.has_control("dut_i2c_mux"):
            logger.info("Switching DUT I2C Mux to ec_prog")
            self.servo.save_state("dut_i2c_mux")
            self.servo.set("dut_i2c_mux", "ec_prog")

        if self.servo.has_control("dut_i2c_en"):
            logger.info("Enabling I2C programming")
            self.servo.save_state("dut_i2c_en")
            self.servo.set("dut_i2c_en", "on")

        if controller == "ccd":
            ccd_i2c = self.servo.get("ccd_i2c_en", default="")
            if ccd_i2c.lower() != "always":
                raise RuntimeError(
                    f"CCD I2C capability is not set as 'Always': {ccd_i2c}"
                )

            logger.info("Asserting sysrst to hold AP off during ITE flash")
            try:
                self.servo.set("gsc_uart_cmd", "sysrst assert")
            except ServoError:
                pass

        self._enter_dfu_mode()

    def cleanup_dut(self) -> None:
        """Deassert sysrst after CCD ITE I2C flashing."""
        if not self.servo:
            return
        if self._is_ccd():
            logger.info("Deasserting sysrst to allow AP boot")
            try:
                self.servo.set("gsc_uart_cmd", "sysrst deassert")
            except ServoError:
                pass

    def execute_flash(self) -> None:
        """Read or write the ITE EC flash using iteflash."""
        iteflash_bin = locate_tool("iteflash", self.args.board)
        cmd = [iteflash_bin]

        controller = self._get_controller()
        serial = self._get_serial()
        cmd.extend(["--send-waveform=0", "--i2c-interface=usb"])
        if serial:
            cmd.extend(["-s", serial])
        if controller == "ccd":
            cmd.append("--block-write-size=256")

        ap_rst_pin = ""
        if self.servo and self.servo.has_control("ite_ap_rst_pin"):
            try:
                ap_rst_pin = self.servo.get("ite_ap_rst_pin", default="")
            except ServoError:
                ap_rst_pin = ""
        board_lower = (getattr(self.args, "board", "") or "").lower()
        if ap_rst_pin and "Problem with" not in ap_rst_pin:
            cmd.append(f"--dbgr-preserve-pin={ap_rst_pin}")
        elif board_lower in (
            "corsola",
            "krabby",
            "tentacruel",
            "magikarp",
            "chinchou",
            "skitty",
            "veluza",
            "staryu",
            "wugtrio",
            "starmie",
            "wyrdeer",
        ):
            cmd.append("--dbgr-preserve-pin=G1")

        if self.args.verbose:
            cmd.append("--debug")

        if self.args.read:
            cmd.append(f"--read={self.args.read}")
            logger.info("Reading EC firmware using iteflash: %s", " ".join(cmd))
            if not self.args.dry_run:
                subprocess.run(cmd, check=True)
            return

        if not self.args.image or not os.path.exists(self.args.image):
            if not self.args.dry_run:
                raise FileNotFoundError(
                    f"Firmware image not found: {self.args.image}"
                )

        with tempfile.TemporaryDirectory(
            prefix="flash_ec.ite_i2c."
        ) as temp_dir:
            target_img = str(self.args.image)
            skip_args = []
            if not getattr(self.args, "no_preserve", False):
                areas = parse_fmap_areas(target_img)
                preserve_ranges = get_preserve_ranges(target_img, areas=areas)
                if preserve_ranges:
                    for start, end in preserve_ranges:
                        skip_args.append(
                            f"--skip-range=0x{start:x}:0x{end - start:x}"
                        )
                elif not areas:
                    cur_img = os.path.join(temp_dir, "ec.cur.bin")
                    read_cmd = cmd + [f"--read={cur_img}"]
                    logger.info(
                        "Reading current image via iteflash to preserve "
                        "FMAP sections..."
                    )
                    if not self.args.dry_run:
                        subprocess.run(read_cmd, check=True)
                        temp_new = os.path.join(temp_dir, "ec.new.cpy.bin")
                        shutil.copyfile(target_img, temp_new)
                        if preserve_sections_with_cur_image(
                            cur_img, temp_new, temp_dir
                        ):
                            target_img = temp_new
                        if self.servo and controller == "c2d2":
                            self.servo.set("gsc_ec_reset", "on")
                        self._enter_dfu_mode()

            write_cmd = cmd + skip_args + ["--erase", f"--write={target_img}"]
            if not getattr(self.args, "verify", True):
                write_cmd.append("--noverify")

            logger.info(
                "Programming EC firmware using iteflash: %s",
                " ".join(write_cmd),
            )
            if not self.args.dry_run:
                subprocess.run(write_cmd, check=True)


class IteUartFlasher(_IteBaseFlasher):
    """Flasher implementation for newer ITE ECs over UART using itecomdbgr."""

    def _get_baudrate(self) -> int:
        """Return the target UART baudrate for itecomdbgr."""
        baud = getattr(self.args, "baud", None)
        if baud:
            return int(baud)
        chip = getattr(self.args, "chip", "")
        if chip in ("ite_uart_3m", "ite_spi_ccd_uart") and not self._is_ccd():
            return 3000000
        return 115200

    def _enter_uart_boot0(self) -> None:
        """Reset EC with UART UUT boot0 strapping asserted."""
        if not self.servo:
            return
        controller = self._get_controller()
        if controller == "ccd":
            # Ti50 requires EC reset to be asserted before UUT mode can be
            # enabled; Cr50 does not mind.
            self.servo.set("cold_reset", "on")
            self.servo.set("ccd_ec_boot_mode_uut", "on")
            self.servo.set("cold_reset", "on")
            self.servo.set("cold_reset", "off")
            time.sleep(0.3)
            self.servo.set("ccd_ec_boot_mode_uut", "off")
            time.sleep(0.3)
        else:
            boot_ctrl = (
                "ec_boot_mode_uut" if controller == "c2d2" else "ec_boot_mode"
            )
            self.servo.set(boot_ctrl, "on")
            if controller == "c2d2":
                self.servo.set_many(
                    [("gsc_ec_reset", "on"), ("gsc_ec_reset", "off")]
                )
            else:
                self.servo.set_many(
                    [("cold_reset", "on"), ("cold_reset", "off")]
                )
            time.sleep(0.3)
            self.servo.set(boot_ctrl, "off")
            time.sleep(0.3)

    def prepare_dut(self) -> None:
        """Configure UART controls, resets, and bitbang mode prior to flash."""
        if not self.servo:
            return

        controller = self._get_controller()
        if controller == "ccd":
            logger.info("Asserting sysrst to hold AP off during ITE UART flash")
            try:
                self.servo.set("gsc_uart_cmd", "sysrst assert")
            except ServoError:
                pass

        self._enter_uart_boot0()

        self.servo.save_state("ec_uart_en")
        self.servo.set("ec_uart_en", "on")

        # Disconnect EC-3PO from the UART so it doesn't interfere with
        # itecomdbgr, like flash_ec's claim_pty().
        try:
            self.servo.save_state("ec_ec3po_interp_connect")
            self.servo.set("ec_ec3po_interp_connect", "off")
        except ServoError:
            pass

        baud = self._get_baudrate()
        self.servo.save_state("ec_uart_baudrate")
        self.servo.save_state("ec_uart_parity")
        self.servo.set("ec_uart_baudrate", str(baud))
        self.servo.set("ec_uart_parity", "even")

        if getattr(self.args, "bitbang", False) or controller == "ccd":
            if self.servo.has_control("ec_uart_bitbang_en"):
                self.servo.save_state("ec_uart_bitbang_en")
                self.servo.set("ec_uart_bitbang_en", "on")

    def cleanup_dut(self) -> None:
        """Release boot0, pulse EC reset, and deassert sysrst after flashing."""
        if not self.servo:
            return
        controller = self._get_controller()
        if controller == "ccd":
            self.servo.set("cold_reset", "off")
            self.servo.set("ccd_ec_boot_mode_uut", "off")
            self.servo.set("ccd_ec_boot_mode_bitbang", "off")
            self.servo.set("cold_reset", "on")
            self.servo.set("cold_reset", "off")
            logger.info("Deasserting sysrst on CCD...")
            try:
                self.servo.set("gsc_uart_cmd", "sysrst deassert")
            except ServoError:
                pass
        elif controller == "c2d2":
            self.servo.set_many(
                [("gsc_ec_reset", "on"), ("gsc_ec_reset", "off")]
            )
        else:
            self.servo.set_many([("cold_reset", "on"), ("cold_reset", "off")])

    def execute_flash(self) -> None:
        """Read or write the ITE EC flash over UART using itecomdbgr."""
        if not self.servo:
            raise ValueError("Servo instance required for ITE UART flash")

        itecomdbgr_bin = locate_tool("itecomdbgr", self.args.board)
        pty = self.servo.get("raw_ec_uart_pty", default="")
        if not pty or "not_applicable" in pty:
            pty = self.servo.get("ec_uart_pty", default="")

        baud = self._get_baudrate()
        base_cmd = [
            itecomdbgr_bin,
            "-d",
            pty,
            "-b",
            str(baud),
        ]

        if self.args.read:
            read_cmd = base_cmd + ["-r", self.args.read]
            logger.info(
                "Executing itecomdbgr read: %s",
                " ".join(read_cmd),
            )
            if not self.args.dry_run:
                subprocess.run(read_cmd, check=True)
            return

        if not self.args.image or not os.path.exists(self.args.image):
            if not self.args.dry_run:
                raise FileNotFoundError(
                    f"Firmware image not found: {self.args.image}"
                )

        with tempfile.TemporaryDirectory(
            prefix="flash_ec.ite_uart."
        ) as temp_dir:
            target_img = str(self.args.image)
            skip_args = []
            if not getattr(self.args, "no_preserve", False):
                areas = parse_fmap_areas(target_img)
                preserve_ranges = get_preserve_ranges(target_img, areas=areas)
                if preserve_ranges:
                    for start, end in preserve_ranges:
                        skip_args.extend(
                            ["-S", f"0x{start:x}", f"0x{end - start:x}"]
                        )
                elif not areas:
                    cur_img = os.path.join(temp_dir, "ec.cur.bin")
                    read_cmd = base_cmd + ["-r", cur_img]
                    logger.info(
                        "Reading current image via itecomdbgr to preserve "
                        "FMAP sections..."
                    )
                    if not self.args.dry_run:
                        subprocess.run(read_cmd, check=True)
                        self._enter_uart_boot0()
                        temp_new = os.path.join(temp_dir, "ec.new.cpy.bin")
                        shutil.copyfile(target_img, temp_new)
                        if preserve_sections_with_cur_image(
                            cur_img, temp_new, temp_dir
                        ):
                            target_img = temp_new

            write_cmd = base_cmd + skip_args + ["-f", target_img]
            if not getattr(self.args, "verify", True):
                write_cmd.append("-n")

            logger.info("Executing itecomdbgr: %s", " ".join(write_cmd))
            if not self.args.dry_run:
                subprocess.run(write_cmd, check=True)
