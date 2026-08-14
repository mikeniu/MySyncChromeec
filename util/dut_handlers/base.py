# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Base class for DUT target handlers."""

from abc import ABC, abstractmethod
import argparse
from enum import Enum
import os
import re
import subprocess
import time
from typing import Any, Dict, List, Optional, Tuple


class DutOsType(str, Enum):
    """Enum representing the supported DUT OS types."""

    CROS = "CrOS"
    ANDROID = "Android"

    def __str__(self) -> str:
        return self.value


# Standard SSH options used across commands
SSH_OPTS = [
    # Disable host key verification prompts for automated lab connections.
    "-o",
    "StrictHostKeyChecking=no",
    # Avoid polluting or checking local ~/.ssh/known_hosts for lab hosts.
    "-o",
    "UserKnownHostsFile=/dev/null",
    # Automatically enable connection sharing (multiplexing) for SSH.
    "-o",
    "ControlMaster=auto",
    # Path template for the shared master SSH control socket file.
    "-o",
    "ControlPath=/tmp/ssh_mux_%h_%p_%r",
    # Keep the master SSH connection alive in the background for 10 minutes.
    "-o",
    "ControlPersist=10m",
    # Fast-fail connection attempts if labstation banner exchange stalls.
    "-o",
    "ConnectTimeout=10",
    # Keepalive options to prevent labstation socket timeouts.
    "-o",
    "ServerAliveInterval=15",
    "-o",
    "ServerAliveCountMax=3",
]


