# Best Practices for Adding Zephyr EC Projects

[TOC]

## Overview

This guide documents design principles, architectural best practices, and
common review pitfalls when bringing up or extending **programs** and
**project variants** in `platform/ec/zephyr/program/`.

These guidelines are synthesized from recurring review feedback across
ChromeOS Zephyr EC programs. Following them ensures that board code remains
maintainable, avoids cross-project copy-paste debt, aligns with modern Zephyr
driver and devicetree architecture, and passes code review quickly.

---

## 1. Eliminate Board-Specific C Code

### The Problem: Copy-Paste Technical Debt
When adding support for a new board variant, it is common to duplicate C source
files from sibling variants for feature areas such as LED control, fan control,
power sequencing, thermal management, charger selection, and display port
alternate mode callbacks. Over time, these duplicated files proliferate across
the tree with slight drifts in implementation.
* Once a board-specific C file is copied, bug fixes and enhancements applied to
  the reference board are rarely propagated to the copies, leading to silent
  regressions and high maintenance overhead (e.g. b/578128557).
* In many cases, these files contain identical logic and differ only in
  configuration parameters that can be expressed cleanly in devicetree.

### Best Practices:
1. **Prefer Devicetree and Kconfig Over C Code:**
   * Before adding board-specific C code, check whether the functionality can
     be configured through existing devicetree bindings and Kconfig options.
   * Common areas such as keyboard matrices, LEDs, thermal trip points, fan
     tables, and power sequence signals have well-defined devicetree
     interfaces.
2. **Implement Hardware Variations as Generic Zephyr Drivers:**
   * When hardware behaviors differ, implement the logic as a generic Zephyr
     driver under `zephyr/drivers/` rather than adding `.c` files under
     `zephyr/program/<program>/src/`.
   * Define a standard `DRIVER_API` structure, devicetree bindings under
     `zephyr/dts/bindings/`, and unit tests under `zephyr/test/`.
   * When multiple hardware implementations exist for the same logical function
     (such as monitoring a power rail via GPIO vs. ADC), both drivers should
     expose the same `DRIVER_API`. This allows higher-level subsystems to
     interact with the device without board-specific `#ifdef`s or branching.
3. **Check for Upstream Zephyr Subsystems and Drivers First:**
   * Before writing new peripheral or driver implementations, check if upstream
     Zephyr already provides a standard API or subsystem. Reusing existing
     Zephyr driver models avoids ad-hoc polling loops and simplifies future
     Zephyr version upgrades.
4. **Isolate Drivers from Board and Program Directories:**
   * Keep driver implementations in `zephyr/drivers/` and generic shim logic in
     `zephyr/shim/src/`.
   * Board directories under `zephyr/program/` should strictly contain
     board-specific configuration (devicetree and Kconfig) rather than general
     driver implementations.
5. **Manage Driver Dependencies via Kconfig, Not Board Makefiles:**
   * Do not directly add common driver source files to a board's
     `CMakeLists.txt` to bypass configuration dependencies. Ensure Kconfig
     symbols (such as `DT_HAS_<COMPAT>_ENABLED`) properly guard and select the
     required driver.

---

## 2. GPIO Architecture & Deprecating `named-gpios`

### The Problem: Legacy `named-gpios` vs. Driver GPIO Ownership
`named-gpios` is a legacy shim mechanism designed to export Zephyr devicetree
GPIOs to legacy EC code via the `gpio_get_level()` and `gpio_set_level()` APIs.
In pure Zephyr drivers, each driver owns its own GPIO properties. Keeping
signals in both places leads to duplicate initialization, conflicting pin
states, and difficult-to-trace power sequencing bugs.

### Best Practices:
1. **Drivers Must Own Their GPIOs:**
   * If a Zephyr driver controls or monitors a signal (e.g., PDC interrupt and
     reset, TCPC alert, charger PROCHOT, power sequencing enables, switchcap
     PG/EN, IT8801 MFD IRQ, or write-protect), the GPIO property belongs to
     that driver's devicetree node.
   * Delete any GPIOs owned by a Zephyr driver from `named-gpios`.
   * Power sequencing signals (e.g., `ap_pwrseq` GPIOs, `power_signals.dtsi`)
     must not be included in `named-gpios`. The AP power sequence driver handles
     setting up and managing these signals directly.
