# Leased DUT EC Testing Guide (`lease_and_test_ec.py`)

[TOC]

## Overview

[`lease_and_test_ec.py`](../util/lease_and_test_ec.py) is an automated utility
located in `util/` that streamlines the workflow of leasing a physical Device
Under Test (DUT) from the ChromeOS Fleet (`crosfleet`), flashing a locally built
EC image, and executing verification/stress tests against real hardware.

It is designed to be **fully parallel-safe**, allowing multiple instances of the
script to run simultaneously across different DUTs, boards, or models without
port collisions or race conditions.

It automatically detects the DUT operating system type from Swarming metadata
and delegates execution to the appropriate DUT handler:
* **CrOS DUTs**: Uses SSH tunneling via `sshwatcher`, flashes EC RW via
  `futility`/`swap_ec_rw` (and optionally EC RO via `flashrom`), verifies
  console status with `dut-control`, and executes Tast tests inside `cros_sdk`.
* **Android (ALOS) DUTs**: Uses ADB via `corp-adb-helper`, manages AP firmware
  and EC sections with `cbfstool`/`futility`, and executes host tests with
  Tradefed (`atest`) in the Android tree.

---

## Prerequisites

Before running `lease_and_test_ec.py`, ensure the following dependencies and
credentials are configured:

1. **Active LOAS/gcert**:
   ```bash
   gcert
   ```
2. **`crosfleet` CLI**:
   Must be installed and authenticated to lease devices from the ChromeOS fleet.
3. **`luci-auth`**:
   Used to query Swarming bot dimensions and automatically determine the target
   DUT's OS type.
4. **Sudo Privileges**:
   The script pre-authenticates `sudo` credentials for launching chroot
   environments and local tools.
5. **For ChromeOS (CrOS) DUTs**:
   - `cros_sdk` accessible in your environment.
   - `sshwatcher` Go utility
     (`src/platform/dev/contrib/sshwatcher/sshwatcher.go`).
   - `fflash` recovery utility (`src/platform/dev/contrib/fflash/fflash`) if the
     DUT requires test image re-provisioning.
6. **For Android (ALOS) DUTs**:
   - Android checkout directory (pointed to by `$ANDROID_BUILD_TOP` or `~/alos`,
     or passed via `--android-dir`).
   - `corp-adb-helper.py` inside the Android tree
     (`tools/vendor/google_prebuilts/arc/corp-adb-helper.py`).

---

## Command-Line Arguments

```
usage: lease_and_test_ec.py [-h] [--model MODEL] [--board BOARD] [--test TEST]
                           [--stress] [--smoke] [--all]
                           [--ec-rw-bin EC_RW_BIN] [--ec-ro-bin EC_RO_BIN]
                           [--skip-flash-ec] [--keep-lease]
                           [--android-dir ANDROID_DIR]
```

### Target Selection (At least one required)
| Flag | Description |
|---|---|
| `--model <MODEL>` | Model name of the DUT to lease/target (e.g. `brox`,`fatcat`, `skyrim`). |
| `--board <BOARD>` | Board / baseboard name of the DUT to lease/target (e.g. `brox`, `geralt`, `brya`). |

### Test Selection Options
| Flag | Description |
|---|---|
| `--smoke` | *(Default if no test specified)* Runs the EC smoke test (`firmware.ECSize` for CrOS, `EcSizeTest` for ALOS). |
| `--stress` | Runs full EC stress test suite (CrOS only: `flash`, `keyscan`, `pd`, `sensors`, `suspend`). |
| `--all` | Runs all EC test suites (`(firmware_ec)` for CrOS, `DesktopFirmwareEcHostTestCases` for ALOS). |
| `--test <NAME>` | Run a specific Tast test (e.g. `--test=firmware.EcStress.sensors`) or ALOS test target (e.g. `--test=DesktopFirmwareEcHostTestCases:com.google.android.firmware.ec.EcSizeTest`). |

### Flashing Options
| Flag | Description |
|---|---|
| `--ec-rw-bin <PATH>` | Path to custom `ec.bin` to flash (defaults to `build/zephyr/<model>/output/ec.bin`). |
| `--ec-ro-bin <PATH>` | Path to custom EC RO binary to flash directly to the EC chip using `flashrom`. |
| `--skip-flash-ec` | Skip copying, flashing, and verifying the EC binary (test existing DUT firmware). |

