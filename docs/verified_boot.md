# EC Verified Boot, Software Sync, and Sysjump Flow

[TOC]

This document describes how the ChromeOS Embedded Controller (EC) transitions
between its Read-Only (`RO`) and Read-Write (`RW`) firmware images during normal
boot, firmware updates (OTA / Software Sync), and recovery mode, as well as the
security invariants governing `sysjump`.

## Overview of EC Firmware Copies & Verification Models

The EC flash is partitioned into two primary executable regions:

*   **Read-Only (`EC_RO` / `WP_RO`)**: Locked in the factory via hardware and
    software [write protection](./write_protection.md). Every cold reset or
    hardware reset boots the MCU into `EC_RO`.
*   **Read-Write (`EC_RW`)**: Updatable in the field. Contains the full runtime
    feature set (such as full USB-PD state machines, keyboard/sensor tasks, and
    AP power sequencing).

Depending on the MCU role and platform generation, ChromeOS uses one of three
mechanisms to verify `EC_RW` before or during execution:

1.  **Early Firmware Selection v2 (EFS2)**: Used on modern Chromebook main ECs
    ([`common/vboot/efs2.c`](../common/vboot/efs2.c)). Before powering on the
    Application Processor (AP), `EC_RO` hashes `EC_RW` (`Heff`) and sends the
    SHA-256 digest over UART to the Google Security Chip (GSC: Cr50/Ti50) to
    compare against a mirrored hash (`Hmir`) stored in TPM kernel `secdata`. If
    verified, `EC_RO` immediately sysjumps to `EC_RW` before booting the AP.
2.  **Legacy AP Software Sync**: Used on older Chromebook main ECs without EFS2
    ([`common/vboot_hash.c`](../common/vboot_hash.c)). `EC_RO` powers on the AP
    while remaining in `RO`. Depthcharge (AP firmware) reads `Heff` from the EC,
    compares it against the expected hash (`Hexp`) in the active AP `RW` slot,
    updates `EC_RW` if mismatched, and commands the EC to sysjump to `EC_RW`.
3.  **RW Signature Verification (`RWSIG`)**: Used on standalone MCUs that cannot
    rely on AP boot-time Software Sync, such as the
    [Fingerprint MCU (FPMCU)](./fingerprint/fingerprint.md) and
    [Detachable Bases](./detachable_base_verified_boot.md)
    ([`common/rwsig.c`](../common/rwsig.c)). `EC_RO` verifies an RSA signature
    over `EC_RW` using an embedded public key and checks a dedicated rollback
    flash block before sysjumping to `EC_RW`.

---

## Sysjump Mechanics and Security Invariants

A **sysjump** transitions execution from one firmware image to another without
resetting the MCU hardware or clearing RAM.

### What Happens During a Sysjump (`jump_to_image()`)

When [`system_run_image_copy_with_flags()`](../common/system.c) validates a
transition and calls [`jump_to_image()`](../common/system.c):

1.  **GSC Trust Latch (`GPIO_ENTERING_RW`)**: Calls
    [`board_pulse_entering_rw()`](../common/system.c) to pulse
    `GPIO_ENTERING_RW`, latching in hardware/GSC that the EC has left trusted
    `RO` code.
2.  **Peripheral Quiescing**: Prepares USB-PD (`pd_prepare_sysjump()`) and I2C
    controllers (`i2c_prepare_sysjump()`), and flushes the UART console buffer
    (`cflush()`).