2. **The `no-auto-init` Exception:**
   * In rare cases where legacy code (such as temperature sensors reading
     power-good signals) still needs visibility into a driver-owned or
     power-sequencing GPIO via `named-gpios`, the entry in `named-gpios` must
     specify the `no-auto-init` property:
     ```dts
     named-gpios {
         compatible = "named-gpios";
         pg_pp1p05_s5: pg_pp1p05_s5 {
             gpios = <&gpio_12_7 0>;
             no-auto-init;
         };
     };
     ```
     This prevents the GPIO shim driver from re-initializing the pin or
     overriding flags configured by the primary driver.
3. **Use Explicit GPIO Flags (Avoid Compound Macros):**
   * Avoid legacy compound GPIO flags. Specify flags explicitly using
     bitwise OR:
     ```dts
     /* Preferred */
     gpios = <&gpio0 5 (GPIO_OPEN_DRAIN | GPIO_OUTPUT_LOW)>;
     ```
   * Include `GPIO_OUTPUT_INIT_LOGICAL` when specifying initial output states to
     ensure active-low / active-high polarity is honored.
4. **Unused Pin Configuration:**
   * The standard setting for unrouted or unused GPIO pins is input with an
     internal pull-up (unless the chip vendor hardware design guide specifies
     otherwise).
   * Do not change unused pins to pull-downs without a documented hardware
     justification.
5. **Do Not Document Non-GPIO Pins in `gpio.dtsi`:**
   * Keep `gpio.dtsi` focused on actual GPIO pin mappings. Do not list dedicated
     analog inputs, crystal pins, or debug UARTs as stub GPIOs in comments or
     tables, as they become stale whenever hardware signals change.

---

## 3. Devicetree Hierarchy & Organization

### The Problem: Leaking Project Overrides into Program Trees
Putting project-specific GPIO overrides, pin muxes, or device configs into
top-level program files (such as `<program>.dtsi` or `gpio.dtsi`) breaks
abstraction and creates merge conflicts when new variant projects are added.

### Best Practices:
1. **Maintain Strict Devicetree Hierarchy:**
   * **Program level (`zephyr/program/<program>/`)**: Contains shared chip
     architecture, AP power sequencing definitions, base SoC buses, and common
     devicetree includes (`<program>.dtsi`).
   * **Project variant level (`zephyr/program/<program>/<project>/`)**:
     Contains hardware differences unique to that model (chassis GPIO routing,
     specific panel/backlight configs, battery nodes, second USB-C port options,
     and SKU-dependent peripherals).
2. **Organize Peripheral Nodes by Function:**
   * Do not place I2C devices, fan configs, or nodelabel references directly
     into `project.overlay`.
   * Place I2C device nodes in `i2c.dtsi`, fans and thermal trip points in
     `fan.dtsi`, and keyboard customizations in `keyboard.dtsi`.
   * Use direct nodelabel references to override device properties rather than
     re-nesting nodes under bus hierarchies:
     ```dts
     /* Preferred: direct nodelabel override */
     &pca95xx_0 {
         zephyr,deferred-init;
     };
     ```
3. **Handling `generated.dtsi`:**
   * When creating a new project, use Arbitrage to auto-generate the initial
     `generated.dtsi` template.
   * Apply hand-edits directly to `<project>/generated.dtsi`. Once generated,
     treat `generated.dtsi` as a standard source file in git. Add a comment at
     the top:
     ```dts
     /* This file was auto-generated but contains manual edits */
     ```
     *(Reference: `zephyr/program/ocelot/ocelotrvp-npcx/generated.dtsi`)*
   * Do not create auxiliary `.dtsi` override files at the program root to fix
     up errors in a project's `generated.dtsi`. Modify the project file
     directly.
4. **Override Properties Instead of Deleting Nodes:**
   * If a property in an included file needs adjustment for a board variant,
     override the property directly rather than deleting (`/delete-node/`) and
     recreating the node.

---

