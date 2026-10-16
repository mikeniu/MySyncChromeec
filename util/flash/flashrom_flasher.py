# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Flashrom SPI EC flasher implementation."""

import logging
import os
import re
import subprocess
import tempfile
import time

from flash.flasher_base import BaseEcFlasher
from flash.helpers import get_writable_ranges
from flash.helpers import locate_tool
from flash.helpers import parse_fmap_areas
from flash.helpers import preserve_sections_with_cur_image
from flash.servo_utils import ServoError


logger = logging.getLogger(__name__)

# Boards whose EC must stay in cold_reset while flashing over a debug header
# (e.g. servo_micro); a running or crash-looping EC disturbs the SPI bus.
BOARDS_HOLD_COLD_RESET_DBG_HDR = ("coral", "reef")


class FlashromFlasher(BaseEcFlasher):
    """Flasher implementation for SPI-connected ECs using flashrom."""

    def on_servov3(self) -> bool:
        """Return True if connected to a servo_v3."""
        if not self.servo:
            return False
        servo_type = self.servo.get("servo_type", default="")
        return "servo_v3" in servo_type

    def on_raiden(self) -> bool:
        """Return True if connected to a Raiden SPI programmer."""
        if not self.servo:
            return bool(getattr(self.args, "raiden", False))
        servo_type = self.servo.get("servo_type", default="")
        if (
            "servo_v4" in servo_type
            or "ccd" in servo_type
            or "servo_micro" in servo_type
            or "c2d2" in servo_type
        ):
            return True
        return bool(getattr(self.args, "raiden", False))

    def prepare_dut(self) -> None:
        """Configure SPI muxes, voltages, and resets prior to SPI flashing."""
        if not self.servo:
            return

        chip = getattr(self.args, "chip", "")
        board = getattr(self.args, "board", "")

        vref = "pp3300"
        if board in ("coral", "reef") or chip in (
            "ite_spi",
            "it8xxx2",
            "ite_spi_ccd_i2c",
            "ite_spi_ccd_uart",
            "npcx_int_spi",
            "npcx_spi",
        ):
            vref = "pp1800"

        for ctrl in ["spi1_vref", "spi1_buf_en", "spi1_buf_on_flex_en"]:
            self.servo.save_state(ctrl)

        try:
            self.servo.set("spi1_vref", vref)
        except ServoError:
            pass

        if not self.on_raiden():
            for ctrl in ["spi1_buf_en", "spi1_buf_on_flex_en"]:
                try:
                    self.servo.set(ctrl, "on")
                except ServoError:
                    pass

        servo_type = self.servo.get("servo_type", default="")
        hold_reset = board in BOARDS_HOLD_COLD_RESET_DBG_HDR and (
            not self.on_raiden() or "servo_micro" in servo_type
        )

        if chip in (
            "ite_spi",
            "it8xxx2",
            "ite_spi_ccd_i2c",
            "ite_spi_ccd_uart",
        ):
            self.servo.save_state("fw_up")
            self.servo.set("cold_reset", "on")
            self.servo.set("fw_up", "on")
            time.sleep(0.1)
            self.servo.set("cold_reset", "off")
            time.sleep(0.2)
            self.servo.set("fw_up", "off")
        elif chip == "npcx_int_spi":
            self.servo.save_state("fw_up")
            self.servo.set("cold_reset", "on")
            self.servo.set("fw_up", "on")
            time.sleep(0.2)
            if not hold_reset:
                self.servo.set("cold_reset", "off")
        elif chip == "npcx_spi":
            self.servo.save_state("dev_mode")
            self.servo.set("dev_mode", "on")
            self.servo.set("cold_reset", "on")
            self.servo.save_state("spi_hold")
            self.servo.set("spi_hold", "off")
            if not hold_reset:
                self.servo.set("cold_reset", "off")
        else:
            # Held in reset while flashing; cleanup_dut() releases it.
            self.servo.set("cold_reset", "on")

    def cleanup_dut(self) -> None:
        """Restore servo controls, then reset the EC to boot the new image."""
        if not self.servo:
            return
        # Mirror flash_ec's cleanup(): servo_restore, then ec_reset.
        self.servo.restore_state()
        self.servo.set_many([("cold_reset", "on"), ("cold_reset", "off")])

    def execute_flash(self) -> None:
        """Read or write the EC SPI flash using flashrom."""
        flashrom_bin = locate_tool("flashrom", self.args.board)
        cmd = ["sudo", flashrom_bin]

        servo_type = (
            self.servo.get("servo_type", default="") if self.servo else ""
        )
        serial = self.servo.get("serialname", default="") if self.servo else ""

        if self.on_servov3():
            cmd.append("--programmer=linux_spi")
        elif self.on_raiden():
            if "servo_micro" in servo_type:
                param = "--programmer=raiden_debug_spi"
                if serial and "unknown" not in serial:
                    param += f":serial={serial}"
            else:
                param = "--programmer=raiden_debug_spi:target=EC"
                if serial and "unknown" not in serial:
                    param += f",serial={serial}"
            cmd.append(param)
        else:
            param = "--programmer=ft2232_spi:type=google-servo-v2,port=B"
            if serial and "unknown" not in serial:
                param += f",serial={serial}"
            cmd.append(param)

        if self.args.read:
            cmd.extend(["-r", self.args.read])
            logger.info("Reading EC firmware using flashrom: %s", " ".join(cmd))
            if not self.args.dry_run:
                subprocess.run(cmd, check=True)
            return

        if not self.args.image or not os.path.exists(self.args.image):
            raise FileNotFoundError(
                f"Firmware image not found: {self.args.image}"
            )

        img_size = os.path.getsize(self.args.image)
        chip_size = img_size
        if not self.args.dry_run:
            try:
                out = subprocess.check_output(
                    cmd + ["--flash-size"], encoding="utf-8"
                )
                # flashrom prints the size on its own line; skip any banner or
                # warning lines around it.
                sizes = re.findall(r"^\d+$", out, flags=re.MULTILINE)
                if sizes:
                    chip_size = int(sizes[-1])
                else:
                    logger.warning("No flash size in flashrom output: %r", out)
            except subprocess.CalledProcessError as err:
                logger.warning(
                    "Could not dynamically determine SPI flash size: %s",
                    err,
                )

        if chip_size == 0:
            raise ValueError("Chip size is 0!")

        if img_size > chip_size:
            raise ValueError(
                f"Image is larger than SPI size! "
                f"(img: {img_size} > chip: {chip_size})"
            )

        with tempfile.TemporaryDirectory(
            prefix="flash_ec.flashrom."
        ) as temp_dir:
            temp_img = os.path.join(temp_dir, "flash_temp.bin")

            pad_size = chip_size - img_size
            with open(temp_img, "wb") as out_f:
                with open(self.args.image, "rb") as in_f:
                    out_f.write(in_f.read())
                if pad_size > 0:
                    out_f.write(b"\xff" * pad_size)

            no_preserve = bool(getattr(self.args, "no_preserve", False))
            areas = parse_fmap_areas(self.args.image)
            ro = bool(getattr(self.args, "ro", False))
            rw = bool(getattr(self.args, "rw", False))
            if ro and rw:
                raise ValueError("Cannot specify both --ro and --rw")

            if ro:
                target_regions = ["EC_RO"]
            elif rw:
                target_regions = ["EC_RW"]
            elif self.on_raiden() and "servo_micro" not in servo_type:
                # Over CCD (without servo_micro), only write WP_RO and EC_RW,
                # matching bash flash_ec.
                target_regions = ["WP_RO", "EC_RW"]
            else:
                # Non-CCD programmer (e.g., servo_micro, servo_v2, linux_spi):
                # Write the whole image (target_regions=None), preserving CBI.
                target_regions = None

            writable_ranges = get_writable_ranges(
                self.args.image,
                chip_size,
                target_regions=target_regions,
                no_preserve=no_preserve,
                areas=areas,
            )

            layout_args = []
            if writable_ranges and writable_ranges != [(0, chip_size)]:
                layout_path = os.path.join(temp_dir, "flashrom.layout")
                with open(layout_path, "w", encoding="utf-8") as layout_f:
                    for idx, (start, end) in enumerate(writable_ranges):
                        region_name = f"WRITE_{idx}"
                        layout_f.write(
                            f"{start:08x}:{end - 1:08x} {region_name}\n"
                        )
                        layout_args.extend(["-i", region_name])
                layout_args.extend(["-l", layout_path, "--noverify-all"])
            elif not no_preserve and not areas:
                cur_img = os.path.join(temp_dir, "ec.cur.bin")
                read_cmd = cmd + ["-r", cur_img]
                logger.info(
                    "Reading current image from SPI to preserve FMAP "
                    "sections..."
                )
                if not self.args.dry_run:
                    subprocess.run(read_cmd, check=True)
                    preserve_sections_with_cur_image(
                        cur_img, temp_img, temp_dir
                    )

            write_cmd = cmd + layout_args + ["-w", temp_img]
            if not getattr(self.args, "verify", True):
                write_cmd.append("-n")

            logger.info(
                "Writing EC firmware using flashrom: %s",
                " ".join(write_cmd),
            )
            if not self.args.dry_run:
                subprocess.run(write_cmd, check=True)
