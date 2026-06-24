#!/usr/bin/env python3
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Lease a DUT from crosfleet and run Tast tests against a local EC image."""

import argparse
import fcntl
import os
import signal
import socket
import subprocess
import sys
import time


# Standard SSH options used across commands
SSH_OPTS = [
    "-o",
    "StrictHostKeyChecking=no",
    "-o",
    "UserKnownHostsFile=/dev/null",
]


def check_gcert():
    """Verify that the user has valid LOAS gcert credentials."""
    print("Checking gcert status...")
    try:
        subprocess.run(
            ["gcertstatus"],
            check=True,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
    except subprocess.CalledProcessError:
        print(
            "Error: Your gcert is invalid or expired. Please run 'gcert' in your terminal first.",
            file=sys.stderr,
        )
        sys.exit(1)
    except FileNotFoundError:
        print(
            "Warning: gcertstatus command not found. Skipping gcert verification.",
            file=sys.stderr,
        )


def get_platform_dir():
    """Resolve and return the absolute path to the src/platform directory."""
    script_dir = os.path.dirname(os.path.abspath(__file__))
    ec_dir = os.path.dirname(script_dir)
    return os.path.dirname(ec_dir)


def get_active_leases():
    """Query currently active crosfleet leases for the user."""
    try:
        result = subprocess.run(
            ["crosfleet", "dut", "leases"],
            capture_output=True,
            text=True,
            check=True,
        )
    except subprocess.CalledProcessError as e:
        raise RuntimeError(f"Failed to query active leases: {e}") from e

    leases = []
    current_lease = {}
    for line in result.stdout.splitlines():
        line = line.strip()
        if not line:
            if current_lease:
                leases.append(current_lease)
                current_lease = {}
            continue
        if "=" in line:
            key, val = line.split("=", 1)
            current_lease[key.lower()] = val

    if current_lease:
        leases.append(current_lease)

    return leases


def lease_dut(model=None, board=None):
    """Lease a new DUT or reuse a matching active lease."""
    # 1. Try to reuse an existing active lease first
    try:
        active_leases = get_active_leases()
        for lease in active_leases:
            matches_board = (not board) or (lease.get("board") == board)
            matches_model = (not model) or (lease.get("model") == model)
            if matches_board and matches_model:
                print(
                    f"Reusing active lease {lease['lease_id']} matching "
                    f"board: {board}, model: {model}..."
                )
                return lease
    except Exception as e:  # pylint: disable=broad-exception-caught
        print(
            f"Warning: Failed to query active leases for reuse: {e}",
            file=sys.stderr,
        )

    # 2. If no active lease matched, request a new lease
    lease_args = []
    if board:
        lease_args += ["-board", board]
    if model:
        lease_args += ["-model", model]
    if not lease_args:
        raise ValueError("Either model or board must be specified")

    target_desc = []
    if board:
        target_desc.append(f"board: {board}")
    if model:
        target_desc.append(f"model: {model}")
    print(f"Leasing new DUT by {', '.join(target_desc)}...")

    try:
        result = subprocess.run(
            ["crosfleet", "dut", "lease"] + lease_args,
            capture_output=True,
            text=True,
            check=True,
            timeout=300,
        )
        print(result.stdout)

        lease_id = None
        for line in result.stdout.splitlines():
            if "Internal Scheduke lease ID" in line:
                parts = line.split(":")
                if len(parts) > 1:
                    lease_id = parts[-1].strip()
    except subprocess.TimeoutExpired as e:
        raise RuntimeError(
            "crosfleet lease command timed out after 300 seconds"
        ) from e
    except subprocess.CalledProcessError as e:
        raise RuntimeError(f"Failed to lease DUT: {e}") from e

    if not lease_id:
        raise RuntimeError(
            "Failed to parse lease ID from crosfleet lease output"
        )

    # Wait a few seconds for the new lease to appear in the active leases query
    time.sleep(3)

    # Query active leases again to find the newly leased DUT details
    active_leases = get_active_leases()
    for lease in active_leases:
        if lease.get("lease_id") == lease_id:
            return lease

    raise RuntimeError(
        f"Failed to find the new lease {lease_id} in active leases list."
    )


def abandon_lease(lease_id):
    """Release/abandon the specified crosfleet lease."""
    if not lease_id:
        return
    print(f"Abandoning lease {lease_id}...")
    try:
        subprocess.run(
            ["crosfleet", "dut", "abandon", "-lease-ids", lease_id], check=True
        )
    except subprocess.CalledProcessError as e:
        print(
            f"Warning: Failed to abandon lease {lease_id}: {e}", file=sys.stderr
        )


def acquire_lease_lock(lease_id):
    """Acquire an exclusive advisory file lock for the lease ID."""
    if not lease_id:
        return None, ""
    lock_file_path = f"/tmp/test_DUT_lease_{lease_id}.lock"
    try:
        fd = os.open(lock_file_path, os.O_CREAT | os.O_RDWR)
        lock_file = os.fdopen(fd, "r+", encoding="utf-8")
        fcntl.flock(lock_file, fcntl.LOCK_EX | fcntl.LOCK_NB)
        lock_file.seek(0)
        lock_file.truncate()
        lock_file.write(str(os.getpid()))
        lock_file.flush()
        return lock_file, lock_file_path
    except BlockingIOError:
        holding_pid = "unknown"
        try:
            with open(lock_file_path, "r", encoding="utf-8") as f:
                holding_pid = f.read().strip()
        except Exception:  # pylint: disable=broad-exception-caught
            pass
        print(
            f"Error: Lease {lease_id} is already being used by another instance "
            f"of this script (PID: {holding_pid}).",
            file=sys.stderr,
        )
        sys.exit(1)


def release_lease_lock(lock_file, lock_file_path):
    """Unlock and remove the lease lock file."""
    if lock_file:
        try:
            fcntl.flock(lock_file, fcntl.LOCK_UN)
            lock_file.close()
            if os.path.exists(lock_file_path):
                os.remove(lock_file_path)
        except Exception:  # pylint: disable=broad-exception-caught
            pass


def get_test_targets(args):
    """Determine the list of test targets based on command line options."""
    if args.all:
        return ["(firmware_ec)"]

    test_targets = []
    if args.test:
        test_targets.append(args.test)
    if args.stress:
        test_targets.extend(
            [
                "firmware.EcStress.flash",
                "firmware.EcStress.keyscan",
                "firmware.EcStress.pd",
                "firmware.EcStress.sensors",
                "firmware.EcStress.suspend",
            ]
        )
    if args.smoke or not test_targets:
        test_targets.insert(0, "firmware.ECSize")
    return test_targets


def copy_ec_rw_bin_to_dut(ec_rw_bin_path, dut_hostname, model):
    """Copy the local EC RW binary to the target DUT's /tmp directory."""
    if not os.path.exists(ec_rw_bin_path):
        raise FileNotFoundError(
            f"ec.bin not found at {ec_rw_bin_path}. Did you build the project?"
        )

    destination = f"root@{dut_hostname}:/tmp/ec_rw_{model}.bin"
    print(f"Copying {ec_rw_bin_path} to {destination}...")
    try:
        subprocess.run(
            [
                "scp",
                *SSH_OPTS,
                ec_rw_bin_path,
                destination,
            ],
            check=True,
        )
    except subprocess.CalledProcessError as e:
        raise RuntimeError(f"Failed to copy ec.bin to DUT: {e}") from e


