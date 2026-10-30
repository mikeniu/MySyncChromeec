# Full-Board Zephyr EC Simulation (`native_sim`)

[TOC]

## Overview

Zephyr EC projects can be built for the POSIX host simulator (`native_sim`) to
produce a standalone Linux executable (`zephyr.exe`) that boots the full EC
application, runs all EC tasks (`HOOKS`, `CHARGER`, `PD_C*`, `UCSI`, `KEYSCAN`,
`CONSOLE`, `HOSTCMD`, `AP_PWRSEQ`), and exposes an interactive EC shell on
`stdin`/`stdout`.

The same board `native_sim.overlay` and Kconfig files can also be referenced
directly from `zephyr/test/` `testcase.yaml` scenarios so integration tests run
against the real board devicetree rather than maintaining duplicate synthetic
test devicetrees.

### Benefits

* **Single Source of Truth (No Test Devicetree Drift):** Integration tests
  (`ap_power`, `pdc`, board tests) include the production `board.dtsi` and
  `project.conf` directly instead of duplicating synthetic board `.dtsi` and
  `.conf` files under `zephyr/test/` that drift out of sync as schematics and
  GPIOs evolve.
* **Full-System Integration Coverage:** Running all EC threads, hooks, and
  drivers together on the real board topology catches cross-subsystem bugs that
  isolated unit tests miss (such as device `INIT_PRIORITY` ordering between I2C
  and charger drivers, GPIO flag mismatches, or interactions between `CHARGER`,
  `PD_C*`, and `AP_PWRSEQ`).
* **Multi-SoC Variant Verification:** A single `twister` test suite can exercise
  both ITE (`it8xxx2`) and Nuvoton (`npcx`) board variants side-by-side on the
  host, validating chip-specific GPIO tables, I2C port mappings, and Kconfig
  selections without physical hardware.
* **Hardware-Free Interactive Debugging:** Developers can launch `zephyr.exe`
  directly on their workstation, run EC console commands on `stdin`/`stdout`,
  and attach host debuggers (`gdb`/`lldb`) or sanitizers without needing a DUT
  or Servo.
* **Zero Production Binary Impact:** Extracting `board.dtsi` and guarding SoC
  pinmux overrides with `#ifndef BOARD_NATIVE_SIM` keeps the hardware `ec.bin`
  image bit-for-bit identical (`zmake compare-builds`).

## Layered Devicetree Architecture

To share a single devicetree source of truth between hardware firmware builds
(`it8xxx2`, `npcx9`) and host simulation builds (`native_sim`), the devicetree
is structured into four layers:

1. **Chip Emulation Skeleton (`zephyr/dts/board-overlays/`)**
   * `common_native_sim.dtsi`: Defines chip-independent `native_sim` nodes and
     preprocessor macros:
     * `#define BOARD_NATIVE_SIM` so board `generated.dtsi` files can exclude
       hardware-only pinmux nodes.
     * Masks `GPIO_OPEN_DRAIN` and `GPIO_OPEN_SOURCE` to `0` because upstream
       `zephyr,gpio-emul` rejects open-drain flags during
       `gpio_pin_configure()`.
     * Deletes conflicting upstream `native_sim.dts` `/adc` and `/i2c@100`
       nodes.
     * Configures `/chosen` (`cros-ec,espi`, `cros-ec,watchdog`,
       `cros-ec,flash`, `cros-ec,flash-controller`, `cros,rtc`), `pwm0`..`pwm7`
       (`cros,pwm-mock`), `tach0`..`tach1` (`cros,tach-mock`), an 8x18
       `gpio-kbd-matrix` (`kbd`), and `ioex_it8801_port0`..`port2`
       (`zephyr,gpio-emul`).
     * Attaches `zephyr,espi-emul-espi-host` under `&espi0` and configures a
       1 MiB `&flash0` and 4 KiB `&cbi_flash`.
   * `it8xxx2_native_sim.dts`: Includes `<it8xxx2_emul.dts>` (`gpioa`..`gpiom`,
     `i2c0`..`i2c5`, `adc0`, `uart2`) and `<board-overlays/common_native_sim.dtsi>`.
   * `npcx_native_sim.dts`: Includes `<npcx_emul.dts>` (`gpio0`..`gpiof`,
     `i2c_ctrl0`..`i2c_ctrl7`), adds `gpio2`, a 12-channel `adc0`, emulated
     keyboard GPIO controllers, aliases `i2c0` and `i2c0_0`..`i2c7_0` to
     `i2c_ctrl0`..`i2c_ctrl7`, and includes
     `<board-overlays/common_native_sim.dtsi>`.