class DutHandler(ABC):
    """Abstract base class handling OS-specific DUT flashing, verification, and testing."""

    def __init__(
        self, details: Dict[str, Any], args: argparse.Namespace, ec_dir: str
    ) -> None:
        self.details = details
        self.args = args
        self.ec_dir = ec_dir

    def _servo_ssh(
        self,
        cmd: str,
        check: bool = True,
        capture_output: bool = False,
        stdout: Optional[int] = None,
        stderr: Optional[int] = None,
    ) -> subprocess.CompletedProcess:
        """Execute a command over SSH on the servo host."""
        servo_hostname = self.details["servo_hostname"]
        ssh_cmd = [
            "ssh",
            *SSH_OPTS,
            f"root@{servo_hostname}",
            cmd,
        ]
        return subprocess.run(
            ssh_cmd,
            check=check,
            capture_output=capture_output,
            text=capture_output,
            stdout=stdout,
            stderr=stderr,
        )

    def _dut_ssh(
        self,
        cmd: str,
        check: bool = True,
        capture_output: bool = False,
        stdout: Optional[int] = None,
        stderr: Optional[int] = None,
        connect_timeout: Optional[int] = None,
    ) -> subprocess.CompletedProcess:
        """Execute a command over SSH on the target DUT host."""
        dut_hostname = self.details["dut_hostname"]
        ssh_cmd = ["ssh", *SSH_OPTS]
        if connect_timeout is not None:
            ssh_cmd.extend(["-o", f"ConnectTimeout={connect_timeout}"])
        ssh_cmd.extend([f"root@{dut_hostname}", cmd])
        return subprocess.run(
            ssh_cmd,
            check=check,
            capture_output=capture_output,
            text=capture_output,
            stdout=stdout,
            stderr=stderr,
        )

    def _dut_scp(
        self,
        local_path: str,
        remote_path: str,
        check: bool = True,
    ) -> subprocess.CompletedProcess:
        """Copy a file to the target DUT host via SCP."""
        dut_hostname = self.details["dut_hostname"]
        destination = f"root@{dut_hostname}:{remote_path}"
        scp_cmd = [
            "scp",
            *SSH_OPTS,
            local_path,
            destination,
        ]
        return subprocess.run(scp_cmd, check=check)

    def ensure_servod_running(self) -> None:
        """Ensure servod is restarted cleanly with correct model configuration."""
        servo_hostname = self.details["servo_hostname"]
        servo_port = self.details["servo_port"]
        board = self.details["board"]
        model = self.details["model"]
        servo_serial = self.details["servo_serial"]

        # 1. Stop any existing servod instance on this port first to ensure clean configuration
        print(
            f"Stopping any existing servod on {servo_hostname} (port {servo_port})..."
        )
        self._servo_ssh(
            f"sudo stop servod PORT={servo_port}",
            check=False,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )

        # 2. Start servod with correct configuration parameters
        print(f"Starting servod on {servo_hostname} (port {servo_port})...")
        self._servo_ssh(
            f"sudo start servod PORT={servo_port} BOARD={board} "
            f"MODEL={model} SERIALNAME={servo_serial}",
            check=False,
        )

        # 3. Wait for active
        print(f"Waiting for servod on port {servo_port} to become active...")
        try:
            self._servo_ssh(
                f"servodtool instance wait-for-active --port {servo_port} --timeout 60"
            )
            # Give servod a brief pause to settle USB/CCD interface initialization
            time.sleep(3)
        except subprocess.CalledProcessError:
            self._recover_servod()

    def _find_uhubctl_target(self, servo_serial: str) -> Tuple[str, str]:
        """Find hub location and port in `sudo uhubctl` matching the device's USB sysfs path."""
        if not servo_serial:
            raise RuntimeError(
                "Cannot recover servod: servo_serial is missing from lease configuration"
            )

        # 1. Query the USB sysfs path via servodtool
        res_path = self._servo_ssh(
            f"servodtool device -s {servo_serial} usb-path",
            capture_output=True,
            check=False,
        )
        usb_path = res_path.stdout.strip()
        if not usb_path:
            raise RuntimeError(
                f"Failed to find USB sysfs path for servo device {servo_serial}"
            )

        # 2. Get active hubs listed by uhubctl
        res_hub = self._servo_ssh(
            "sudo uhubctl",
            capture_output=True,
            check=False,
        )
        uhubctl_out = res_hub.stdout
        if not uhubctl_out:
            raise RuntimeError(
                "Failed to list USB hubs: sudo uhubctl command returned empty output"
            )

        hub_re = re.compile(r"hub (\S+)\s*\[")

        # 3. Match usb_path against uhubctl hubs
        dev_name = os.path.basename(usb_path.rstrip("/"))
        hubs = [
            m.group(1)
            for line in uhubctl_out.splitlines()
            if (m := hub_re.search(line))
        ]

        # Find the longest hub location in uhubctl that is a parent prefix of dev_name
        matching_target = None
        max_len = -1
        for hub in hubs:
            prefix = f"{hub}."
            if dev_name.startswith(prefix):
                remainder = dev_name[len(prefix) :]
                port = remainder.split(".")[0]
                if len(hub) > max_len:
                    max_len = len(hub)
                    matching_target = (hub, port)

        if matching_target:
            return matching_target

        raise RuntimeError(
            f"Could not determine uhubctl target for servo {servo_serial} "
            f"(usb-path: {usb_path}). No matching hub found in uhubctl output."
        )

    def _recover_servod(self) -> None:
        """Attempt to recover servod by power-cycling the USB port via uhubctl."""
        servo_hostname = self.details["servo_hostname"]
        servo_port = self.details["servo_port"]
        board = self.details["board"]
        model = self.details["model"]
        servo_serial = self.details["servo_serial"]

        print(
            f"Warning: servod on port {servo_port} failed to become active. "
            "Attempting recovery by power cycling USB port via uhubctl..."
        )

        # 1. Stop servod
        self._servo_ssh(
            f"sudo stop servod PORT={servo_port}",
            check=False,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )

        # 2. Find uhubctl target (raises RuntimeError on failure)
        hub_loc, port = self._find_uhubctl_target(servo_serial)
        print(f"Power cycling USB port {port} on hub {hub_loc} via uhubctl...")
        self._servo_ssh(
            f"sudo uhubctl -l {hub_loc} -p {port} -a cycle",
        )

        time.sleep(5)

        # 3. Restart servod
        self._servo_ssh(
            f"sudo start servod PORT={servo_port} BOARD={board} "
            f"MODEL={model} SERIALNAME={servo_serial}",
            check=False,
        )
        try:
            self._servo_ssh(
                f"servodtool instance wait-for-active --port {servo_port} --timeout 60"
            )
            time.sleep(3)
        except subprocess.CalledProcessError as e:
            raise RuntimeError(
                f"servod failed to become active on {servo_hostname} "
                f"(port {servo_port}) even after recovery attempt: {e}"
            ) from e

    def verify_ec_up(self) -> None:
        """Query the EC console via servo to verify the EC is up and responsive."""
        servo_hostname = self.details["servo_hostname"]
        servo_port = self.details["servo_port"]
        print(
            f"Verifying EC is up and responsive on {servo_hostname} (port {servo_port})..."
        )
        for attempt in range(1, 6):
            try:
                result = self._servo_ssh(
                    f"dut-control -p {servo_port} ec_board",
                    capture_output=True,
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
        print(f"Running GBB configuration on {servo_hostname}...")
        try:
            self._servo_ssh(
                f"futility gbb -s --flash --flags +0x39 --servo_port {servo_port}"
            )
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
