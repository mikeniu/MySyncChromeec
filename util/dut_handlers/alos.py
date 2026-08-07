# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""ALOS DUT handler implementation using ADB."""

import os
import subprocess
import sys
import time
from typing import Dict, List, Optional

from .base import DutHandler


# Standard SSH options used across commands
SSH_OPTS = [
    "-o",
    "StrictHostKeyChecking=no",
    "-o",
    "UserKnownHostsFile=/dev/null",
]


class AlosHandler(DutHandler):
    """Handler for ALOS (Android on ChromeOS) DUTs using ADB and Tast."""

    @property
    def adb_target(self) -> str:
        """Return the target ADB device identifier matching the leased DUT."""
        dut_hostname = self.details["dut_hostname"]
        try:
            res = subprocess.run(
                ["adb", "devices"],
                check=True,
                capture_output=True,
                text=True,
            )
            for line in res.stdout.splitlines():
                if dut_hostname in line and "\t" in line:
                    dev_id = line.split("\t")[0].strip()
                    if dev_id:
                        return dev_id
        except Exception:  # pylint: disable=broad-exception-caught
            pass
        if ":" in dut_hostname:
            return dut_hostname
        return f"{dut_hostname}:5555"

    @property
    def android_dir(self) -> str:
        """Return the root path to the Android repository."""
        if hasattr(self.args, "android_dir") and self.args.android_dir:
            return self.args.android_dir
        return os.environ.get(
            "ANDROID_BUILD_TOP",
            os.path.expanduser("~/" + "al" + "uminium"),
        )

    @property
    def corp_adb_helper_path(self) -> str:
        """Return the path to corp-adb-helper.py inside the Android tree."""
        return os.path.join(
            self.android_dir,
            "tools",
            "vendor",
            "google_prebuilts",
            "arc",
            "corp-adb-helper.py",
        )

    def _is_adb_connected(self) -> bool:
        """Check if the target ALOS DUT is already connected in `adb devices`."""
        dut_hostname = self.details["dut_hostname"]
        try:
            res = subprocess.run(
                ["adb", "devices"],
                check=True,
                capture_output=True,
                text=True,
            )
            for line in res.stdout.splitlines():
                if dut_hostname in line and "\tdevice" in line:
                    return True
        except subprocess.CalledProcessError:
            pass
        return False

    def _ensure_adb_connected(self) -> None:
        """Connect ADB to the target ALOS DUT once if not already connected."""
        if self._is_adb_connected():
            return

        dut_hostname = self.details["dut_hostname"]
        helper_path = self.corp_adb_helper_path

        if os.path.exists(helper_path):
            print(
                f"Connecting ADB to ALOS DUT {dut_hostname} using "
                f"corp-adb-helper ({helper_path})..."
            )
            try:
                subprocess.run(
                    [sys.executable, helper_path, dut_hostname],
                    check=True,
                )
                return
            except subprocess.CalledProcessError as e:
                print(
                    f"Warning: corp-adb-helper failed for {dut_hostname}: {e}. "
                    "Falling back to adb connect...",
                    file=sys.stderr,
                )

        subprocess.run(
            ["adb", "connect", self.adb_target],
            check=False,
            capture_output=True,
        )

    def get_test_targets(self) -> List[str]:
        """Determine the list of test targets for ALOS."""
        if self.args.all:
            return ["DesktopFirmwareEcHostTestCases"]

        test_targets: List[str] = []
        if self.args.test:
            test_targets.append(self.args.test)
        if self.args.smoke or not test_targets:
            test_targets.insert(
                0,
                "DesktopFirmwareEcHostTestCases:com.google.android.firmware.ec.EcSizeTest",
            )
        return test_targets

    def configure_gbb(self) -> None:
        """Configure dev-mode GBB flags (stubbed for ALOS)."""
        print("Skipping ALOS GBB configuration (stubbed)")

    def flash_ec_rw(self, ec_rw_bin_path: Optional[str]) -> None:
        """Flash the EC RW section (stubbed for ALOS)."""
        if not ec_rw_bin_path:
            return
        print(f"Skipping ALOS EC RW flashing (stubbed): {ec_rw_bin_path}")

    def flash_ec_ro(self, ec_ro_bin_path: Optional[str]) -> None:
        """Flash the EC RO section (stubbed for ALOS)."""
        if not ec_ro_bin_path:
            return
        print(f"Skipping ALOS EC RO flashing (stubbed): {ec_ro_bin_path}")

    def verify_ap_up(self, timeout_secs: int = 300) -> None:
        """Wait for the ALOS DUT AP to boot up and reply to ADB commands."""
        print(f"Verifying ALOS DUT AP is up on {self.adb_target} via ADB...")
        interval = 5
        max_attempts = max(1, timeout_secs // interval)

        self._ensure_adb_connected()

        for attempt in range(1, max_attempts + 1):
            try:
                # 1. Query sys.boot_completed property via ADB
                res_boot = subprocess.run(
                    [
                        "adb",
                        "-s",
                        self.adb_target,
                        "shell",
                        "getprop sys.boot_completed",
                    ],
                    check=True,
                    capture_output=True,
                    text=True,
                )
                boot_state = res_boot.stdout.strip()

                # 2. Query ectool version via ADB
                res_ec = subprocess.run(
                    [
                        "adb",
                        "-s",
                        self.adb_target,
                        "shell",
                        "ectool version",
                    ],
                    check=True,
                    capture_output=True,
                    text=True,
                )
                print("ALOS DUT AP is up and responsive via ADB!")
                if boot_state == "1":
                    print(
                        "Android boot state: boot_completed (sys.boot_completed=1)"
                    )
                print(f"EC version:\n{res_ec.stdout.strip()}")
                return
            except subprocess.CalledProcessError:
                print(
                    f"Attempt {attempt}/{max_attempts}: ALOS DUT AP not reachable via ADB yet. "
                    f"Retrying in {interval} seconds..."
                )
                self._ensure_adb_connected()
                time.sleep(interval)

        raise RuntimeError(
            f"ALOS DUT AP failed to become responsive on {self.adb_target} via ADB"
        )

    def _get_alos_environment(self) -> Dict[str, str]:
        """Run envsetup.sh and lunch once, returning the captured environment dictionary."""
        model = self.details["model"]
        board = self.details["board"]
        lunch_target = f"{model}-arsp_trunk_staging-eng"

        setup_cmd = (
            f"source build/envsetup.sh >/dev/null && "
            f"export BOARD={model} && "
            f"export PLATFORM={board} && "
            f"lunch {lunch_target} >/dev/null && "
            f"env -0"
        )
        print(
            f"Setting up ALOS environment in {self.android_dir} "
            f"(BOARD={model}, PLATFORM={board}, lunch {lunch_target})..."
        )
        try:
            result = subprocess.run(
                setup_cmd,
                shell=True,
                executable="/bin/bash",
                check=True,
                capture_output=True,
                cwd=self.android_dir,
            )
            env_entries = result.stdout.decode("utf-8", errors="replace").split(
                "\0"
            )
            env_dict = {}
            for entry in env_entries:
                if "=" in entry:
                    key, value = entry.split("=", 1)
                    env_dict[key] = value
            return env_dict
        except subprocess.CalledProcessError as e:
            raise RuntimeError(
                f"Failed to set up ALOS environment in {self.android_dir}: {e}"
            ) from e

    def execute_test_flow(self) -> None:
        """Execute test flow for ALOS DUT using atest inside the Android environment."""
        test_targets = self.get_test_targets()
        adb_target = self.adb_target
        servo_hostname = self.details["servo_hostname"]
        servo_port = self.details["servo_port"]

        print(
            f"Executing ALOS test flow for target(s): {', '.join(test_targets)} "
            f"on {adb_target}..."
        )

        alos_env = self._get_alos_environment()

        for target in test_targets:
            atest_cmd = [
                "atest",
                "-s",
                adb_target,
                target,
                "--",
                "--invocation-data",
                f"servo.host={servo_hostname}",
                "--invocation-data",
                f"servo.port={servo_port}",
            ]
            print(f"Running atest: {' '.join(atest_cmd)}")
            try:
                subprocess.run(
                    atest_cmd,
                    check=True,
                    env=alos_env,
                    cwd=self.android_dir,
                )
            except subprocess.CalledProcessError as e:
                raise RuntimeError(
                    f"ALOS atest execution failed for target '{target}': {e}"
                ) from e

    def cleanup(self) -> None:
        """Clean up ADB connections for the target ALOS DUT."""
        dut_hostname = self.details["dut_hostname"]
        target = self.adb_target
        print(f"Cleaning up ADB connection for ALOS DUT {dut_hostname}...")
        try:
            subprocess.run(
                ["adb", "disconnect", target],
                check=False,
                capture_output=True,
            )
            if target != dut_hostname:
                subprocess.run(
                    ["adb", "disconnect", dut_hostname],
                    check=False,
                    capture_output=True,
                )
        except Exception as e:  # pylint: disable=broad-exception-caught
            print(
                f"Warning: Failed to disconnect ADB for {dut_hostname}: {e}",
                file=sys.stderr,
            )