2. **Shared Board Topology (`zephyr/program/<program>/<project>/board.dtsi`)**
   * `generated.dtsi`: Wraps the bottom hardware-only pinmux node overrides
     (`&adc0`, `&i2c*`, `&pwm*`, `&tach*`, `&kbd`) in
     `#ifndef BOARD_NATIVE_SIM ... #endif`, leaving `/named-adc-channels`,
     `/named-gpios`, `/named-i2c-ports`, and `/unused-pins` unmodified.
   * `board.dtsi`: Extracted from `project.overlay` to hold all portable board
     declarations (`generated.dtsi`, `power_signals.dtsi`, I2C/GPIO/tach
     aliases, shared program `.dtsi` includes, `/chosen`, `cse-early-recovery`,
     `ap-power-events`, PDC IRQ pins, `&fan0`, `&kblight`, etc.).
   * `project.overlay`: Includes `"board.dtsi"` and retains only hardware-only
     nodes (`binman.dtsi`, `ite_hibernate.dtsi` / `psl.dtsi`,
     `<ite/it8801-common-cfg.dtsi>`, `&sha0`, `&espi0` pinctrl). Splitting
     `board.dtsi` from `project.overlay` must keep the hardware firmware binary
     (`ec.bin`) bit-for-bit identical (`zmake compare-builds <project>`).

3. **Program & Project `native_sim` Overlays**
   * Program-level shared emulator overlay (for example,
     `zephyr/program/ocelot/rvp_native_sim.dtsi`):
     * Removes hardware-only `row-size` / `col-size` properties from `&kbd`.
     * Attaches off-chip I2C emulators that are not auto-instantiated by
       `CONFIG_EMUL=y` (`zephyr,smbus-ara-emul` and
       `zephyr,smart-battery-emul`).
     * Overrides I2C GPIO expanders (`&pca95xx_0`, `&pca95xx_1`) with
       `compatible = "zephyr,gpio-emul"`.
     * Deletes un-emulated nodes (`/delete-node/ &it8801_mfd;`,
       `/delete-node/ &seven_seg_display;`).
     * Defines common USB-C / UCSI node aliases expected by shared test suites
       (`usbc0: &usbc_port0 {};`, `usbc1: &usbc_port1 {};`,
       `ppm_driver_nodelabel: &{/usbc/ppm-driver} {};`).
   * Project-level `<project>/native_sim.overlay`:
     * Includes `<board-overlays/it8xxx2_native_sim.dts>` or
       `<board-overlays/npcx_native_sim.dts>`.
     * Includes `"board.dtsi"` and the program emulator `.dtsi`.
     * Adds `GPIO_PULL_UP` to the power button and lid open GPIO pins so
       `zephyr,gpio-emul` initializes the power button as unpressed and the lid
       as open at boot.

```dts
/* Example: zephyr/program/ocelot/ocelotrvp-npcx/native_sim.overlay */
#include <board-overlays/npcx_native_sim.dts>

#include "board.dtsi"
#include "../rvp_native_sim.dtsi"

&gpio_mech_pwr_btn_odl {
	gpios = <&gpio0 0 (GPIO_INPUT | GPIO_PULL_UP)>;
};

&gpio_lid_open {
	gpios = <&gpio0 1 (GPIO_INPUT | GPIO_PULL_UP)>;
};
```

4. **Test-Specific Overlays (`zephyr/test/`)**
   * Test suites in `zephyr/test/` include `<project>/native_sim.overlay` via
     `extra_dtc_overlay_files` and `<project>/project.conf` via
     `extra_conf_files` in `testcase.yaml`, optionally layering test stimuli on
     top (such as `zephyr/test/ap_power/boards/mtl_emul.dtsi` for AP power
     sequencing waveforms, or `zephyr/test/pdc/boards/rvp_rts54xx.overlay` and
     `rvp_tps6699x.overlay` to select the active PDC emulator pair via
     `pdc_emul1` / `pdc_emul2`).
   * Because YAML 1.2 anchors (`&anchor` / `<<: *anchor`) are scoped to a single
     `testcase.yaml` file:
     * Use devicetree-driven `configdefault` rules in test `Kconfig` files
       (`default y if DT_HAS_<COMPAT>_ENABLED`) to auto-enable board hardware
       Kconfig dependencies (such as charger driver, `FLASH`, `ESPI`, and
       `I2C_INIT_PRIORITY=39`) directly from `<project>/native_sim.overlay`.
     * Use intra-file YAML anchors (`&<suite>_base`, `<<: *<suite>_base`) in
       `testcase.yaml` so board scenarios inherit the test suite's
       `extra_configs` and only override `extra_dtc_overlay_files`.

