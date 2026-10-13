# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""STM32 UART (stm32mon) and USB DFU (dfu-util) EC flasher implementations."""

import argparse
import logging
import os
import shutil
import subprocess
import tempfile
import time
from typing import Optional

from flash.flasher_base import BaseEcFlasher
from flash.helpers import locate_tool
from flash.helpers import preserve_sections_with_cur_image
from flash.servo_utils import ServoDriver
from flash.servo_utils import ServoError


logger = logging.getLogger(__name__)


class Stm32UartFlasher(BaseEcFlasher):
    """Flasher for STM32 chips over UART using stm32mon."""

    def _get_mcu_prefix(self) -> str:
        """Return the servo MCU control prefix for the target board."""
        board = getattr(self.args, "board", "")
        if board in (
            "chocodile_bec",
            "oak_pd",
            "strago_pd",
            "chell_pd",
            "glados_pd",
        ):
            return "usbpd"
        if board in (
            "bloonchipper",
            "dartmonkey",
            "hatch_fp",
            "nami_fp",
            "nocturne_fp",
        ):
            return "fpmcu"
        return "ec"

    def _get_uart_prefix(self) -> str:
        """Return the servo UART control prefix for the target MCU."""
        mcu = self._get_mcu_prefix()
        if mcu == "fpmcu":
            return "fpmcu_platform_uart"
        return f"{mcu}_uart"

    def _is_ccd(self) -> bool:
        """Return True if the active servo interface is CCD for the main EC."""
        if not self.servo or self._get_mcu_prefix() != "ec":
            return False
        return "ccd" in self.servo.get("servo_type", default="")

    def _set_boot0(self, enable: bool) -> None:
        """Assert or deassert the STM32 BOOT0 strapping pin."""
        if not self.servo:
            return
        val = "on" if enable else "off"
        mcu = self._get_mcu_prefix()
        servo_type = self.servo.get("servo_type", default="")

        if self._is_ccd():
            self.servo.set("ccd_ec_boot_mode_bitbang", val)
        elif mcu == "ec" and "c2d2" in servo_type:
            self.servo.set("ec_boot_mode_uut", val)
        else:
            self.servo.set(f"{mcu}_boot_mode", val)

    def _hard_reset(self) -> None:
        """Pulse the MCU reset line."""
        if not self.servo:
            return
        mcu = self._get_mcu_prefix()
        board = getattr(self.args, "board", "")

        if mcu == "usbpd":
            self.servo.set("usbpd_reset", "on")
            time.sleep(0.5)
            self.servo.set("usbpd_reset", "off")
        elif mcu == "fpmcu":
            self.servo.set("fpmcu_reset", "on")
            time.sleep(0.5)
            self.servo.set("fpmcu_reset", "off")
            if "dartmonkey" in board:
                time.sleep(0.5)
                self.servo.set("fpmcu_reset", "on")
                time.sleep(0.5)
                self.servo.set("fpmcu_reset", "off")
        else:
            # On C2D2, cold_reset reboots the GSC (and relocks CCD), so reset
            # just the EC with the GSC's ecrst command, like flash_ec's
            # servo_ec_hard_reset_or_die().
            servo_type = self.servo.get("servo_type", default="")
            ctrl = "gsc_ec_reset" if "c2d2" in servo_type else "cold_reset"
            self.servo.set(ctrl, "on")
            time.sleep(0.5)
            self.servo.set(ctrl, "off")

    def prepare_dut(self) -> None:
        """Configure STM32 BOOT0, reset, and UART parity/baudrate."""
        if not self.servo:
            return

        logger.info("Using serial bootloader for STM32")
        uart_prefix = self._get_uart_prefix()
        mcu = self._get_mcu_prefix()

        try:
            self.servo.save_state(f"{mcu}_ec3po_interp_connect")
            self.servo.set(f"{mcu}_ec3po_interp_connect", "off")
        except ServoError:
            pass

        self._set_boot0(True)
        self._hard_reset()

        self.servo.save_state(f"{uart_prefix}_baudrate")
        self.servo.save_state(f"{uart_prefix}_parity")
        self.servo.set(f"{uart_prefix}_parity", "even")

        if self._is_ccd() or getattr(self.args, "bitbang", False):
            baud = self.args.baud or 57600
            self.servo.set(f"{uart_prefix}_baudrate", str(baud))
            self.servo.save_state(f"{uart_prefix}_bitbang_en")
            self.servo.set(f"{uart_prefix}_bitbang_en", "on")
            time.sleep(2)
        else:
            baud = self.args.baud or 115200
            self.servo.set(f"{uart_prefix}_baudrate", str(baud))

    def cleanup_dut(self) -> None:
        """Release BOOT0 and pulse MCU reset to exit bootloader mode."""
        if not self.servo:
            return
        self._set_boot0(False)
        self._hard_reset()

    def execute_flash(self) -> None:
        """Read or write the STM32 flash using stm32mon."""
        if not self.servo:
            raise ValueError("Servo required for STM32 UART flash")

        stm32mon_bin = locate_tool("stm32mon", self.args.board)
        uart_prefix = self._get_uart_prefix()

        pty = self.servo.get(f"raw_{uart_prefix}_pty", default="")
        if not pty or "not_applicable" in pty:
            pty = self.servo.get(f"{uart_prefix}_pty")

        cmd = ["sudo", "timeout", "-k", "10", "-s", "9", str(self.args.timeout)]
        cmd.extend([stm32mon_bin, "-d", pty])

        if self.args.baud:
            cmd.extend(["-b", str(self.args.baud)])

        if self._is_ccd() or getattr(self.args, "bitbang", False):
            cmd.append("-c")

        if self.args.read:
            read_cmd = cmd + ["-r", self.args.read]
            logger.info("Reading STM32 firmware: %s", " ".join(read_cmd))
            if not self.args.dry_run:
                subprocess.run(read_cmd, check=True)
            return

        if not self.args.image or not os.path.exists(self.args.image):
            raise FileNotFoundError(
                f"Firmware image not found: {self.args.image}"
            )

        with tempfile.TemporaryDirectory(prefix="flash_ec.stm32.") as temp_dir:
            target_img = self.args.image
            if not getattr(self.args, "no_preserve", False):
                cur_img = os.path.join(temp_dir, "ec.cur.bin")
                read_cmd = cmd + ["-r", cur_img]
                logger.info(
                    "Reading current image via stm32mon to preserve FMAP "
                    "sections..."
                )
                if not self.args.dry_run:
                    subprocess.run(read_cmd, check=True)
                    temp_new = os.path.join(temp_dir, "ec.new.cpy.bin")
                    shutil.copyfile(self.args.image, temp_new)
                    if preserve_sections_with_cur_image(
                        cur_img, temp_new, temp_dir
                    ):
                        target_img = temp_new

            write_cmd = cmd + ["-U", "-u", "-e", "-w", target_img]
            logger.info("Writing STM32 firmware: %s", " ".join(write_cmd))
            if not self.args.dry_run:
                subprocess.run(write_cmd, check=True)

            if getattr(self.args, "verify", True):
                logger.info("Verifying STM32 firmware...")
                read_verify = os.path.join(temp_dir, "ec.read.bin")
                ver_size = os.path.getsize(target_img)
                ver_cmd = cmd + ["-r", read_verify]
                if not self.args.dry_run:
                    subprocess.run(ver_cmd, check=True)
                    with open(target_img, "rb") as f_expected:
                        with open(read_verify, "rb") as f_actual:
                            if f_expected.read() != f_actual.read(ver_size):
                                raise RuntimeError(
                                    "STM32 firmware verification failed! "
                                    "Files do not match."
                                )


