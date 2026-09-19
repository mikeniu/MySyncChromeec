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
            return os.path.abspath(os.path.expanduser(self.args.android_dir))
        return os.path.abspath(
            os.path.expanduser(os.environ.get("ANDROID_BUILD_TOP", "~/alos"))
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

        if not self._is_adb_connected():
            if not os.path.isdir(self.android_dir):
                raise FileNotFoundError(
                    f"Android directory not found at '{self.android_dir}'. "
                    "Cannot run corp-adb-helper to connect ADB to DUT. "
                    "Please set the ANDROID_BUILD_TOP environment variable, "
                    "pass --android-dir, or see go/al-care for setup instructions."
                )
            if not os.path.exists(helper_path):
                raise FileNotFoundError(
                    f"corp-adb-helper script not found at '{helper_path}'. "
                    "Cannot connect ADB to DUT. "
                    "Please check your Android repository or see go/al-care."
                )

        start_time = time.time()
        while not self._is_adb_connected():
            self._cached_adb_target = None
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
        """Configure dev-mode GBB flags locally on the ALOS DUT via ADB shell.

        Note: Configuring GBB flags through the servo host can cause the DUT to
        reboot, which causes issues with flashing and running tests on ALOS DUTs.
        Thus GBB flags are configured locally via ADB shell instead.

        The flag value 0x39 is a bitmask enabling:
          - 0x0001 (GBB_FLAG_DEV_SCREEN_SHORT_DELAY): Shortens the dev screen warning.
          - 0x0008 (GBB_FLAG_FORCE_DEV_SWITCH_ON): Forces developer mode active.
          - 0x0010 (GBB_FLAG_FORCE_DEV_BOOT_USB): Allows booting from USB drives.
          - 0x0020 (GBB_FLAG_DISABLE_ROLLBACK_CHECK): Bypasses version rollback checks.
        """
        self._ensure_adb_connected()
        print(
            f"Configuring GBB flags on ALOS DUT via ADB ({self.adb_target})..."
        )
        try:
            self._adb_shell("futility gbb -s --flash --flags +0x39")
        except subprocess.CalledProcessError as e:
            raise RuntimeError(
                f"Failed to set GBB flags on ALOS DUT via ADB: {e}"
            ) from e

    def _copy_ec_rw_bin_to_dut(self, ec_rw_bin_path: str) -> None:
        """Copy the local EC RW binary and ec.config to the target DUT via ADB."""
        if not os.path.exists(ec_rw_bin_path):
            raise FileNotFoundError(
                f"ec.bin not found at {ec_rw_bin_path}. Did you build the project?"
            )

        self._ensure_adb_connected()
        model = self.details["model"]
        destination = f"/data/local/tmp/ec_rw_{model}.bin"
        print(
            f"Pushing {ec_rw_bin_path} to ALOS DUT via ADB ({self.adb_target}:{destination})..."
        )
        try:
            self._adb_push(ec_rw_bin_path, destination)

            ec_config_path = os.path.join(
                os.path.dirname(ec_rw_bin_path), "ec.config"
            )
            if os.path.exists(ec_config_path):
                cfg_destination = f"/data/local/tmp/ecrw_cfg_{model}.bin"
                print(
                    f"Pushing {ec_config_path} to ALOS DUT via ADB "
                    f"({self.adb_target}:{cfg_destination})..."
                )
                self._adb_push(ec_config_path, cfg_destination)
        except subprocess.CalledProcessError as e:
            raise RuntimeError(
                f"Failed to copy ec.bin or ec.config to ALOS DUT via ADB: {e}"
            ) from e

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

    def _swap_ec_rw_on_dut(self, ap_path: str, ec_rw_path: str) -> None:
        """Replace EC RW in AP image on DUT using cbfstool expand and futility."""
        model = self.details["model"]
        tmp_dir = "/data/local/tmp"

        tmp_bin = f"{tmp_dir}/ecrw_extracted_{model}.bin"
        tmp_ver = f"{tmp_dir}/ecrw_ver_{model}.bin"
        tmp_hash = f"{tmp_dir}/ecrw_hash_{model}.bin"
        tmp_cfg = f"{tmp_dir}/ecrw_cfg_{model}.bin"

        print(
            f"Swapping EC RW in AP firmware image {ap_path} using cbfstool..."
        )

        try:
            # 1. Extract raw EC RW, version, and config from input binary
            self._adb_shell(
                f"futility dump_fmap -x {ec_rw_path} RW_FW:{tmp_bin} RW_FWID:{tmp_ver}",
                check=False,
            )

            # 2. Compute binary SHA-256 hash of ecrw
            self._adb_shell(
                f"sha256sum -b {tmp_bin} | xxd -r -p > {tmp_hash}",
                check=False,
            )

            # 3. For CBFS regions, remove old files, expand, and add updated
            for region in ("FW_MAIN_A", "FW_MAIN_B"):
                # Remove existing files
                for name in (
                    "ecrw",
                    "ecrw.hash",
                    "ecrw.version",
                    "ecrw.config",
                ):
                    self._adb_shell(
                        f"cbfstool {ap_path} remove -r {region} -n {name}",
                        check=False,
                    )

                # Reclaim free space in CBFS
                self._adb_shell(
                    f"cbfstool {ap_path} expand -r {region}",
                    check=False,
                )

                # Add updated files
                self._adb_shell(
                    f"cbfstool {ap_path} add -r {region} -t raw -c LZMA "
                    f"-f {tmp_bin} -n ecrw"
                )

                res_hash = self._adb_shell(f"[ -s {tmp_hash} ]", check=False)
                if res_hash.returncode == 0:
                    self._adb_shell(
                        f"cbfstool {ap_path} add -r {region} -t raw -c none "
                        f"-f {tmp_hash} -n ecrw.hash"
                    )

                res_ver = self._adb_shell(f"[ -s {tmp_ver} ]", check=False)
                if res_ver.returncode == 0:
                    self._adb_shell(
                        f"cbfstool {ap_path} add -r {region} -t raw -c none "
                        f"-f {tmp_ver} -n ecrw.version"
                    )

                res_cfg = self._adb_shell(f"[ -s {tmp_cfg} ]", check=False)
                if res_cfg.returncode == 0:
                    self._adb_shell(
                        f"cbfstool {ap_path} add -r {region} -t raw -c LZMA "
                        f"-f {tmp_cfg} -n ecrw.config"
                    )
        finally:
            self._adb_shell(
                f"rm -f {tmp_bin} {tmp_ver} {tmp_hash} {tmp_cfg}", check=False
            )

    def _sign_ap_image_on_dut(self, ap_path: str) -> None:
        """Re-sign the AP image on the DUT using futility sign with devkeys."""
        keys_dir = "/data/local/tmp/devkeys"
        host_keys_dir = os.path.join(
            self.android_dir,
            "external",
            "vboot_reference",
            "tests",
            "devkeys",
        )
        if os.path.exists(host_keys_dir):
            print(f"Pushing devkeys to ALOS DUT ({keys_dir})...")
            self._adb_push(host_keys_dir, keys_dir)

        print(f"Re-signing AP firmware image {ap_path} using futility sign...")
        self._adb_shell(f"futility sign --keyset {keys_dir} {ap_path}")

    def flash_ec_rw(self, ec_rw_bin_path: Optional[str]) -> None:
        """Flash the EC RW section on the ALOS DUT using futility via ADB shell."""
        if not ec_rw_bin_path:
            return

        self._ensure_adb_connected()
        model = self.details["model"]
        self._copy_ec_rw_bin_to_dut(ec_rw_bin_path)

        print(f"Flashing EC RW on ALOS DUT via ADB ({self.adb_target})...")
        try:
            ap_tmp_path = f"/data/local/tmp/ap_{model}.bin"
            print("Reading AP firmware image on ALOS DUT via ADB...")
            self._adb_shell(f"futility read {ap_tmp_path}")

            print("Swapping EC RW section in AP firmware image on ALOS DUT...")
            self._swap_ec_rw_on_dut(
                ap_tmp_path,
                f"/data/local/tmp/ec_rw_{model}.bin",
            )

            print("Re-signing AP firmware image on ALOS DUT...")
            self._sign_ap_image_on_dut(ap_tmp_path)

            print("Updating AP firmware with new EC RW image via futility...")
            self._adb_shell(f"futility update --fast -i {ap_tmp_path}")

            print(f"Rebooting ALOS DUT via ADB ({self.adb_target})...")
            self._adb_shell("reboot", check=False)
        except subprocess.CalledProcessError as e:
            raise RuntimeError(
                f"Flashing EC RW on ALOS DUT failed via ADB futility: {e}"
            ) from e
        finally:
            self._adb_shell(
                f"rm -rf {ap_tmp_path} /data/local/tmp/ec_rw_{model}.bin "
                "/data/local/tmp/devkeys",
                check=False,
            )

    def flash_ec_ro(self, ec_ro_bin_path: Optional[str]) -> None:
        """Flash the EC RO section on the ALOS DUT using futility via ADB shell."""
        if not ec_ro_bin_path:
            return

        self._ensure_adb_connected()
        model = self.details["model"]
        destination = f"/data/local/tmp/ec_ro_{model}.bin"
        print(
            f"Pushing EC RO binary {ec_ro_bin_path} to ALOS DUT via ADB ({self.adb_target})..."
        )
        try:
            self._adb_push(ec_ro_bin_path, destination)
            print(
                f"Flashing EC RO on ALOS DUT using futility via ADB ({self.adb_target})..."
            )
            self._adb_shell(
                f"futility update --ec_image {destination} || "
                f"flashrom -p ec -w {destination}"
            )
            self._adb_shell("ectool reboot_ec", check=False)
        except subprocess.CalledProcessError as e:
            raise RuntimeError(
                f"Flashing EC RO on ALOS DUT failed via ADB: {e}"
            ) from e
        finally:
            self._adb_shell(f"rm -f {destination}", check=False)

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