## Devicetree Nodes Excluded in the `native_sim` Variant

Nodes and properties that depend on physical SoC hardware or un-emulated
peripherals are excluded from `native_sim` across four places:

1. **Conflicting Upstream `native_sim.dts` Nodes (`common_native_sim.dtsi`)**
   * `/adc` and `/i2c@100` (`&i2c0`): Deleted via `/delete-node/` because
     upstream Zephyr's `boards/native/native_sim/native_sim.dts` defines dummy
     `/adc` and `/i2c@100` nodes that collide with the EC SoC's `adc0` and
     `i2c0` node names and register addresses.

2. **SoC Pinmux and Controller Overrides (`generated.dtsi` under `#ifndef BOARD_NATIVE_SIM`)**
   * `&adc0`, `&i2c*` / `&i2c_ctrl*`, `&pwm*`, `&tach*`, and `&kbd` at the
     bottom of `generated.dtsi`: Excluded because they attach SoC-specific
     `pinctrl-0` phandles, I2C recovery `scl-gpios`/`sda-gpios`, PWM
     `prescaler-cx`, tachometer `sample-clk`/`pin-select`, and hardware KBD
     `kso-mapping`/`mfdctrl` properties that do not exist in the `zephyr,*-emul`
     or `cros,*-mock` devicetree bindings.

3. **Hardware-Only Packaging and SoC Blocks Kept in `project.overlay` (Excluded from `board.dtsi`)**
   * `<cros/integrated_fwid.dtsi>`, `"binman.dtsi"`, `<cros/cbi_flash.dtsi>`,
     and `&binman`: Define physical SPI flash RO/RW layout for `ec.bin`
     packaging; `native_sim` builds a host executable (`zephyr.exe`) and uses
     the simulated `&flash0` and `&cbi_flash` partitions in
     `common_native_sim.dtsi`.
   * `"../ite_hibernate.dtsi"` (ITE) and `"psl.dtsi"` (NPCX Power Switch Logic):
     Configure SoC-specific deep-sleep wake pins and PSL pads.
   * `<ite/it8801-common-cfg.dtsi>` and `&it8801_mfd { irq-gpios = ...; }`:
     Configures the IT8801 I2C MFD sub-devices; on `native_sim`, its GPIO
     controllers (`ioex_it8801_port0`..`port2`) are provided directly by
     `zephyr,gpio-emul` in `common_native_sim.dtsi`.
   * `&sha0 { status = "okay"; }`: Enables the SoC hardware SHA-256 accelerator.
   * `&espi0 { pinctrl-0 = <...>; }`: Attaches NPCX hardware eSPI/LPC pinmux
     phandles.

4. **Un-emulated I2C Peripherals (`rvp_native_sim.dtsi` and Test Overlays)**
   * In Zephyr's `drivers/i2c/i2c_emul.c`, every `status = "okay"` child of a
     `zephyr,i2c-emul-controller` bus emits `.dev = DEVICE_DT_GET(node_id)` into
     `emuls_<n>[]`. Any child node whose driver is not compiled (no
     `DEVICE_DT_DEFINE`) causes a link error
     (`undefined symbol: __device_dts_ord_<N>`), and any child node without a
     matching `EMUL_DT_DEFINE` fails I2C transfers at runtime:
     * `/delete-node/ &it8801_mfd;` (`ite,it8801-mfd` at `@0x39`) and
       `/delete-node/ &seven_seg_display;` (`maxim,max695x` at `@0x38`): Deleted
       in `rvp_native_sim.dtsi` because neither device has a compiled driver or
       I2C emulator on `native_sim`.
     * `&pca95xx_0` and `&pca95xx_1` (`nxp,pca95xx` at `@0x22`, `@0x23`):
       Overridden in `rvp_native_sim.dtsi` with `compatible = "zephyr,gpio-emul"`
       (and `/delete-property/ zephyr,deferred-init;`) so they act as host GPIO
       emulators rather than un-emulated I2C expanders.
     * `/delete-node/ &dsp_service;` (`cros,dsp-service` at `@0x29`): Kept in
       full-board `ocelotrvp-*-sim` and `zephyr/test/intelrvp` builds (where
       `CONFIG_PLATFORM_EC_DSP_SERVICE=y` and `EMUL_STUB_DEVICE(cros_dsp_service)`
       are compiled), but deleted in `zephyr/test/pdc/boards/rvp_*.overlay`
       where `CONFIG_PLATFORM_EC_DSP_SERVICE` is disabled.