class Stm32DfuFlasher(BaseEcFlasher):
    """Flasher for STM32 USB devices using dfu-util."""

    def __init__(
        self, _servo: Optional[ServoDriver], args: argparse.Namespace
    ) -> None:
        super().__init__(None, args)

    def prepare_dut(self) -> None:
        """No servo preparation required for USB DFU mode."""

    def cleanup_dut(self) -> None:
        """No servo cleanup required for USB DFU mode."""

    def execute_flash(self) -> None:
        """Unprotect and program STM32 flash over USB DFU."""
        if self.args.read:
            raise NotImplementedError(
                "Reading is not supported in STM32 DFU mode."
            )

        if not self.args.image or not os.path.exists(self.args.image):
            raise FileNotFoundError(
                f"Firmware image not found: {self.args.image}"
            )

        dfu_util_bin = locate_tool("dfu-util")
        dfu_device = "0483:df11"
        addr = hex(0x08000000 + self.args.offset)

        if not self.args.dry_run:
            try:
                out = subprocess.check_output(
                    ["lsusb", "-d", dfu_device], encoding="utf-8"
                )
            except subprocess.CalledProcessError:
                out = ""
            lines = [line for line in out.strip().splitlines() if line.strip()]
            if not lines:
                raise RuntimeError(
                    f"Unable to locate DFU device at {dfu_device}"
                )
            if len(lines) > 1:
                raise RuntimeError(
                    f"Too many DFU devices ({len(lines)}). "
                    "Disconnect all but one."
                )

        img_size = os.path.getsize(self.args.image)

        base_cmd = [
            "sudo",
            "timeout",
            "-k",
            "10",
            "-s",
            "9",
            str(self.args.timeout),
            dfu_util_bin,
            "-a",
            "0",
            "-d",
            dfu_device,
        ]

        unprot_cmd = base_cmd + [
            "-s",
            f"{addr}:{img_size}:force:unprotect",
            "-D",
            self.args.image,
        ]
        logger.info("Removing DFU read protection: %s", " ".join(unprot_cmd))
        if not self.args.dry_run:
            subprocess.run(unprot_cmd, check=False)
            time.sleep(1)

        write_cmd = base_cmd + [
            "-s",
            f"{addr}:{img_size}",
            "-D",
            self.args.image,
        ]
        logger.info("Flashing image via DFU: %s", " ".join(write_cmd))
        if not self.args.dry_run:
            subprocess.run(write_cmd, check=True)
