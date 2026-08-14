#!/usr/bin/env python3
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Lease a DUT from crosfleet and run Tast tests against a local EC image."""

import argparse
import fcntl
import json
import os
import subprocess
import sys
import time
from typing import Optional
import urllib.request

from dut_handlers import create_dut_handler
from dut_handlers import DutOsType


# Standard SSH options used across commands
SSH_OPTS = [
    "-o",
    "StrictHostKeyChecking=no",
    "-o",
    "UserKnownHostsFile=/dev/null",
]


def get_dut_os_type(dut_hostname: str) -> DutOsType:
    """Determine if DUT OS is Android or CrOS via Swarming, defaulting to CrOS with warning."""
    res_type: Optional[DutOsType] = None

    if not dut_hostname:
        print(
            "Warning: No DUT hostname provided. Falling back to CrOS.",
            file=sys.stderr,
        )
        return DutOsType.CROS

    try:
        token = subprocess.check_output(
            ["luci-auth", "token"], text=True, stderr=subprocess.DEVNULL
        ).strip()
    except Exception as e:  # pylint: disable=broad-exception-caught
        print(
            f"Warning: Failed to obtain luci-auth token ({e}). "
            f"Falling back to CrOS for {dut_hostname}.",
            file=sys.stderr,
        )
        return DutOsType.CROS

    url = (
        "https://chromeos-swarming.appspot.com/_ah/api/swarming/v1/bots/list"
        f"?dimensions=dut_name:{dut_hostname}"
    )
    req = urllib.request.Request(url)
    req.add_header("Authorization", f"Bearer {token}")

    try:
        with urllib.request.urlopen(req, timeout=10) as resp:
            data = json.loads(resp.read().decode())
        items = data.get("items", [])

        alos_types = {
            "AL",
            "ANDROID",
            "OSR_ANDROID_ONLY",
            "OS_TYPE_AL",
            "OS_TYPE_ANDROID",
        }
        cros_types = {"CHROMEOS", "CROS", "OS_TYPE_CROS"}

        for bot in items:
            dims = {d["key"]: d["value"] for d in bot.get("dimensions", [])}
            os_type_vals = (
                dims.get("version_info_os_type", [])
                + dims.get("os_restriction", [])
                + dims.get("label-os_type", [])
                + dims.get("os_type", [])
            )
            for os_type in os_type_vals:
                os_type_upper = str(os_type).upper()
                if os_type_upper in alos_types:
                    res_type = DutOsType.ANDROID
                    break
                if os_type_upper in cros_types:
                    res_type = DutOsType.CROS
                    break
            if res_type:
                break
    except Exception as e:  # pylint: disable=broad-exception-caught
        print(
            f"Warning: Failed to query DUT OS type from Swarming for {dut_hostname}: {e}.",
            file=sys.stderr,
        )

    if not res_type:
        print(
            f"Warning: Could not determine DUT OS type for {dut_hostname} from Swarming. "
            "Falling back to CrOS.",
            file=sys.stderr,
        )
        res_type = DutOsType.CROS

    return res_type


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
                lease["os_type"] = get_dut_os_type(lease.get("dut_hostname"))
                print(f"DUT OS type: {lease['os_type']}")
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
            lease["os_type"] = get_dut_os_type(lease.get("dut_hostname"))
            print(f"DUT OS type: {lease['os_type']}")
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
    print(f"DUT_OS_TYPE={details.get('os_type', 'Unknown')}")
    print(f"SERVO_HOSTNAME={details['servo_hostname']}")
    print(f"SERVO_PORT={details['servo_port']}")
    print(f"SERVO_SERIAL={details['servo_serial']}")

    try:
        handler = create_dut_handler(details, args, ec_dir)
    except NotImplementedError as e:
        print(f"Error: {e}", file=sys.stderr)
        release_lease_lock(lock_file, lock_file_path)
        if not args.keep_lease:
            abandon_lease(details["lease_id"])
        sys.exit(1)

    try:
        handler.ensure_servod_running()
        if not args.skip_flash_ec:
            handler.configure_gbb()
            if ec_ro_bin_path:
                handler.flash_ec_ro(ec_ro_bin_path)
            handler.flash_ec_rw(ec_rw_bin_path)
            handler.verify_ec_up()
        handler.verify_ap_up()
        handler.execute_test_flow()
    except KeyboardInterrupt:
        print("\nInterrupted by user. Cleaning up...", file=sys.stderr)
        sys.exit(1)
    except Exception as e:  # pylint: disable=broad-exception-caught
        print(e, file=sys.stderr)
        sys.exit(1)
    finally:
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