## Kconfig, Shim Stubs, and `zmake` Integration

* `zephyr/program/native_sim.conf` provides the base `native_sim` Kconfig
  settings (`CONFIG_EMUL=y`, `CONFIG_FLASH=y`, `CONFIG_I2C_INIT_PRIORITY=39`,
  `CONFIG_LOG_MODE_MINIMAL=y`, `CONFIG_POSIX_ARCH_CONSOLE_INIT_PRIORITY=60`,
  `CONFIG_UART_NATIVE_PTY_0_ON_STDINOUT=y`, and eSPI peripheral capabilities).
* Avoid hardcoding SoC-specific Kconfig options in `<project>/project.conf` when
  they are already defaulted from the devicetree (for example,
  `CONFIG_PLATFORM_EC_HIBERNATE_PSL` in `zephyr/Kconfig.system` already defaults
  to `y` when `DT_HAS_NUVOTON_NPCX_POWER_PSL_ENABLED && SOC_FAMILY_NPCX`, so
  omitting `CONFIG_PLATFORM_EC_HIBERNATE_PSL=y` from `project.conf` keeps the
  hardware build identical while allowing `native_sim` to include
  `project.conf` without Kconfig warnings).
* Program-specific overrides live in `zephyr/program/<program>/native_sim.conf`
  (or `<project>/native_sim.conf`).
* In `BUILD.py`, `maybe_register_sim_project()` automatically registers a
  `<project>-sim` host target whenever `<project>/native_sim.overlay` is
  present.
* When adding `__weak` or `test_mockable` stubs in `zephyr/mock/` or
  `zephyr/shim/src/ztest_system.c` for subsystems that use `static inline`
  no-ops when disabled (such as `usb_mux_set()` in `include/usb_mux.h` or
  `pd_task_intel_altmode.h`), always match the header's exact
  `#if defined(CONFIG_...) || defined(CONFIG_ZTEST)` guard so non-`ZTEST`
  `zmake` builds do not hit redefinition errors.

## Adding `native_sim` to Another Board

Follow these steps to enable `<project>-sim` and `twister` integration tests for
another board (`zephyr/program/<program>/<project>/`):

1. **Guard SoC Pinmux Overrides in `generated.dtsi`**
   * Open `<project>/generated.dtsi` (or the board's pinmux/controller `.dtsi`
     files) and wrap the bottom hardware-only controller overrides (`&adc0`,
     `&i2c*` / `&i2c_ctrl*`, `&pwm*`, `&tach*`, `&kbd`) in
     `#ifndef BOARD_NATIVE_SIM ... #endif`.
   * Keep `/named-adc-channels`, `/named-gpios`, `/named-i2c-ports`, and
     `/unused-pins` outside the guard so `native_sim` uses the exact same GPIO,
     ADC, and I2C tables as the hardware build.

2. **Extract `board.dtsi` from `project.overlay`**
   * Move portable board declarations (`generated.dtsi`, `power_signals.dtsi`,
     I2C/GPIO/tach aliases, shared program `.dtsi` includes, `/chosen`, USB-C,
     charger, fan, keyboard, LED, and sensor nodes) into `<project>/board.dtsi`
     and `#include "board.dtsi"` from `<project>/project.overlay`.
   * Leave hardware-only packaging and SoC nodes in `<project>/project.overlay`
     (`binman.dtsi`, `<cros/cbi_flash.dtsi>`, hibernate/PSL `.dtsi`,
     `<ite/it8801-common-cfg.dtsi>`, `&sha0`, and `&espi0` pinctrl).
   * Run `zmake compare-builds <project>` to verify the hardware `ec.bin` is
     bit-for-bit unchanged.