def configure_gbb_flags(servo_hostname, servo_port):
    """Configure dev-mode GBB flags via servo.

    The flag value 0x39 is a bitmask enabling:
      - 0x0001 (GBB_FLAG_DEV_SCREEN_SHORT_DELAY): Shortens the dev screen warning.
      - 0x0008 (GBB_FLAG_FORCE_DEV_SWITCH_ON): Forces developer mode active.
      - 0x0010 (GBB_FLAG_FORCE_DEV_BOOT_USB): Allows booting from USB drives.
      - 0x0020 (GBB_FLAG_DISABLE_ROLLBACK_CHECK): Bypasses version rollback checks.
    """
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


def flash_dut(dut_hostname):
    """Perform a recovery flash on the DUT using the fflash utility."""
    platform_dir = get_platform_dir()
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


def flash_ec_rw_locally_on_dut(dut_hostname, model, ec_rw_bin_path):
    """Flash the EC RW by reading, swapping, and updating the AP firmware locally on the DUT."""
    copy_ec_rw_bin_to_dut(ec_rw_bin_path, dut_hostname, model)
    try:
        # 1. Read current AP firmware image locally on the DUT
        print(f"Reading AP firmware image locally on DUT {dut_hostname}...")
        subprocess.run(
            [
                "ssh",
                *SSH_OPTS,
                f"root@{dut_hostname}",
                f"futility read /tmp/ap_{model}.bin",
            ],
            check=True,
        )

        # 2. Swap the custom EC binary into the AP firmware image using swap_ec_rw locally
        print(
            "Populating AP firmware image with custom EC binary using swap_ec_rw locally on DUT..."
        )
        subprocess.run(
            [
                "ssh",
                *SSH_OPTS,
                f"root@{dut_hostname}",
                (
                    f"/usr/share/vboot/bin/swap_ec_rw "
                    f"-i /tmp/ap_{model}.bin -e /tmp/ec_rw_{model}.bin"
                ),
            ],
            check=True,
        )

        # 3. Write the modified AP firmware image back using futility update locally on the DUT
        print(
            f"Writing modified AP firmware image back locally on DUT {dut_hostname}..."
        )
        subprocess.run(
            [
                "ssh",
                *SSH_OPTS,
                f"root@{dut_hostname}",
                f"futility update --fast -i /tmp/ap_{model}.bin",
            ],
            check=True,
        )

        # 4. Clean up temporary AP image on the DUT
        subprocess.run(
            [
                "ssh",
                *SSH_OPTS,
                f"root@{dut_hostname}",
                f"rm -f /tmp/ap_{model}.bin",
            ],
            check=False,
        )

        # 5. Reboot DUT to trigger Software Sync
        reboot_dut(dut_hostname)

    except subprocess.CalledProcessError as e:
        raise RuntimeError(f"Local DUT flashing failed: {e}") from e
    finally:
        delete_ec_rw_bin_from_dut(dut_hostname, model)


