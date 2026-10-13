# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Nuvoton NPCX SPI, UUT (UART), and JTAG EC flasher implementations."""

import logging
import os
import shutil
import subprocess
import tempfile
import time
from typing import Optional

from flash.flasher_base import BaseEcFlasher
from flash.flashrom_flasher import FlashromFlasher
from flash.helpers import EC_DIR
from flash.helpers import locate_tool
from flash.helpers import preserve_sections_with_cur_image
from flash.servo_utils import ServoError


logger = logging.getLogger(__name__)

# Chips that util/openocd/npcx_cmds.tcl's flash_npcx_* procs can program.
NPCX_JTAG_CHIPS = (
    "npcx_5m5g_jtag",
    "npcx_5m6g_jtag",
    "npcx_7m6x_jtag",
    "npcx_7m7x_jtag",
)


def _find_monitor_binary(board: str, image_path: Optional[str]) -> str:
    """Locate the npcx_monitor.bin file required by NPCX flashers."""
    img_dir = os.path.dirname(os.path.abspath(image_path)) if image_path else ""
    search_dirs = [
        os.path.join(img_dir, "chip", "npcx", "spiflashfw") if img_dir else "",
        img_dir,
        os.path.join(EC_DIR, "build", board, "chip", "npcx", "spiflashfw"),
        f"/build/{board}/firmware",
        "/usr/share/ec-devutils",
    ]
    for directory in search_dirs:
        if not directory:
            continue
        cand = os.path.join(directory, "npcx_monitor.bin")
        if os.path.isfile(cand):
            logger.info("Using NPCX monitor image: %s", cand)
            return cand
    raise FileNotFoundError("Failed to find npcx_monitor.bin")


class NpcxSpiFlasher(FlashromFlasher):
    """Flasher for NPCX chips over SPI using flashrom."""

    def prepare_dut(self) -> None:
        """Configure NPCX SPI muxes and resets prior to flashing."""
        if not self.servo:
            return
        if getattr(self.args, "chip", "") != "npcx_spi":
            self.args.chip = "npcx_int_spi"
        super().prepare_dut()


