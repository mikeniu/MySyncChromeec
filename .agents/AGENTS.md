This document contains agent context for EC development.

# Overview

This repository contains the Zephyr EC (embedded controller) firmware for Chromium OS devices. The EC handles low-level tasks such as power sequencing, battery charging, keyboard control, thermal management, power delivery, etc.

There are two distinct EC implementations:
1.  **Legacy EC**: aka CrOS EC. A custom, lightweight OS. Legacy EC development has moved to the [ec-legacy branch](../ec-legacy).
2.  **Zephyr EC**: (this repo) Based on Zephyr RTOS. Prefer Zephyr APIs/drivers. Source: `../../third_party/zephyrproject/zephyr`. See @docs/zephyr/README.md.

*Note: The Legacy and Zephyr EC codebases have forked. Code in this repository is focused on Zephyr EC; legacy-only code is pending removal, and common directories are no longer shared with the `ec-legacy` branch.*

## Core Concepts

*   **RO/RW Firmware & Software Sync**: EC flash is divided into Read-Only (RO) and Read-Write (RW) sections. RO is permanently locked before devices ship. RW can be updated in the field. **Software Sync** is the mechanism where the AP re-flashes the EC RW region at boot if it doesn't match the copy in the AP firmware. **sysjump** is the mechanism used to transition between these firmware images (e.g., jumping from RO to RW).
*   **Threads (Tasks)**: The Zephyr EC uses Zephyr threads. The legacy term "task" is still used in some older common code, but they map directly to Zephyr threads. There is no heap (`malloc` is forbidden).
*   **Hooks**: Functions registered to run on specific events (e.g., `HOOK_AC_CHANGE`, `HOOK_CHIPSET_RESUME`) or periodically (`HOOK_TICK`, `HOOK_SECOND`).
*   **Host Commands**: The protocol the AP (Application Processor / ChromeOS) uses to send synchronous requests to the EC (e.g., "get battery status"). Data structures and command IDs are defined in @include/ec_commands.h. See @docs/ap-ec-comm.md.
*   **Host Events**: The mechanism the EC uses to asynchronously notify or wake the AP when an event occurs (e.g., "lid closed", "power button pressed", "AC connected").

## Tooling & Debugging

*   **`cros_sdk` (Chroot)**: The ChromiumOS build environment. Build and test commands must be run inside this chroot. Use `cros_sdk --working-dir . <cmd>` from the host to run commands inside.
*   **Servo**: A physical hardware debug board connected to the device. It provides access to the EC's serial console (UART), hardware reset controls, and allows flashing the EC even if it's completely bricked.
*   **`zmake`**: The build tool for Zephyr EC projects (run inside `cros_sdk`).
*   **`ectool`**: Host utility to send commands to the EC from userspace (run inside `cros_sdk` or on the DUT).
*   **`util/flash_ec`**: Utility script to flash EC firmware via Servo.
*   **`dut-control`**: Command-line tool to interact with the Servo. It allows you to control the DUT (e.g., `dut-control cold_reset:on`), read GPIOs, and find the EC's UART path (`dut-control ec_uart_pty`). Use `dut-control -i` to list all available commands.
*   **`futility`**: Used to manage GBB flags (e.g., to disable Software Sync for development) (run inside `cros_sdk` or on the DUT).
*   **EC Console**: Interactive shell available via UART (Servo). Use `help` for commands.

## Directory Structure

*   **`build/`**: Build artifacts are generated here. **Always exclude this directory when searching or grepping.**
*   **`common/`**: Core functionality, formerly shared with legacy EC.
*   **`docs/`**: Technical documentation. See @docs/ and @docs/zephyr/.
*   **`driver/`**: Low-level drivers for peripherals (accelerometers, chargers, TCPCs).
*   **`include/`**: Header files for all EC components.
*   **`util/`**: Host utilities and scripts for interacting with and flashing the EC.
*   **`zephyr/`**: Root directory for the Zephyr EC application.
    *   **`zephyr/app/`**: The application entry point and core initialization logic.
    *   **`zephyr/program/`**: Project-specific code and configuration (replaces `board/` from legacy). Contains devicetree (DTS) and Kconfig files for each board.
    *   **`zephyr/shim/`**: The compatibility layer that maps Legacy EC APIs (like hooks and host commands) to Zephyr-native primitives.
    *   **`zephyr/test/`**: Unit and integration tests (ztest) for the Zephyr implementation.
    *   **`zephyr/zmake/`**: Source code for the `zmake` build tool.

## Development Workflow

* **VCS**: Use `repo` outside chroot. `repo upload . --cbr`.
* **Style**: Linux Kernel style (Tabs, 80 cols). See @README.md. Use `cros format <file>`.
* **Commit Messages**: Must include `BUG=b:<id>` and `TEST=<description>`. `Change-Id` must be preserved.
* **Building**: `cros_sdk --working-dir . zmake build <project>`.
* **Testing**: `cros_sdk --working-dir . ./twister -T zephyr/test/drivers/`. See @docs/zephyr/ztest.md.

## Meta Guidelines

When updating this context or adding new skills/commands to the `.agents/` folder:
*   **No Sensitive Data**: Check for sensitive or private data before submitting.
*   **Public Only**: This repository is public. Internal-only context, skills, or commands MUST be added to the internal `cros-ec-development` extension instead.
