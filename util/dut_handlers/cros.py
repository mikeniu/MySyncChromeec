# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""CrOS DUT target handler implementation."""

from contextlib import contextmanager
import os
import signal
import socket
import subprocess
import sys
import time
from typing import Iterator, List, Optional, Tuple

from .base import DutHandler


class CrosHandler(DutHandler):
    """Handler for Classic CrOS DUTs using Tast, futility, and flashrom."""

    def get_test_targets(self) -> List[str]:
        """Determine the list of Tast test targets based on command line options."""
        if self.args.all:
            return ["(firmware_ec)"]

        test_targets: List[str] = []
        if self.args.test:
            test_targets.append(self.args.test)
        if self.args.stress:
            test_targets.extend(
                [
                    "firmware.EcStress.flash",
                    "firmware.EcStress.keyscan",
                    "firmware.EcStress.pd",
                    "firmware.EcStress.sensors",
                    "firmware.EcStress.suspend",
                ]
            )
        if self.args.smoke or not test_targets:
            test_targets.insert(0, "firmware.ECSize")
        return test_targets

    def _copy_ec_rw_bin_to_dut(self, ec_rw_bin_path: str) -> None:
        """Copy the local EC RW binary to the target DUT's /tmp directory."""
        if not os.path.exists(ec_rw_bin_path):
            raise FileNotFoundError(
                f"ec.bin not found at {ec_rw_bin_path}. Did you build the project?"
            )

        dut_hostname = self.details["dut_hostname"]
        model = self.details["model"]
        print(
            f"Copying {ec_rw_bin_path} to root@{dut_hostname}:/tmp/ec_rw_{model}.bin..."
        )
        try:
            self._dut_scp(ec_rw_bin_path, f"/tmp/ec_rw_{model}.bin")
        except subprocess.CalledProcessError as e:
            raise RuntimeError(f"Failed to copy ec.bin to DUT: {e}") from e

    def _delete_ec_rw_bin_from_dut(self) -> None:
        """Delete the temporary EC RW binary file from the DUT."""
        dut_hostname = self.details["dut_hostname"]
        model = self.details["model"]
        print(f"Cleaning up /tmp/ec_rw_{model}.bin from DUT {dut_hostname}...")
        try:
            self._dut_ssh(f"rm -f /tmp/ec_rw_{model}.bin")
        except subprocess.CalledProcessError as e:
            print(
                f"Warning: Failed to delete /tmp/ec_rw_{model}.bin on DUT: {e}",
                file=sys.stderr,
            )

    def _reboot_dut(self) -> None:
        """Reboot the DUT to trigger Software Sync update of the EC."""
        dut_hostname = self.details["dut_hostname"]
        print(f"Rebooting DUT {dut_hostname} to trigger Software Sync...")
        try:
            self._dut_ssh(
                "reboot",
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
            time.sleep(5)
        except subprocess.CalledProcessError:
            pass

    def flash_ec_rw(self, ec_rw_bin_path: Optional[str]) -> None:
        """Flash the EC RW by reading, swapping, and updating the AP firmware locally on the DUT."""
        if not ec_rw_bin_path:
            return

        dut_hostname = self.details["dut_hostname"]
        model = self.details["model"]
        self._copy_ec_rw_bin_to_dut(ec_rw_bin_path)
        try:
            print(f"Reading AP firmware image locally on DUT {dut_hostname}...")
            self._dut_ssh(f"futility read /tmp/ap_{model}.bin")

            print(
                "Populating AP firmware image with custom EC binary using "
                "swap_ec_rw locally on DUT..."
            )
            self._dut_ssh(
                f"/usr/share/vboot/bin/swap_ec_rw "
                f"-i /tmp/ap_{model}.bin -e /tmp/ec_rw_{model}.bin"
            )

            print(
                f"Writing modified AP firmware image back locally on DUT {dut_hostname}..."
            )
            self._dut_ssh(f"futility update --fast -i /tmp/ap_{model}.bin")

            self._dut_ssh(f"rm -f /tmp/ap_{model}.bin", check=False)

            self._reboot_dut()

        except subprocess.CalledProcessError as e:
            raise RuntimeError(f"Local DUT flashing failed: {e}") from e
        finally:
            self._delete_ec_rw_bin_from_dut()

    def _copy_ec_ro_bin_to_dut(self, ec_ro_bin_path: str) -> None:
        """Copy the local EC RO binary to the target DUT's /tmp directory."""
        if not os.path.exists(ec_ro_bin_path):
            raise FileNotFoundError(
                f"EC RO binary not found at {ec_ro_bin_path}"
            )

        dut_hostname = self.details["dut_hostname"]
        model = self.details["model"]
        print(
            f"Copying RO binary {ec_ro_bin_path} to root@{dut_hostname}:/tmp/ec_ro_{model}.bin..."
        )
        try:
            self._dut_scp(ec_ro_bin_path, f"/tmp/ec_ro_{model}.bin")
        except subprocess.CalledProcessError as e:
            raise RuntimeError(f"Failed to copy EC RO bin to DUT: {e}") from e

    def _delete_ec_ro_bin_from_dut(self) -> None:
        """Delete the temporary EC RO binary file from the DUT."""
        dut_hostname = self.details["dut_hostname"]
        model = self.details["model"]
        print(f"Cleaning up /tmp/ec_ro_{model}.bin from DUT {dut_hostname}...")
        try:
            self._dut_ssh(f"rm -f /tmp/ec_ro_{model}.bin")
        except subprocess.CalledProcessError as e:
            print(
                f"Warning: Failed to delete /tmp/ec_ro_{model}.bin on DUT: {e}",
                file=sys.stderr,
            )

    def flash_ec_ro(self, ec_ro_bin_path: Optional[str]) -> None:
        """Flash the EC RO section locally on the DUT using flashrom."""
        if not ec_ro_bin_path:
            return

        dut_hostname = self.details["dut_hostname"]
        model = self.details["model"]
        self._copy_ec_ro_bin_to_dut(ec_ro_bin_path)
        try:
            print(
                f"Flashing EC RO (or combined image) locally on DUT {dut_hostname} "
                "using flashrom..."
            )
            self._dut_ssh(f"flashrom -p ec -w /tmp/ec_ro_{model}.bin")
            print(f"Rebooting EC on DUT {dut_hostname}...")
            self._dut_ssh("ectool reboot_ec", check=False)
        except subprocess.CalledProcessError as e:
            raise RuntimeError(f"Local DUT EC RO flashing failed: {e}") from e
        finally:
            self._delete_ec_ro_bin_from_dut()

    def verify_ap_up(self, timeout_secs: int = 300) -> None:
        """Wait for the DUT AP to boot up and reply to SSH commands."""
        dut_hostname = self.details["dut_hostname"]
        print(
            f"Verifying DUT AP is up and SSH is responsive on {dut_hostname}..."
        )
        interval = 5
        max_attempts = max(1, timeout_secs // interval)
        for attempt in range(1, max_attempts + 1):
            try:
                result = self._dut_ssh(
                    "ectool version", connect_timeout=5, capture_output=True
                )
                print("DUT AP is up and responsive!")
                print(f"EC version:\n{result.stdout.strip()}")
                return
            except subprocess.CalledProcessError:
                print(
                    f"Attempt {attempt}/{max_attempts}: DUT AP not reachable "
                    f"yet. Retrying in {interval} seconds..."
                )
                time.sleep(interval)
        raise RuntimeError(
            f"DUT AP failed to become responsive on {dut_hostname}"
        )

    def _flash_dut(self) -> None:
        """Perform a recovery flash on the DUT using the fflash utility."""
        dut_hostname = self.details["dut_hostname"]
        platform_dir = os.path.dirname(self.ec_dir)
        fflash_path = os.path.join(
            platform_dir, "dev", "contrib", "fflash", "fflash"
        )
        if not os.path.exists(fflash_path):
            raise FileNotFoundError(f"fflash tool not found at {fflash_path}")

        print(f"Flashing DUT {dut_hostname} using fflash...")
        try:
            subprocess.run([fflash_path, dut_hostname], check=True)
        except subprocess.CalledProcessError as e:
            raise RuntimeError(f"Failed to flash DUT using fflash: {e}") from e

    def _get_free_port(self) -> int:
        """Allocate a random free local TCP port."""
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.bind(("", 0))
            return s.getsockname()[1]

    def _release_ports(self, dut_port: int, servo_port: int) -> None:
        """Release/kill any processes using the specified ports."""
        print(f"Releasing ports {dut_port} and {servo_port}...")
        subprocess.run(["fuser", "-k", f"{dut_port}/tcp"], check=False)
        subprocess.run(["fuser", "-k", f"{servo_port}/tcp"], check=False)

    def _setup_tunnels_sshwatcher(
        self, local_dut_port: int, local_servo_port: int
    ) -> subprocess.Popen:
        """Start the sshwatcher background process to tunnel SSH connections."""
        dut_hostname = self.details["dut_hostname"]
        servo_hostname = self.details["servo_hostname"]
        platform_dir = os.path.dirname(self.ec_dir)
        sshwatcher_path = os.path.join(
            platform_dir, "dev", "contrib", "sshwatcher", "sshwatcher.go"
        )
        if not os.path.exists(sshwatcher_path):
            raise FileNotFoundError(
                f"sshwatcher.go not found at {sshwatcher_path}"
            )

        cmd = [
            "go",
            "run",
            sshwatcher_path,
            dut_hostname,
            str(local_dut_port),
            servo_hostname,
            str(local_servo_port),
        ]
        print(
            f"Starting sshwatcher tunnels (DUT: {local_dut_port}, Servo: {local_servo_port})...."
        )
        try:
            # pylint: disable=consider-using-with, subprocess-popen-preexec-fn
            process = subprocess.Popen(
                cmd,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                preexec_fn=os.setsid,
            )
            time.sleep(2)
            return process
        except Exception as e:
            raise RuntimeError(f"Failed to start sshwatcher: {e}") from e

    def _stop_sshwatcher(self, process: Optional[subprocess.Popen]) -> None:
        """Stop the sshwatcher process and clean up its process group."""
        if not process:
            return
        print("Stopping sshwatcher tunnels...")
        try:
            os.killpg(os.getpgid(process.pid), signal.SIGTERM)
            process.wait(timeout=5)
        except Exception as e:  # pylint: disable=broad-exception-caught
            print(
                f"Warning: Failed to stop sshwatcher process group: {e}",
                file=sys.stderr,
            )

    @contextmanager
    def _ssh_tunnels(self) -> Iterator[Tuple[int, int]]:
        """Context manager for managing local sshwatcher tunnels and port release."""
        dut_port = self._get_free_port()
        servo_port = self._get_free_port()
        process = self._setup_tunnels_sshwatcher(dut_port, servo_port)
        try:
            yield dut_port, servo_port
        finally:
            self._stop_sshwatcher(process)
            self._release_ports(dut_port, servo_port)

    def _run_tast_tests(
        self,
        test_targets: List[str],
        local_dut_port: int,
        local_servo_port: int,
    ) -> None:
        """Execute Tast tests using the tunneled local port settings."""
        servo_port = self.details["servo_port"]
        if len(test_targets) > 1:
            pattern = (
                "(" + " || ".join(f'"name:{t}"' for t in test_targets) + ")"
            )
        else:
            pattern = test_targets[0]

        tast_cmd = [
            "cros_sdk",
            "tast",
            "run",
            f"-var=servo=localhost:{servo_port}:ssh:{local_servo_port}",
            f"localhost:{local_dut_port}",
            pattern,
        ]
        print(
            f"Running TAST tests ({pattern}) using local ports "
            f"(DUT: {local_dut_port}, Servo: {local_servo_port})..."
        )

        has_provisioning_error = False
        # pylint: disable=consider-using-with
        process = subprocess.Popen(
            tast_cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
        )

        for line in process.stdout:
            print(line, end="")
            if (
                "please check if the DUT is provisioned with a test image"
                in line
            ):
                has_provisioning_error = True

        process.wait()

        if has_provisioning_error:
            raise ValueError("DUT is not provisioned with a test image")

        if process.returncode != 0:
            raise RuntimeError(
                f"TAST tests failed with exit code {process.returncode}"
            )

    def execute_test_flow(self) -> None:
        """Execute Tast test flow via sshwatcher tunnels, recovering with fflash if needed."""
        with self._ssh_tunnels() as (local_dut_port, local_servo_port):
            test_targets = self.get_test_targets()
            try:
                self._run_tast_tests(
                    test_targets, local_dut_port, local_servo_port
                )
            except ValueError as e:
                if "DUT is not provisioned with a test image" in str(e):
                    print(
                        "Tast failed because DUT is not provisioned with a test image. "
                        "Attempting to flash DUT first..."
                    )
                    self._flash_dut()
                    self.verify_ap_up()
                    print("Retrying TAST tests...")
                    self._run_tast_tests(
                        test_targets, local_dut_port, local_servo_port
                    )
                else:
                    raise
