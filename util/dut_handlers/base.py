# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Base class for DUT target handlers."""

from abc import ABC, abstractmethod
import argparse
from enum import Enum
import subprocess
import time
from typing import Any, Dict, List, Optional


class DutOsType(str, Enum):
    """Enum representing the supported DUT OS types."""

    CROS = "CrOS"
    ANDROID = "Android"

    def __str__(self) -> str:
        return self.value


# Standard SSH options used across commands
SSH_OPTS = [
    "-o",
    "StrictHostKeyChecking=no",
    "-o",
    "UserKnownHostsFile=/dev/null",
]


class DutHandler(ABC):
    """Abstract base class handling OS-specific DUT flashing, verification, and testing."""

    def __init__(
        self, details: Dict[str, Any], args: argparse.Namespace, ec_dir: str
    ) -> None:
        self.details = details
        self.args = args
        self.ec_dir = ec_dir

    def ensure_servod_running(self) -> None:
        """Ensure servod is restarted cleanly with correct model configuration."""
        servo_hostname = self.details["servo_hostname"]
        servo_port = self.details["servo_port"]
        board = self.details["board"]
        model = self.details["model"]
        servo_serial = self.details["servo_serial"]

        # 1. Stop any existing servod instance on this port first to ensure clean configuration
        stop_cmd = [
            "ssh",
            *SSH_OPTS,
            f"root@{servo_hostname}",
            f"sudo stop servod PORT={servo_port}",
        ]
        print(
            f"Stopping any existing servod on {servo_hostname} (port {servo_port})..."
        )
        subprocess.run(
            stop_cmd,
            check=False,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )

        # 2. Start servod with correct configuration parameters
        start_cmd = [
            "ssh",
            *SSH_OPTS,
            f"root@{servo_hostname}",
            f"sudo start servod PORT={servo_port} BOARD={board} "
            f"MODEL={model} SERIALNAME={servo_serial}",
        ]
        print(f"Starting servod on {servo_hostname} (port {servo_port})...")
        subprocess.run(start_cmd, check=False)

        # 3. Wait for active
        wait_cmd = [
            "ssh",
            *SSH_OPTS,
            f"root@{servo_hostname}",
            f"servodtool instance wait-for-active --port {servo_port} --timeout 60",
        ]
        print(f"Waiting for servod on port {servo_port} to become active...")
        try:
            subprocess.run(wait_cmd, check=True)
        except subprocess.CalledProcessError as e:
            raise RuntimeError(
                f"servod failed to become active on {servo_hostname} "
                f"(port {servo_port}): {e}"
            ) from e

    def verify_ec_up(self) -> None:
        """Query the EC console via servo to verify the EC is up and responsive."""
        servo_hostname = self.details["servo_hostname"]
        servo_port = self.details["servo_port"]
        ssh_cmd = [
            "ssh",
            *SSH_OPTS,
            f"root@{servo_hostname}",
            f"dut-control -p {servo_port} ec_board",
        ]
        print(
            f"Verifying EC is up and responsive on {servo_hostname} (port {servo_port})..."
        )
        for attempt in range(1, 6):
            try:
                result = subprocess.run(
                    ssh_cmd, capture_output=True, text=True, check=True
                )
                print(f"EC is responsive: {result.stdout.strip()}")
                return
            except subprocess.CalledProcessError as e:
                err_msg = e.stderr.strip() if e.stderr else str(e)
                print(
                    f"Attempt {attempt}/5: EC not responsive yet (error: "
                    f"{err_msg}). Retrying in 2 seconds..."
                )
                time.sleep(2)
        raise RuntimeError(
            f"EC failed to become responsive after flashing on {servo_hostname}"
        )

    @abstractmethod
    def get_test_targets(self) -> List[str]:
        """Determine the list of test targets for this OS target."""

    def configure_gbb(self) -> None:
        """Configure dev-mode GBB flags via servo.

        The flag value 0x39 is a bitmask enabling:
          - 0x0001 (GBB_FLAG_DEV_SCREEN_SHORT_DELAY): Shortens the dev screen warning.
          - 0x0008 (GBB_FLAG_FORCE_DEV_SWITCH_ON): Forces developer mode active.
          - 0x0010 (GBB_FLAG_FORCE_DEV_BOOT_USB): Allows booting from USB drives.
          - 0x0020 (GBB_FLAG_DISABLE_ROLLBACK_CHECK): Bypasses version rollback checks.
        """
        servo_hostname = self.details["servo_hostname"]
        servo_port = self.details["servo_port"]
        ssh_cmd = [
            "ssh",
            *SSH_OPTS,
            f"root@{servo_hostname}",
            f"futility gbb -s --flash --flags +0x39 --servo_port {servo_port}",
        ]
        print(f"Running GBB configuration on {servo_hostname}...")
        try:
            subprocess.run(ssh_cmd, check=True)
        except subprocess.CalledProcessError as e:
            raise RuntimeError(
                f"Failed to set GBB flags on {servo_hostname}: {e}"
            ) from e

    @abstractmethod
    def flash_ec_ro(self, ec_ro_bin_path: Optional[str]) -> None:
        """Flash the EC RO section locally on the DUT."""

    @abstractmethod
    def flash_ec_rw(self, ec_rw_bin_path: Optional[str]) -> None:
        """Flash the EC RW section locally on the DUT."""

    @abstractmethod
    def verify_ap_up(self, timeout_secs: int = 300) -> None:
        """Verify that the AP is up and reachable."""

    @abstractmethod
    def execute_test_flow(self) -> None:
        """Execute the test flow for the target OS."""
