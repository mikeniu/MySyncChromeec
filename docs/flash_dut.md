# Remote DUT EC Flashing Guide (`flash_dut.py`)

[TOC]

## Overview

[`flash_dut.py`](../util/flash_dut.py) is a helper utility located in `util/`
that flashes EC firmware images directly onto remote ChromeOS (`cros`) and
GoogleBook (`alos`) Devices Under Test (DUTs) over SSH or ADB.

Unlike [`flash_ec`](../util/flash_ec) (which requires a physical Servo debug
connection) or [`lease_and_test_ec.py`](./lease_and_test_ec.md) (which manages
a full `crosfleet` lease and automated test lifecycle), `flash_dut.py` directly
targets any network-reachable DUT by hostname or IP address without requiring a
physical Servo connection or a `crosfleet` lease.

It leverages the shared [`dut_handlers`](../util/dut_handlers/) implementations:
* **ChromeOS (`cros`) DUTs**: Connects over SSH (`root@<hostname>`).
  * **EC RW**: Reads the current AP firmware image (`futility read`), swaps the
    `ecrw` payload using `/usr/share/vboot/bin/swap_ec_rw`, writes the modified
    AP image back (`futility update --fast`), and reboots the DUT so AP
    Software Sync updates EC RW.
  * **EC RO**: Writes directly to the EC flash chip via `flashrom -p ec -w` and
    reboots the EC (`ectool reboot_ec`).
* **GoogleBook (`alos`) DUTs**: Connects over ADB (`corp-adb-helper` or direct ADB).
  * **EC RW**: Reads the AP firmware image (`futility read`), extracts `RW_FW`
    and `RW_FWID` via `futility dump_fmap`, updates the CBFS regions
    (`FW_MAIN_A` and `FW_MAIN_B`) using `cbfstool`, re-signs the AP image with
    devkeys (`futility sign`), writes the image back (`futility update --fast`),
    and reboots the DUT.
  * **EC RO**: Flashes the EC image via `futility update --ec_image` (falling
    back to `flashrom -p ec -w`) and reboots the EC (`ectool reboot_ec`).

After flashing completes, `flash_dut.py` automatically waits for the DUT AP to
boot up and verifies the active EC firmware version using `ectool version`
(unless `--skip-verify` is passed).

---

## Comparison of Flashing Utilities

| Utility | Connection | Requires Servo? | Manages Lease / Tests? | Primary Use Case |
|---|---|---|---|---|
| [`util/flash_dut.py`](../util/flash_dut.py) | SSH (`cros`) or ADB (`alos`) | **No** | **No** | Quickly flashing EC RW/RO onto a known lab or desk DUT over the network. |
| [`util/flash_ec`](../util/flash_ec) | Servo (`dut-control` / UART / SPI / CCD) | **Yes** | **No** | Local hardware bringup, unbricking unresponsive ECs, or flashing via Servo/CCD. |
| [`util/lease_and_test_ec.py`](./lease_and_test_ec.md) | SSH / ADB + Servo | **Yes** | **Yes** (`crosfleet` + Tast/Tradefed) | End-to-end automated DUT leasing, Servo/GBB setup, EC flashing, and Tast/Tradefed test execution. |

---

## Prerequisites

1. **Network / ADB Reachability**:
   * **CrOS DUTs**: Passwordless SSH access as `root@<hostname>` (standard test
     image SSH keys configured in `~/.ssh/`).
   * **GoogleBook DUTs**: ADB connectivity to `<hostname>`. If connecting to lab/corp
     devices via `corp-adb-helper.py`, ensure an GoogleBook checkout is available
     at `$ANDROID_BUILD_TOP` or `~/alos` (or pass `--android-dir`).
2. **Built EC Image (`ec.bin`)**:
   * Build your target project first using `zmake build <model>`, which outputs
     `build/zephyr/<model>/output/ec.bin`, or supply a custom `ec.bin` path via
     `-i` / `--image`.
   * Note that a **full `ec.bin` image** (containing both RO and RW sections
     and FMAP metadata) is required even when flashing only RW or RO.
