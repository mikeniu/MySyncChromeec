# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Realtek RTK UART EC flasher implementation."""

import logging
import os
import shutil
import subprocess
import tempfile
import time

from flash.flasher_base import BaseEcFlasher
from flash.helpers import EC_DIR
from flash.helpers import locate_tool
from flash.helpers import preserve_sections_with_cur_image
from flash.helpers import resolve_image_writable_ranges
from flash.servo_utils import ServoError


logger = logging.getLogger(__name__)

# Size of the RTS5915 internal flash, read in full when no image is given.
RTK_FLASH_SIZE = 0x100000


class RtkFlasher(BaseEcFlasher):
    """Flasher for Realtek RTK ECs over UART using rtkupdate."""

    def _find_monitor_binary(self) -> str:
        """Locate the rts5915_flash_upload.bin monitor binary."""
        board = getattr(self.args, "board", "")
        img_dir = (
            os.path.dirname(os.path.abspath(self.args.image))
            if self.args.image
            else ""
        )
        search_dirs = [
            img_dir,
            os.path.join(
                EC_DIR, "build", "zephyr", "rtk_flame", "build-singleimage"
            ),
            f"/build/{board}/firmware",
            "/usr/share/ec-devutils",
        ]
        for directory in search_dirs:
            if not directory:
                continue
            cand = os.path.join(directory, "rts5915_flash_upload.bin")
            if os.path.isfile(cand):
                logger.info("Using RTK monitor image: %s", cand)
                return cand
        raise FileNotFoundError("Failed to find rts5915_flash_upload.bin")

    def prepare_dut(self) -> None:
        """Enter Realtek UART bootloader mode and configure UART controls."""
        if not self.servo:
            return
        logger.info("Using Realtek serial bootloader")

        self.servo.set("ec_boot_mode", "on")
        self.servo.set_many([("cold_reset", "on"), ("cold_reset", "off")])
        time.sleep(0.3)
        self.servo.set("ec_boot_mode", "off")
        time.sleep(0.3)

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
        self.servo.set_many([("cold_reset", "on"), ("cold_reset", "off")])

    def execute_flash(self) -> None:
        """Read or write the Realtek EC flash using rtkupdate."""
        if not self.servo:
            raise ValueError("Servo required for Realtek UART")

        rtkupdate_bin = locate_tool("rtkupdate", self.args.board)
        pty = self.servo.get("raw_ec_uart_pty", default="")
        if not pty or "not_applicable" in pty:
            pty = self.servo.get("ec_uart_pty")

        mon_bin = self._find_monitor_binary()
        wp_cmd = [
            rtkupdate_bin,
            "--method=wp",
            "--protect=0",
            f"--uart_device={pty}",
        ]
        frame_cmd = [
            rtkupdate_bin,
            "--method=frame",
            f"--file={mon_bin}",
            f"--uart_device={pty}",
        ]

        if not self.args.dry_run:
            subprocess.run(wp_cmd, check=True)
            subprocess.run(frame_cmd, check=True)

        if self.args.read:
            read_cmd = [
                rtkupdate_bin,
                "--method=read_bin",
                "--spi_start=0x00000000",
                f"--file={self.args.read}",
                f"--uart_device={pty}",
                f"--bin_length=0x{RTK_FLASH_SIZE:x}",
            ]
            logger.info("Reading RTK EC firmware: %s", " ".join(read_cmd))
            if not self.args.dry_run:
                subprocess.run(read_cmd, check=True)
            return

        if not self.args.image or not os.path.exists(self.args.image):
            raise FileNotFoundError(
                f"Firmware image not found: {self.args.image}"
            )

        with tempfile.TemporaryDirectory(prefix="flash_ec.rtk.") as temp_dir:
            target_img = self.args.image
            img_size = os.path.getsize(target_img)
            no_preserve = bool(getattr(self.args, "no_preserve", False))
            areas, _, writable_ranges = resolve_image_writable_ranges(
                target_img,
                ro_only=bool(getattr(self.args, "ro", False)),
                rw_only=bool(getattr(self.args, "rw", False)),
                no_preserve=no_preserve,
            )
            if writable_ranges and writable_ranges != [(0, img_size)]:
                img_bytes = b""
                if not self.args.dry_run:
                    with open(target_img, "rb") as img_f:
                        img_bytes = img_f.read()
                for start, end in writable_ranges:
                    slice_path = os.path.join(
                        temp_dir, f"ec.slice.{start:x}.bin"
                    )
                    if not self.args.dry_run:
                        with open(slice_path, "wb") as slice_f:
                            slice_f.write(img_bytes[start:end])
                    slice_cmd = [
                        rtkupdate_bin,
                        "--method=flash",
                        f"--spi_start=0x{start:08x}",
                        f"--file={slice_path}",
                        f"--uart_device={pty}",
                    ]
                    logger.info(
                        "Programming RTK EC slice [0x%x:0x%x]: %s",
                        start,
                        end,
                        " ".join(slice_cmd),
                    )
                    if not self.args.dry_run:
                        subprocess.run(slice_cmd, check=True)
                if getattr(self.args, "verify", True):
                    logger.info("Verifying RTK EC firmware...")
                    read_verify = os.path.join(temp_dir, "ec.read.bin")
                    ver_cmd = [
                        rtkupdate_bin,
                        "--method=read_bin",
                        "--spi_start=0x00000000",
                        f"--file={read_verify}",
                        f"--uart_device={pty}",
                        f"--bin_length=0x{img_size:x}",
                    ]
                    if not self.args.dry_run:
                        subprocess.run(ver_cmd, check=True)
                        with open(read_verify, "rb") as f_actual:
                            actual_bytes = f_actual.read()
                        for start, end in writable_ranges:
                            if img_bytes[start:end] != actual_bytes[start:end]:
                                raise RuntimeError(
                                    "RTK firmware verification failed! "
                                    "Files do not match."
                                )
                return

            if not no_preserve and not areas:
                cur_img = os.path.join(temp_dir, "ec.cur.bin")
                read_cmd = [
                    rtkupdate_bin,
                    "--method=read_bin",
                    "--spi_start=0x00000000",
                    f"--file={cur_img}",
                    f"--uart_device={pty}",
                    f"--bin_length=0x{img_size:x}",
                ]
                logger.info(
                    "Reading current image via rtkupdate to preserve FMAP "
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

            write_cmd = [
                rtkupdate_bin,
                "--method=flash",
                "--spi_start=0x00000000",
                f"--file={target_img}",
                f"--uart_device={pty}",
            ]

            logger.info("Programming RTK EC firmware: %s", " ".join(write_cmd))
            if not self.args.dry_run:
                subprocess.run(write_cmd, check=True)

            if getattr(self.args, "verify", True):
                logger.info("Verifying RTK EC firmware...")
                read_verify = os.path.join(temp_dir, "ec.read.bin")
                ver_cmd = [
                    rtkupdate_bin,
                    "--method=read_bin",
                    "--spi_start=0x00000000",
                    f"--file={read_verify}",
                    f"--uart_device={pty}",
                    f"--bin_length=0x{img_size:x}",
                ]
                if not self.args.dry_run:
                    subprocess.run(ver_cmd, check=True)
                    with open(target_img, "rb") as f_expected:
                        with open(read_verify, "rb") as f_actual:
                            if f_expected.read() != f_actual.read(img_size):
                                raise RuntimeError(
                                    "RTK firmware verification failed! "
                                    "Files do not match."
                                )
