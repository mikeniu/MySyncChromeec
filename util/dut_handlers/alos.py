# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""ALOS DUT handler implementation using ADB."""

import argparse
import os
import subprocess
import sys
import time
from typing import Any, Dict, List, Optional

from .base import DutHandler


class AlosHandler(DutHandler):
    """Handler for ALOS DUTs using ADB and DTS."""

    def __init__(
        self, details: Dict[str, Any], args: argparse.Namespace, ec_dir: str
    ) -> None:
        super().__init__(details, args, ec_dir)
        self._cached_adb_target: Optional[str] = None

    @property
    def adb_target(self) -> str:
        """Return the target ADB device identifier matching the leased DUT."""
        if self._cached_adb_target:
            return self._cached_adb_target

        dut_hostname = self.details["dut_hostname"]
        try:
            res = subprocess.run(
                ["adb", "devices"],
                check=True,
                capture_output=True,
                text=True,
            )
            for line in res.stdout.splitlines():
                if (
                    dut_hostname in line
                    and "\t" in line
                    and "offline" not in line
                ):
                    dev_id = line.split("\t")[0].strip()
                    if dev_id:
                        self._cached_adb_target = dev_id
                        return dev_id
        except Exception:  # pylint: disable=broad-exception-caught
            pass
        if ":" in dut_hostname:
            target = dut_hostname
        else:
            target = f"{dut_hostname}:5555"
        self._cached_adb_target = target
        return target

    @property
    def android_dir(self) -> str:
        """Return the root path to the Android repository.

        Defaults to the ANDROID_BUILD_TOP environment variable or ~/alos.
        See go/al-care for environment setup instructions.
        """
        if hasattr(self.args, "android_dir") and self.args.android_dir:
            return self.args.android_dir
        return os.environ.get("ANDROID_BUILD_TOP", os.path.expanduser("~/alos"))

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
                if (
                    dut_hostname in line
                    and "device" in line
                    and "offline" not in line
                ):
                    return True
        except subprocess.CalledProcessError:
            pass
        return False

    def _ensure_adb_connected(self, timeout_secs: int = 180) -> None:
        """Connect ADB to the target ALOS DUT and ensure root, retrying if the DUT is rebooting."""
        dut_hostname = self.details["dut_hostname"]
        helper_path = self.corp_adb_helper_path

        start_time = time.time()
        while not self._is_adb_connected():
            self._cached_adb_target = None
            if os.path.exists(helper_path):
                print(
                    f"Connecting ADB to ALOS DUT {dut_hostname} using "
                    f"corp-adb-helper ({helper_path})..."
                )
                res = subprocess.run(
                    [sys.executable, helper_path, dut_hostname, "-f"],
                    check=False,
                )
                if res.returncode == 0 and self._is_adb_connected():
                    break

            if time.time() - start_time > timeout_secs:
                raise RuntimeError(
                    f"Failed to connect ADB to ALOS DUT {dut_hostname} "
                    f"after {timeout_secs} seconds (DUT may still be rebooting)."
                )

            print(
                f"ADB connection to {dut_hostname} not established yet. "
                "Retrying in 5 seconds (DUT may be rebooting)..."
            )
            time.sleep(5)

        print(f"Waiting for ADB device {self.adb_target} to be ready...")
        self._adb(["wait-for-device"], check=False, timeout=180)

        print(f"Ensuring root permissions via ADB on {self.adb_target}...")
        self._adb(["root"], check=False, capture_output=True)
        self._adb(["wait-for-device"], check=False, timeout=180)

    def get_test_targets(self) -> List[str]:
        """Determine the list of test targets for ALOS."""
        if getattr(self.args, "stress", False):
            raise RuntimeError("Stress tests are not supported on ALOS yet.")

        if self.args.all:
            return ["DesktopFirmwareEcHostTestCases"]

        if self.args.test:
            return [self.args.test]

        return [
            "DesktopFirmwareEcHostTestCases:com.google.android.firmware.ec.EcSizeTest"
        ]

    def configure_gbb(self) -> None:
        """Configure dev-mode GBB flags (stubbed for ALOS)."""
        print("Skipping ALOS GBB configuration (stubbed)")

    def _adb(
        self,
        cmd: List[str],
        check: bool = True,
        capture_output: bool = False,
        text: Optional[bool] = None,
        timeout: Optional[int] = None,
        stdout: Optional[int] = None,
        stderr: Optional[int] = None,
    ) -> subprocess.CompletedProcess:
        """Execute an ADB command targeted at the ALOS DUT."""
        return subprocess.run(
            ["adb", "-s", self.adb_target, *cmd],
            check=check,
            capture_output=capture_output,
            text=text,
            timeout=timeout,
            stdout=stdout,
            stderr=stderr,
        )

    def _adb_push(
        self, local_path: str, remote_path: str, check: bool = True
    ) -> subprocess.CompletedProcess:
        """Copy a file or directory to the target ALOS DUT via ADB push."""
        return self._adb(["push", local_path, remote_path], check=check)

    def _adb_shell(
        self, cmd: str, check: bool = True, capture_output: bool = False
    ) -> subprocess.CompletedProcess:
        """Execute a command in adb shell on the target ALOS DUT."""
        full_cmd = f"export PATH=$PATH:/vendor/bin:/system/bin; {cmd}"
        return self._adb(
            ["shell", full_cmd],
            check=check,
            capture_output=capture_output,
            text=capture_output,
        )

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
        self._ensure_adb_connected(timeout_secs=timeout_secs)

        start_time = time.time()
        while True:
            try:
                # 1. Query sys.boot_completed property via ADB
                res_boot = self._adb_shell(
                    "getprop sys.boot_completed", capture_output=True
                )
                boot_state = res_boot.stdout.strip()

                # 2. Query ectool version via ADB
                res_ec = self._adb_shell("ectool version", capture_output=True)
                print("ALOS DUT AP is up and responsive via ADB!")
                if boot_state == "1":
                    print(
                        "Android boot state: boot_completed (sys.boot_completed=1)"
                    )
                print(f"EC version:\n{res_ec.stdout.strip()}")
                return
            except subprocess.CalledProcessError as e:
                if time.time() - start_time > timeout_secs:
                    raise RuntimeError(
                        f"ALOS DUT AP failed to become responsive on {self.adb_target} "
                        f"via ADB after {timeout_secs} seconds."
                    ) from e
                print(
                    "ALOS DUT AP boot not complete yet. Retrying in 5 seconds..."
                )
                time.sleep(5)

    def _get_alos_environment(self) -> Dict[str, str]:
        """Run envsetup.sh and lunch once, returning the captured environment dictionary."""
        if not os.path.isdir(self.android_dir):
            raise FileNotFoundError(
                f"Android directory not found at '{self.android_dir}'. "
                "Please set the ANDROID_BUILD_TOP environment variable, "
                "pass --android-dir, or see go/al-care for setup instructions."
            )

        envsetup_path = os.path.join(self.android_dir, "build", "envsetup.sh")
        if not os.path.exists(envsetup_path):
            raise FileNotFoundError(
                f"Android build environment script not found at '{envsetup_path}'. "
                "Please check your Android repository or see go/al-care."
            )

        model = self.details["model"]
        board = self.details["board"]
        lunch_target = f"{model}-arsp_trunk_staging-eng"

        setup_cmd = (
            f"source build/envsetup.sh >/dev/null && "
            f"export BOARD={model} && "
            f"export PLATFORM={board} && "
            f"export OUT_DIR=out_{model} && "
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