def delete_ec_rw_bin_from_dut(dut_hostname, model):
    """Delete the temporary EC RW binary file from the DUT."""
    ssh_cmd = [
        "ssh",
        *SSH_OPTS,
        f"root@{dut_hostname}",
        f"rm -f /tmp/ec_rw_{model}.bin",
    ]
    print(f"Cleaning up /tmp/ec_rw_{model}.bin from DUT {dut_hostname}...")
    try:
        subprocess.run(ssh_cmd, check=True)
    except subprocess.CalledProcessError as e:
        print(
            f"Warning: Failed to delete /tmp/ec_rw_{model}.bin on DUT: {e}",
            file=sys.stderr,
        )


def copy_ec_ro_bin_to_dut(ec_ro_bin_path, dut_hostname, model):
    """Copy the local EC RO binary to the target DUT's /tmp directory."""
    if not os.path.exists(ec_ro_bin_path):
        raise FileNotFoundError(f"EC RO binary not found at {ec_ro_bin_path}")

    destination = f"root@{dut_hostname}:/tmp/ec_ro_{model}.bin"
    print(f"Copying RO binary {ec_ro_bin_path} to {destination}...")
    try:
        subprocess.run(
            [
                "scp",
                *SSH_OPTS,
                ec_ro_bin_path,
                destination,
            ],
            check=True,
        )
    except subprocess.CalledProcessError as e:
        raise RuntimeError(f"Failed to copy EC RO bin to DUT: {e}") from e