3. **Create `<project>/native_sim.overlay` (and Shared Program `.dtsi`)**
   * Include `<board-overlays/it8xxx2_native_sim.dts>` or
     `<board-overlays/npcx_native_sim.dts>` (for a new EC SoC family, create
     `zephyr/dts/board-overlays/<soc>_native_sim.dts` wrapping `<soc>_emul.dts`
     and `<board-overlays/common_native_sim.dtsi>`).
   * Include `"board.dtsi"`.
   * Configure off-chip I2C emulators and delete un-emulated nodes (either in a
     shared `<program>/*_native_sim.dtsi` or directly in `native_sim.overlay`):
     * Delete `row-size` and `col-size` from `&kbd` if set by the board.
     * Add `zephyr,smart-battery-emul` (`@0xb`) on the battery/charger I2C bus
       and `zephyr,smbus-ara-emul` (`@0xc`) if using an ISL9241/RAA489110
       charger.
     * Override I2C GPIO expanders (such as `nxp,pca95xx`) with
       `compatible = "zephyr,gpio-emul"` (and
       `/delete-property/ zephyr,deferred-init;`).
     * Delete any remaining I2C child nodes that lack a `native_sim` driver or
       emulator (`/delete-node/ &...;`).
   * Add `GPIO_PULL_UP` to the active-low power button pin and active-high lid
     open pin so `zephyr,gpio-emul` boots with the power button unpressed and
     the lid open.

4. **Register `<project>-sim` in `BUILD.py` and Check Kconfig**
   * Add `maybe_register_sim_project()` to `zephyr/program/<program>/BUILD.py`
     (see `zephyr/program/ocelot/BUILD.py`) so `register_host_project()`
     registers `<project>-sim` with `zephyr/program/native_sim.conf` whenever
     `<project>/native_sim.overlay` exists.
   * Remove redundant SoC-specific Kconfig assignments from
     `<project>/project.conf` that are already defaulted by devicetree on
     hardware (such as `CONFIG_PLATFORM_EC_HIBERNATE_PSL=y`).
   * Add program- or project-specific simulator Kconfig overrides in
     `<program>/native_sim.conf` or `<project>/native_sim.conf` if needed (for
     example, disabling un-emulated factory-update drivers).

5. **Wire `<project>/native_sim.overlay` into `zephyr/test/` Suites**
   * In `zephyr/test/<suite>/testcase.yaml`, add a scenario referencing
     `<project>/native_sim.overlay` in `extra_dtc_overlay_files` (plus
     `<program>/program.conf`, `<project>/project.conf`, and `native_sim.conf`
     in `extra_conf_files` for full-board test suites).
   * Use `configdefault` in the test suite's `Kconfig` to auto-enable board
     hardware drivers from `DT_HAS_<COMPAT>_ENABLED` and YAML anchors
     (`&<suite>_base` / `<<: *<suite>_base`) in `testcase.yaml` to inherit the
     suite's `extra_configs`.

## Building and Running

Build both the hardware image and the `native_sim` image with `zmake`:

```bash
(chroot) $ zmake build ocelotrvp-ite ocelotrvp-ite-sim ocelotrvp-npcx ocelotrvp-npcx-sim
```

Verify that splitting `board.dtsi` and `generated.dtsi` did not alter the
production firmware binary (`ec.bin`):

```bash
(chroot) $ zmake compare-builds ocelotrvp-ite ocelotrvp-npcx
```

Run the compiled simulator binary directly on the host (inside or outside the
chroot) to interact with the EC console:

```bash
$ ./build/zephyr/ocelotrvp-npcx-sim/build-singleimage/zephyr/zephyr.exe
```

Run `twister` test suites wired to the board's `native_sim.overlay`:

```bash
(chroot) $ ./twister -T zephyr/test/ap_power -s ap_power.ocelotrvp_ite -s ap_power.ocelotrvp_npcx
(chroot) $ ./twister -T zephyr/test/intelrvp
(chroot) $ ./twister -T zephyr/test/pdc -s pdc.generic.ocelotrvp_ite.rts54xx -s pdc.generic.ocelotrvp_ite.tps6699x -s pdc.generic.ocelotrvp_npcx.rts54xx -s pdc.generic.ocelotrvp_npcx.tps6699x
```
