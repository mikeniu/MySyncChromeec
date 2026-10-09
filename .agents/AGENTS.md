This document contains agent context for EC development.

# Overview

This repository contains the Legacy EC (embedded controller) firmware for
Chromium OS devices. The EC handles low-level tasks such as power sequencing,
battery charging, keyboard control, thermal management, power delivery, etc.

Starting roughly in July of 2021, Chromebooks switched from the original Google
Chrome EC to an application based on Zephyr.
The following terms are used to describe these two implementations:
* Original Chrome EC: ECOS, cros-ec, and legacy EC
* Zephyr-based EC: Zephyr EC

This repository `chromiumos/src/platform/ec-legacy` is for Legacy EC development.

Zephyr EC development is at `chromiumos/src/platform/ec`.

See local `docs/` folder for detailed documentation on specific topics.

# Glossary and Terminology

@context/ec_terms.md

## General Terminology

*   **CBMEM**: Coreboot memory. Used to retrieve early boot/BIOS logs using the
    `cbmem` command on DUT host.
*   **CCD (Case Closed Debugging)**: A feature provided by the GSC that allows
    developers to access the EC console, AP console, and flash the EC.
*   **DUT**: Device Under Test. The device being tested or developed on. The DUT
    may be local or remote in lab.
*   **EC Console**: Interactive shell available via UART (Servo). Use `help` for
    commands. Use `dut-control ec_uart_pty` to find the EC's UART path.
*   **FAFT**: Fully Automated Firmware Tests. A collection of tests and related
    infrastructure that exercise and verify capabilities of Chrome OS. Run using
    `tast`.
*   **GBB (Google Binary Block)**: A read-only section of the AP firmware that
    stores keys and flags. GBB flags are often manipulated during development
    (using futility) to bypass developer mode screens, alter boot behavior, or
    disable Software Sync.
*   **GSC / Cr50 / Ti50**: Google Security Chip. A custom Titan-based secure
    microcontroller on the board. The EC communicates heavily with the GSC for
    TPM functionality, Case Closed Debugging (CCD), and power sequencing.
*   **Program** The program includes all Chromebooks based on a single AP SoC.
    The term baseboard is often used as a synonym.
*   **Project** The name of a specific variant of a program. The term board or
    model is often used as a synonym.
*   **Servo**: A physical hardware debug board connected to the device. It
    provides access to the EC's serial console (UART), hardware reset controls,
    and allows flashing the EC even if DUT is unresponsive.
*   **Tast**: The ChromeOS integration testing framework, frequently used to
    validate firmware behavior (e.g., `firmware.EcStress`).

# Tooling

*   **`cros_sdk` (chroot)**: The ChromiumOS build environment. Build and test
    commands must be run inside this chroot. Use `cros_sdk --working-dir . <cmd>`
    from the host to run commands inside.
*   **`dut-control`**: Command-line tool to interact with the Servo. It allows
    you to control the DUT (e.g., `dut-control cold_reset:on`), read GPIOs, etc.
    Use `dut-control -i` to list all available commands.
*   **`ectool`**: Host utility to send commands to the EC from userspace (run
    from the DUT host).
*   **`flash_ec`**: Utility script (`util/flash_ec`) to flash EC firmware via Servo.
*   **`futility`**: Used to manage GBB flags (e.g., to disable Software Sync
    for development) (run inside `cros_sdk` or on the DUT).
*   **`repo`**: The repository management tool built on top of Git, used to
    manage the massive ChromeOS source tree.

# Source Code Organization

All paths below are relative to the Chrome EC base directory
`chromiumos/src/platform/ec-legacy`.

* `common/` - Upper-level code shared across boards and the EC implementations.
  This includes the following features:
  * Battery charging
  * USB Power Delivery
  * AP chipset power sequencing
  * Motionsense (EC sensor support)
  * Keyboard handling
  * Verified boot support

* `driver/` - Low-level drivers for on-board peripherals controlled by the EC.
  This does not include any drivers for modules integrated directly into the EC
  chipset (such as GPIO controllers, I2C, controllers, keyboard controller).
  On-board peripheral drivers include:
  * Charge controllers
  * USB Power Delivery chips (TCPCs, PPCs, muxes, etc)
  * Temperature sensors
  * Motionsense sensors (accelerometers, gyroscopes, light sensors, etc)

* `include/` - Header files for the `common/`, `driver/`, `chip/` code
   * `include/config.h` - Configuration header file. Individual boards may override
     the default values defined in this file.

* `build/` - The build output directory. This directory should be excluded when
  searching (e.g. grep --exclude-dir=build).

* `util/` - The EC utilities, including the `ectool` command-line tool.

* `baseboard/` - Baseboard-specific code. Every board is based on a baseboard.
* `board/` - Board-specific code. Every board is based on a baseboard.
* `chip/` - Chip-specific code. This directory contains the low-level drivers for on-board peripherals.
* `core/` - Core EC code. This directory contains the low-level chip-specific code (ARM Cortex-M, RISC-V, NDS32, etc).

# Building

To build the EC for a single project, run `make BOARD=<board> -j` in the chroot:

For example, to build the EC for `elm`, run:

```bash
cros_sdk --working-dir . -- make BOARD=elm -j
```

The output binary will then be located at `build/elm/ec.bin`.

Additional output files:

*   `build/<board>/RW/ec.RW.elf` - read-write ELF for debugging
*   `build/<board>/RO/ec.RO.elf` - read-only ELF for debugging

To generate disassembly files, run `cros_sdk --working-dir . -- make BOARD=<board> dis`. The output files will be:
*   `build/<board>/RO/ec.RO.dis` - read-only disassembly
*   `build/<board>/RW/ec.RW.dis` - read-write disassembly

# Testing

Legacy EC unit tests run on the host machine using a mock `host` board.

## Running Tests

### Run all host tests
```bash
cros_sdk --working-dir . -- make runhosttests -j
```

### Run a specific host test
```bash
cros_sdk --working-dir . -- make run-<test_name>
```

For example:
```bash
cros_sdk --working-dir . -- make run-host_command
```

### Build a test for a specific board
```bash
cros_sdk --working-dir . -- make test-<test_name> BOARD=<board_name>
```

# Development Workflow

* **VCS**: Use `repo` outside chroot. `repo start <branch>` and
  `repo upload . --cbr`. Gerrit is used for code review.
* **Coding Style**:
  [Linux Kernel style guide](https://www.kernel.org/doc/html/latest/process/coding-style.html).
  Use `cros format <file>` to format code.
* **Commit Messages**: Must include `BUG=b:<id>` and `TEST=<description>`. Keep
  each line under 80 characters. `Change-Id` must be preserved. See previous
  commits for examples.
* **Small Changes**: Prefer smaller logical changes that solve a single problem.
  When changing common code shared between boards along with board specific
  code, split the shared code change into its own change list (CL). The board
  specific CL can depend on the shared code CL.
* **Validate Changes**: Always validate changes by running unit tests and
  building for all relevant boards. If possible run validation tests on an
  actual device and tast integration tests.
* **Code Coverage**: All changes should aim for high unit test coverage.
* **Firmware Branches**: Some programs use program specific firmware release
  branches. Changes are first applied to main and then cherry-picked to the
  program specific firmware branches.

# Meta Guidelines

*   **No Sensitive Data**: Check for sensitive or private data before
    submitting.
*   **Public Only**: This repository is public. Internal-only context, skills,
    or commands MUST be added to the internal `cros-ec-development` extension
    instead.