def flash_ec_ro_locally_on_dut(dut_hostname, model, ec_ro_bin_path):
    """Flash the EC RO section locally on the DUT using flashrom."""
    copy_ec_ro_bin_to_dut(ec_ro_bin_path, dut_hostname, model)
    try:
        print(
            f"Flashing EC RO (or combined image) locally on DUT {dut_hostname} "
            "using flashrom..."
        )
        subprocess.run(
            [
                "ssh",
                *SSH_OPTS,
                f"root@{dut_hostname}",
                f"flashrom -p ec -w /tmp/ec_ro_{model}.bin",
            ],
            check=True,
        )
        # Reboot EC to ensure the new RO/RW image is loaded
        print(f"Rebooting EC on DUT {dut_hostname}...")
        subprocess.run(
            [
                "ssh",
                *SSH_OPTS,
                f"root@{dut_hostname}",
                "ectool reboot_ec",
            ],
            check=False,
        )
    except subprocess.CalledProcessError as e:
        raise RuntimeError(f"Local DUT EC RO flashing failed: {e}") from e
    finally:
        delete_ec_ro_bin_from_dut(dut_hostname, model)


def delete_ec_ro_bin_from_dut(dut_hostname, model):
    """Delete the temporary EC RO binary file from the DUT."""
    ssh_cmd = [
        "ssh",
        *SSH_OPTS,
        f"root@{dut_hostname}",
        f"rm -f /tmp/ec_ro_{model}.bin",
    ]
    print(f"Cleaning up /tmp/ec_ro_{model}.bin from DUT {dut_hostname}...")
    try:
        subprocess.run(ssh_cmd, check=True)
    except subprocess.CalledProcessError as e:
        print(
            f"Warning: Failed to delete /tmp/ec_ro_{model}.bin on DUT: {e}",
            file=sys.stderr,
        )


def verify_ec_up(servo_hostname, servo_port):
    """Query the EC console to verify it is up and responsive."""
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