## 4. Kconfig Cleanliness & Redundancy

### Best Practices:
1. **Avoid Redundant Kconfigs (Devicetree Auto-Selection):**
   * Do not explicitly enable Kconfig symbols in `project.conf` or
     `program.conf` that are already automatically selected by devicetree
     dependencies (e.g., driver `DT_HAS_<COMPAT>_ENABLED` symbols like
     `CONFIG_CROS_EC_KEYBOARD` or `CONFIG_KEYBOARD_MICROCHIP_XEC`).
2. **Avoid 1:1 Board Kconfigs:**
   * Do not create new feature Kconfig flags that map 1:1 with a single board
     variant. Use the board Kconfig symbol directly until a second board
     actually requires the shared feature. If added later, place the symbol in
     the common subsystem Kconfig (e.g., `Kconfig.keyboard`), not a
     board-specific Kconfig.

---

## 5. Peripherals & Batteries

### Best Practices:
1. **Commit Messages Must Link Datasheets:**
   * When adding a new battery definition, fuel gauge profile, or specialized
     sensor, the commit message should reference a Buganizer ticket that
     includes the vendor component datasheet.
2. **Validate Variant USB-C Port Counts:**
   * Never assume all variants in a program have the same number of USB-C ports.
     Query the board SKU or CBI port count (`board_get_usb_pd_port_count()`)
     rather than indexing up to `CONFIG_USB_PD_PORT_MAX_COUNT` to prevent
     watchdog timeouts when communicating with unpopulated I2C TCPCs/chargers.

---

## 6. CL Hygiene & Review Workflow

### Best Practices:
1. **Split Refactoring, Drivers, and Board Enablement into Separate CLs:**
   * Move generic driver framework changes and board-specific configuration
     into distinct CLs.
   * Avoid mixing USB-C/charger changes, AP power sequencing, and GPIO pinmux
     setup in a single large CL. Focused CLs compile and review faster.
2. **Use Targeted Tracking Bugs:**
   * Avoid pointing bringup CLs to a generic parent tracker. File and reference
     child bugs for specific subsystems (keyboard, power sequencing, PDC).
   * Any `TODO` comment in code must reference a valid Buganizer issue
     (e.g., `/* TODO(b/123456): ... */`).
3. **Run Pre-upload Checks and Format Code Before Uploading:**
   * Always upload changes using `repo upload`. This automatically executes the
     pre-upload hooks configured in `PRESUBMIT.cfg` (including `cros format`
     and `cros lint`), verifying that code style, formatting, and licensing
     conform to ChromiumOS requirements before reaching review.
   * If formatting of a specific table or array in C source needs to be
     preserved, guard it with `/* clang-format off */` and
     `/* clang-format on */`.
   * For details on pre-upload checks and hook configurations, see the
     [ChromiumOS Developer Guide][developer-guide-pre-upload] and the
     [ChromiumOS Pre-upload Hooks Documentation][repohooks-docs].

---

[developer-guide-pre-upload]: https://www.chromium.org/chromium-os/developer-library/guides/development/developer-guide/#upload-changes
[repohooks-docs]: https://chromium.googlesource.com/chromiumos/repohooks/+/HEAD/README.md

## 7. Verification Checklist for New Board CLs

Before sending your board bringup CLs for review:
* [ ] **No duplicated C files**: Check if any `.c` files in your project
      directory can be replaced with devicetree or generic drivers.
* [ ] **`named-gpios` cleaned**: All GPIOs used by PDC, TCPC, charger, power
      sequencing, or sensors are removed from `named-gpios` (or marked
      `no-auto-init` if strictly needed by legacy shim).
* [ ] **Separate Driver CLs**: Any new driver or subsystem API is split into
      its own CL ahead of the board enablement CL.
* [ ] **`zmake compare-builds`**: Run `zmake compare-builds <board>` on
      refactoring CLs to prove zero binary/functional regressions.
* [ ] **Unit Tests**: Ensure unit tests exist under `zephyr/test/` for new
      driver logic.
* [ ] **Kconfig verified**: No redundant `CONFIG_*=y` lines in `project.conf`
      that devicetree already enables.