### Lifecycle & Environment Options
| Flag | Description |
|---|---|
| `--keep-lease` | Retain the crosfleet lease active after testing completes (essential for fast iterative runs). |
| `--android-dir <PATH>` | Path to Android environment root (defaults to `$ANDROID_BUILD_TOP` or `~/alos`). |

---

## Common Usage Examples

### 1. Build and Run Smoke Test (Default)
Build your target board with `zmake`, then lease a DUT and run the default smoke
test (`firmware.ECSize`):
```bash
# Build EC binary for the target model
zmake build brox

# Lease DUT, flash ec.bin, and run smoke test
./util/lease_and_test_ec.py --model=brox
```

### 2. Fast Iteration with `--keep-lease`
Keep the lease active across runs to avoid leasing overhead (leasing a new DUT
can take several minutes):
```bash
# First run: leases a DUT and keeps it
./util/lease_and_test_ec.py --model=brox --keep-lease

# Make your EC changes and rebuild
zmake build brox

# Subsequent runs: reuses the existing lease, reflashes EC, and re-tests
./util/lease_and_test_ec.py --model=brox --keep-lease
```

### 3. Running Specific Tast Tests or Stress Suite
```bash
# Run a specific Tast test
./util/lease_and_test_ec.py --model=brox \
  --test=firmware.EcStress.sensors \
  --keep-lease

# Run all 5 EC stress tests (flash, keyscan, pd, sensors, suspend)
./util/lease_and_test_ec.py --model=brox --stress --keep-lease

# Run the complete firmware_ec test suite
./util/lease_and_test_ec.py --model=brox --all --keep-lease
```

### 4. Flashing a Custom EC Binary or RO Image
```bash
# Flash a custom RW binary
./util/lease_and_test_ec.py --model=brox \
  --ec-rw-bin=/path/to/custom_ec.bin \
  --keep-lease

# Flash both custom RO and RW binaries
./util/lease_and_test_ec.py --model=brox \
  --ec-ro-bin=/path/to/ec_ro.bin \
  --ec-rw-bin=/path/to/ec_rw.bin \
  --keep-lease
```

### 5. Running Tests on Leased DUT Without Reflashing
```bash
# Skip flashing step to run tests against the image already on the DUT
./util/lease_and_test_ec.py --model=brox --skip-flash-ec --test=firmware.ECSize
```

### 6. Testing an Android (ALOS) Device
```bash
# Run default ALOS test suite using active Android environment
./util/lease_and_test_ec.py --board=fatcat --keep-lease

# Specify custom Android checkout directory
./util/lease_and_test_ec.py --board=fatcat \
  --android-dir=/work/android/alos \
  --keep-lease
```

### 7. Running Multiple Tests in Parallel

You can safely run multiple instances of `lease_and_test_ec.py` simultaneously
across different boards or models (e.g., testing `brox`, `fatcat`, and `skyrim`
at the same time).

```bash
# Launch test runs across multiple boards concurrently in the background
./util/lease_and_test_ec.py --model=brox --keep-lease > brox_test.log 2>&1 &
./util/lease_and_test_ec.py --model=skyrim --keep-lease > skyrim_test.log 2>&1 &
./util/lease_and_test_ec.py --board=fatcat --keep-lease > fatcat_test.log 2>&1 &

# Wait for all background instances to finish
wait
```

---

## How It Works: Execution Lifecycle

1. **Pre-flight Validation**:
   Checks `gcertstatus`, verifies that the target EC binary exists, and pre-
authenticates `sudo`.
2. **Lease Management & Deduplication**:
   - First checks existing active leases via `crosfleet dut leases`. If an
active lease matches the requested `--model` or `--board`, it reuses it
immediately.
   - If no match exists, requests a new lease via `crosfleet dut lease`.
   - Acquires an exclusive lockfile (`/tmp/test_DUT_lease_<lease_id>.lock`) to
ensure multiple local script invocations do not step on the same lease.
3. **OS Detection**:
   Queries Swarming bot dimensions (`label-os_type`, `version_info_os_type`,
`os_restriction`) using `luci-auth` token to route to either
[`CrosHandler`](../util/dut_handlers/cros.py) or
[`AlosHandler`](../util/dut_handlers/alos.py).
4. **Servod Initialization & Resilient Recovery**:
   - Restarts `servod` on the labstation with the leased device parameters.
   - If `servodtool instance wait-for-active` fails or hangs, it queries the USB
