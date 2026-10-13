# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Base EC flasher lifecycle abstraction."""

import abc
import argparse
import logging
import time
from typing import Optional

from flash.servo_utils import ServoDriver
from flash.servo_utils import ServoError


logger = logging.getLogger(__name__)


class BaseEcFlasher(abc.ABC):
    """Abstract base class for all EC chip flashing strategies."""

    def __init__(self, servo: Optional[ServoDriver], args: argparse.Namespace):
        self.servo = servo
        self.args = args

    @abc.abstractmethod
    def prepare_dut(self) -> None:
        """Configure servo muxes, power states, and resets prior to flashing."""

    @abc.abstractmethod
    def execute_flash(self) -> None:
        """Execute the external flashing binary to read/write the image."""

    @abc.abstractmethod
    def cleanup_dut(self) -> None:
        """Restore chip-specific states and release resets after flashing."""

    def verify_flash(self) -> None:
        """Verify the written image if not handled natively by the tool."""

    def _try_apshutdown(self) -> None:
        """Send 'apshutdown' to the EC console if --try_apshutdown is set."""
        if not self.servo or not getattr(self.args, "try_apshutdown", False):
            return
        try:
            self.servo.set("ec_uart_regexp", "None")
            count = getattr(self.args, "try_apshutdown_count", 16)
            for _ in range(count):
                self.servo.set("ec_uart_cmd", "apshutdown")
                time.sleep(0.1)
        except ServoError as err:
            logger.warning("Failed to try apshutdown: %s", err)

    def run(self) -> None:
        """Execute the complete flashing lifecycle with guaranteed cleanup."""
        try:
            logger.info("Preparing DUT for flashing...")
            if self.servo:
                # Always release the EC from reset at the end so it boots,
                # even if it was held in reset before flashing.
                self.servo.save_state("cold_reset", override_restore_val="off")
                self._try_apshutdown()

            self.prepare_dut()

            logger.info("Executing flash operation...")
            self.execute_flash()

            if self.args.verify and not self.args.read:
                self.verify_flash()

            logger.info("Flashing completed successfully.")
        finally:
            logger.info("Cleaning up DUT state...")
            try:
                self.cleanup_dut()
            except (ServoError, RuntimeError, OSError) as err:
                logger.error("Error during flasher cleanup: %s", err)

            if self.servo:
                self._try_apshutdown()
                self.servo.restore_state()