class NpcxUutFlasher(BaseEcFlasher):
    """Flasher for NPCX chips over UART using the uartupdatetool utility."""

    def _is_ccd(self) -> bool:
        """Return True if the active servo interface is CCD."""
        if not self.servo:
            return False
        return "ccd" in self.servo.get("servo_type", default="")

    def _find_monitor_binary(self) -> str:
        """Locate the npcx_monitor.bin file required by uartupdatetool."""
        return _find_monitor_binary(
            getattr(self.args, "board", ""), self.args.image
        )

    def _enable_boot0(self, enable: bool) -> None:
        """Toggle NPCX UUT boot0 strapping pin."""
        if not self.servo:
            return
        val = "on" if enable else "off"
        servo_type = self.servo.get("servo_type", default="")

        if self._is_ccd():
            if enable:
                self.servo.set("cold_reset", "on")
            self.servo.set("ccd_ec_boot_mode_uut", val)
        elif "servo_micro" in servo_type:
            self.servo.set("ec_boot_mode", val)
        elif "c2d2" in servo_type:
            self.servo.set("ec_boot_mode_uut", val)
        else:
            self.servo.set("ec_boot_mode", val)

    def _ec_reset(self) -> None:
        """Pulse EC reset appropriate for the active servo interface."""
        if not self.servo:
            return
        servo_type = self.servo.get("servo_type", default="")
        if "c2d2" in servo_type:
            self.servo.set_many(
                [("gsc_ec_reset", "on"), ("gsc_ec_reset", "off")]
            )
        else:
            self.servo.set_many([("cold_reset", "on"), ("cold_reset", "off")])

    def prepare_dut(self) -> None:
        """Enter NPCX UUT boot mode and save UART state."""
        if not self.servo:
            return

        logger.info("Using NPCX UUT Bootloader via UART")
        self._enable_boot0(True)
        self._ec_reset()
        self._enable_boot0(False)

        self.servo.save_state("ec_uart_baudrate")
        self.servo.save_state("ec_uart_parity")
        self.servo.set("ec_uart_baudrate", "115200")
        self.servo.set("ec_uart_parity", "none")

        try:
            self.servo.save_state("ec_ec3po_interp_connect")
            self.servo.set("ec_ec3po_interp_connect", "off")
        except ServoError:
            pass

    def cleanup_dut(self) -> None:
        """Restore UART controls, then reset the EC to boot the new image."""
        if not self.servo:
            return
        # Mirror flash_ec's cleanup(): servo_restore, then ec_reset.
        self.servo.restore_state()
        self._ec_reset()

    def execute_flash(self) -> None:
        """Read or write the NPCX EC flash using uartupdatetool."""
        if not self.servo:
            raise ValueError("Servo required for NPCX UUT")

        uut_bin = locate_tool("uartupdatetool", self.args.board)
        pty = self.servo.get("raw_ec_uart_pty", default="")
        if not pty or "not_applicable" in pty:
            pty = self.servo.get("ec_uart_pty")

        dev_name = pty.replace("/dev/", "")
        base_args = [
            uut_bin,
            f"--port={dev_name}",
            "--baudrate=115200",
        ]

        if self.args.read:
            cmd = base_args + [
                "--read-flash",
                f"--file={self.args.read}",
            ]
            logger.info("Reading EC firmware using UUT: %s", " ".join(cmd))
            if not self.args.dry_run:
                subprocess.run(cmd, check=True)
            return

        if not self.args.image or not os.path.exists(self.args.image):
            raise FileNotFoundError(
                f"Firmware image not found: {self.args.image}"
            )

        mon_bin = self._find_monitor_binary()
        mon_cmd = base_args + [
            "--opr=wr",
            "--addr=0x200C3020",
            f"--file={mon_bin}",
        ]
        logger.info("Loading NPCX monitor binary: %s", " ".join(mon_cmd))
        if not self.args.dry_run:
            subprocess.run(mon_cmd, check=True)

        with tempfile.TemporaryDirectory(
            prefix="flash_ec.npcx_uut."
        ) as temp_dir:
            target_img = self.args.image

            if not getattr(self.args, "no_preserve", False):
                cur_img = os.path.join(temp_dir, "ec.cur.bin")
                read_cmd = base_args + [
                    "--read-flash",
                    f"--file={cur_img}",
                ]
                logger.info(
                    "Reading current image via UUT to preserve FMAP "
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

            write_cmd = base_args + [
                "--auto",
                f"--offset={self.args.offset}",
                f"--file={target_img}",
            ]

            logger.info(
                "Programming EC firmware using UUT: %s",
                " ".join(write_cmd),
            )
            if not self.args.dry_run:
                subprocess.run(write_cmd, check=True)

            if getattr(self.args, "verify", True):
                logger.info("Verifying EC firmware using UUT...")
                read_verify = os.path.join(temp_dir, "ec.verify.bin")
                ver_size = os.path.getsize(target_img)
                ver_cmd = base_args + [
                    "--read-flash",
                    f"--file={read_verify}",
                ]
                if not self.args.dry_run:
                    subprocess.run(ver_cmd, check=True)
                    with open(target_img, "rb") as f_expected:
                        with open(read_verify, "rb") as f_actual:
                            f_actual.seek(self.args.offset)
                            if f_expected.read() != f_actual.read(ver_size):
                                raise RuntimeError(
                                    "UUT firmware verification failed! "
                                    "Files do not match."
                                )


class NpcxJtagFlasher(BaseEcFlasher):
    """Flasher for NPCX chips over JTAG using OpenOCD."""

    def prepare_dut(self) -> None:
        """Configure JTAG buffers and cold reset prior to flashing."""
        if not self.servo:
            return
        for ctrl in ["jtag_vref_sel1", "jtag_vref_sel0"]:
            self.servo.save_state(ctrl)
            self.servo.set(ctrl, "pp3300")
        for ctrl in ["jtag_buf_en", "jtag_buf_on_flex_en"]:
            self.servo.save_state(ctrl)
            self.servo.set(ctrl, "on")

        self.servo.set("cold_reset", "on")
        time.sleep(0.05)
        self.servo.set("cold_reset", "off")

    def cleanup_dut(self) -> None:
        """Restore JTAG buffer and VREF controls, then reset the EC."""
        if not self.servo:
            return
        # Mirror flash_ec's cleanup(): servo_restore, then ec_reset.
        self.servo.restore_state()
        self.servo.set("cold_reset", "on")
        time.sleep(0.05)
        self.servo.set("cold_reset", "off")

    def execute_flash(self) -> None:
        """Program the NPCX EC with OpenOCD, like flash_ec's flash_npcx_jtag."""
        if self.args.read:
            raise NotImplementedError(
                "Reading is not supported over NPCX JTAG."
            )

        if not self.args.image or not os.path.exists(self.args.image):
            raise FileNotFoundError(
                f"Firmware image not found: {self.args.image}"
            )

        # The Tcl procs silently do nothing for other chips.
        if self.args.chip not in NPCX_JTAG_CHIPS:
            raise ValueError(
                f"Chip '{self.args.chip}' is not supported over NPCX JTAG"
            )

        # flash_npcx_all (util/openocd/npcx_cmds.tcl) reads RO/ec.RO.flat,
        # RW/ec.RW.bin, and the monitor from the build dir.
        img_dir = os.path.dirname(os.path.abspath(self.args.image))
        cmd = [
            "sudo",
            "timeout",
            "-k",
            "10",
            "-s",
            "9",
            str(self.args.timeout),
            "openocd",
            "-s",
            os.path.join(EC_DIR, "util", "openocd"),
            "-f",
            "servo.cfg",
            "-f",
            "npcx_chip.cfg",
            "-c",
            f"init; flash_npcx_all {self.args.chip} {img_dir} "
            f"{self.args.offset}; shutdown;",
        ]

        logger.info("Executing NPCX JTAG flash: %s", " ".join(cmd))
        if not self.args.dry_run:
            subprocess.run(cmd, check=True)
