#!/usr/bin/env vpython3

"""Module docstring."""

# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
import os
import subprocess
import tempfile
import time  # pylint: disable=wrong-import-order

from util.flash.flasher_base import BaseEcFlasher
from util.flash.helpers import locate_tool
from util.flash.helpers import preserve_sections_with_cur_image


class FlashromFlasher(BaseEcFlasher):
    """Docstring."""

    def _on_servov3(self):
        return self.servo and "servo_v3" == self.servo.get("servo_type", "")

    def _on_raiden(self):
        servo_type = self.servo.get("servo_type", "") if self.servo else ""
        if (
            "servo_v4" in servo_type
            or "ccd" in servo_type
            or "servo_micro" in servo_type
        ):
            return True
        return getattr(self.args, "raiden", False)

    def prepare_dut(self):
        self._logger.info("Preparing DUT for Flashrom SPI flashing")

        chip = getattr(self.args, "chip", "").lower()
        board = getattr(self.args, "board", "").lower()

        if not self._on_raiden() or "servo_micro" in self.servo.get(
            "servo_type", ""
        ):
            boards_spi_1800mv = ["coral", "reef"]
            if board in boards_spi_1800mv or chip in [
                "ite_spi",
                "ite_spi_ccd_i2c",
                "ite_spi_ccd_uart",
                "it8xxx2",
            ]:
                spi_voltage = "pp1800"
            else:
                spi_voltage = "pp3300"

            try:
                self.servo.set("spi1_vref", spi_voltage)
            except Exception:
                pass
            try:
                self.servo.set("spi2_vref", spi_voltage)
            except Exception:
                pass

        if chip == "npcx_int_spi":
            self.servo.set("fw_up", "off")
            self.servo.set("fw_up", "on")
            self.servo.set("cold_reset", "off")
        elif chip == "npcx_spi":
            self.servo.set("dev_mode", "off")
            self.servo.set("dev_mode", "on")
            self.servo.set("cold_reset", "off")
        elif chip in [
            "ite_spi",
            "ite_spi_ccd_i2c",
            "ite_spi_ccd_uart",
            "it8xxx2",
        ]:
            self.servo.set("fw_up", "on")

            time.sleep(0.1)
            self.servo.set("cold_reset", "off")
            time.sleep(0.2)
            self.servo.set("fw_up", "off")
        else:
            self.servo.set("cold_reset", "on")

    def execute_flash(self):
        img_path = getattr(self.args, "image", None)
        read_path = getattr(self.args, "read", None)
        if not read_path and not os.path.exists(img_path):
            raise FileNotFoundError(f"Image not found: {img_path}")

        flashrom_path = locate_tool("flashrom")
        flashrom_args = ["sudo", flashrom_path]

        servo_type = self.servo.get("servo_type", "") if self.servo else ""
        if self._on_servov3():
            flashrom_args.append("--programmer=linux_spi")
        elif self._on_raiden():
            if "servo_micro" in servo_type:
                flashrom_args.append("--programmer=raiden_debug_spi")
            else:
                flashrom_args.append("--programmer=raiden_debug_spi:target=EC")
        else:
            flashrom_args.append(
                "--programmer=ft2232_spi:type=google-servo-v2,port=B"
            )

        # add serialname
        if not (self._on_servov3() or self._on_raiden()):
            serial = self.servo.get("serialname") if self.servo else None
            if serial:
                flashrom_args[-1] += f",serial={serial}"

        if read_path:
            self._logger.info("Reading EC firmware to %s", read_path)
            subprocess.run(flashrom_args + ["-r", read_path], check=True)
            return

        img_size = os.path.getsize(img_path)

        get_size_cmd = flashrom_args + ["--flash-size"]
        try:
            output = subprocess.check_output(get_size_cmd, encoding="utf-8")
            spi_size_line = [
                l for l in output.splitlines() if l.strip().isdigit()
            ]
            spi_size = (
                int(spi_size_line[-1].strip()) if spi_size_line else img_size
            )
        except subprocess.CalledProcessError:
            self._logger.warning(
                "Failed to determine chip size, assuming img_size"
            )
            spi_size = img_size

        if spi_size == 0:
            raise ValueError("Chip size is 0!")

        patch_size = spi_size - img_size
        if patch_size < 0:
            raise ValueError("Image is larger than SPI size!")

        chip = getattr(self.args, "chip", "").lower()
        append_ff = chip in [
            "npcx_spi",
            "npcx_int_spi",
            "ite_spi_ccd_i2c",
            "ite_spi_ccd_uart",
            "it8xxx2",
            "ite_spi",
        ]

        with tempfile.TemporaryDirectory() as temp_dir:
            temp_img = os.path.join(temp_dir, "flash_temp.bin")

            with open(img_path, "rb") as f_in:
                img_data = f_in.read()

            with open(temp_img, "wb") as f_out:
                if patch_size > 0:
                    padding = b"\xff" * patch_size
                    if append_ff:
                        f_out.write(img_data)
                        f_out.write(padding)
                    else:
                        f_out.write(padding)
                        f_out.write(img_data)
                else:
                    f_out.write(img_data)

            if not getattr(self.args, "no_preserve", False):
                self._logger.info("Preserving sections...")
                cur_img = os.path.join(temp_dir, "ec.cur.bin")
                try:
                    subprocess.run(flashrom_args + ["-r", cur_img], check=True)
                    preserve_sections_with_cur_image(
                        cur_img, temp_img, temp_dir
                    )
                except Exception as e:  # pylint: disable=broad-exception-caught
                    self._logger.warning("Section preservation failed: %s", e)

            write_cmd = flashrom_args + ["-w", temp_img]
            if getattr(self.args, "verify", True) is False:
                write_cmd.append("-n")

            self._logger.info(  # pylint: disable=logging-fstring-interpolation
                f"Programming EC firmware image: {' '.join(write_cmd)}"
            )
            subprocess.run(write_cmd, check=True)

    def cleanup_dut(self):
        self._logger.info("Cleaning up DUT after Flashrom")