3. **Device Permissions / Protection State**:
   * **ALOS EC RW Flashing**: Re-signs the AP firmware image with developer
     keys (`devkeys`), which requires developer GBB flags (`0x39`) enabled on
     the DUT.
   * **EC RO Flashing (`--ro`)**: Requires hardware and software write
     protection to be disabled on the DUT.

---

## Command-Line Arguments

```
usage: flash_dut.py [-h] -m MODEL -H HOSTNAME -t DUT_TYPE [-i IMAGE] [--ro]
                    [--rw] [--board BOARD] [--skip-verify] [--timeout TIMEOUT]
                    [--android-dir ANDROID_DIR]
```

### Mandatory Arguments
| Flag | Description |
|---|---|
| `-m`, `--model <MODEL>` | DUT model/project name (e.g. `brya`, `skyrim`, `redrix`, `brox`). |
| `-H`, `--hostname`, `--host`, `--ip`, `--dut <HOSTNAME>` | DUT hostname or IP address (e.g. `192.168.1.50`, `chromeos8-row1-rack2-host3`, `al-brya-ip6`). |
| `-t`, `--type`, `--dut-type`, `--os-type <TYPE>` | Target DUT OS type: `cros` (or `chromeos`, `cr`) or `alos` (or `android`, `al`). |

### Firmware Images & Region Selection
| Flag | Description |
|---|---|
| `-i`, `--image <PATH>` | Path to custom EC binary (`ec.bin`) to flash. Defaults to `<ec_dir>/build/zephyr/<model>/output/ec.bin`. |
| `--rw` | Flash the EC RW firmware region via AP Software Sync. *(Default behavior when neither `--ro` nor `--rw` is specified).* |
| `--ro` | Flash the EC RO firmware region via `flashrom`/`futility`. If specified without `--rw`, only RO is flashed. |

### Additional Options
| Flag | Description |
|---|---|
| `--board <BOARD>` | DUT board name if different from model (default: same as `--model`). |
| `--skip-verify` | Skip waiting for the DUT AP to reboot and verifying the EC version via `ectool version`. |
| `--timeout <SECONDS>` | Timeout in seconds to wait for the AP to boot up during verification (default: `300`). |
| `--android-dir <PATH>` | Path to Android root directory for ALOS DUTs (default: `$ANDROID_BUILD_TOP` or `~/alos`). |

---

## Common Usage Examples

### 1. Build and Flash EC RW on a ChromeOS DUT (Default)
By default, `flash_dut.py` flashes the EC RW region from
`build/zephyr/<model>/output/ec.bin` via AP Software Sync and verifies the new
EC version after reboot:
```bash
# Build the EC image
cros_sdk --working-dir . -- zmake build skyrim

# Flash EC RW onto a CrOS DUT over SSH
./util/flash_dut.py -m skyrim -H 192.168.1.50 -t cros
```

### 2. Flash EC RW on an GoogleBook (ALOS) DUT
```bash
# Flash EC RW onto an ALOS DUT over ADB
./util/flash_dut.py -m brya -H al-brya-ip6 -t alos
```

### 3. Flash a Custom `ec.bin` Image Path
Use `-i` / `--image` to override the default build output path:
```bash
./util/flash_dut.py -m brox -H 192.168.1.50 -t cros -i /tmp/my_custom_ec.bin
```

### 4. Flash EC RO Only
Pass `--ro` without `--rw` to flash only the EC RO region:
```bash
./util/flash_dut.py -m skyrim -H 192.168.1.50 -t cros --ro
```

### 5. Flash Both EC RO and EC RW
Pass both `--ro` and `--rw` to update RO first and then update RW via AP
Software Sync from the same `ec.bin` image:
```bash
./util/flash_dut.py -m skyrim -H 192.168.1.50 -t cros --ro --rw
```