path with `servodtool device -s <serial> usb-path`, identifies the corresponding
USB port via `uhubctl`, power cycles the physical USB port, and restarts
`servod`.
5. **GBB Flag Configuration**:
   Configures developer GBB flags (`0x39`):
   - `0x0001` (`GBB_FLAG_DEV_SCREEN_SHORT_DELAY`): Shortens developer screen
delay.
   - `0x0008` (`GBB_FLAG_FORCE_DEV_SWITCH_ON`): Forces developer mode active.
   - `0x0010` (`GBB_FLAG_FORCE_DEV_BOOT_USB`): Allows USB boot.
   - `0x0020` (`GBB_FLAG_DISABLE_ROLLBACK_CHECK`): Bypasses rollback version
checks.
6. **EC Flashing & Verification**:
   - **CrOS**: Extracts the current AP image via `futility read`, replaces the
EC RW section using `/usr/share/vboot/bin/swap_ec_rw`, flashes the AP with
`futility update --fast`, and reboots the DUT to let Software Sync update the
EC.
   - **ALOS**: Extracts EC RW sections and replaces CBFS components (`ecrw`,
`ecrw.hash`, `ecrw.version`, `ecrw.config`) in regions `FW_MAIN_A` and
`FW_MAIN_B` using `cbfstool`, re-signs using `futility sign --keyset devkeys`,
updates via `futility update --fast`, and reboots.
   - Verifies the EC is active using `dut-control ec_board` via Servo.
   - Verifies the AP is fully booted and responsive (`ectool version` over SSH
for CrOS; `getprop sys.boot_completed` and `ectool version` over ADB for ALOS).
7. **Test Execution**:
   - **CrOS**: Sets up local TCP port forwarders via `sshwatcher.go` to
multiplex SSH connections to both the DUT and Servo host. Executes `cros_sdk
tast run`. If the DUT lacks a test image, it invokes `fflash` to provision the
DUT and retries.
   - **ALOS**: Sets up Android build variables (`build/envsetup.sh` and
`lunch`), connects ADB via `corp-adb-helper`, and launches `atest`.
8. **Teardown & Cleanup**:
   Closes SSH tunnels, releases ports, disconnects ADB, unlocks the lease
lockfile, and releases the crosfleet lease unless `--keep-lease` was specified.

---

## Parallel & Concurrent Execution Architecture

`lease_and_test_ec.py` is architected from the ground up to support parallel
execution:

### 1. Dynamic Local Port Allocation
Whenever Tast tests run against a CrOS DUT, `CrosHandler` opens local SSH
tunnels for both the DUT and Servo. Instead of hardcoding static ports (such as
`2222` or `9999`), the script dynamically binds to available ephemeral TCP ports
(`_get_free_port()`). Multiple concurrent instances will each allocate their own
unique local ports without conflict.

### 2. Per-Lease Advisory File Locking
To prevent race conditions where two simultaneous script instances try to
control and flash the *same* physical DUT, each instance acquires an exclusive
advisory file lock (`fcntl.flock`) keyed by the lease ID:
```
/tmp/test_DUT_lease_<lease_id>.lock
```
* **Different DUTs / Boards**: Each leased device receives a unique lease ID and
  lock file, executing completely in parallel.
* **Same DUT**: If two instances attempt to run against the exact same lease
  concurrently, the second instance exits fast with an explanatory error and the
  holding PID rather than corrupting the flashing/testing state.

### 3. Isolated SSH Sockets & Subprocesses
* SSH connection sharing uses templated control socket paths
  (`/tmp/ssh_mux_%h_%p_%r`), ensuring distinct multiplexed connections per
  target host and port.
* Background processes (such as `sshwatcher`) are spawned in separate process
  groups (`os.setsid`) and terminated cleanly during teardown without impacting
  peer instances.

---

## Troubleshooting & Tips

* **Expired gcert**:
  If you see `Error: Your gcert is invalid or expired`, run `gcert` in your
terminal and retry.
* **Lease Collision / Lock File**:
  If a previous run was abruptly killed and left a stale lock file at
`/tmp/test_DUT_lease_<lease_id>.lock`, check if the holding PID is still alive.
If not, delete the lock file.
* **Servod Not Active**:
  The script automatically attempts USB hub power cycling via `uhubctl` if
servod fails to start. If it persists, inspect the labstation status or abandon
the lease with `crosfleet dut abandon -lease-ids <id>` and request a different
DUT.
* **Releasing Active Leases Manually**:
  To view and release active leases manually:
  ```bash
  crosfleet dut leases
  crosfleet dut abandon -lease-ids <lease_id>
  ```
