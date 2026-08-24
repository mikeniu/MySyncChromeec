#!/usr/bin/env vpython3

"""Module docstring."""

# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
import abc
import logging
import shutil  # pylint: disable=wrong-import-order
import subprocess
import time  # pylint: disable=wrong-import-order

from .servo_utils import ServoDriver


class BaseEcFlasher(abc.ABC):
    """
    Abstract base class defining the Template Method for EC flashing.
    """

    def __init__(self, servo: ServoDriver, args):
        self.servo = servo
        self.args = args  # CLI arguments object mapping config overrides
        self._logger = logging.getLogger(self.__class__.__name__)

    def prepare_dut(self):
        """Put the DUT into a flashable state (e.g., assert resets)."""
        pass  # pylint: disable=unnecessary-pass

    @abc.abstractmethod
    def execute_flash(self):
        """Invoke the target flashing tool."""
        pass  # pylint: disable=unnecessary-pass

    def cleanup_dut(self):
        """Restore the DUT to a bootable state."""
        pass  # pylint: disable=unnecessary-pass

    def run(self):
        """Unified Code Flow Path."""
        board = getattr(self.args, "board", "")
        self._logger.info("Starting EC flash sequence for board: %s", board)
        try:
            self._logger.info("Applying Deep Cr50/GSC Hacks")
            # Ensure cr50-update service is stopped.
            # Avoid using 'sudo -n' to allow interactive terminals to prompt for password,
            # while CI/CD environments should have NOPASSWD configured.

            # Check if initctl is available
            if shutil.which("initctl"):
                try:
                    subprocess.run(  # pylint: disable=subprocess-run-check
                        ["sudo", "initctl", "stop", "cr50-update"],
                        stderr=subprocess.DEVNULL,
                        stdout=subprocess.DEVNULL,
                    )
                except Exception:  # pylint: disable=broad-exception-caught
                    pass

            if shutil.which("systemctl"):
                try:
                    subprocess.run(  # pylint: disable=subprocess-run-check
                        ["sudo", "systemctl", "stop", "cr50-update"],
                        stderr=subprocess.DEVNULL,
                        stdout=subprocess.DEVNULL,
                    )
                except Exception:  # pylint: disable=broad-exception-caught
                    pass

            # Drop write protection securely
            if self.servo:
                try:
                    self.servo.set("fw_wp_state", "force_off")
                    self._logger.info("Successfully forced fw_wp_state off.")
                except Exception as e:  # pylint: disable=broad-exception-caught
                    self._logger.warning(  # pylint: disable=logging-fstring-interpolation
                        f"Could not force_off fw_wp_state: {e}"
                    )

            if getattr(self.args, "try_apshutdown", False):
                self._logger.info("Attempting to shut down the AP...")
                if self.servo:
                    count = getattr(self.args, "try_apshutdown_count", 16)
                    try:
                        self.servo.save_add("ec_uart_regexp")
                        self.servo.set("ec_uart_regexp", "None")
                    except Exception as e:
                        self._logger.warning(
                            "Error setting ec_uart_regexp: %s", e
                        )
                    for _ in range(count):
                        try:
                            self.servo.set("ec_uart_cmd", "apshutdown")
                        except (
                            Exception
                        ) as e:  # pylint: disable=broad-exception-caught
                            self._logger.warning(
                                "Error during AP shutdown attempt: %s", e
                            )
                        time.sleep(1)

            if self.servo:
                self.prepare_dut()

            self.execute_flash()

            self._logger.info("EC Flash completed successfully.")
        except Exception as e:  # pylint: disable=broad-exception-caught
            self._logger.error("EC Flash failed: %s", e)
            raise
        finally:
            if self.servo:
                self.cleanup_dut()
                self.servo.restore_all()
