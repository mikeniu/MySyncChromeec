# Panic Handling in EC

[TOC]

This document describes the design, memory layout, historical evolution, and debugging mechanisms of the panic handling system in the ChromeOS Embedded Controller (EC) firmware. The panic data is designed to capture the state of the EC at the moment of a fatal exception, persist this state across reboots, and expose it to the host for crash reporting.

## Memory Layout

To ensure panic data survives a reboot, it is stored in a dedicated, uninitialized section of RAM. The panic data section is interpreted by both the RO and RW images, so changes must be compatible with both images. Jump data (used for transitioning between RO and RW) is aligned to be immediately before the panic data.

The structure of `struct panic_data` is defined in [include/panic_defs.h](../include/panic_defs.h).

```
              +------------------------------------+  <- RAM_BASE (Low Address)
              |                                    |
              |             Used RAM               |
              |                                    |
              +------------------------------------+
              |                                    |
              |             Unused RAM             |  <- JUMP_DATA_MIN_ADDRESS = END_OF_RAM - PRESERVED_END_OF_RAM_SIZE
              |                                    |
              +------------------------------------+
              |        Jump Tags (Grow Up)         | <- get_jump_data() - jump_tag_total
              |                                    |
              +------------------------------------+
              | struct jump_data                   | <- get_jump_data() = get_panic_data_start() - sizeof(struct jump_data)
              |   ...                              |
              |   int jump_tag_total               |
              |   uint32_t reset_flags             |
              |   int version                      |
              |   int magic (JUMP_DATA_MAGIC)      |  <- get_panic_data_start() - 4
              +------------------------------------+
              | struct panic_data                  |  <- get_panic_data_start() = END_OF_RAM - struct_size
              |   uint8_t arch                     |  <- END_OF_RAM - struct_size
              |   uint8_t struct_version           |  <- END_OF_RAM - struct_size + 1
              |   uint8_t flags                    |
              |   ...                              |
              |   uint32_t struct_size             |  <- END_OF_RAM - 8
              |   uint32_t magic (PANIC_DATA_MAGIC)|  <- END_OF_RAM - 4
              +------------------------------------+  <- END_OF_RAM = RAM_BASE + RAM_SIZE (High Address)
```

The alignment and values of the following fields are critical for backward and forward compatibility:

1. **`panic_data.magic`**: Always located at `END_OF_RAM - 4` and must be `0x21636e50` (`"Pnc!"`).
2. **`panic_data.struct_size`**: Always located at `END_OF_RAM - 8`. Note that some older RO versions require `struct_size` to exactly match their own `sizeof(struct panic_data)`.
3. **`panic_data.arch`**: Located at `END_OF_RAM - panic_data.struct_size`.
4. **`panic_data.struct_version`**: Located at `END_OF_RAM - panic_data.struct_size + 1`. Currently 2. (Some older RO versions require this to be exactly 2).
5. **`jump_data.magic`**: Located at `get_panic_data_start() - 4` (which is `END_OF_RAM - panic_data.struct_size - 4` if panic data is present) and must be `0x706d754a` (`"Jump"`).

Because `struct_size` and `magic` are at the very end of `struct panic_data`, and the struct is aligned to end at `END_OF_RAM`, any image can find these two fields at `END_OF_RAM - 8` and `END_OF_RAM - 4` respectively, regardless of what that image believes `sizeof(struct panic_data)` to be. This allows a newer RW image with a larger `struct panic_data` to write its panic data, and an older RO image can still find the magic and see the actual size of the data in `struct_size`.

### Constraints on Panic Data Size

Since `struct panic_data` contains a union of architecture-specific structures (the `arch` union), its size is determined by the largest member of the union. **Therefore, adding a new architecture to the union or updating an existing architecture may affect the `panic_data` struct size.**

