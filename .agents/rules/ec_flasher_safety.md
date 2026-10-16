---
trigger: model_decision
description: Enforce CBI region preservation, C2D2/Servo reset sequencing, and VBUS brownout guards when modifying util/flash_ec or util/flash/.
---

# EC Flasher Safety Rules (`util/flash_ec` & `util/flash/`)

When modifying `util/flash_ec` or `util/flash/`:

## 1. Preserve `EC_RO` / `FMAP` `CBI` EEPROM & Flash Sections by Default

-   **Mandatory Section Preservation**: Always preserve `FMAP` sections marked
    with the `preserve` flag (such as `EC_RO` / `CBI` Cros Board Info regions)
    by default during EC flashing (`--skip-cbi`, `--skip-range`, or
    `preserve_sections_with_cur_image` via `futility dump_fmap` / `load_fmap`)
    unless `--no_preserve` (`--no-preserve`) is explicitly passed.
-   **Protect Board Identity & Straps**: Never allow a default flash operation
    to erase or overwrite `CBI` data (board ID, SKU ID, DRAM part/strap
    configuration, OEM ID, and `FW_CONFIG`), as corrupting `CBI` bricks platform
    hardware identification and memory initialization.

## 2. Guaranteed `cleanup_dut()` in `try/finally` Blocks Across Servo Types

-   **Unconditional Cleanup Execution**: Every flasher lifecycle (`run()`) must
    execute `cleanup_dut()` and `servo.restore_state()` inside a `try/finally`
    block so that interrupted or failed flash attempts never leave a DUT wedged
    in reset or DFU/programming mode.
-   **Controller-Specific State Restoration**: Ensure `cleanup_dut()` and state
    restoration cleanly release and restore:
    -   `gsc_ec_reset:off` (on `c2d2`, where `cold_reset` triggers an H1-level
        reset rather than an EC-only reset).
    -   `cold_reset:off` (on `servo_micro`, `servo_v2`, `servo_v4`, and
        `ccd_cr50`/`ccd_ti50`).
    -   `ccd_cpu_fw_spi:off`, `ccd_ec_boot_mode_uut:off`,
        `ccd_ec_boot_mode_bitbang:off`, and `gsc_uart_cmd:sysrst deassert` on
        `ccd_cr50`/`ccd_ti50`.
    -   Saved `ec_uart_cmd`, `ec_uart_regexp`, `ec_uart_baudrate`,
        `ec_uart_parity`, `ec_uart_bitbang_en`, and `ec_ec3po_interp_connect`
        settings across `c2d2`, `servo_micro`, and `ccd_cr50`/`ccd_ti50`.

## 3. Dedede `it83xx` vs. `npcx` Differentiation & I2C DFU VBUS Brownout Guards

-   **Model-Level Chip Differentiation on Dedede**: While the `dedede`
    baseboard defaults to `it83xx` (`ite_i2c`), specific models or variants may
    use `it8xxx2` or `npcx` ECs. Always query `ec_chip` from `servod` first,
    then fall back to `zephyr/program/*/BUILD.py` (`query_ec_source_tree`) and
    `board_map.py` (`get_chip_from_board`) before selecting an ITE I2C/UART vs.
    NPCX UUT/SPI flasher.
-   **VBUS Brownout Protection During I2C DFU**: Entering ITE I2C DFU resets
    the EC and drops USB-C PD negotiation, which can cause VBUS brownouts if
    the DUT is powered solely through Servo or an unconfigured charger path.
    Ensure power/reset sequencing and mux controls (`HOST_OR_CHG_CTL 0`,
    `DUT_CHG_EN 1`, `bpforce dis atboot`, and `EC_FLASH_SELECT 1` /
    `ec_flash_select:on` with `gsc_ec_reset` and `gsc_idle_level:active`) keep
    DUT rail power and I2C programming lines stable throughout the DFU erase
    and write cycle.