def reboot_dut(dut_hostname):
    """Reboot the DUT to trigger Software Sync update of the EC."""
    print(f"Rebooting DUT {dut_hostname} to trigger Software Sync...")
    try:
        subprocess.run(
            ["ssh", *SSH_OPTS, f"root@{dut_hostname}", "reboot"],
            check=True,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        time.sleep(5)  # Give it a moment to begin rebooting
    except subprocess.CalledProcessError:
        # Sometimes reboot disconnects SSH immediately, causing exit code 255.
        pass


def verify_ap_up(dut_hostname, timeout_secs=300):
    """Wait for the DUT AP to boot up and reply to SSH commands."""
    ssh_cmd = [
        "ssh",
        *SSH_OPTS,
        "-o",
        "ConnectTimeout=5",
        f"root@{dut_hostname}",
        "ectool version",
    ]
    print(f"Verifying DUT AP is up and SSH is responsive on {dut_hostname}...")
    interval = 5
    max_attempts = max(1, timeout_secs // interval)
    for attempt in range(1, max_attempts + 1):
        try:
            result = subprocess.run(
                ssh_cmd, check=True, capture_output=True, text=True
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
    raise RuntimeError(f"DUT AP failed to become responsive on {dut_hostname}")


def ensure_servod_running(
    servo_hostname, servo_port, board, model, servo_serial
):
    """Ensure servod is restarted cleanly with correct model configuration."""
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


def get_free_port():
    """Allocate a random free local TCP port."""
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(("", 0))
        return s.getsockname()[1]


def release_ports(dut_port, servo_port):
    """Release/kill any processes using the specified ports."""
    print(f"Releasing ports {dut_port} and {servo_port}...")
    subprocess.run(["fuser", "-k", f"{dut_port}/tcp"], check=False)
    subprocess.run(["fuser", "-k", f"{servo_port}/tcp"], check=False)


def resolve_ec_rw_bin_path(args, details, ec_dir):
    """Resolve the path to the EC RW binary to flash, validating its existence."""
    if args.skip_flash_ec:
        return None

    if args.ec_rw_bin:
        return args.ec_rw_bin

    ec_rw_bin_path = os.path.join(
        ec_dir, "build", "zephyr", details["model"], "output", "ec.bin"
    )
    if args.board and not os.path.exists(ec_rw_bin_path):
        raise FileNotFoundError(
            f"ec.bin not found at {ec_rw_bin_path}. Did you build the project?"
        )

    return ec_rw_bin_path


def resolve_ec_ro_bin_path(args):
    """Resolve the path to the EC RO binary to flash, validating its existence."""
    if args.skip_flash_ec or not args.ec_ro_bin:
        return None

    ec_ro_bin_path = args.ec_ro_bin
    if not os.path.exists(ec_ro_bin_path):
        print(
            f"Warning: EC RO binary not found at {ec_ro_bin_path}. "
            "Skipping EC RO flashing."
        )
        return None

    return ec_ro_bin_path


def setup_tunnels_sshwatcher(
    dut_hostname, servo_hostname, local_dut_port, local_servo_port
):
    """Start the sshwatcher background process to tunnel SSH connections."""
    platform_dir = get_platform_dir()
    sshwatcher_path = os.path.join(
        platform_dir, "dev", "contrib", "sshwatcher", "sshwatcher.go"
    )
    if not os.path.exists(sshwatcher_path):
        raise FileNotFoundError(f"sshwatcher.go not found at {sshwatcher_path}")

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
        # Give sshwatcher a moment to initialize the tunnels
        time.sleep(2)
        return process
    except Exception as e:
        raise RuntimeError(f"Failed to start sshwatcher: {e}") from e


def stop_sshwatcher(process):
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


def run_tast_tests(test_targets, servo_port, local_dut_port, local_servo_port):
    """Execute Tast tests using the tunneled local port settings."""
    if len(test_targets) > 1:
        pattern = "(" + " || ".join(f'"name:{t}"' for t in test_targets) + ")"
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
        tast_cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True
    )

    for line in process.stdout:
        print(line, end="")
        if "please check if the DUT is provisioned with a test image" in line:
            has_provisioning_error = True

    process.wait()

    if has_provisioning_error:
        raise ValueError("DUT is not provisioned with a test image")

    if process.returncode != 0:
        raise RuntimeError(
            f"TAST tests failed with exit code {process.returncode}"
        )


def execute_test_flow(test_targets, details, local_dut_port, local_servo_port):
    """Execute the Tast test flow, recovering with an fflash if required."""
    try:
        run_tast_tests(
            test_targets,
            details["servo_port"],
            local_dut_port,
            local_servo_port,
        )
    except ValueError as e:
        if "DUT is not provisioned with a test image" in str(e):
            print(
                "Tast failed because DUT is not provisioned with a test image. "
                "Attempting to flash DUT first..."
            )
            flash_dut(details["dut_hostname"])
            verify_ap_up(details["dut_hostname"])
            print("Retrying TAST tests...")
            run_tast_tests(
                test_targets,
                details["servo_port"],
                local_dut_port,
                local_servo_port,
            )
        else:
            raise


def main():
    """Main program entry point to parse options and run test lifecycle."""
    parser = argparse.ArgumentParser(
        description="Lease a DUT from crosfleet and run tests against a local EC image."
    )
    parser.add_argument("--model", help="Model name of the DUT to lease")
    parser.add_argument("--board", help="Board name of the DUT to lease")
    parser.add_argument("--test", help="Tast test(s) to run")
    parser.add_argument(
        "--stress",
        action="store_true",
        help="Run the EC stress tests (flash, keyscan, pd, sensors, suspend)",
    )
    parser.add_argument(
        "--smoke",
        action="store_true",
        help="Run the EC smoke test (firmware.ECSize)",
    )
    parser.add_argument(
        "--all",
        action="store_true",
        help="Run all firmware_ec tests ((firmware_ec))",
    )
    parser.add_argument(
        "--ec-rw-bin",
        help="Path to custom ec.bin to flash (default: "
        "{EC_DIR}/build/zephyr/{model}/output/ec.bin)",
    )
    parser.add_argument(
        "--ec-ro-bin",
        help="Path to custom EC RO binary (or combined image) to flash "
        "directly to the EC chip using flashrom",
    )
    parser.add_argument(
        "--skip-flash-ec",
        action="store_true",
        help="Skip copying, flashing, and verifying the EC binary",
    )
    parser.add_argument(
        "--keep-lease",
        action="store_true",
        help="Keep the leased DUT active after tests finish",
    )
    args = parser.parse_args()

    if not args.model and not args.board:
        parser.error("At least one of --model or --board must be specified.")

    check_gcert()

    start_time = time.time()
    platform_dir = get_platform_dir()
    ec_dir = os.path.join(platform_dir, "ec")

    test_targets = get_test_targets(args)

    # 1. Pre-verify the EC binary file if we can determine the path early
    try:
        if not args.skip_flash_ec and (args.ec_rw_bin or args.model):
            resolve_ec_rw_bin_path(args, {"model": args.model}, ec_dir)
        if not args.skip_flash_ec and args.ec_ro_bin:
            resolve_ec_ro_bin_path(args)
    except FileNotFoundError as e:
        print(f"Error: {e}", file=sys.stderr)
        sys.exit(1)

    # 2. Pre-authenticate sudo credentials so future SDK commands run without prompting
    print("Pre-authenticating sudo credentials...")
    try:
        subprocess.run(["sudo", "true"], check=True)
    except subprocess.CalledProcessError as e:
        print(f"Failed to authenticate sudo: {e}", file=sys.stderr)
        sys.exit(1)

    # 3. Lease or reuse the DUT
    try:
        details = lease_dut(model=args.model, board=args.board)
    except RuntimeError as e:
        print(e, file=sys.stderr)
        sys.exit(1)

    # 4. Acquire lease lock to prevent concurrent clashes
    lock_file, lock_file_path = acquire_lease_lock(details.get("lease_id"))

    # 5. Resolve ec_rw_bin_path and ec_ro_bin_path now that we have details
    try:
        ec_rw_bin_path = resolve_ec_rw_bin_path(args, details, ec_dir)
        ec_ro_bin_path = resolve_ec_ro_bin_path(args)
    except FileNotFoundError as e:
        print(f"Error: {e}", file=sys.stderr)
        release_lease_lock(lock_file, lock_file_path)
        if not args.keep_lease:
            abandon_lease(details["lease_id"])
        sys.exit(1)

    print(f"DUT_HOSTNAME={details['dut_hostname']}")
    print(f"MODEL={details['model']}")
    print(f"BOARD={details['board']}")
    print(f"SERVO_HOSTNAME={details['servo_hostname']}")
    print(f"SERVO_PORT={details['servo_port']}")
    print(f"SERVO_SERIAL={details['servo_serial']}")

    local_dut_port = get_free_port()
    local_servo_port = get_free_port()
    sshwatcher_process = None

    try:
        ensure_servod_running(
            details["servo_hostname"],
            details["servo_port"],
            details["board"],
            details["model"],
            details["servo_serial"],
        )
        if not args.skip_flash_ec:
            configure_gbb_flags(
                details["servo_hostname"], details["servo_port"]
            )
            if ec_ro_bin_path:
                flash_ec_ro_locally_on_dut(
                    details["dut_hostname"],
                    details["model"],
                    ec_ro_bin_path,
                )
            flash_ec_rw_locally_on_dut(
                details["dut_hostname"],
                details["model"],
                ec_rw_bin_path,
            )
            verify_ec_up(details["servo_hostname"], details["servo_port"])
        verify_ap_up(details["dut_hostname"])
        sshwatcher_process = setup_tunnels_sshwatcher(
            details["dut_hostname"],
            details["servo_hostname"],
            local_dut_port,
            local_servo_port,
        )
        execute_test_flow(
            test_targets, details, local_dut_port, local_servo_port
        )
    except KeyboardInterrupt:
        print("\nInterrupted by user. Cleaning up...", file=sys.stderr)
        sys.exit(1)
    except Exception as e:  # pylint: disable=broad-exception-caught
        print(e, file=sys.stderr)
        sys.exit(1)
    finally:
        stop_sshwatcher(sshwatcher_process)
        release_ports(local_dut_port, local_servo_port)
        if "details" in locals():
            release_lease_lock(lock_file, lock_file_path)
            if details.get("lease_id") and not args.keep_lease:
                abandon_lease(details["lease_id"])

        elapsed_time = time.time() - start_time
        print(
            f"\nExecution took {elapsed_time:.2f} seconds ({elapsed_time/60:.2f} minutes)"
        )


if __name__ == "__main__":
    main()