The EC firmware checks that the boot-time panic data's `struct_size` matches its own compiled `sizeof(struct panic_data)` (`CONFIG_PANIC_DATA_SIZE`). This check (introduced in commit [2187da3974](https://chromium.googlesource.com/chromiumos/platform/ec/+/2187da39745e802fa89539ed28a9cf3fa9661876) (introduced safe pointers)) is used by `panic_get_data()` to ensure the structure can be safely interpreted; if there is a size mismatch, `panic_get_data()` returns `NULL`. This leads to two distinct scenarios where an older RO image might overwrite or corrupt the panic data written by a newer RW image:

1.  **Jump Data Overwrite (RO prior to commit [b95c5c31c0](https://chromium.googlesource.com/chromiumos/platform/ec/+/b95c5c31c0b0f9082f9bfbe5ec6eaaa24c1e3294) (used panic data start for layout))**:
    Before commit `b95c5c31c0`, the RO image used `panic_get_data()` to find the start of the panic data when calculating the offset for jump data. If the sizes mismatched, `panic_get_data()` returned `NULL`, causing RO to assume no panic data existed. RO would then place its jump data at `RAM_END - sizeof(struct jump_data)`, directly overwriting the RW panic data.

    *For RO images after commit [b95c5c31c0](https://chromium.googlesource.com/chromiumos/platform/ec/+/b95c5c31c0b0f9082f9bfbe5ec6eaaa24c1e3294)*, this is no longer an issue because RO uses `get_panic_data_start()`, which reads `pdata_ptr->struct_size` directly (bypassing the equality check) to correctly align the jump data before the panic data, preserving it.

2.  **Watchdog Reset Promotion (RO prior to commit [682f4c61f1](https://chromium.googlesource.com/chromiumos/platform/ec/+/682f4c61f142f622091dcf60ccac339a8d5ac8f2) (restricted promotion to RW))**:
    Before commit `682f4c61f1`, if a watchdog reset occurred, the RO image would attempt to promote the watchdog panic reason. If the RO image had a smaller `struct panic_data` than the RW image, RO's `panic_set_reason` would invoke `get_panic_data_write()`. This would recalculate the layout using RO's smaller compiled size, moving the jump data and overwriting the extra fields of the RW panic data. Even if it could interpret the data, RO's promotion would only preserve the `reason`, `info`, and `exception` fields, blanking out any new registers or fields that the newer RW image might have populated. This was resolved by restricting the promotion logic to run only in the RW image.

A key historical example of these compatibility issues occurred when RISC-V support was added, increasing the struct size by 28 bytes. To prevent older, already-deployed RO images from corrupting the panic data, commit [4f49ac0fa7](https://chromium.googlesource.com/chromiumos/platform/ec/+/4f49ac0fa76a2dff55e99ac7b1c5afc66693174e) (allowed excluding RV32I) introduced `CONFIG_DO_NOT_INCLUDE_RV32I_PANIC_DATA` to allow RW to match the older RO size.

### Unused RAM Allocation
`CONFIG_PRESERVED_END_OF_RAM_SIZE` (defaults to 1024 bytes) is a compile-time check ensuring that at least this much RAM is left uninitialized at the end of memory for panic data, jump data, and jump tags.

While `sizeof(struct panic_data)` and `sizeof(struct jump_data)` are static, the space required for jump tags depends on the number and size of tags allocated at runtime during a sysjump. Therefore, `CONFIG_PRESERVED_END_OF_RAM_SIZE` must be allocated conservatively to prevent jump tags from overflowing into the usable RAM area.

Furthermore, this uninitialized RAM space must be aligned with the RO image. In the majority of cases, the RW image uses more RAM than the RO image, so if the RW image has at least `PRESERVED_END_OF_RAM_SIZE` bytes available, the RO image will also have this much or more. However, in theory, it is possible for the RO image to use more RAM than the RW image. This would result in the `PRESERVED_END_OF_RAM` space of the RW image being corrupted by the RO image's used RAM during boot.

### Host Command Size Constraint
Historically, the `EC_CMD_GET_PANIC_INFO` host command (v0 and v1) required the entire `panic_data` struct to fit within a single host command response payload. This limited the maximum size of `struct panic_data` to the maximum payload size of the host interface bus (e.g., LPC, I2C, SPI), which is typically 256 bytes (before accounting for headers).

Version 2 of `EC_CMD_GET_PANIC_INFO`, introduced in commit [d2fb1a5bf0](https://chromium.googlesource.com/chromiumos/platform/ec/+/d2fb1a5bf0b5c156689bd708a3d5ea7ef484beaa) (introduced chunked reads v2), removed this constraint by supporting multi-part reads using a `read_offset` parameter. This allows the host to read panic data of arbitrary size in chunks.

## Gotchas

### Corrupt Panic Data
Since jump data alignment is derived from the panic data struct size, corruption in this structure can cause out-of-bounds memory faults during initialization. The `magic` field is the canary used to detect corrupt panic data, but this is not robust against all forms of corruption.

*   **Zephyr Heap Overlap**: By default, Zephyr's `CONFIG_COMMON_LIBC_MALLOC` allocates all unused RAM at startup for the heap. This dynamic allocation will overlap with and corrupt the panic and jump data stored at the end of RAM. To prevent this, if `CONFIG_COMMON_LIBC_MALLOC` is enabled, `CONFIG_COMMON_LIBC_MALLOC_ARENA_SIZE` must be set to a positive value to limit the heap size and preserve the end of RAM (see commit [3f203a5436](https://chromium.googlesource.com/chromiumos/platform/ec/+/3f203a5436f9d6fdb9c8bd120463fbfc75b826ee) (restricted startup heap)).

### False Positives
The mechanism for storing and retrieving panic data across resets is complex and has historically resulted in false positives or duplicate reports.

1.  **Watchdog Warning Stale Data**: A watchdog warning timer populates panic data before the hardware watchdog triggers. If the EC recovers before the hardware watchdog triggers, and the pre-populated panic data is not cleared, it may be incorrectly reported as a watchdog reset on the next reboot. `PANIC_SW_WATCHDOG_WARN` was added (commit [a58ecc0d44](https://chromium.googlesource.com/chromiumos/platform/ec/+/a58ecc0d44) (added watchdog warning)) to distinguish a watchdog warning from an actual watchdog reset.
2.  **Firmware Updates**: A false positive attribution can occur after a firmware update. Since the panic data does not indicate which version of the EC firmware was running when the panic occurred, it is possible for a panic to occur in an older RW firmware but be attributed to the newly updated RW firmware after the reboot.
3.  **BBRAM Flag Restore Bug**: On legacy systems using battery-backed RAM (BBRAM) for panic backup, a bug (prior to commit [256be11586](https://chromium.googlesource.com/chromiumos/platform/ec/+/256be11586da69da137518352d53b8011b0762e2) (fixed BBRAM restore)) existed where panic flags were not preserved properly across resets. This caused the panic to appear as new on every EC boot, leading to duplicate crash reports.
    *(Note: BBRAM backup of panic data is a legacy feature and is not supported in Zephyr-based EC firmware).*

### False Negatives
1.  **Double Panic**: If two consecutive panics occur before the host can collect the first one, the second panic will overwrite the first.
2.  **RO Overwrite**: If the RO image cannot interpret the panic data, it may overwrite it. See [Constraints on Panic Data Size](#constraints-on-panic-data-size).
3.  **Early Host Fetch**: If a host or AP process fetches the panic data via `EC_CMD_GET_PANIC_INFO` (e.g., during early boot scripting) without setting the `preserve_old_hostcmd_flag`, it will mark the panic as old (`PANIC_DATA_FLAG_OLD_HOSTCMD`). The host crash collector will then ignore it, resulting in a missed crash report.
4.  **Power Loss**: If the EC RAM loses power (e.g. during hibernation or battery cut-off) the stored panic data in RAM will be lost.

## Panic Data Flags

| Flag | Description |
| :---- | :---- |
| `PANIC_DATA_FLAG_FRAME_VALID` | For ARM only, indicates that the captured exception frame is valid. Also indicates `BKUP_PANIC_DATA_VALID` when written to bbram.  |
| `PANIC_DATA_FLAG_OLD_CONSOLE` | Indicates that the panic data has been displayed in the EC console with the `panicinfo` command. |
| `PANIC_DATA_FLAG_OLD_HOSTCMD` | Indicates that the panic data has been fetched by the host OS. This is the primary method used to indicate stale panic data. |
| `PANIC_DATA_FLAG_OLD_HOSTEVENT` | Indicates if the `EC_HOST_EVENT_PANIC` host event has been sent to the host. This is primarily used for event logging by AP firmware. |
| `PANIC_DATA_FLAG_TRUNCATED` | Used to indicate that the panic_data structure was too large to fit in the v1 `EC_CMD_GET_PANIC_INFO` host command response. This flag is deprecated in the v2 `EC_CMD_GET_PANIC_INFO` host command. |
| `PANIC_DATA_FLAG_RO_IMAGE` | Indicates that the panic occurred while executing the RO image. |
| `PANIC_DATA_FLAG_RW_IMAGE` | Indicates that the panic occurred while executing the RW image. Both `PANIC_DATA_FLAG_RO_IMAGE` and `PANIC_DATA_FLAG_RW_IMAGE` may be unset since it is not always possible to determine the source image of the panic. |

## Hardware Panics

For hardware fault panics (e.g. Usage Fault) the exception stack frame (ESF) containing the registers at the moment of the fault is copied to the respective registers in the panic data struct.

## Software Panics

For software panics (e.g., watchdog resets, stack overflows, assertion failures), the EC does not have a hardware exception stack frame. Instead, it overloads specific register fields in the `panic_data` struct to store software-specific panic details:

*   **Reason**: A `PANIC_SW_*` constant (defined in [include/software_panic.h](../include/software_panic.h)), which always begins with `0xDEAD` (e.g., `0xDEAD6664` for `PANIC_SW_WATCHDOG`).
*   **Info**: Reason-specific context.
*   **Exception**: Additional context, such as the active task ID or truncated thread ID in Zephyr EC.

The register fields used to store these overloaded values are architecture-specific:

| Architecture | Reason | Info | Exception |
| :--- | :--- | :--- | :--- |
| **ARM (Cortex-M)** | `R4` (`v1`) | `R5` (`v2`) | `ipsr` |
| **RISC-V** | `s0`  | `s1` | `mcause` |
| **x86** | `vector` | `error_code` | `eflags` |


## Assert Handling

When an assertion fails, the EC logs a software panic with the reason `PANIC_SW_ASSERT` (`0xDEAD6663`) and reboots. To help identify where the assertion occurred, the filename and line number of the assertion are encoded into the `info` field:

*   **Info Field Encoding**:
    *   Bits 31–24: ASCII value of the 1st character of the filename.
    *   Bits 23–16: ASCII value of the 2nd character of the filename.
    *   Bits 15–0: Line number of the assertion.

For example, an assertion failure in `charge_state.c` at line `124` would have `info` encoded as:
`('c' << 24) | ('h' << 16) | (124 & 0xffff)` => `0x6368007c`.

*   **Exception Field**: This field is populated with the thread ID (`k_tid_t` cast to `uint8_t`, which truncates it to 1 byte) of the thread that triggered the assertion.

In Zephyr EC, asserts override Zephyr's `zassert_post_action()` (see [zephyr/shim/src/panic.c](../zephyr/shim/src/panic.c#216)). It extracts the basename of the file path before encoding it, writes the panic data, and then reboots.

## Watchdog Handling

Watchdog resets require special coordination between the watchdog timer, the panic logging system, and the RO/RW images.

### Unified Watchdog Panic Structure (`PANIC_ARCH_WATCHDOG`)

In Zephyr EC, when `CONFIG_PLATFORM_EC_PANIC_WATCHDOG_STRUCT` is enabled (default `y`), watchdog warnings use a dedicated, architecture-agnostic panic structure (`struct watchdog_panic_data`, with `pdata->arch = PANIC_ARCH_WATCHDOG`, `pdata->wd.struct_major_version = WATCHDOG_PANIC_DATA_MAJOR_VERSION` (`1`), and `pdata->wd.struct_minor_version = WATCHDOG_PANIC_DATA_MINOR_VERSION` (`0`)):

| Offset | Type | Field | Description |
| :--- | :--- | :--- | :--- |
| `0` | `uint32_t` | `reason_x86` | Legacy reason slot aligned with `x86.vector` and `nds_n8.itype`. |
| `4` | `uint8_t` | `struct_major_version` | Major version (`WATCHDOG_PANIC_DATA_MAJOR_VERSION`, currently `1`). Incremented only on breaking layout changes. |
| `5` | `uint8_t` | `struct_minor_version` | Minor version (`WATCHDOG_PANIC_DATA_MINOR_VERSION`, currently `0`). Incremented on append-only field additions. |
| `6` | `uint16_t` | `struct_flags` | `WATCHDOG_PANIC_FLAG_*` bitmask (`BIT(0)` = `WATCHDOG_PANIC_FLAG_WARNING`). |
| `8` | `uint32_t` | `thread_id` | Interrupted Zephyr `k_tid_t` pointer (`k_current_get()`). |
| `12` | `uint32_t` | `reason_cm` | Legacy reason slot aligned with Cortex-M `cm.regs[3]` (`R4`). |
| `16` | `uint32_t` | `pc` | Interrupted Program Counter (`PC` on ARM, `MEPC` on RISC-V). |
| `20` | `uint32_t` | `lr` | Interrupted Link / Return Address register (`LR` on ARM, `RA` on RISC-V). |
| `24` | `uint32_t` | `sp` | Interrupted Stack Pointer (`get_stack_ptr(k_current_get())`). |
| `28..43` | `char[16]` | `version` | Tail 16 bytes of running firmware version (`current_image_data.version`). |
| `44` | `uint32_t` | `reason_riscv` | Legacy reason slot aligned with RISC-V `riscv.regs[11]` (`s0`). |
| `48` | `uint32_t` | `warning_uptime` | EC uptime (`k_uptime_get()`, ms) when the watchdog warning fired. |
| `52` | `uint32_t` | `reset_flags` | EC reset flags (`system_get_reset_flags()`). |
| `56` | `uint16_t` | `power_signals` | Current AP power signal bitmask (`power_get_signals()`). |
| `58` | `uint8_t` | `power_state` | Current AP power state (`power_get_state()` / `ap_pwrseq_get_current_state()` / `pwr_sm_get_state()`). |
| `59` | `uint8_t` | `battery_level` | Cached battery charge percentage (`charge_get_percent()`, `0..100`) (total `v1.0` size: `60` bytes). |

#### Backward Compatibility with Deployed RO Images

Older RO (or RW) firmware images do not inspect `pdata->arch` for `PANIC_ARCH_WATCHDOG`; they read the software panic reason directly from their native architecture's register offset (`x86.vector` at offset `0`, `cm.regs[3]` at offset `12`, or `riscv.regs[11]` at offset `44`). By placing `reason_x86`, `reason_cm`, and `reason_riscv` at those exact byte offsets (enforced via `BUILD_ASSERT`s in `include/panic_defs.h`) and populating all three with `PANIC_SW_WATCHDOG` (`0xDEAD6664`), older firmware recognizes the watchdog panic and preserves the panic data across sysjump.

#### Watchdog Warning and Post-Reset Finalization

1. **Watchdog Warning (`panic_data_write_watchdog_warning`)**:
   When the watchdog pre-warning timer fires (`WATCHDOG_WARNING_LEADING_TIME_MS` before hardware watchdog expiry) and `CONFIG_PLATFORM_EC_PANIC_WATCHDOG_STRUCT` is enabled, the handler populates `struct watchdog_panic_data` with `pc`, `lr`, `sp`, `thread_id`, `version`, `warning_uptime`, `reset_flags`, `power_signals`, `power_state`, `battery_level`, sets all three `reason_*` fields to `PANIC_SW_WATCHDOG`, and sets `WATCHDOG_PANIC_FLAG_WARNING` in `wd.struct_flags`.
2. **Post-Reset Finalization (`panic_finalize_watchdog` in RW `panic_data_init`)**:
   On subsequent boot in RW when `EC_RESET_FLAG_WATCHDOG` is set:
   - If unread (`panic_data_is_new()`) `PANIC_ARCH_WATCHDOG` data has `WATCHDOG_PANIC_FLAG_WARNING` set, `panic_finalize_watchdog()` clears `WATCHDOG_PANIC_FLAG_WARNING`, preserving all warning context captured before the reset.
   - If unread legacy panic data has `PANIC_SW_WATCHDOG_WARN` (`0xDEAD6668`), it is promoted in-place to `PANIC_SW_WATCHDOG` (`0xDEAD6664`).
   - Otherwise (no prior unread warning, e.g., interrupts were locked during a hard hang), `panic_data_init()` resets the standard architecture `panic_data` structure, sets `PANIC_SW_WATCHDOG_HARD` (`0xDEAD666A`), and clears the `RO_IMAGE`/`RW_IMAGE` flags (since the source image of a hard watchdog reset without a warning cannot be determined).
   - As before (since commit [682f4c61f1](https://chromium.googlesource.com/chromiumos/platform/ec/+/682f4c61f142f622091dcf60ccac339a8d5ac8f2)), RO skips watchdog finalization/promotion so legacy RO images never clobber RW panic data before jumping to RW.

#### Updating and Augmenting `struct watchdog_panic_data` Going Forward

`struct watchdog_panic_data` uses semantic versioning (`struct_major_version`, `struct_minor_version`) combined with a versioned `union` and compile-time `BUILD_ASSERT`s to guarantee forward and backward compatibility across RO/RW images and host parsers (`libec`, `ectool`, crash-reporter):

1. **Append-Only Additions (Increment `struct_minor_version`, Keep `struct_major_version == 1`)**:
   - All `v1.0` fields (offsets `0..59`) are frozen in `WATCHDOG_PANIC_DATA_V1_0_FIELDS` / `struct watchdog_panic_data_v1_0` (`60` bytes) and locked down by per-field `offsetof()` `BUILD_ASSERT`s in `include/panic_defs.h`.
   - To add new fields in minor version `1.(N+1)`:
     1. Append the new fields to the anonymous struct inside `struct watchdog_panic_data` after the `v1.N` fields (so direct `pdata->wd.<field>` access works seamlessly).
     2. Define `struct watchdog_panic_data_v1_<N+1>` (embedding `struct watchdog_panic_data_v1_<N>` as its first member) and add it to the `union` inside `struct watchdog_panic_data` with a `BUILD_ASSERT(sizeof(struct watchdog_panic_data_v1_<N+1>) == ...)`.
     3. Increment `WATCHDOG_PANIC_DATA_MINOR_VERSION`.
   - **Size Ceiling (`104` bytes)**: `panic_get_data()` requires `pdata->struct_size == sizeof(struct panic_data)` (`CONFIG_PANIC_DATA_SIZE`). The union inside `struct panic_data` is bounded by `sizeof(struct cortex_panic_data)` (`104` bytes on boards with `CONFIG_DO_NOT_INCLUDE_RV32I_PANIC_DATA`) or `sizeof(struct rv32i_panic_data)` (`132` bytes otherwise). Because `v1.0` is `60` bytes, up to **`44` bytes** of new fields can be appended (up to `104` bytes total, enforced by `BUILD_ASSERT(sizeof(struct watchdog_panic_data) <= sizeof(struct cortex_panic_data))`) without increasing `sizeof(struct panic_data)` on any board or breaking deployed RO images.
2. **Reader Forward Compatibility (`struct_major_version == 1`)**:
   - Any `v1.x` reader (`panic_data_print()`, `panic_finalize_watchdog()`, `libec`) checks `pdata->wd.struct_major_version == 1`.
   - Because minor versions are strictly append-only, an older `v1.0` processor or tool can always decode the `v1.0` fields (`0..59`) even when reading panic data written by newer `v1.1+` firmware, and newer readers check `pdata->wd.struct_minor_version >= N` before reading fields added in `v1.N`.
3. **Breaking Layout Changes (Increment `struct_major_version`)**:
   - If existing fields must ever be removed or reordered, increment `WATCHDOG_PANIC_DATA_MAJOR_VERSION` (e.g., to `2`), reset `WATCHDOG_PANIC_DATA_MINOR_VERSION` to `0`, and add `struct watchdog_panic_data_v2_0 v2_0` to the `union` in `struct watchdog_panic_data` while keeping `reason_x86` (offset `0`), `struct_major_version` (offset `4`), `struct_minor_version` (offset `5`), `reason_cm` (offset `12`), and `reason_riscv` (offset `44`) at their fixed offsets.
   - Older processors checking `struct_major_version == 1` will cleanly recognize that they cannot interpret `struct_major_version == 2` without misparsing fields or requiring a new `PANIC_ARCH_*` value.

## Zephyr Integration

Zephyr's `k_sys_fatal_error_handler` is overridden by the EC (see [zephyr/shim/src/panic.c](../zephyr/shim/src/panic.c#167)). When a fatal error occurs:

1.  **With Exception Stack Frame (ESF)**: If the error provides an ESF (e.g., CPU exception like MemManage or BusFault) and the architecture is supported:
    *   The ESF registers are copied to the corresponding arch-specific fields in `struct panic_data` (e.g., `cm` for ARM, `riscv` for RISC-V).
    *   For ARM, the `PANIC_DATA_FLAG_FRAME_VALID` flag is set.
2.  **Without ESF**: If no ESF is available (e.g., kernel panic, `k_oops()`, spurious interrupt):
    *   The panic reason is set to `PANIC_ZEPHYR_FATAL_ERROR` (`0xDEAD6800`).
    *   The Zephyr fatal `reason` code is stored in the `info` field.
    *   The current thread ID (truncated to 1 byte) is stored in the `exception` field.
3.  **Immediate Reboot**: The handler flushes the console logs (`LOG_PANIC()`) and immediately triggers a software reset via `panic_reboot()`.

## Host Interface

The host OS retrieves panic data during boot via the `EC_CMD_GET_PANIC_INFO` host command. To prevent duplicate crash reports from stale panic data, the EC sets the `PANIC_DATA_FLAG_OLD_HOSTCMD` flag in `panic_data.flags` to indicate the panic data is old. For Version 2 (chunked reads), this flag is only set after the host has read the last byte of the panic data.

### Host Command Versions
*   **Version 0**: Returns the raw `panic_data` structure. The entire structure must fit within a single host transfer protocol packet.
*   **Version 1**: Introduces the `preserve_old_hostcmd_flag` in the parameters. If set, the EC will not mark the panic data as "old" after this read. This is useful for debugging tools (like `ectool panicinfo`) to inspect the panic data without preventing the host crash collector from reporting it.
*   **Version 2**: Introduces `read_offset` in the parameters. This allows reading the panic data in chunks, removing the constraint that the panic data must fit in a single host packet.

## Testing and Debugging

The EC console provides commands to inspect, clear, and simulate crashes for testing.

*   **`panicinfo`**: Prints the saved panic data from the previous crash.
    *   Once printed, the EC sets the `PANIC_DATA_FLAG_OLD_CONSOLE` flag so subsequent runs know it has been viewed.
    *   If no panic data is valid (or if there is a size mismatch), it will print: `No saved panic data available or panic data can't be safely interpreted.`
*   **`panicinfo clear`**: Clears the saved panic data by zeroing out the entire `panic_data` memory region (including the magic number and size).
*   **`crash <type>`**: Simulates a fatal crash to verify panic logging and recovery. Supported crash types include:
    *   `assert`: Triggers a failed assertion (`ASSERT(0)`).
    *   `divzero` / `udivzero`: Triggers a signed/unsigned integer division by zero.
    *   `stack`: Triggers a stack overflow via infinite recursion.
    *   `unaligned`: Triggers an unaligned memory access.
    *   `watchdog`: Enters an infinite loop (with interrupts enabled) to trigger a hardware watchdog reset.
    *   `hang`: Enters an infinite loop with interrupts disabled (`irq_lock()`), simulating a hard lockup.
    *   `null`: Dereferences a null pointer.
    *   `oops`: Triggers a Zephyr kernel oops (`k_oops()`) (Zephyr only).

    *Note: Some architectures may not support every simulated crash type (e.g., unaligned access might be silently handled by hardware on some CPUs).*

The `crash` command supports nested crashes if `CONFIG_CMD_CRASH_NESTED` is enabled. If you provide multiple arguments (e.g., `crash assert divzero`), the EC will stage the second crash to be executed *during* the handling of the first crash. This is used to test the stability of the panic handler itself when a fault occurs inside the exception handler.

### Panic Data Display Format

*Note: Annotations starting with `#` in the examples below are explanations added for this document and are not part of the actual tool output.*

#### EC Console `panicinfo` Output (ARM Watchdog Example)

Example output for a promoted watchdog reset (`PANIC_SW_WATCHDOG`):
```text
Saved panic data: 0x48 (NEW)  # Panic Flags (RW_IMAGE | OLD_HOSTEVENT)
  a1       = 0x00000000
  a2       = 0x00000000
  a3       = 0x00000000
  a4       = 0x00000000
  ip       = 0x00000000
  lr       = 0x00000000
  pc       = 0x00000000
  xpsr     = 0x00000000
  v1       = 0xDEAD6664   # Software Panic Reason (PANIC_SW_WATCHDOG)
  v2       = 0x2001c9c2   # Info: PC (or stack pointer in this example)
  v3       = 0x00000000
  v4       = 0x00000000
  v5       = 0x00000000
  v6       = 0x00000000
  v7       = 0x00000000
  v8       = 0x00000000
  psp      = 0x00000000
  ipsr     = 0x00000004   # Exception: Truncated Thread Id
  exc_rtn  = 0x00000000
  msp      = 0x00000000
```

#### Host `ectool panicinfo` Output (ARM Watchdog Example)

The host-side `ectool panicinfo` command parses the binary panic data. This command depends on the `libec` library to decode the architecture-specific layouts into human-readable text.

```text
# ectool panicinfo
Saved panic data: 4a                                    # Panic Flags (RW_IMAGE | OLD_HOSTEVENT | OLD_CONSOLE)
=== PROCESS EXCEPTION: 04 ====== xPSR: ffffffff ===     # Exception / Task ID = 04
r0 :         r1 :         r2 :         r3 :             # Blank fields because FRAME_VALID is not set
r4 :dead6664 r5 :2001c9c2 r6 :00000000 r7 :00000000     # r4: Reason (PANIC_SW_WATCHDOG), r5: Info (PC)
r8 :00000000 r9 :00000000 r10:00000000 r11:00000000
r12:         sp :00000000 lr :         pc :

cfsr=00000000, shcsr=00000000, hfsr=00000000, dfsr=00000000, ipsr=00000004  # ipsr: Exception / Task ID = 04
```

## See Also

*   [Zephyr Fatal Errors (External)](https://docs.zephyrproject.org/latest/kernel/services/errors/index.html)
*   [Software Panic Definitions](../include/software_panic.h)
*   [Panic Definitions](../include/panic_defs.h)
*   [Panic Log](../common/panic_log.c)