3.  **Preserved RAM Metadata (`struct jump_data`)**: Populates `struct jump_data`
    (`JUMP_DATA_MAGIC`) in uninitialized end-of-RAM (immediately below
    `struct panic_data`; see [Panic Data Memory Layout](./panic_data.md#memory-layout))
    with the current `reset_flags`.
4.  **Atomic `HOOK_SYSJUMP` Execution**: Disables all interrupts
    (`interrupt_disable_all()`) and notifies `HOOK_SYSJUMP` listeners. Modules
    use [`system_add_jump_tag()`](../common/system.c) during this hook to
    serialize state across the jump into preserved RAM; the target image
    retrieves these tags on startup via
    [`system_get_jump_tag()`](../common/system.c) and sets
    `EC_RESET_FLAG_SYSJUMP`.
5.  **Watchdog Reload & Vector Branch**: Reloads the hardware watchdog
    (`watchdog_reload()`) so the destination image inherits a full timeout
    window, runs chip pre-jump teardown (`chip_pre_system_jump()`), disables DMA
    channels, and branches directly to the target image's reset vector.

### Why the EC Never Sysjumps `RW -> RO` in Production

On a locked production device ([`system_is_locked()`](../common/system.c)
returns `1`, meaning hardware write protect is asserted and
`EC_FLASH_PROTECT_RO_NOW` is active), **the EC never sysjumps from `RW` back to
`RO`**:

*   **Host Command Rejection**: In
    [`validate_reboot_command()`](../common/system.c), `EC_REBOOT_JUMP_RO`
    returns `EC_ERROR_ACCESS_DENIED` whenever `system_is_locked()` is true and
    `system_get_image_copy() != EC_IMAGE_RO`.
*   **Core Sysjump Gate**: In
    [`system_run_image_copy_with_flags()`](../common/system.c), if
    `system_is_locked()` is true, the EC enforces:
    1.  Current image **must** be `EC_IMAGE_RO`.
    2.  Target image **must** be an `RW` image (`EC_IMAGE_RW` or `EC_IMAGE_RW_B`).
    3.  Jumping **must not** be disabled (`disable_jump == 0`).
*   **Post-Sync Lockout (`EC_REBOOT_DISABLE_JUMP`)**: At the conclusion of EC
    Software Sync, Depthcharge sends `EC_REBOOT_DISABLE_JUMP`
    ([`system_disable_jump()`](../common/system.c)), which sets
    `disable_jump = 1` and enables the Memory Protection Unit (`mpu_enable()`).

**Security Rationale**: Because a sysjump does not reset CPU peripherals, MMIO
registers, or the `GPIO_ENTERING_RW` hardware latch on the GSC, allowing
`RW -> RO` via sysjump would permit potentially compromised `RW` state to
persist while executing inside `RO`. Consequently, **every `RW -> RO` transition
in production must be a cold hardware reset (`EC_REBOOT_COLD`)**.

*(Note: `RW -> RO` sysjumps are only permitted on unlocked development systems
where `!system_is_locked()`, such as when running `ectool reboot_ec ro`, console
`sysjump ro`, or `flashrom -p ec` on chips without `EC_FEATURE_EXEC_IN_RAM`.)*

---

## Normal Boot Flow (`RO -> RW`)

| Stage | EFS2 Boards (`CONFIG_VBOOT_EFS2`) | Legacy Non-EFS2 Boards |
| :--- | :--- | :--- |
| **1. Cold Reset** | EC boots into `EC_RO`. | EC boots into `EC_RO` and starts background hashing `EC_RW`. |
| **2. Pre-AP Verification** | [`vboot_main()`](../common/vboot/efs2.c) hashes `EC_RW` (`Heff`) and sends `CR50_COMM_CMD_VERIFY_HASH` over UART to GSC. | None; `EC_RO` powers on the AP while still running `RO`. |
| **3. `RO -> RW` Sysjump** | GSC confirms `Heff == Hmir` (`CR50_COMM_SUCCESS`). `EC_RO` sets `EC_RESET_FLAG_EFS`, **sysjumps `RO -> RW`** via [`verify_and_jump()`](../common/vboot/efs2.c), and powers on the AP from `EC_RW`. | Depthcharge runs `vb2api_ec_sync()`, verifies `Heff == Hexp`, and sends `EC_REBOOT_JUMP_RW` to **sysjump `RO -> RW`**. |
| **4. Lockout** | Depthcharge sees EC is already in `RW` with `Heff == Hexp == Hmir`, sets `EC_FLASH_PROTECT_ALL_NOW`, and sends `EC_REBOOT_DISABLE_JUMP`. | Depthcharge sets `EC_FLASH_PROTECT_ALL_NOW` and sends `EC_REBOOT_DISABLE_JUMP`. |

---

## Firmware Update Flow (OTA & Software Sync)

Three SHA-256 digests participate in EC Software Sync (`2lib/2ec_sync.c` in
`vboot_reference`):

*   **`Hexp` (Expected Hash)**: Hash of the `ecrw` binary bundled inside the
    active AP RW firmware slot (`FW_MAIN_A` or `FW_MAIN_B`).
*   **`Hmir` (Mirrored Hash)**: Hash stored in GSC/TPM kernel `secdata`, used by
    EFS2 during early EC boot before the AP is powered on.
*   **`Heff` (Effective Hash)**: Hash calculated by the EC over its own `EC_RW`
    flash partition (`EC_CMD_VBOOT_HASH`).

### 1. Userspace OTA Stage (`futility update`)

When ChromeOS installs a firmware update in production (`WP == 1`), `futility`
writes the new AP firmware image (which embeds the new `ecrw` payload and
`Hexp`) to the inactive AP RW slot (`FW_MAIN_A` or `FW_MAIN_B`) and sets the AP
try-slot cookie for the next reboot. **Userspace does not flash `EC_RW`
directly** when write protection is enabled; the update is applied by
Depthcharge on the next boot via Software Sync.

### 2. Boot-Time Software Sync Sequence (EFS2)

When the system reboots after an OTA update, an EFS2 board goes through the
following sequence:

1.  **Boot 1 — Sync `Hmir` and Cold-Reset EC to `RO`**:
    *   At the start of Boot 1, the EC is either still running the old `EC_RW`
        (warm AP reboot) or cold-booted and passed EFS2 against the *old* `Hmir`
        (`old Heff == old Hmir`), sysjumping `RO -> RW` into the old `EC_RW`.
    *   Depthcharge boots the new AP RW slot and runs `vb2api_ec_sync()` ->
        `ec_sync_phase1()` -> `check_ec_hash()`.
    *   Depthcharge detects `Hmir != new Hexp`, updates `Hmir = new Hexp` in GSC
        `secdata` (`VB2_SD_FLAG_ECSYNC_HMIR_UPDATED`), and flags an `EC_RW`
        update (`old Heff != new Hexp`).
    *   Because the EC is currently in `RW` (`VB2_SD_FLAG_ECSYNC_EC_IN_RW`) and
        cannot overwrite its active image or sysjump back to `RO`,
        `ec_sync_phase1()` returns `VB2_REQUEST_REBOOT_EC_TO_RO`.
    *   Depthcharge calls `vboot_reboot_to_ro()`, which sends
        `EC_REBOOT_COLD | EC_REBOOT_FLAG_ON_AP_SHUTDOWN` and powers off the AP.
        When the AP drops to S5/G3, the EC performs a **cold hardware reset**
        into `EC_RO`.
2.  **Boot 2 — Stay in `RO`, Reflash `EC_RW`, and Cold-Reset**:
    *   Coming out of cold reset, `EC_RO` runs [`vboot_main()`](../common/vboot/efs2.c)
        and sends `old Heff` to the GSC.
    *   Because `Hmir` was updated to `new Hexp` in Boot 1, the GSC replies with
        `CR50_COMM_ERR_BAD_PAYLOAD` (`Hash mismatch`). `EC_RO` **does not
        sysjump**; it remains in `RO` and powers on the AP.
    *   Depthcharge enters `vb2api_ec_sync()` -> `ec_sync_phase2()` ->
        `update_ec()` with the EC in `RO`:
        *   Clears `EC_FLASH_PROTECT_ALL_NOW` (if set).
        *   Erases `EC_RW` (`EC_CMD_FLASH_ERASE`) and writes the new `ecrw`
            payload (`EC_CMD_FLASH_WRITE`).
        *   Re-hashes `EC_RW` to verify `new Heff == new Hexp`.
    *   To switch the EC into the newly flashed `EC_RW`:
        *   **EFS2 Boards**: In Depthcharge's `vboot_jump_to_rw()`, if
            `EC_FEATURE_EFS2` is present, Depthcharge sends `EC_REBOOT_COLD` and
            powers off the AP instead of sending `EC_REBOOT_JUMP_RW`, ensuring
            `EC_RW` is entered via a clean EFS2 early-boot sysjump.
        *   **Legacy Non-EFS2 Boards**: Depthcharge sends `EC_REBOOT_JUMP_RW`
            (**sysjumps `RO -> RW`**), protects flash, disables jumps, and boots
            the OS immediately without a third boot.
3.  **Boot 3 (EFS2 Only) — Clean EFS2 Boot into New `EC_RW`**:
    *   `EC_RO` boots, verifies `new Heff == new Hmir` with the GSC
        (`CR50_COMM_SUCCESS`), **sysjumps `RO -> RW`** into the new `EC_RW`, and
        powers on the AP.
    *   Depthcharge verifies `Heff == Hexp == Hmir`, enables
        `EC_FLASH_PROTECT_ALL_NOW`, sends `EC_REBOOT_DISABLE_JUMP`, and boots
        the OS.

---

## Recovery Mode Flow

### 1. Entering Recovery Mode

*   **Manual Recovery (`Esc + Refresh + Power` or Dedicated Recovery Button)**:
    *   Pressing the recovery key combination with the power button resets the
        EC and sets `EC_HOST_EVENT_KEYBOARD_RECOVERY`, which invokes
        [`system_enter_manual_recovery()`](../common/system.c)
        (`SYSTEM_IN_MANUAL_RECOVERY`).
    *   In [`vboot_main()`](../common/vboot/efs2.c), when
        `system_is_manual_recovery()` or `EC_RESET_FLAG_STAY_IN_RO` is set,
        `EC_RO` **skips `verify_and_jump()`**, schedules `EC_REBOOT_COLD` on AP
        shutdown, and **stays in `RO`** while powering on the AP.
*   **AP-Initiated Recovery (e.g., Corrupt OS Kernel or `recovery_request`)**:
    *   If the AP warm-reboots into recovery mode without a cold EC reset, the
        EC remains in **RW** (from the initial power-on EFS2 jump).
    *   If recovery mode was triggered by an EC Software Sync error
        (`VB2_RECOVERY_EC_*`), Depthcharge cold-resets the EC via
        `VB2_REQUEST_REBOOT_EC_TO_RO` so the EC boots into **RO**.

### 2. Depthcharge Skips EC Sync in Recovery Mode

In `vb2api_ec_sync()` (`2lib/2ec_sync.c`), if `VB2_CONTEXT_RECOVERY_MODE` is
set, Depthcharge immediately returns `VB2_SUCCESS` (`"In recovery mode, skipping
EC sync"`).

Because the AP boots recovery mode from `AP_RO` to load an external recovery
image (and the AP's own `FW_MAIN_A/B` slots may be corrupt), Depthcharge never
flashes or sysjumps the EC during recovery boot. The EC continues running
whichever image it booted into (`RO` for manual recovery, `RW` for warm AP
recovery) throughout the recovery UI and USB recovery OS session.

### 3. Firmware Updates During USB Recovery (`futility update --mode=recovery`)

*   **Production Devices (`WP == 1`)**:
    *   When recovering from a USB image, `futility` runs
        `update_rw_firmware()`, which writes the recovery image's AP RW slots
        (`FW_MAIN_A` and `FW_MAIN_B`, containing the bundled `ecrw` payload) to
        the AP SPI flash.
    *   `futility` **does not** directly flash the EC from the recovery USB OS.
    *   Once USB recovery finishes and cold-reboots the system into normal mode,
        Depthcharge executes the standard [Boot-Time Software Sync
        Sequence](#2-boot-time-software-sync-sequence-efs2) above to update
        `EC_RW`.
*   **Unlocked / Factory Devices (`WP == 0`)**:
    *   When hardware write protection is disabled, `futility` runs
        `update_whole_firmware()` -> `update_ec_firmware()` via `flashrom -p ec`.
    *   If EC Software Sync is enabled and the EC is currently running `RW`,
        `QUIRK_EC_PARTIAL_RECOVERY` updates only the `WP_RO` section directly in
        flash and defers the `EC_RW` update to AP Software Sync on the
        subsequent reboot. Otherwise, it writes the full `RO + RW` image.
