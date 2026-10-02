/* Copyright 2016 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * config.h - Top-level configuration Chrome EC
 *
 * All configuration settings (CONFIG_*) are defined in this file or in a
 * sub-configuration file (config_chip.h, board.h, etc.) included by this file.
 *
 * Note that this file is included by assembly (.S) files.  Any C-isms such as
 * struct definitions or enums in a sub-configuration file MUST be guarded with
 * #ifndef __ASSEMBLER__ to prevent those C-isms from being evaluated by the
 * assembler.
 */

#ifndef PLATFORM_EC_INCLUDE_CONFIG_H_
#define PLATFORM_EC_INCLUDE_CONFIG_H_

#ifndef HOST_TOOLS_BUILD

#ifdef INCLUDE_ENV_CONFIG
/*
 * When building for an EC target, pick up the .h file which allows to
 * keep track of changing make variables.
 */
#include "env_config.h"
#endif

/*
 * All config options are listed alphabetically and described here.
 *
 * If you add a new config option somewhere in the code, you must add a
 * default value here and describe what it does.
 *
 * To get a list current list, run this command:
 *    git grep " CONFIG_" | grep -o "CONFIG_[A-Za-z0-9_]\+" | sort | uniq
 *
 * Some options are #defined here to enable them by default.  Chips or boards
 * may override this by #undef'ing them in config_chip.h or board.h,
 * respectively.
 *
 * TODO(crosbug.com/p/23758): Describe all of these.  Also describe the
 * HAS_TASK_* macro and how/when it should be used vs. a config define.  And
 * BOARD_*, CHIP_*, and CHIP_FAMILY_*.
 */

/* When the ec_rate config is set, put the sensor in force mode */

/* Add support for sensor FIFO */

/* Define the size of the global fifo, must be a power of 2. */

/* The amount of free entries that trigger an interrupt to the AP. */

/*
 * Sensors in this mask are in forced mode: they needed to be polled
 * at their data rate frequency.
 */

/* Enable accelerometer interrupts. */

/*
 * Support "spoof" mode for sensors.  This allows sensors to have their values
 * spoofed to any arbitrary value.  This is useful for testing.
 */
#ifndef CONFIG_ACCEL_SPOOF_MODE
#define CONFIG_ACCEL_SPOOF_MODE
#endif

/* Specify type of accelerometers attached. */

/*
 * lis2dh/lis2de/lng2dm have the same register interface but different
 * supported resolution. In normal mode, lis2dh works in 10-bit resolution,
 * but lis2de/lng2dm only support 8bit resolution.
 *
 * Use the define for your correct chip and the CONFIG_ACCEL_LIS2D_COMMON will
 * automatically get defined.
 */

/*
 * lis2dw12 and lis2dwl have almost the same register interface.
 * lis2dw12 supports 4 low power modes but lis2dwl only supports one. lis2dwl
 * only supports 12 bit resolution under low power mode. But lis2dw12 can
 * support 12 bit or 14 bit resolution at different low power modes. In order
 * to get 14 bit resolution, lis2dwl does not use low power mode and lis2dw12
 * only uses 3 of 4 low power modes.
 *
 * Use the define for your correct chip and the CONFIG_ACCEL_LIS2DW_COMMON will
 * automatically get defined.
 */

/* Use CONFIG_ACCELGYRO_LSM6DSM for LSM6DSL, LSM6DSM, and/or LSM6DS3 */

/* Select the communication mode for the accelgyro ICM. Only one of these should
 * be set. To set the value manually, simply define one or the other. If neither
 * is defined, but I2C_PORT_ACCEL is defined, then CONFIG_ACCELGYRO_ICM_I2C will
 * automatically be set.
 */

/* Select the communication mode for the accelgyro BMI. Only one of these should
 * be set. To set the value manually, simply define one or the other. If neither
 * is defined, but I2C_PORT_ACCEL is defined, then CONFIG_ACCELGYRO_BMI_I2C will
 * automatically be set.
 */

/*
 * Some chips have a portion of memory which will remain powered even
 * during a reset.  This is called Always-On, or AON memory, and
 * typically has a separate firmware to manage the memory.  These
 * values can be used to configure the RAM layout for Always-On.
 *
 * See chip/ish/ for an example implementation.
 */

/* Add sensorhub function for LSM6DSM, required if 2nd device attached. */

/* Specify type of Magnetometer attached. */

/* Presence of a Bosh Sensortec BMM150 magnetometer behind a BMIxxx. */

/* Presence of a Bosh Sensortec BMM150 magnetometer behind a LSM6DSM. */

/* Presence of a ST LIS2MDL magnetometer behind a BMIxxx. */

/* Presence of a ST LIS2MDL magnetometer behind a LSM6DSM. */

/* Specify barometer attached */

/* When set, it indicates a secondary sensor is attached behind a BMIxxx. */

/* When set, it indicates a secondary sensor is attached behind a LSM6DSM/L. */

/* Support for BMIxxx hardware orientation sensor */

/* Support for KIONIX KX022 hardware orientation sensor */

/* Define the i2c address of the sensor behind the main sensor, if present. */

/*
 * Define if either CONFIG_BMI_ORIENTATION_SENSOR or
 * CONFIG_KX022_ORIENTATION_SENSOR is set.
 */

/* Support the orientation gesture */

/* Support the body_detection */

/* Which sensor body_detection use */

/* The max number of sampling data for 1 second */

/* The threshold of acceleration variance */

/* How much noise affect threshold of variance */

/* The confidence limit of on_body/off_body */

/* The threshold duration to change to off_body */

/* Send the SCI event to notify host when body status change */

/* Send the MKBP event to notify host when body status change */

/* Always enable the body detection function in S0 */

/*
 * Use the old standard reference frame for accelerometers. The old
 * reference frame is:
 * Z-axis: perpendicular to keyboard, pointing up, such that if the device
 *  is sitting flat on a table, the accel reads +G.
 * X-axis: in the plane of the keyboard, pointing from the front lip to the
 *  hinge, such that if the device is oriented with the front lip touching
 *  the table and the hinge directly above, the accel reads +G.
 * Y-axis: in the plane of the keyboard, pointing to the right, such that
 *  if the device is on it's left side, the accel reads +G.
 *
 * Also, in the old reference frame, the lid accel matches the base accel
 * readings when lid is closed.
 */

/* Set when INT2 is an output */

/* Specify type of Gyrometers attached. */

/*
 * If this is defined, motion_sense sends sensor events to the AP in the format
 * +-----------+
 * | Timestamp |
 * |  Payload  |
 * | Timestamp |
 * |  Payload  |
 * |    ...    |
 * +-----------+
 *
 * If this is not defined, the events will be sent in the format
 * +-----------+
 * |  Payload  |
 * |  Payload  |
 * |  Payload  |
 * |    ...    |
 * | Timestamp |
 * +-----------+
 *
 * The former format enables improved filtering of sensor event timestamps on
 * the AP, but comes with stricter jitter requirements.
 */
#ifndef CONFIG_SENSOR_TIGHT_TIMESTAMPS
#define CONFIG_SENSOR_TIGHT_TIMESTAMPS
#endif

/* Sync event driver */

/*
 * How many sync events to buffer before motion_sense gets a chance to run.
 * This is similar to sensor side fifos.
 * Note: for vsync, anything above 2 is probably plenty.
 */
#ifndef CONFIG_SYNC_QUEUE_SIZE
#define CONFIG_SYNC_QUEUE_SIZE 8
#endif

/* Simulate command for sync */

/*
 * Define the event to raise when the sync event happens.
 * Must be within TASK_EVENT_MOTION_INTERRUPT_MASK.
 */

/*
 * Allow runtime configuration of the adc_channels[] array
 */

/*
 * ADC sample time selection. The value is chip-dependent.
 * TODO: Replace this with CONFIG_ADC_PROFILE entries.
 */

/* Support voltage comparator */

/* Include the ADC analog watchdog feature in the ADC code */
#ifndef CONFIG_ADC_WATCHDOG
#define CONFIG_ADC_WATCHDOG
#endif

/*
 * Chip-dependent ADC configuration - select one.
 * SINGLE - Sample all inputs once when requested.
 * FAST_CONTINUOUS - Sample all inputs continuously using DMA, with minimal
 *                   sample time.
 */
#ifndef CONFIG_ADC_PROFILE_SINGLE
#define CONFIG_ADC_PROFILE_SINGLE
#endif

/*
 * Some ALS modules may be connected to the EC. We need the command, and
 * specific drivers for each module.
 */
/*
 * If defined, BH1730 uses board specific lux calculation formula parameters.
 * If not defined, BH1730 uses default parameters to calculate lux.
 */
/* Define the exact model ID present on the board: SI1141 = 41, SI1142 = 42, */
/* Check if the device revision is supported */

/* Define to include the clear channel driver for the tcs3400 light sensor */

/* Define to include Vishay VEML3328 driver */

/*
 * Define the event to raise when a sensor interrupt triggers.
 * Must be within TASK_EVENT_MOTION_INTERRUPT_MASK.
 */

/*
 * Enable Si114x to operate in polling mode. This config is used in conjunction
 * with CONFIG_ALS_SI114X_INT_EVENT. When polling is enabled, the read is
 * initiated in the same manner as when interrupts are used, but the event which
 * triggers the irq_handler is generated by deferred call using a fixed delay.
 */

/*
 * Enable tcs3400 to operate without interrupt pin. This config is used in
 * conjunction with CONFIG_ALS_TCS3400_INT_EVENT. When this option is enabled,
 * the read is initiated in the same manner as when interrupts are used, but the
 * event which triggers the irq_handler is generated by deferred call.
 */

/* Define which ALS sensor is used for dimming the lightbar when dark */

/* Support AP hang detection host command and state machine */

/* Support AP Warm reset Interrupt. */

/*
 * Enable support for CPU caches behaving according to the ARMv7-M ISA.
 * (so far, only the Cortex-M7 has such caches)
 */

/*
 * Defined if core/ code provides assembly optimized implementation of
 * multiply-accumulate operations (32-bit operands, 64-bit result), for the
 * cores that lack native instructions.
 */

/* Audio codec caps. */
/* Support audio codec on DMIC. */
/* Support audio codec software gain on DMIC. */
/* Support audio codec on I2S RX. */
/* Support audio codec on WoV. */
/* Audio codec buffers. */

/*
 * Support controlling the display backlight based on the state of the lid
 * switch.  The EC will disable the backlight when the lid is closed.
 *
 * The GPIO should be named GPIO_ENABLE_BACKLIGHT if active high, or
 * GPIO_ENABLE_BACKLIGHT_L if active low. See CONFIG_BACKLIGHT_LID_ACTIVE_LOW.
 */

/*
 * The backlight GPIO pin is active low and named GPIO_BACKLIGHT_ENABLED_L
 */

/*
 * If defined, EC will enable the backlight signal only if this GPIO is
 * asserted AND the lid is open.  This supports passing the backlight-enable
 * signal from the AP through EC.
 */

/* Support base32 text encoding */

/*****************************************************************************/
/* Battery config */

/*
 * Support battery management and interrogation.
 *
 * This is implied by CONFIG_BATTERY_<device> (below); if not enabled and
 * CONFIG_BATTERY_PRESENT_CUSTOM is also disabled, the board is assumed to not
 * have or support a battery.
 */

/**
 * Enable Battery-config-in-CBI. It makes a board read battery info from CBI.
 */

/*
 * Compile battery-specific code.
 *
 * Note that some boards have their own unique battery constants / functions.
 * In this case, those are provided in board/(boardname)/battery.c, and none of
 * these are defined.
 * Defining one of these will automatically define CONFIG_BATTERY near the end
 * of this file. If you add a new config here, you'll need to update that
 * check.
 */

/*
 * MAX17055 support alert on voltage, current, temperature, and state-of-charge.
 */

/*
 * Enable full model driver of MAX17055.
 *
 * It provides a better soc estimation. ocv_table needs to be supplied.
 */

/* Compile mock battery support; used by tests. */

/* Maximum time to wake a non-responsive battery, in second */
#ifndef CONFIG_BATTERY_PRECHARGE_TIMEOUT
#define CONFIG_BATTERY_PRECHARGE_TIMEOUT 30
#endif

/*
 * If defined, the charger will check a board specific function for battery hw
 * presence as an additional condition to determine if power on is allowed for
 * factory override, where allowing booting of a bare board with no battery and
 * no power button press is required.
 */

/*
 * battery_is_present() support.
 *
 * Choice of battery driver normally determines the implementation of
 * battery_is_present(); it is also possible to provide a board-specific
 * implementation or note its presence from a GPIO level.
 *
 * If CONFIG_BATTERY is not enabled, a stub implementation that always returns
 * "not present" is provided unless CONFIG_BATTERY_PRESENT_CUSTOM is enabled.
 *
 * These options are mutually exclusive.
 */
/* The board provides a custom battery_is_present() implementation. */
/* Battery is present if the GPIO named by this define reads logic-low. */

/*
 * Compile smart battery support
 *
 * For batteries which support this specification:
 * http://sbs-forum.org/specs/sbdat110.pdf)
 */

/* Chemistry of the battery device */

/*
 * If defined, the board must supply fuel gauge and battery information for
 * each supported battery. This information is then used for battery cut off
 * and to check the charge/discharge FET status.
 */

/*
 * Defines retry count for reading Manuf/Device name in init_battery_type
 */
#ifndef CONFIG_BATTERY_INIT_TYPE_RETRY_COUNT
#define CONFIG_BATTERY_INIT_TYPE_RETRY_COUNT 0
#endif

/*
 * Number of deferred retries when initializing the battery type.
 */
#ifndef CONFIG_BATTERY_INIT_TYPE_DEFERRED_RETRY_COUNT
#define CONFIG_BATTERY_INIT_TYPE_DEFERRED_RETRY_COUNT 0
#endif

/*
 * Critical battery shutdown timeout (seconds)
 *
 * If the battery is at extremely low charge (and discharging) or extremely
 * high temperature, the EC will notify the AP and start a timer with the
 * timeout defined here. If the critical condition is not corrected before
 * the timeout expires, the EC will shut down the AP (if the AP is not already
 * off) and then optionally hibernate or cut off battery.
 */
#ifndef CONFIG_BATTERY_CRITICAL_SHUTDOWN_TIMEOUT
#define CONFIG_BATTERY_CRITICAL_SHUTDOWN_TIMEOUT 30
#endif

/* Perform a battery cut-off when we reach the battery critical level */

/* If the battery is too hot or too cold, stop charging */

/*
 * If this option is enabled, all battery cutoff commands will be
 * deferred until the chipset is shutting down.
 */

/*
 * Support battery cut-off as host command and console command.
 *
 * Once defined, you have to implement a board_cut_off_battery() function
 * in board/???/battery.c file.
 */

/*
 * The default delay is 1 second. Define this if a board prefers
 * different delay.
 */

/* Delay between consecutive battery cutoff retry attempts (in microseconds).
 * Can be overridden by CONFIG_PLATFORM_EC_BATTERY_CUTOFF_RETRY_DELAY_US.
 */
#ifndef CONFIG_BATTERY_CUTOFF_RETRY_DELAY_US
#define CONFIG_BATTERY_CUTOFF_RETRY_DELAY_US (500 * MSEC)
#endif

/* Number of extra retry attempts after the first battery cutoff failure.
 * Set to 0 to disable retry.
 * Can be overridden by CONFIG_PLATFORM_EC_BATTERY_CUTOFF_RETRY_COUNT.
 */
#ifndef CONFIG_BATTERY_CUTOFF_RETRY_COUNT
#define CONFIG_BATTERY_CUTOFF_RETRY_COUNT 0
#endif

/*
 * After the EC executes battery cutoff, it'll wait for this amount of time in
 * msec before deciding the cutoff failed.
 */
#ifndef CONFIG_BATTERY_CUTOFF_TIMEOUT_MSEC
#define CONFIG_BATTERY_CUTOFF_TIMEOUT_MSEC 8000
#endif

/*
 * Disable VolumeUp + AC disconnect battery cutoff sequence.
 */

/*
 * The board-specific battery.c implements get and set functions to read and
 * write arbirary vendor-specific parameters stored in the battery.
 * See include/battery.h for prototypes.
 */

/*
 * TODO(crosbug.com/p/29467): allows charging of a dead battery that
 * requests nil for current and voltage. Remove this workaround when
 * possible.
 */

/*
 * Check for battery in disconnect state (similar to cut-off state). If this
 * battery is found to be in disconnect state, take it out of this state by
 * force-applying a charge current. This option requires
 * battery_get_disconnect_state() to be defined.
 */

/*
 * Low voltage protection for a battery (a.k.a. deep charge inspection):
 * If battery voltage is lower than voltage_min, deep charge for more
 * than precharge time The battery voltage is still lower than voltage_min,
 * the system will stop charging
 */

/*
 * If battery voltage is lower than voltage_min, precharge voltage & current
 * are supplied and charging will be disabled after
 * CONFIG_BATTERY_LOW_VOLTAGE_TIMEOUT seconds.
 */
#ifndef CONFIG_BATTERY_LOW_VOLTAGE_TIMEOUT
#define CONFIG_BATTERY_LOW_VOLTAGE_TIMEOUT (30 * 60 * SECOND)
#endif

/*
 * Use an alternative method to store battery information: Instead of writing
 * directly to host memory mapped region, this keeps the battery information in
 * ec_response_battery_static/dynamic_info structures, that can then be fetched
 * using host commands, or via EC_ACPI_MEM_BATTERY_INDEX command, which tells
 * the EC to update the shared memory.
 *
 * This is required on dual-battery systems and hostless bases with a battery.
 */

/*
 * Number of batteries, only matters when CONFIG_BATTERY_INFO is used.
 */

/*
 * Smart battery driver should measure the voltage cell imbalance in the battery
 * pack.  This requires a battery driver capable of the measurement.
 */

/*
 * Some boards needs to lower input voltage when battery is full and chipset
 * is in S5/G3. This should be defined to integer value in mV.
 */

/*
 * Check the specific battery status to judge whether the battery is
 * initialized and stable when the battery wakes up from ship mode.
 */

/*
 * Some batteries don't update full capacity timely or don't update it at all.
 * On such systems, compensation is required to guarantee remaining_capacity
 * will be equal to full_capacity eventually. This used to be done in ACPI.
 *
 * Powerd uses CONFIG_BATT_HOST_SHUTDOWN_PERCENTAGE as the threshold for low
 * battery shutdown.
 *
 * We want to show the low battery alert whenever we can. Thus, we make EC not
 * inhibit power-on even if it knows the host would immediately shut down. To
 * get that behavior, we need:
 *
 *   MIN_BAT_PCT_FOR_POWER_ON < HOST_SHUTDOWN_PER = BATTERY_LEVEL_SHUTDOWN
 *
 * Thus, we set them as follows by default:
 *
 *   CONFIG_CHARGER_MIN_BAT_PCT_FOR_POWER_ON = 2 (don't boot if soc < 2%)
 *   CONFIG_BATT_HOST_SHUTDOWN_PERCENTAGE = 4    (shutdown if soc <= 4%)
 *   BATTERY_LEVEL_SHUTDOWN = 3                  (shutdown if soc < 3%)
 *
 * This produces the following behavior:
 *
 * - If soc = 1%, system doesn't boot. User wouldn't know why.
 * - If soc = 2~4%, system boots. Alert is shown. System immediately shuts down.
 * - If battery discharges to 4% while the system is running, system shuts down.
 *   If that happens while a user is away, they can press the power button to
 *   learn what happened.
 * - If system fails to shutdown for some reason and battery further discharges
 *   to 2%, EC will trigger shutdown.
 */
#ifndef CONFIG_BATT_HOST_SHUTDOWN_PERCENTAGE
#define CONFIG_BATT_HOST_SHUTDOWN_PERCENTAGE 4 /* shutdown if soc <= 4% */
#endif

/*
 * Powerd's full_factor. The value comes from:
 *   src/platform2/power_manager/default_prefs/power_supply_full_factor
 *
 * This value is used by the host to calculate the ETA for full charge.
 */
#ifndef CONFIG_BATT_HOST_FULL_FACTOR
#define CONFIG_BATT_HOST_FULL_FACTOR 97
#endif

/*
 * Expose some data when it is needed.
 * For example, battery disconnect state
 */

/* Include support for Bluetooth LE */

/* Include support for testing the radio for Bluetooth LE */

/* Include support for the HCI and link layers for Bluetooth LE */

/* Include debugging support for the Bluetooth link layer */

/* Include debugging support for Bluetooth HCI */

/* Boot header storage offset. */

/* Size of boot header in storage. */

/*****************************************************************************/
/* Bootblock config */

/* Pack AP-FW bootblock in EC image. */

/*****************************************************************************/

/* EC can choose power signal gpio by schematic version */

/* EC has GPIOs to allow board to reset RTC */

/*
 * Call board_config_post_gpio_init() after GPIOs are initialized.  See
 * include/board_config.h for more information.
 */

/*
 * Call board_config_pre_init() before any inits are called.  See
 * include/board_config.h for more information.
 */

/* The board version comes from Cros Board Info within EEPROM. */
/*
 * The board version is encoded with 3 GPIO signals where GPIO_BOARD_VERSION1
 * is the LSB.
 */

/* EC responses to a board defined I2C address */

/*
 * The board is unable to distinguish EC reset from power-on so it should treat
 * all resets as triggered by RESET_PIN even if it is a POWER_ON reset.
 */

/*
 * For some boards on power-on, the EC is reset by the H1 after power-on,
 * so the EC sees 2 resets. This config enables the EC to save a flag
 * on the first power-up restart, and then wait for the second reset before
 * any other setup is done (such as GPIOs, timers, UART etc.)
 * On the second reset, the saved flag is used to detect the previous
 * power-on, and treat the second reset as a power-on instead of a reset.
 *
 * NOTE: Implemented only for npcx and ite
 */

/* Permanent LM4 boot configuration */

/*****************************************************************************/
/* Modify the default behavior to make system bringup easier. */

/*
 * Enable debug prints / asserts that may helpful for debugging board bring-up,
 * but probably shouldn't be enabled for production for performance reasons.
 */

/*****************************************************************************/

/*
 * Support for entering recovery mode using the volume buttons or a dedicated
 * recovery button.  Note that these are *buttons* and not keys in the keyboard
 * matrix.
 */

/*
 * Compile detachable base support
 *
 * Enabled on all boards that have a detachable base.
 */

/*
 * Indicates there is a dedicated recovery button.  Note, that if there are
 * volume buttons, a dedicated recovery button is not needed.  This is intended
 * because if a board has volume buttons, they can do everything a dedicated
 * recovery button can do.
 * For various reasons, on some platforms there may be multiple recovery inputs.
 * See b/149967026.
 */

/* Configure recovery button. e.g. BUTTON_FLAG_ACTIVE_HIGH */

/*
 * RISC-V core specific panic data is bigger than Cortex-M core specific panic
 * data. Including this into union in panic_data structure causes whole
 * to grow by 28 bytes. In many boards EC RO is still obtaining pointer to
 * beginning of panic data by subtracting its panic data structure size from
 * the end of RAM. When EC RW saves panic data it will be corrupted by EC RO.
 * Moreover, during next boot EC RW won't be able to find jump data (see
 * b/165773837 for more details).
 *
 * This config allows boards to not include RV32I panic data if their EC RO
 * doesn't include it to keep panic data structure in sync.
 */

/*
 * The board has volume up and volume down buttons.  Note, these are *buttons*
 * and not keys in the keyboard matrix.
 */

/*
 * The board has buttons that are connected to ADC pins which pressed and
 * released values are determined by the analog voltage
 */

/*
 * Allow runtime configuration of the buttons[] array
 */

/* Support simulation of a button press using EC tool command */
/*
 * Configuration for button simulation i.e. dependent on
 * CONFIG_HOSTCMD_BUTTON or CONFIG_CMD_BUTTON config.
 */

/* Set the default button debounce time in us */
#ifndef CONFIG_BUTTON_DEBOUNCE
#define CONFIG_BUTTON_DEBOUNCE (30 * MSEC)
#endif

/*
 * Capsense chip has buttons, too.
 */

/*****************************************************************************/
/* Support CEC */

/* CEC drivers */

/*****************************************************************************/

/* Compile charge manager */

/*
 * Number of charge ports excluding type-c ports
 *
 * If defined, the board must define a macro DEDICATED_CHARGE_PORT indicates
 * the dedicated port number.
 *
 * See include/charge_manager.h for more details about dedicated port.
 */
#ifndef CONFIG_DEDICATED_CHARGE_PORT_COUNT
#define CONFIG_DEDICATED_CHARGE_PORT_COUNT 0
#endif

/* Allow charge manager to default to charging from dual-role partners */

/* Handle the external power limit host command in charge manager */

/* Initially enter safe mode, with relaxed port / current selection rules */
#ifndef CONFIG_CHARGE_MANAGER_SAFE_MODE
#define CONFIG_CHARGE_MANAGER_SAFE_MODE
#endif

/* Leave safe mode when battery pct meets or exceeds this value */
#ifndef CONFIG_CHARGE_MANAGER_BAT_PCT_SAFE_MODE_EXIT
#define CONFIG_CHARGE_MANAGER_BAT_PCT_SAFE_MODE_EXIT 2
#endif

/* The hardware has some input current ramping/back-off mechanism */

/* Compile input current ramping support using software control */

/* Enable EC support for charging splashscreen */

/*****************************************************************************/
/* Charger config */

/* Compile charger-specific code for these chargers (pick at most one) */

/* Allow run-time completion of the charger driver structure */

/*
 * Board has only one charger chip (default, undef when board contains multiple
 * charger chips
 */
#ifndef CONFIG_CHARGER_SINGLE_CHIP
#define CONFIG_CHARGER_SINGLE_CHIP
#endif

/*
 * Enable the CHG_EN at initialization to turn-on the BGATE which allows voltage
 * to be applied to the battery PACK & wakes the battery if it is in shipmode.
 */

/*
 * BD9995X Power Save Mode
 *
 * Which power save mode should the charger enter when VBUS is removed.  Check
 * driver/bd9995x.h for the power save settings.  By default, no power save mode
 * is enabled.
 */

/*
 * If the battery temperature sense pin is connected to charger,
 * get the battery temperature from the charger.
 */

/*
 * Board specific charging current limit, in mA.  If defined, the charge state
 * machine will not allow the battery to request more current than this.
 */

/*
 * MT6370 backlight control settings.
 * If defined, Panel backlight power is controlled by MT6370.
 */

/*
 * MT6360/MT6370 BC1.2 USB-PHY control.
 * If defined, USB-PHY connection is controlled by GPIO_BC12_DET_EN.
 * Assert GPIO_BC12_DET_EN to detect BC1.2 device, and deassert
 * GPIO_BC12_DET_EN to mux USB-PHY back.
 */

/*
 * Enable/disable system power monitor PSYS function: this enables output
 * from charger chip to SoC.
 */

/*
 * Enable reading PSYS (system power) value, either via "psys" console command,
 * or via charger_get_system_power function.
 */

/*
 * Sets the AC current threshold that triggers PROCHOT assertion from the
 * charger.  -1 uses the power on reset threshold defined by the charger chip.
 */
#ifndef CONFIG_CHARGER_AC_PROCHOT_CURRENT_MA
#define CONFIG_CHARGER_AC_PROCHOT_CURRENT_MA -1
#endif

/*
 * Sets the DC current threshold that triggers PROCHOT assertion from the
 * charger.  -1 uses the power on reset threshold defined by the charger chip.
 */
#ifndef CONFIG_CHARGER_DC_PROCHOT_CURRENT_MA
#define CONFIG_CHARGER_DC_PROCHOT_CURRENT_MA -1
#endif

/*
 * Board supports discharge mode.  In this mode, the battery will discharge
 * even if AC is present.  Used for testing.
 */

/* Board has a custom discharge mode. */

/*
 * Board specific flag used to disable external ILIM pin used to determine input
 * current limit. When defined, the input current limit is decided only by
 * the software register value.
 */

/*
 * Default input current for the board, in mA.  Many boards also use this as the
 * least maximum input current during transients.
 *
 * This value should depend on external power adapter, designed charging
 * voltage, and the maximum power of the running system. For type-C chargers,
 * this should be set to 512 mA in order to not brown-out low-current USB
 * charge ports in accordance with USB-PD r3.0 Sec. 7.3
 */

/*
 * Minimum current limit that will ever be set for chargers, even if a lower
 * limit is requested. This will allow the charger to draw more power than
 * the requested limit.
 *
 * If set, this should usually be set to no more than 2.5W divided by the
 * maximum supported input voltage in order to satisfy USB-PD pSnkStdby
 * requirements. Higher values may help devices stay alive under low-battery
 * conditions at the cost of violating standby power limits.
 *
 * Many boards set this to large values, since historically this number was
 * usually equal to CONFIG_CHARGER_DEFAULT_CURRENT_LIMIT. New boards should
 * avoid doing so if possible.
 */

/*
 * Percentage derating factor applied to charger input current limits.
 *
 * Desired charger current is reduced by this many percent when programming
 * chargers via the charge manager, which is usually used to account for
 * chargers that draw slightly more current than the programmed limit or to
 * provide some margin for accuracy. For example, if this value is set to 4
 * and input current is limited to 1000 mA, the charger will be given a limit
 * of 960 mA.
 *
 * The default value is set to prevent most overcurrent conditions during load
 * transients, because power supplies vary in their tolerance to such
 * short-lived overcurrent conditions and many chargers respond slowly to those
 * transients.
 *
 * Projects SHOULD characterize system behavior to tune for system
 * behavior and charger response in order to optimize this (allowing the
 * derating to be reduced) and ensure transients do not exceed the range of
 * acceptable current (which might require greater derating).
 *
 * Boards requiring more complex control over input current should leave this
 * undefined and override board_set_charge_limit instead.
 */
#ifndef CONFIG_CHARGER_INPUT_CURRENT_DERATE_PCT
#define CONFIG_CHARGER_INPUT_CURRENT_DERATE_PCT 5
#endif

/*
 * This config option is used to enable IDCHG trigger for prochot. This macro
 * should be set to the desired current limit to draw from the battery before
 * triggering prochot. Note that is has a 512 mA granularity. The function that
 * sets the limit will mask of the lower 10 bits. For this check to be active
 * the bq25710 must be in performance mode and this config option is also used
 * to keep the bq25710 in performance mode when the AP is in S0.
 */

/* Enable if CONFIG_CHARGER_BQ25720_VSYS_TH2_DV should be applied */

/*
 * This config option is used to set the charger's VSYS voltage
 * threshold. When the voltage drops to this level, PROCHOT is asserted
 * by the charger to request reduced system power demand and hopefully
 * avoid a voltage droop leading to system instability. The voltage is
 * specified in deci-volts, so a value of 80 would set the threshold to
 * 8.0v.
 */

/* Enable if CONFIG_CHARGER_BQ25720_VSYS_UVP should be applied */

/*
 * This config option is used to set the VSYS under voltage (VSYS_UVP)
 * lockout threshold. This is a 3 bit field with default value 0. The
 * actual voltage encoded is (0.8 * <value> + 2.4), allowing a threshold
 * in the range of 2.4 V to 8.0 V to be specified.
 */

/* Enable if CONFIG_CHARGER_BQ25720_IDCHG_DEG2 should be applied */

/*
 * This config option is used to set the 2nd battery discharge current
 * limit (IDCHG_TH2) deglitch time (IDCHG_DEG2). This is a 2 bit field
 * with default value 1 (1.6 ms). The encoded value ranges from 100 us
 * to 12 ms.
 */

/* Enable if CONFIG_CHARGER_BQ25720_IDCHG_TH2 should be applied */

/*
 * This config option is used to set the charger's 2nd battery discharge
 * current limit (IDCHG_TH2) as a percentage of IDCHG_TH1. This is a 3
 * bit field with default value 1 (150%). The encoded value ranges from
 * 125% to 400%.
 */

/* Value of the bq25710 charge sense resistor, in mOhms */

/* Value of the bq25710 input current sense resistor, in mOhms */

/* Value of the bq25770 charge sense resistor, in mOhms */

/* Value of the bq25770 input current sense resistor, in mOhms */

/*
 * This config option is used to enable the PSYS sensing circuit on the
 * BQ25710 and BQ25720 chargers. This is used for system power
 * monitoring on board designs that support this capability. This
 * circuit is disabled by default (reset) and needs to be explicitly
 * enabled for meaningful results.
 */

/*
 * This config option is used to change the charger's internal
 * comparator reference voltage to 1.2 V. The power-on default is 2.3
 * V. This must be enabled if the board was designed for 1.2 V instead
 * of 2.3 V.
 */

/*
 * This config option is used to change the charger's independent comparator
 * output polarity. The default setting is CMPIN is above internal threshold,
 * CMPOUT is LOW (internal hysteresis).
 */

/* Enable if CONFIG_CHARGER_BQ25710_PKPWR_TOVLD_DEG should be applied */

/*
 * Input overload time when in peak power mode (PKPWR_TOVLD_DEG). This
 * limits how long the charger can draw ILIM2 from the adapter. This is
 * a 2 bit field. On the bq25710 1 ms to 20 ms can be encoded. On the
 * bq25720 1 ms to 10 ms can be encoded.
 */

/*
 * This config option is used to enable the charger's AC over-current
 * protection. The converter turns off when the OC threshold is
 * reached. The threshold is selected using the ACOC_VTH bit.
 */

/*
 * This config option selects which ACOC protection threshold is used
 * with EN_ACOC. Enabling this option selects 133% of ILIM2. Otherwise,
 * the default is 200% of ILIM2.
 */

/*
 * This config option selects the minimum BATOC protection threshold to
 * be used with EN_BATOC. The minimum threshold is 150% of PROCHOT IDCHG
 * on the bq25710 and 133% of PROCHOT IDCHG_TH2 on the bq25720. The
 * default threshold is 200% on both chips.
 */

/*
 * This config option sets the PP_INOM bit in Prochot Option 1
 * register. This causes PROCHOT to be pulsed when the nominal adapter
 * current threshold is reached. INOM is 110% of IDPM/IIN_DPM (input
 * current setting).
 */

/*
 * This config option sets the PP_BATPRES bit in Prochot Option 1
 * register. This causes PROCHOT to be pulsed when the battery is
 * removed.
 */

/*
 * This config option sets the PP_ACOK in Prochot Option 1
 * register. This causes PROCHOT to be pulsed when the AC adapter is
 * removed.
 */

/*
 * This config option sets the PP_COMP in Prochot Option 1
 * register. Need to use EN_PROCHOT_LPWR to enable independent comparator
 * and its PROCHOT profile.
 */

/*
 * This config option sets the PP_IDCHG2 bit in the Charge Option 4
 * register. This causes PROCHOT to be pulsed when IDCHG_TH2 is reached.
 */

/* Enable if CONFIG_CHARGER_BQ25710_VSYS_MIN_VOLTAGE_MV should be applied */

/*
 * This config option sets the minimum system voltage in
 * milli-volts. The bq25710 uses 6 bits of resolution and can be
 * configured from 1.024 V to 16.128 V in 256 mV increments. The bq25720
 * uses 8 bits of resolution and can be set from 1.0 V to 19.2 V in 100
 * mV increments. The default value depends on configured number of
 * battery cells connected in series using the CELL_BATPRESZ strap.
 */

/* Enable if CONFIG_CHARGER_RT9478_IAICR2 should be applied */

/*
 * This config option sets percentage of IAICR1 as threshold. ICRIT threshold is
 * set to be 110% of IAICR2. Input overcurrent protection (IBUS_OCP1) threshold
 * is also 1.33x or 2x of IAICR2 (1.33x or 2x based on IBUS_OCP1_TH field).
 * 1  - 25: 110% - 230%, step is 5%.
 * 26 - 30: 250% - 450%, step is 50%.
 * 31: Out of range (ignored).
 */
/*
 * Board specific maximum input current limit, in mA.
 */

/*
 * Leave charger VBAT configured to battery-requested voltage under all
 * conditions, even when AC is not present. This may be necessary to work
 * around quirks of certain charger chips, such as the BD9995X.
 */

/*
 * Power thresholds for AP boot
 *
 * If one of the following conditions is met, EC boots AP:
 *
 * 1. Battery charge >= CONFIG_CHARGER_MIN_BAT_PCT_FOR_POWER_ON
 * 2. AC power >= CONFIG_CHARGER_MIN_POWER_MW_FOR_POWER_ON
 * 3. Battery charge >= CONFIG_CHARGER_MIN_BAT_PCT_FOR_POWER_ON_WITH_AC
 *    and
 *    AC power >= CONFIG_CHARGER_MIN_POWER_MW_FOR_POWER_ON_WITH_BATT
 *
 * Note that CONFIG_CHARGER_LIMIT_POWER_THRESH_BAT_PCT/_CHG_MW are thresholds
 * for the OS boot used by Depthcharge. The OS has higher power requirement
 * but PD power is also available.
 *
 * WARNING: Locked RO firmware does not negotiate power greater than 15W via
 * analog signaling.  If the AP requires greater than 15W to boot, then see
 * CONFIG_CHARGER_LIMIT_POWER_THRESH_CHG_MW.
 */
#ifndef CONFIG_CHARGER_MIN_BAT_PCT_FOR_POWER_ON
#define CONFIG_CHARGER_MIN_BAT_PCT_FOR_POWER_ON 2 /* Don't boot if soc < 2% */
#endif
#ifndef CONFIG_CHARGER_MIN_BAT_PCT_FOR_POWER_ON_WITH_AC
#define CONFIG_CHARGER_MIN_BAT_PCT_FOR_POWER_ON_WITH_AC 1
#endif
/* Default: 15000 */
/* Default: Disabled */

/* Minimum battery percentage for power on with an imbalanced pack */

/*
 * Maximum battery cell imbalance to accept before considering the pack to be
 * imbalanced, in millivolts.
 */

/*
 * Select this option if the charger will be used in a bypass mode in
 * order to pass the input current from AC directly to the system
 * power rail for efficiency.
 */

/* Set this option when using a Narrow VDC (NVDC) charger, such as ISL9237/8. */

/*
 * Low energy thresholds - when battery level is below BAT_PCT and an external
 * charger provides less than CHG_MW of power, inform the AP of the situation
 * through the LIMIT_POWER charge state parameter.  Depthcharge will hold off on
 * the boot for up to 3 seconds while waiting for either condition to clear
 * before starting the kernel.  This wait happens after sw sync in RW mode, so
 * the firmware may set it high enough that PD negotiation is required to clear
 * it.
 *
 * Default: Disabled.  Depthcharge is immediately released to boot the kernel.
 *
 * Setting this value to 15001 will require PD negotiation to be complete prior
 * to releasing depthcharge.  During PD negotiation, the charger will be briefly
 * reduced to about 2.5W for a few hundred ms.
 */
/* Default: CHARGER_MIN_BAT_PCT_FOR_POWER_ON */

/*
 * Enable charger's OTG functions, i.e. make it possible to supply output power
 * from battery.
 */

/*
 * Charger should call battery_override_params() to limit/correct the voltage
 * and current requested by the battery pack before acting on the request.
 */

/*
 * Common code for charger profile override. Should be used with
 * CONFIG_CHARGER_PROFILE_OVERRIDE.
 */

/*
 * Battery voltage threshold ranges for charge profile override.
 * Override it in board.h if battery has multiple threshold ranges.
 */
#ifndef CONFIG_CHARGER_PROFILE_VOLTAGE_RANGES
#define CONFIG_CHARGER_PROFILE_VOLTAGE_RANGES 2
#endif

/* Value of the charge sense resistor, in mOhms */

/* Value of the input current sense resistor, in mOhms */

/*
 * Board has an GPIO pin to enable or disable charging.
 *
 * This GPIO should be named GPIO_CHARGER_EN, if active high. Or
 * GPIO_CHARGER_EN_L if active low.
 */

/* Charger enable GPIO is active low */

/*
 * OCPC - One Charger IC Per Type-C
 *
 * Define this if the board may have multiple charger ICs in the system.  The
 * assumption is that that primary charger is index 0 and is the charger IC
 * connected to the battery FET.  Additionally, `chgnum` is assumed to be the
 * same as the charge port index.
 */

/*
 * Boards using OCPC must define this value in order to seed the starting board
 * battery and system resistance between the secondary charger IC and the
 * battery.  This should be at a minimum the Rds(on) resistance of the BFET plus
 * the series sense resistor.
 */

/* Set a default OCPC drive limit for legacy boards */
#ifndef CONFIG_OCPC_DEF_DRIVELIMIT_MILLIVOLTS
#define CONFIG_OCPC_DEF_DRIVELIMIT_MILLIVOLTS 10
#endif

/* Enable trickle charging */

/* Set trickle charge current by taking integer value */
#ifndef CONFIG_RAA489000_TRICKLE_CHARGE_CURRENT
#define CONFIG_RAA489000_TRICKLE_CHARGE_CURRENT 128
#endif

/* Set two level input current limit function  */

/* Wireless chargers */

/*
 * SM5803 PROCHOT configuration
 * This follow the hardware default value.
 */
#ifndef CONFIG_CHARGER_SM5803_PROCHOT_DURATION
#define CONFIG_CHARGER_SM5803_PROCHOT_DURATION 2
#endif
#ifndef CONFIG_CHARGER_SM5803_VBUS_MON_SEL
#define CONFIG_CHARGER_SM5803_VBUS_MON_SEL 2
#endif
#ifndef CONFIG_CHARGER_SM5803_VSYS_MON_SEL
#define CONFIG_CHARGER_SM5803_VSYS_MON_SEL 10
#endif
#ifndef CONFIG_CHARGER_SM5803_IBAT_PHOT_SEL
#define CONFIG_CHARGER_SM5803_IBAT_PHOT_SEL IBAT_SEL_MAX
#endif

/*
 * Precharge delay time to wait for the charger is stable
 * to set charge current/voltage.
 */

/*****************************************************************************/

/*
 * The chip needs to define special SRAM memory regions as linker sections.
 * Those regions are defined in the special-purpose preprocessed file in
 * chip/<chip_name>/memory_regions.inc using the following macro:
 * REGION(name, attributes, start_address, size)
 *
 * Note: these 'special' regions are NOT cleared at startup contrary to .bss.
 */

/*
 * Chip needs to do pre-init very early in main(), and provides chip_pre_init()
 * to do so.
 */

/*
 * Set the caching attributes of one of the RAM regions to uncached.
 *
 * When defined, CONFIG_CHIP_UNCACHED_REGION must be equal to the name of one
 * of the regions defined in memory_regions.inc for CONFIG_CHIP_MEMORY_REGIONS.
 */

/*
 * When defined, adds a new linker section to store objects that remain resident
 * in ROM/flash. This is useful on ECs that execute all code from RAM and
 * in which the RAM size is smaller than the flash size.
 *
 * Code can force objects into the .init_rom resident section using the
 * __init_rom macro. Objects should accessed using the include/init_rom.h
 * module.
 */

/*
 * This is a convenience macro that causes the .data section to link into
 * the ROM/flash resident section defined above.
 *
 * When enabled, the EC initialization code copies the .data section directly
 * from flash into data RAM.
 *
 * When this is not defined, the bootloader copies the .data section from flash
 * to code RAM. The EC initialization code copies .data from code RAM to data
 * RAM.
 *
 * This is automatically enabled when both CONFIG_CHIP_INIT_ROM_REGION and
 * CONFIG_MAPPED_STORAGE are enabled.
 */

/*****************************************************************************/
/* Chipset config */

/* AP chipset support; pick at most one */
*discrete EC control * /

/* Shared chipset support; automatically gets defined below. */

/* Support chipset throttling */

/* Enable additional chipset debugging */

/* Enable chipset reset hook, requires a deferrable function */

/*
 * Enable chipset resume init and suspend complete hooks. These hooks are
 * usually used to initialize/disable the SPI driver, which goes to sleep
 * on suspend. Require to initialize it first such that it can receive a
 * host resume event, that notifies the normal resume hook.
 */

/*
 * Enable turning on PP3300_A rail before PP5000_A rail on the Ice Lake
 * and Tiger Lake chipsets. Enable this option if there is leakage from PP5000_A
 * resources into PP3300_A resources.
 */

/*
 * Enable the EC to drive SLP_S3_L during the G3 to S3 transition. This is
 * needed on TigerLake designs to prevent a glitch on the SLP_S3_L and PCH_PWROK
 * signals during power on.
 */

/*
 * Enable if chipset requires delay between power signals going high
 * and deasserting RSMRST to PCH.
 */

/* Passthrough RSMRST_L de-assertion after S5 */

/* Support PMIC reset(using LDO_EN) in chipset */

/* Board requires chipset pre-init callback */

/* Redefine when we need a different power-on sequence on the same chipset. */
#ifndef CONFIG_CHIPSET_POWER_SEQ_VERSION
#define CONFIG_CHIPSET_POWER_SEQ_VERSION 0
#endif

/*
 * Allow fake control of the power states.
 *
 * Note: This should NOT be used on platforms which have an SoC present.
 */

/* AMD Side-Band Remote Management Interface (SB-RMI) support */

/*****************************************************************************/
/*
 * Chip config for clock circuitry
 *	define = crystal / undef = oscillator
 */

/*
 * Indicate if a clock source is connected to stm32f4's high speed external
 * clock signal (HSE) specific input
 */

/*
 * Indicate if a clock source is connected to low speed external (LSE) specific
 * input
 */

/*
 * Chip config for clock source
 *	 define = external crystal oscillator / undef = internal clock source
 */

/*****************************************************************************/
/* Support curve25519 public key cryptography */

/*****************************************************************************/
/* PMIC config */

/* Support firmware long press power-off timer */

/* Support PMIC power control */

/*****************************************************************************/
/*
 * Example functionality that is used to test boards.
 */

/*
 * Enable the blink example.
 *
 * LEDs are used to count in binary.
 *
 * Required Configuration:
 * - CONFIG_BLINK_LEDS        --> List of LEDs (gpio enum names) to use as bits
 */

/*****************************************************************************/
/*
 * Optional console commands
 *
 * Defining these options will enable the corresponding command on the EC
 * console.
 */

#ifndef CONFIG_CMD_ACCELSPOOF
#define CONFIG_CMD_ACCELSPOOF
#endif
#ifndef CONFIG_CMD_ADC
#define CONFIG_CMD_ADC
#endif
#ifndef CONFIG_CMD_APTHROTTLE
#define CONFIG_CMD_APTHROTTLE
#endif
#ifndef CONFIG_CMD_BATTFAKE
#define CONFIG_CMD_BATTFAKE
#endif
#ifndef CONFIG_CMD_CBI
#define CONFIG_CMD_CBI
#endif

#ifndef CONFIG_CMD_CHARGER
#define CONFIG_CMD_CHARGER
#endif

/* Extra debugging info for the charger */
#ifndef CONFIG_CHARGE_DEBUG
#define CONFIG_CHARGE_DEBUG
#endif

#ifndef CONFIG_CMD_CHARGE_SUPPLIER_INFO
#define CONFIG_CMD_CHARGE_SUPPLIER_INFO
#endif
#ifndef CONFIG_CMD_CRASH
#define CONFIG_CMD_CRASH
#endif
#ifndef CONFIG_CMD_DEVICE_EVENT
#define CONFIG_CMD_DEVICE_EVENT
#endif
#ifndef CONFIG_CMD_FASTCHARGE
#define CONFIG_CMD_FASTCHARGE
#endif
#ifndef CONFIG_CMD_FLASHINFO
#define CONFIG_CMD_FLASHINFO
#endif
#ifndef CONFIG_CMD_GETTIME
#define CONFIG_CMD_GETTIME
#endif
#ifndef CONFIG_CMD_HASH
#define CONFIG_CMD_HASH
#endif
#ifndef CONFIG_CMD_HCDEBUG
#define CONFIG_CMD_HCDEBUG
#endif
#ifndef CONFIG_CMD_I2C_SCAN
#define CONFIG_CMD_I2C_SCAN
#endif
#ifndef CONFIG_CMD_I2C_XFER
#define CONFIG_CMD_I2C_XFER
#endif
#ifndef CONFIG_CMD_IDLE_STATS
#define CONFIG_CMD_IDLE_STATS
#endif
#ifndef CONFIG_CMD_INA
#define CONFIG_CMD_INA
#endif
#ifndef CONFIG_CMD_KEYBOARD
#define CONFIG_CMD_KEYBOARD
#endif
#ifndef CONFIG_CMD_MD
#define CONFIG_CMD_MD
#endif
#ifndef CONFIG_CMD_MEM
#define CONFIG_CMD_MEM
#endif
#ifndef CONFIG_CMD_MFALLOW
#define CONFIG_CMD_MFALLOW
#endif
#ifndef CONFIG_CMD_MMAPINFO
#define CONFIG_CMD_MMAPINFO
#endif
#ifndef CONFIG_CMD_PD
#define CONFIG_CMD_PD
#endif
#ifndef CONFIG_CMD_PECI
#define CONFIG_CMD_PECI
#endif
#ifndef CONFIG_CMD_POWERINDEBUG
#define CONFIG_CMD_POWERINDEBUG
#endif
#ifndef CONFIG_CMD_PWR_AVG
#define CONFIG_CMD_PWR_AVG
#endif
#ifndef CONFIG_CMD_POWER_AP
#define CONFIG_CMD_POWER_AP
#endif
#ifndef CONFIG_CMD_REGULATOR
#define CONFIG_CMD_REGULATOR
#endif
#ifndef CONFIG_CMD_RW
#define CONFIG_CMD_RW
#endif
#ifndef CONFIG_CMD_SHMEM
#define CONFIG_CMD_SHMEM
#endif
#ifndef CONFIG_CMD_SLEEPMASK
#define CONFIG_CMD_SLEEPMASK
#endif
#ifndef CONFIG_CMD_SLEEPMASK_SET
#define CONFIG_CMD_SLEEPMASK_SET
#endif
#ifndef CONFIG_CMD_SYSINFO
#define CONFIG_CMD_SYSINFO
#endif
#ifndef CONFIG_CMD_SYSJUMP
#define CONFIG_CMD_SYSJUMP
#endif
#ifndef CONFIG_CMD_SYSLOCK
#define CONFIG_CMD_SYSLOCK
#endif
#ifndef CONFIG_CMD_TEMP_SENSOR
#define CONFIG_CMD_TEMP_SENSOR
#endif
#ifndef CONFIG_CMD_TIMERINFO
#define CONFIG_CMD_TIMERINFO
#endif
#ifndef CONFIG_CMD_TYPEC
#define CONFIG_CMD_TYPEC
#endif
#ifndef CONFIG_CMD_WAITMS
#define CONFIG_CMD_WAITMS
#endif

/*****************************************************************************/

/* Provide common core code to output panic information without interrupts. */
#ifndef CONFIG_COMMON_PANIC_OUTPUT
#define CONFIG_COMMON_PANIC_OUTPUT
#endif

/*
 * Certain platforms(e.g. eve, poppy) cannot retain panic info in data ram since
 * VCC is powered down on EC reset. On such platforms, panic data needs to be
 * saved/restored to persistent storage by using chip specific
 * implementations. This option can be enabled by those platforms that have and
 * wish to use chip-implemented panic backup/restore functions.
 */

/* Don't save General Purpose Registers during panic */

/* Provide another output method of panic information by console channel */

/* When defined, it enables build assert for panic data structure size */

/*
 * Enables fetching a memory dump using host commands. This is useful when
 * debugging panics. May not dump all memory, e.g. sensitive memory will
 * not be dumped.
 */

/*
 * Panic on watchdog warning instead of waiting for a regular watchdog.
 * Combined with with system safe mode, this allows for capturing
 * extra debug information about the system state.
 * WATCHDOG_PERIOD_MS should be lengthened when this option is enabled,
 * since it is effectivley shortened by WATCHDOG_WARNING_LEADING_TIME_MS.
 */

/**
 * Enables nesting for the `crash` console command.
 * Calling the crash console command with multiple crash arguments
 * will result in nested crashes in the order specified.
 */
#ifndef CONFIG_CMD_CRASH_NESTED
#define CONFIG_CMD_CRASH_NESTED
#endif

/**
 * Panic log captures a copy of the console log in uninitialized
 * preserved ram. After a panic, the panic_log is frozen until it is
 * dumped by the OS. Preserving the log is best effort. Integrity checks
 * ensure corruption is detected and the log is discarded. The implementation
 * is designed to be low overhead.
 */
/* Size of panic log in bytes. Must be a power of 2. */
#ifndef CONFIG_PANIC_LOG_SIZE
#define CONFIG_PANIC_LOG_SIZE 2048
#endif
/**
 * Enables panic log debug features,
 * including the `paniclog` console command.
 */

/* Enable generic SVDM response support */

/*
 * noinit_end_of_ram is a memory section placed at the very end
 * of used ram, where it is least likely to overlap with RO ram.
 * The section is useful for preserving data across reboots.
 * May not be enabled in RO.
 */
#ifndef CONFIG_CROS_EC_RO
#ifndef CONFIG_NOINIT_END_OF_RAM_SECTION
#define CONFIG_NOINIT_END_OF_RAM_SECTION
#endif
#else
#endif

/*
 * Offset noinit_end_of_ram section. Only adjust in the rare case that
 * RO ram overlaps with RW noinit_end_of_ram section.
 */
#ifndef CONFIG_NOINIT_END_OF_RAM_SECTION_OFFSET
#define CONFIG_NOINIT_END_OF_RAM_SECTION_OFFSET 0
#endif

/*
 * Provide the default GPIO abstraction layer.
 * You want this unless you are doing a really tiny firmware.
 */
#ifndef CONFIG_COMMON_GPIO
#define CONFIG_COMMON_GPIO
#endif

/*
 * Provides smaller GPIO names to reduce flash size.  Instead of the 'name'
 * field in GPIO macro it will concat 'port' and 'pin' to reduce flash size.
 */

/*
 * Control the IO pins of IO expander via IO Expander APIs
 *
 * If defined, declare the IOEX pin with macro IOEX. For example:
 *     IOEX(IO_NAME, EXPIN(0, 0, 0), GPIO_OUT_HIGH)
 * For more details, see gpio_list.h.
 *
 * WARNING: make sure none of IOEX IOs are accessed at interrupt level / with
 * interrupts disabled. Doing so may hang the EC because IO expanders may rely
 * on I2C interrupts.
 *
 * Some reasons that not unify the GPIO and IOEX APIs have been disscussed and
 * filed in the crbug.com/985540.
 */

/*
 * Enable reading levels for whole IO expander port with one call.
 * This adds 'get_port' function pointer to 'ioexpander_drv' structure.
 * Most drivers don't implement this functionality.
 */

/*
 * EC's supporting powering down GPIO pins.
 * Add flag GPIO_POWER_DOWN and additional API's.
 */

/*
 * Protect the code RAM section on devices that execute code from RAM. On these
 * devices, this mechanism protects the code from being modified using the MPU.
 * The MPU protections are setup on boot.
 */

/*
 * Provide common runtime layer code (tasks, hooks ...)
 * You want this unless you are doing a really tiny firmware.
 */
#ifndef CONFIG_COMMON_RUNTIME
#define CONFIG_COMMON_RUNTIME
#endif

/* Allow deferred (async) flash protect*/
#ifndef CONFIG_FLASH_PROTECT_DEFERRED
#define CONFIG_FLASH_PROTECT_DEFERRED
#endif

/* Provide common core code to handle the operating system timers. */
#ifndef CONFIG_COMMON_TIMER
#define CONFIG_COMMON_TIMER
#endif

/*****************************************************************************/

/*
 * Provide additional help on console commands, such as the supported
 * options/usage.
 *
 * Boards may #undef this to reduce image size.
 */
#ifndef CONFIG_CONSOLE_CMDHELP
#define CONFIG_CONSOLE_CMDHELP
#endif

/*
 * Add a .flags field to the console commands data structure, to distinguish
 * some commands from others. The available flags bits are defined in
 * include/console.h
 */

/*
 * One use of the .flags field is to make some console commands restricted, so
 * that they can be disabled or enabled at run time.
 */

/* The default .flags field value is zero, unless overridden with this. */

/*
 * Enable EC_CMD_CONSOLE_READ V1. One could disable this config to prevent
 * kernel from creating the `console_log` debugfs entry.
 */
#ifndef CONFIG_CONSOLE_ENABLE_READ_V1
#define CONFIG_CONSOLE_ENABLE_READ_V1
#endif

/*
 * Number of entries in console history buffer.
 *
 * Boards may #undef this to reduce memory usage.
 */
#ifndef CONFIG_CONSOLE_HISTORY
#define CONFIG_CONSOLE_HISTORY 8
#endif

/* Max length of a single line of input */
#ifndef CONFIG_CONSOLE_INPUT_LINE_SIZE
#define CONFIG_CONSOLE_INPUT_LINE_SIZE 80
#endif

/* Amount of time to keep the console in use flag */
#ifndef CONFIG_CONSOLE_IN_USE_ON_BOOT_TIME
#define CONFIG_CONSOLE_IN_USE_ON_BOOT_TIME (15 * SECOND)
#endif

/* Enable verbose output to UART console and extra timestamp print precision. */
#ifndef CONFIG_CONSOLE_VERBOSE
#define CONFIG_CONSOLE_VERBOSE
#endif

/* Enable the console print command. This allows the host to print messages
 * directly in the EC console.
 */
#ifndef CONFIG_HOSTCMD_CONSOLE_PRINT
#define CONFIG_HOSTCMD_CONSOLE_PRINT
#endif

/*****************************************************************************/
/* Support for EC-EC communication */

/*
 * Board is client or server in EC-EC communication.
 */

/*
 * Board support battery-related functions in EC-EC communication.
 */

/* Include CRC-8 utility function */

/*****************************************************************************/
/*
 * Debugging config
 *
 * Note that these options are enabled by default, because they're really
 * handy for debugging systems during bringup and even at factory time.
 *
 * A board may undefine any or all of these to reduce image size and RAM usage,
 * at the cost of debuggability.
 */

/*
 * ASSERT() macros are checked at runtime.  See CONFIG_DEBUG_ASSERT_REBOOTS
 * to see what happens if one fails.
 *
 * Boards may #undef this to reduce image size.
 */
#ifndef CONFIG_DEBUG_ASSERT
#define CONFIG_DEBUG_ASSERT
#endif

/*
 * Prints a message and reboots if an ASSERT() macro fails at runtime.  When
 * enabled, an ASSERT() which fails will produce a message of the form:
 *
 * ASSERTION FAILURE '<expr>' in function() at file:line
 *
 * If this is not defined, failing ASSERT() will trigger a BKPT instruction
 * instead.
 *
 * Ignored if CONFIG_DEBUG_ASSERT is not defined.
 *
 * Boards may #undef this to reduce image size.
 */
#ifndef CONFIG_DEBUG_ASSERT_REBOOTS
#define CONFIG_DEBUG_ASSERT_REBOOTS
#endif

/*
 * On assertion failure, prints only the file name and the line number.
 *
 * Ignored if CONFIG_DEBUG_ASSERT_REBOOTS is not defined.
 *
 * Boards may define this to reduce image size.
 */

/*
 * Disable the write buffer used for default memory map accesses.
 * This turns "Imprecise data bus errors" into "Precise" errors
 * in exception traces at the cost of some performance.
 * This may help identify the offending instruction causing an
 * exception. Supported on cortex-m.
 */

/*
 * Print additional information when exceptions are triggered, such as the
 * fault address, here shown as bfar. This shows the reason for the fault
 * and may help to determine the cause.
 *
 *	=== EXCEPTION: 03 ====== xPSR: 01000000 ===========
 *	r0 :0000000b r1 :00000047 r2 :60000000 r3 :200013dd
 *	r4 :00000000 r5 :080053f4 r6 :200013d0 r7 :00000002
 *	r8 :00000000 r9 :200013de r10:00000000 r11:00000000
 *	r12:00000000 sp :200009a0 lr :08002b85 pc :08003a8a
 *	Precise data bus error, Forced hard fault, Vector catch, bfar = 60000000
 *	cfsr = 00008200, shcsr = 00000000, hfsr = 40000000, dfsr = 00000008
 *
 * If this is not defined, only a register dump will be printed.
 *
 * Boards may #undef this to reduce image size.
 */
#ifndef CONFIG_DEBUG_EXCEPTIONS
#define CONFIG_DEBUG_EXCEPTIONS
#endif

/*
 * Print orientation when device orientation changes
 * (requires CONFIG_SENSOR_ORIENTATION)
 */

/* Support Synchronous UART debug printf. */

/* Check for stack overflows on every context switch */
#ifndef CONFIG_DEBUG_STACK_OVERFLOW
#define CONFIG_DEBUG_STACK_OVERFLOW
#endif

/*****************************************************************************/

/* Support events from devices attached to the EC */

/* Monitor the states of other devices */

/* Support DMA transfers inside the EC */

/* Use the common interrupt handlers for DMA IRQs */
#ifndef CONFIG_DMA_DEFAULT_HANDLERS
#define CONFIG_DMA_DEFAULT_HANDLERS
#endif

/* Compile extra debugging and tests for the DMA module */

/*
 * If the board supports DRAM, base DRAM address for the chip, where we want
 * to load extra code/data (address from chip address space).
 */

/*
 * If the board supports DRAM, base DRAM address to load the extra code/data
 * (if loaded by AP, this is the AP physical address space).
 */

/* DRAM size. */

/* Usually, EC capable of sensor speeds up to 250 Hz */
#ifndef CONFIG_EC_MAX_SENSOR_FREQ_DEFAULT_MILLIHZ
#define CONFIG_EC_MAX_SENSOR_FREQ_DEFAULT_MILLIHZ 250000
#endif

/* Maximal EC sampling rate */

/*
 * Support for sending emulated sysrq events to AP (on designs with a keyboard,
 * sysrq is passed as normal key presses).
 */

/* Include code for handling external power */
#ifndef CONFIG_EXTPOWER
#define CONFIG_EXTPOWER
#endif

/* Support detecting external power presence via a GPIO */

/* Default debounce time for external power signal */
#ifndef CONFIG_EXTPOWER_DEBOUNCE_MS
#define CONFIG_EXTPOWER_DEBOUNCE_MS 30
#endif

/* Enable fake shared memory buffer, which is used by emulators. */

/*****************************************************************************/
/* Number of cooling fans. Undef if none. */

/* Percentage to which all fans are set at initiation */
#ifndef CONFIG_FAN_INIT_SPEED
#define CONFIG_FAN_INIT_SPEED 100
#endif

/* Allow board custom fan control */

/* Support fan control while in low-power idle */

/*
 * Fans have non-const configuration.
 */

/*
 * Fan config have non-const configuration.
 */

/*
 * Replace the default fan_percent_to_rpm() function with a board-specific
 * implementation in board.c
 */

/*
 * We normally check and update the fans once per second (HOOK_SECOND). If this
 * is #defined to a postive integer N, we will only update the fans every N
 * seconds instead.
 */

/*
 * Enable fan slow response control mechanism.
 * A specific type of fan needs a longer time to output the TACH
 * signal to EC after EC outputs the PWM signal to the fan.
 * During this period, the driver will read two consecutive RPM = 0.
 * In this case, don't step the PWM duty too aggressively
 */

/*****************************************************************************/
/* Flash configuration */

/* This enables console commands and higher-level features */
#ifndef CONFIG_FLASH_CROS
#define CONFIG_FLASH_CROS
#endif
/* This enables chip-specific access functions */
#ifndef CONFIG_FLASH_PHYSICAL
#define CONFIG_FLASH_PHYSICAL
#endif
/* Provide event log stored in flash memory. */
/* Allow deferred (async) flash erase */
/* Flash must be selected for write/erase operations to succeed. */

/* Base address of program memory */
/* Base address of program memory (physical address of AP) */

/* ec.bin image will be padded to match flash size. */
#ifndef CONFIG_IMAGE_PADDING
#define CONFIG_IMAGE_PADDING
#endif

/*
 * EC code can reside on internal or external storage. Only one of these
 * CONFIGs should be defined. CONFIG_INTERNAL_STORAGE implies XIP
 * (eXecute-In-Place) semantics. i.e. code is being fetched directly from
 * storage media.
 */

/*
 * Flash is directly mapped into the EC's address space.  If this is not
 * defined, the flash driver must implement flash_physical_read().
 */
#ifndef CONFIG_MAPPED_STORAGE
#define CONFIG_MAPPED_STORAGE
#endif

/*
 * Base address of memory-mapped flash storage, for platforms which define
 * CONFIG_MAPPED_STORAGE.
 */

/*
 * Some platforms need to write protect RW independently of all flash.
 */

/*
 * Enable Flash Write Protect by default. Some platforms like Servo_v4
 * development tools do not use write protection. This enables the feature
 * to be removed to save flash space
 */
#ifndef CONFIG_CMD_FLASH_WP
#define CONFIG_CMD_FLASH_WP
#endif

/*
 * Store persistent write protect for the flash inside the flash data itself.
 * This allows ECs with internal flash to emulate something closer to a SPI
 * flash write protect register.  If this is not defined, write protect state
 * is maintained solely by the physical flash driver.
 */
#ifndef CONFIG_FLASH_PSTATE
#define CONFIG_FLASH_PSTATE
#endif

/*
 * Store the pstate data in its own dedicated bank of flash.  This allows
 * disabling the protect-RO-at-boot flag without rewriting the RO firmware,
 * but costs a bank of flash.
 *
 * If this is not defined, the pstate data is stored inside the RO firmware
 * image itself.  This is more space-efficient, but the only way to clear the
 * flag once it's set is to rewrite the RO firmware (after removing the WP
 * screw, of course).
 */
#ifndef CONFIG_FLASH_PSTATE_BANK
#define CONFIG_FLASH_PSTATE_BANK
#endif

/*
 * Lock the PSTATE by default (currently only supported when
 * CONFIG_FLASH_PSTATE_BANK is not defined).
 */

/*
 * Enable readout protection.
 */

/*
 * Use Read-out protection status as PSTATE, i.e. after RDP is enabled, we never
 * allow RO protection to be disabled.
 *
 * This is used when we want to prevent read-back of some critical region (e.g.
 * rollback), even in DFU/BOOT0 mode.
 *
 * Note that this significantly changes the behaviour or flash protection,
 * as this tie EC_FLASH_PROTECT_RO_AT_BOOT with RDP status: it makes no
 * sense to be able to unlock RO protection if RDP is enabled, as a custom RO
 * could allow protected regions readback.
 *
 * TODO(crbug.com/888109): Implementation is currently only available on
 * STM32H7 and STM32F4, and requires more documentation.
 */

/*
 * For flash that is segemented in different regions.
 */
/* Number of regions of different size/type */

/* Total size of writable flash */

/* Minimum flash write size (in bytes) */
/* Most efficient flash write size (in bytes) */

/* Protected region of storage belonging to EC */

/* Writable region of storage belonging to EC */

/*****************************************************************************/
/* Fingerprint Sensor Configuration */

/*****************************************************************************/

/* Include a flashmap in the compiled firmware image */
#ifndef CONFIG_FMAP
#define CONFIG_FMAP
#endif

/* Allow EC serial console input to wake up the EC from STOP mode */

/* Enable warnings on FPU exceptions */

/*****************************************************************************/
/* Firmware region configuration */

/*
 * Read-only / read-write image configuration.
 * Images may reside on storage (ex. external or internal SPI) at a different
 * offset than when copied to program memory. Hence, two sets of offsets,
 * for STORAGE and for MEMORY.
 */
/* Offset relative to CONFIG_EC_PROTECTED_STORAGE_OFF */

/* Some targets include two RW sections in the image. */
/* This is the offset of the second RW section into the flash. */

/* Offset relative to CONFIG_EC_WRITABLE_STORAGE_OFF */

/*
 * Offset relative to CONFIG_EC_PROTECTED_STORAGE_OFF
 * These define a region of flash used to store ROM resident data objects
 * for RO images.  This is only possible when the program memory is smaller
 * than CONFIG_EC_PROTECTED_STORAGE_SIZE.
 */

/*
 * Offset relative to CONFIG_EC_WRITABLE_STORAGE_OFF
 * These define a region of flash used to store ROM resident data objects
 * for RW images.  This is only possible when the program memory is smaller
 * than CONFIG_EC_WRITABLE_STORAGE_SIZE.
 */

/*
 * NPCX-specific bootheader geometry.
 * TODO(crosbug.com/p/23796): Factor these CONFIGs out.
 */

/*
 * Support for saving extended reset flags in backup RAM.
 *
 * Please undefine it when RO firmware doesn't support extended reset flags.
 * Otherwise, compatibility between RO and RW will be broken, because
 * BKPDATA_INDEX_SAVED_RESET_FLAGS_2 was defined in the middle of bkpdata_index
 * enum.
 */
#ifndef CONFIG_STM32_EXTENDED_RESET_FLAGS
#define CONFIG_STM32_EXTENDED_RESET_FLAGS
#endif

/*
 * Write protect region offset / size. This region normally encompasses the
 * RO image, but may also contain additional images or data.
 */

/*
 * Rollback protect region. If CONFIG_ROLLBACK is defined to enable the rollback
 * protect region, CONFIG_ROLLBACK_OFF and CONFIG_ROLLBACK_SIZE must be defined
 * too.
 */

/* If defined, add support for storing some entropy in the rollback region. */

/* If defined, protect rollback region readback using MPU. */

/*
 * If defined, inject some locally generated entropy when secret is updated,
 * using board_get_entropy function.
 * Large values may take a long time to generate.
 *
 * This is only meant to add a little bit of extra entropy, when the hardware
 * lacks a random number generator (otherwise, the strong entropy can be
 * directly added to the secret, using rollback_add_entropy).
 */

/* If defined, we can update rollback information (RW can unset this). */
#ifndef CONFIG_ROLLBACK_UPDATE
#define CONFIG_ROLLBACK_UPDATE
#endif

/*
 * Current rollback version. Meaningless for RO (but provides the minimum value
 * that will be written to the rollback protection at flash time).
 *
 * For RW, rollback version included in version structure, used by RO to
 * determine if the RW image is recent enough and can be jumped to.
 *
 * Valid values are >= 0, <= INT32_MAX (positive, 32-bit signed integer).
 */
#ifndef CONFIG_ROLLBACK_VERSION
#define CONFIG_ROLLBACK_VERSION 0
#endif

/*
 * Board Image ec.bin contains a RO firmware.  If not defined, the image will
 * only contain the RW firmware.
 */
#ifndef CONFIG_FW_INCLUDE_RO
#define CONFIG_FW_INCLUDE_RO
#endif

/* If defined, another image (RW) exists with more features */

/*
 * If defined, we can use system_get_fw_reset_vector function to decide
 * reset vector of RO/RW firmware for sysjump.
 */

/*****************************************************************************/
/* Motion sensor based gesture recognition information */
/* These all require HAS_TASK_MOTIONSENSE to work */

/* Do we want to detect gestures? */

/* Mask of all sensors used for gesture dectections */

/* some gesture recognition done in software */

/* enable gesture host interface */

/* Sensor sampling interval for gesture recognition */

/* Enable double tap support. */

/*
 * Double tap detection parameters
 * Double tap works by looking for two isolated Z-axis accelerometer impulses
 * preceded and followed by relatively calm periods of accelerometer motion.
 *
 * Define an outer and inner window. The inner window specifies how
 * long the tap impulse is expected to last. The outer window specifies the
 * period before the initial tap impluse and after the final tap impulse for
 * which to check for relatively calm periods. In between the two impulses
 * there is a minimum and maximum interstice time allowed.
 *
 * Define an acceleration threshold to detect a tap, in mg.
 * Which sensor to look for double tap recognition.
 * Use for waking up host.
 */

/* Significant motion activity */

/*
 * Significant motion parameters
 * Sigmo state machine looks for movement, waits skip milli-seconds,
 * and check for movement again with proof milli-seconds.
 */

/* Support getting gpio flags. */

/*
 * GPU Drivers
 */

/* Do we want to detect the lid angle? */

/* Which sensor is located on the base? */
/* Which sensor is located on the lid? */
/*
 * Allows using the lid angle measurement to determine if peripheral devices
 * should be enabled or disabled, like key scanning, trackpad interrupt.
 */

/*
 * Defer the (re)configuration of motion sensors after the suspend event or
 * resume event.  Sensor power rails may be powered up or down asynchronously
 * from the EC, so it may be necessary to wait some time period before
 * reconfiguring after a transition.
 */
#ifndef CONFIG_MOTION_SENSE_SUSPEND_DELAY_US
#define CONFIG_MOTION_SENSE_SUSPEND_DELAY_US 0
#endif
#ifndef CONFIG_MOTION_SENSE_RESUME_DELAY_US
#define CONFIG_MOTION_SENSE_RESUME_DELAY_US 0
#endif

/* Define motion sensor count in board layer */

/* Define when LPC memory space needs to be populated. */

/******************************************************************************/
/* Host to RAM (H2RAM) Memory Mapping */

/* H2RAM Base memory address */

/* H2RAM Size */

/* H2RAM Host LPC I/O base memory address */

/* ISH boot start address */
/*
 * Define the minimal amount of time (in ms) betwen running motion sense task
 * loop.
 */
#ifndef CONFIG_MOTION_MIN_SENSE_WAIT_TIME
#define CONFIG_MOTION_MIN_SENSE_WAIT_TIME 3
#endif

/*****************************************************************************/
/*
 * Support the host asking the EC about the status of the most recent host
 * command.
 *
 * When the AP is attached to the EC via a serialized bus such as I2C or SPI,
 * it needs a way to minimize the length of time an EC command will tie up the
 * bus (and the kernel driver on the AP).  If this config is defined, the EC
 * may return an in-progress result code for slow commands such as flash
 * erase/write instead of stalling until the command finishes processing, and
 * the AP may then inquire the status of the current command and/or the result
 * of the previous command.
 */

/* clear bit(s) to mask reporting of an EC_HOST_EVENT_XXX event(s) */
#ifndef CONFIG_HOST_EVENT_REPORT_MASK
#define CONFIG_HOST_EVENT_REPORT_MASK 0xffffffffffffffffULL
#endif

/*
 * The host commands are sorted in the .rodata.hcmds section so use the binary
 * search algorithm to match a command to its handler
 */

/*
 * Host command parameters and response are 32-bit aligned.  This generates
 * much more efficient code on ARM.
 */

/*
 * Include host commands to fetch battery information from
 * ec_response_battery_static/dynamic_info structures, only makes sense when
 * CONFIG_BATTERY_INFO is enabled.
 */

/* If we have host command task, assume we also are using host events. */
#ifdef CONFIG_HAS_HOSTCMD
#ifndef CONFIG_HOSTCMD_EVENTS
#define CONFIG_HOSTCMD_EVENTS
#endif
#else
#endif

/*
 * Board supports host command to get EC SPI flash info.  This is typically
 * only needed if the factory needs to determine which of several possible SPI
 * flash chips is attached to the EC on a given board.
 */

/*
 * For ECs where the host command interface is I2C, peripheral
 * address which the EC will respond to.
 */

/*
 * Accept EC host commands over the SPI host interface.  The AP is SPI
 * controller and the EC is the SPI peripheral for this configuration.
 */

/*
 * Host command rate limiting assures EC will have time to process lower
 * priority tasks even if the AP is hammering the EC with host commands.
 * If there is less than CONFIG_HOSTCMD_RATE_LIMITING_MIN_REST between
 * host commands for CONFIG_HOSTCMD_RATE_LIMITING_PERIOD, then a
 * recess period of CONFIG_HOSTCMD_RATE_LIMITING_RECESS will be
 * enforced.
 */
#ifndef CONFIG_HOSTCMD_RATE_LIMITING_PERIOD
#define CONFIG_HOSTCMD_RATE_LIMITING_PERIOD (500 * MSEC)
#endif
#ifndef CONFIG_HOSTCMD_RATE_LIMITING_MIN_REST
#define CONFIG_HOSTCMD_RATE_LIMITING_MIN_REST (3 * MSEC)
#endif
#ifndef CONFIG_HOSTCMD_RATE_LIMITING_RECESS
#define CONFIG_HOSTCMD_RATE_LIMITING_RECESS (20 * MSEC)
#endif

/* PD MCU supports host commands */

/* EC supports EC_CMD_PD_CHIP_INFO */
#ifndef CONFIG_HOSTCMD_PD_CHIP_INFO
#define CONFIG_HOSTCMD_PD_CHIP_INFO
#endif

/* EC supports EC_CMD_TYPEC_DISCOVERY */
#ifndef CONFIG_HOSTCMD_TYPEC_DISCOVERY
#define CONFIG_HOSTCMD_TYPEC_DISCOVERY
#endif

/* EC supports EC_CMD_TYPEC_CONTROL */
#ifndef CONFIG_HOSTCMD_TYPEC_CONTROL
#define CONFIG_HOSTCMD_TYPEC_CONTROL
#endif

/* EC supports EC_CMD_TYPEC_STATUS */
#ifndef CONFIG_HOSTCMD_TYPEC_STATUS
#define CONFIG_HOSTCMD_TYPEC_STATUS
#endif

/*
 * Use if PD MCU controls charging (selecting charging port and input
 * current limit).
 */

/* Panic when status of PD MCU reflects that it has crashed */

/* Board supports RTC host commands */

/* EC controls the board's SKU ID and can report that to the AP */

/* Set SKU ID from AP */

/* Command to issue AP reset */

/* Command to issue AP shutdown */

/*
 * Support voltage regulator host command
 * If defined, the board should also implement board functions defined in
 * include/regulator.h
 */

/* Flash commands over PD */
#ifndef CONFIG_HOSTCMD_FLASHPD
#define CONFIG_HOSTCMD_FLASHPD
#endif

/* Host command to control USB-PD chip */

/* Set entry in PD MCU's device rw_hash table */
#ifndef CONFIG_HOSTCMD_RWHASHPD
#define CONFIG_HOSTCMD_RWHASHPD
#endif

/* Enable EC_CMD_LOCATE_CHIP */
#ifndef CONFIG_HOSTCMD_LOCATE_CHIP
#define CONFIG_HOSTCMD_LOCATE_CHIP
#endif

/* Command to get the EC uptime (and optionally AP reset stats) */
#ifndef CONFIG_HOSTCMD_GET_UPTIME_INFO
#define CONFIG_HOSTCMD_GET_UPTIME_INFO
#endif

/* Include host command to control I2C busses (get, set speed, etc.) */

/*
 * List of host commands whose debug output will be suppressed
 * By default remove periodic commands and commands called often (SENSE).
 */
#ifndef CONFIG_SUPPRESSED_HOST_COMMANDS
#define CONFIG_SUPPRESSED_HOST_COMMANDS                                        \
	EC_CMD_CONSOLE_SNAPSHOT, EC_CMD_CONSOLE_READ, EC_CMD_USB_PD_DISCOVERY, \
		EC_CMD_USB_PD_POWER_INFO, EC_CMD_PD_GET_LOG_ENTRY,             \
		EC_CMD_MOTION_SENSE_CMD, EC_CMD_GET_NEXT_EVENT,                \
		EC_CMD_GET_UPTIME_INFO
#endif

/*****************************************************************************/

/* Enable debugging and profiling statistics for hook functions */

/*****************************************************************************/
/* CRC configuration */

/* Enable the hardware accelerator for CRC computation */

/* Enable the software routine for CRC computation */

/*****************************************************************************/

/* Enable system hibernate */
#ifndef CONFIG_HIBERNATE
#define CONFIG_HIBERNATE
#endif

/* Default delay after shutting down before hibernating */
#ifndef CONFIG_HIBERNATE_DELAY_SEC
#define CONFIG_HIBERNATE_DELAY_SEC 3600
#endif

/* For ECs with multiple wakeup pins, define enabled wakeup pins */

/*
 * If defined, chip hibernation is used. Your board needs to define wake-up
 * signals. Undefine this to use board hibernation capability.
 */
#ifndef CONFIG_SUPPORT_CHIP_HIBERNATION
#define CONFIG_SUPPORT_CHIP_HIBERNATION
#endif

/*
 * Use PSL (Power Switch Logic) for hibernating. It turns off VCC power rail
 * for ultra-low power consumption and uses PSL inputs rely on VSBY power rail
 * to wake up ec and the whole system.
 */

/* Wake up pins have non-const configuration. */

/* In npcx9 and later chips, enhanced PSL features are supported including:
 *   (1) Pulse mode for PSL_OUT signal.
 *   (2) Open-drain for PSL_OUT signal (when Pulse mode is enabled.)
 * These features can be enabled in board configuration file by adding
 * the following bit masks to this flag:
 *   (1) NPCX_PSL_CFG_PSL_OUT_PULSE.
 *   (2) NPCX_PSL_CFG_PSL_OUT_OD.
 * Ex:  #define CONFIG_HIBERNATE_PSL_OUT_FLAGS	 \
		 (NPCX_PSL_CFG_PSL_OUT_PULSE | NPCX_PSL_CFG_PSL_OUT_OD)
 */

/*
 * Enable VCC1_RST pin as the input of PSL wakeup source. When Enabling this,
 * the VCC1_RST pin must be connected to the VSBY supply via an external pull-up
 * resistor of maximum 100K ohm .
 * TODO: Remove this when NPCX9 A2 chip is available because A2
 * chip will enable VCC1_RST to PSL wakeup source and lock it in
 * the booter.
 */

/*
 * Compensate the elapsed time for the RTC which couldn't work in hibernate PSL
 * after hibernation wake-up. Currently, NPCX9 supports LCT to compensate the
 * elapsed time for the RTC.
 */

/*
 * Chip supports a 64-bit hardware timer and implements
 * __hw_clock_source_read64 and __hw_clock_source_set64.
 *
 * Chips with this config enabled may optionally define
 * __hw_clock_source_read as a 32-bit set function for
 * latency-sensitive situations.
 */

/*****************************************************************************/
/* I2C configuration */

/*
 * Define this configuration to support smart battery MFG function
 * for virtual battery.
 */

/*
 * Define this option if an i2c bus may be unpowered at a certain point during
 * runtime.  An example could be, a sensor bus which is not needed in lower
 * power states so the power rail for those sensors is completely disabled.
 *
 * If defined, your board must provide a board_is_i2c_port_powered() function.
 */

/*
 * Conservative I2C transmission size per single transaction. For example,
 * register of stm32f0 and stm32l4 are limited to be 8 bits for this field.
 */
#ifndef CONFIG_I2C_CHIP_MAX_TRANSFER_SIZE
#define CONFIG_I2C_CHIP_MAX_TRANSFER_SIZE 255
#endif

/*
 * EC uses an I2C controller interface.
 * Note: if this is defined, i2c_init() will be called
 * automatically at board boot.
 */

/* EC uses an I2C peripheral interface */

/* Defines I2C operation retry count when slave nack'd(EC_ERROR_BUSY) */
#ifndef CONFIG_I2C_NACK_RETRY_COUNT
#define CONFIG_I2C_NACK_RETRY_COUNT 0
#endif
/*
 * I2C SCL gating.
 *
 * If CONFIG_I2C_SCL_GATE_ADDR/PORT is defined, whenever the defined address
 * is addressed, CONFIG_I2C_SCL_GATE_GPIO is set to high. When the I2C
 * transaction is done, the pin is set back to low.
 */

/*
 * Some chip supports two owned slave address. The second slave address is used
 * for other purpose such as board specific i2c commands. This option can be
 * set if user of the second slave address requires larger host packet buffer
 * size.
 */
#ifndef CONFIG_I2C_EXTRA_PACKET_SIZE
#define CONFIG_I2C_EXTRA_PACKET_SIZE 0
#endif

/*
 * If defined, reduce I2C traffic from update functions (i2c_update8/16
 * and i2c_field_update8/16) by skipping the write if the new value is
 * unchanged from the old value. This assumes no side effects from writing an
 * unchanged value back out.
 */

/*
 * Packet error checking support for SMBus.
 *
 * If defined, adds error checking support for i2c_readN, i2c_writeN,
 * i2c_read_string and i2c_write_block. Where
 * - write operation appends an error checking byte at end of transfer, and
 * - read operatoin verifies the correctness of error checking byte from the
 * slave.
 * Set I2C_FLAG on addr_flags parameter to use this feature.
 *
 * This option also enables error checking function on smart batteries.
 */

/*
 * The buffer size for i2c_read_sized_block. Set this to 256 could ensure
 * that i2c_read_sized_block only call i2c_xfer_unlocked twice each try.
 */
#ifndef CONFIG_I2C_READ_SIZE_BUFFER
#define CONFIG_I2C_READ_SIZE_BUFFER 1
#endif

/*
 * Add hosts-side support for entering programming mode for I2C ITE ECs.
 * Must define ite_dfu_config_t for configuration in board file.
 */

/*****************************************************************************/
/* IPI configuration.  Support mt_scp only for now. */

/* EC support Inter-Processor Interrupt. */

/*
 * IPC0/IPI shared object address. This is the starting address of the send
 * object and the receive object.  Each object contains a buffer.
 */

/* "buffer" size of ipc_shared_obj. */

/* EC support rpmsg name service over IPI. */

/*****************************************************************************/
/* Current/Power monitor */

/*
 * Compile driver for INA219 or INA231 or INA3221.
 * Only one of these may be defined (if any).
 */

/*****************************************************************************/
/* Inductive charging */

/* Enable inductive charging support */

/******************************************************************************/

/* Support CCGXXF I/O expander built inside PD chip */

/*
 * Support IT8801 I/O expander.
 *
 * I2C address KB_DISCRETE_I2C_ADDR_FLAGS and I2C port
 * I2C_PORT_KB_DISCRETE must be defined as well.
 * Note: these values are only used when accessing the keyboard and PWM
 * function of the IT8801 chip.  I/O expander functions are accessed using
 * the ioex_config[] array.
 */

/* Support Nuvoton NCT38xx I/O expander. */

/* Support NXP PCA9534 I/O expander. */

/* Support NXP PCA9675 I/O expander. */

/* Support NXP PCAL6408 I/O expander. */

/* Support TI TCA64xxA I/O expander. */

/* Number of IO Expander ports */

/*****************************************************************************/

/* Number of IRQs supported on the EC chip */

/* Enable LDN for KBC mouse */

/*
 * Enable more ILM to cache chip's drivers to reduce latency in fetching
 * instructions from internal flash.
 */
/* IT8320 hardware timer driver runs from ILM */
/* I2C controller runs from ILM */
/* IT8320 I2C driver runs from ILM */
/* IT8320 IRQ driver runs from ILM */
/* IT8320 UART driver runs from ILM */
/* USBC PD timer runs from ILM */
/* TCPCI driver runs from ILM */
/* Timer runs from ILM */
/* RAA489000 driver runs from ILM */

/*
 * The IT8320 supports e-flash clock up to 48 MHz (IT8390 maximum is 32 MHz).
 * Enable it if we want better performance of fetching instruction from e-flash.
 *
 * This is valid with PLL frequency equal to 48/96MHz only.
 */

/*
 * If this option is enabled, EC will assert GPG1 pin to reset itself instead of
 * triggering an internal reset while receiving a reset request.
 *
 * IMPORTANT:
 * - Don't enable this option if board doesn't support the mechanism.
 * - If this option is enabled, please don't declare GPG1 signal in gpio.inc to
 *   keep its output level is low after reset.
 */

/*
 * Use i2c command queue mode for a single i2c transaction.
 * (Applied to port D, E, and F)
 */

/*
 * Enable it if EC's VBAT won't go low when system's power isn't
 * presented (no battery and no AC)
 * If EC's VSTBY and VBAT(power source of BRAM) aren't connected to the same
 * power rail and VBAT doesn't go low immediately (eg: there is a larger
 * capacitance on the rail) after all power off: PD contract recorded in BRAM
 * won't get cleared (But actually we have unplugged type-c adaptor, so the
 * contract should be cleared).
 */

/* To define it, if I2C channel C and PECI used at the same time. */

/*
 * Enable board to tune cc physical parameters (ex.rising, falling time).
 * NOTE: board must define board_get_cc_tuning_parameter(enum usbpd_port port)
 *       function.
 */

/*
 * Enable the corresponding config option, according to EC's VCC is connected
 * to 1.8V or 3.3V
 */

/*
 * Overwrite integer multiplication and division arithmetic library routines
 * with using hardware multiplication and division and nop instructions.
 */

/*
 * Allows pre-watchdog timer interrupt to be unmasked even when all interrupts
 * are disabled.
 * NOTE: this config is only relevant for non-zephyr boards with NDS32 arch
 */

/*
 * For IT83xx boards shipped with Watchdog timer locked in RO this config
 * allows to extend the duration of Watchdog timer by resetting it once.
 */

/*
 * Support the standard integer multiplication and division instruction
 * extension.
 */
#ifndef CONFIG_RISCV_EXTENSION_M
#define CONFIG_RISCV_EXTENSION_M
#endif

/*
 * If this is not defined, the firmware will revert the JTAG selection
 * triggered by the hardware strap pin.
 * Un-define this flag by default for all real platforms. see (b/129908668)
 * If some boards (Ex:EVB) require JTAG function, they can define it in
 * their board.h
 */

/*****************************************************************************/
/* Keyboard config */

/*
 * The Silego reset chip sits in between the EC and the physical keyboard on
 * column 2.  To save power in low-power modes, some Silego variants require
 * the signal to be inverted so that the open-drain output from the EC isn't
 * costing power due to the pull-up resistor in the Silego.
 */

/*
 * Keyboards with the assistant key also move the refresh key matrix to row 3
 * instead of row 2.  This is used by the boot key detection code to determine
 * if the refresh key is held down at boot.
 */

/*
 * Config KSO to start from a different KSO pin. This is to allow some chips
 * to use alternate functions on KSO pins.
 */
#ifndef CONFIG_KEYBOARD_KSO_BASE
#define CONFIG_KEYBOARD_KSO_BASE 0
#endif

/* Enable extra debugging output from keyboard modules */

/*
 * Disables the directly connected keyboard pins and drivers on a particular
 * chip. You might want this enabled if the keyboard is indirectly connected
 * to the EC, perhaps through an I2C controller.
 */

/* The board uses a negative edge-triggered GPIO for keyboard interrupts. */

/* Compile code for 8042 keyboard protocol */

/*
 * Enable code for chromeos vivaldi keyboard (standard for new chromeos devices)
 * This config only takes effect if CONFIG_KEYBOARD_PROTOCOL_8042 is selected. A
 * chromeos device is Vivaldi compatible if the keyboard matrix complies with:
 * go/vivaldi-matrix
 * Vivaldi code enables:
 *  - A response to EC_CMD_GET_KEYBD_CONFIG command from coreboot
 *  - Boards can specify their custom layout for top keys.
 */
/* Keyboard features */
#ifndef CONFIG_KEYBOARD_VIVALDI
#define CONFIG_KEYBOARD_VIVALDI
#endif

/* Compile code for MKBP keyboard protocol */

/* Support keyboard factory test scanning */

/*
 * Support for boot key combinations (e.g. refresh key being held on boot to
 * trigger recovery).
 */
#ifndef CONFIG_KEYBOARD_BOOT_KEYS
#define CONFIG_KEYBOARD_BOOT_KEYS
#endif

/* Add support for the assistant key. */

/* Add support for a switch that indicates if the device is in tablet mode. */

/* Add support for skipping lid close when the system into tablet mode. */

/* Add hall control to power on/off the WPC.*/

/*
 * The debounce time for the WPC HALL
 * Default is 30ms.
 */
#ifndef CONFIG_WPC_HALL_DEBOUNCE_US
#define CONFIG_WPC_HALL_DEBOUNCE_US (30 * MSEC)
#endif

/* Add AC S5 charge feature for the WPC */

/*
 * Minimum CPU clocks between scans.  This ensures that keyboard scanning
 * doesn't starve the other EC tasks of CPU when running at a decreased system
 * clock.
 */

/*  Print keyboard scan time intervals. */

/*
 * Support for extra runtime key combinations (e.g. alt+volup+h/r for hibernate
 * and warm reboot, respectively).
 */
#ifndef CONFIG_KEYBOARD_RUNTIME_KEYS
#define CONFIG_KEYBOARD_RUNTIME_KEYS
#endif

/* Add support for ADC based antighost feature */

/*
 * Allow the board layer keyboard customization. If define, the board layer
 * needs to implement:
 * 1. the function board_keyboard_drive_col() which is used to control
 *    the refresh key column.
 * 2. the scancode_set2 and keycap_label array
 * 3. keyboard_customization.h which is similar to keyboard_config.h
 *
 * Note that if your board has the standard chromeos keyboard layout other
 * than the top row, and you are looking only for top row customization, then
 * you should be looking at overriding board_vivaldi_keybd_config() instead.
 */

/*
 * Allow support multiple keyboard matrix for special key.
 */

/*
 * Enable keyboard testing functionality. This enables a message which receives
 * a list of keyscan events from the AP and processes them.  This will cause
 * keypresses to appear on the AP through the same mechanism as a normal
 * keyboard press.
 *
 * This can be used to spoof keyboard events, so is not normally defined,
 * except during internal testing.
 */

/*
 * Enable quasi-bidirectional buffers for KSO pins. It has an open-drain output
 * and a low-impedance pull-up. The low-impedance pull-up is active when ec
 * changes the output data buffers from 0 to 1, thereby reducing the
 * low-to-high transition time.
 */

/*
 * Enable keypad (a palm-sized keyboard section usually placed on the far right)
 */

/*
 * Enable strict debouncer. A strict debouncer waits until debounce is done
 * before registering key up/down while a non-strict debouncer registers a key
 * up/down as soon as a key is pressed or released.
 *
 * A strict debouncer is robust against unintentional key presses, caused by a
 * device drop, for example. However, its latency isn't as fast as a non-strict
 * debouncer.
 *
 * If a strict debouncer is used, it's recommended to set debounce_down_us and
 * debounce_up_us to an equal value. This guarantees key events are registered
 * in the order the keys are pressed.
 */

/*
 * Enable Strauss keyboard.
 */

/*
 * Invert the IRQ1/IRQ12 interrupts that come from the NPCX keyboard controller
 * such that they are active low.
 */

/*****************************************************************************/

/*
 * Enable IT8801 pwm module.
 */

/*****************************************************************************/

/* Support common LED interface */

/*
 * Support common PWM-controlled LEDs that do not conform to the Chrom OS LED
 * behavior specification
 */

/*
 * Here are some recommended color settings by default, but a board can change
 * the colors to one of "enum ec_led_colors" as they see fit.
 */
#ifndef CONFIG_LED_PWM_CHARGE_COLOR
#define CONFIG_LED_PWM_CHARGE_COLOR EC_LED_COLOR_AMBER
#endif
#ifndef CONFIG_LED_PWM_NEAR_FULL_COLOR
#define CONFIG_LED_PWM_NEAR_FULL_COLOR EC_LED_COLOR_GREEN
#endif
#ifndef CONFIG_LED_PWM_CHARGE_ERROR_COLOR
#define CONFIG_LED_PWM_CHARGE_ERROR_COLOR EC_LED_COLOR_RED
#endif
#ifndef CONFIG_LED_PWM_SOC_ON_COLOR
#define CONFIG_LED_PWM_SOC_ON_COLOR EC_LED_COLOR_GREEN
#endif
#ifndef CONFIG_LED_PWM_SOC_SUSPEND_COLOR
#define CONFIG_LED_PWM_SOC_SUSPEND_COLOR EC_LED_COLOR_GREEN
#endif
#ifndef CONFIG_LED_PWM_LOW_BATT_COLOR
#define CONFIG_LED_PWM_LOW_BATT_COLOR EC_LED_COLOR_AMBER
#endif

/* By default, 500 ms period, 50% duty cycle. */
#define LED_CHARGER_ERROR_ON_TIME 1
#define LED_CHARGER_ERROR_PERIOD 2

/*
 * By default the PWM LED behaviour is reflected on both LEDs and includes the
 * chipset state, battery state, as well as the charging state.  Enable
 * this CONFIG_* option to show only the charging state on the LEDs.
 */

/*
 * By default the PWM LED behaviour is reflected on both LEDs and includes the
 * chipset state, battery state, as well as the charging state.  Enable
 * this CONFIG_* option to show only the charging state, and only on the LED of
 * the active charge port.
 */

/*
 * How many PWM LEDs does the system have that will be controlled by the common
 * PWM LED policy?  Currently, this may be at most 2.
 */

/*
 * Support GPIO-controlled LEDs for common battery/power
 * states through a board-defined lookup table.
 */

/*
 * Set the battery charge percentage for optional STATE_DISCHARGE_S0_BAT_LOW
 * provided by CONFIG_LED_ONOFF_STATES.
 */

/*
 * LEDs for LED_POLICY STD may be inverted.  In this case they are active low
 * and the GPIO names will be GPIO_LED..._L.
 */

/* Support for LED driver chip(s) */

/* Offset in flash where little firmware will live. */

/*
 * Compile lid switch support.
 *
 * This is enabled by default because all boards other than reference boards
 * are for laptops with lid switchs.  Reference boards #undef it.
 */
#ifndef CONFIG_LID_SWITCH
#define CONFIG_LID_SWITCH
#endif

/*
 * GPIOs to use to detect that the lid is opened.
 *
 * This is a X-macro composed of a list of LID_OPEN(GPIO_xxx) elements defining
 * all the GPIOs to check to find whether the lid is currently opened.
 * If not defined, it is using GPIO_LID_OPEN.
 */

/*
 * Lid swtich debounce timing customize.
 * Default is 30ms.
 */
#ifndef CONFIG_LID_DEBOUNCE_US
#define CONFIG_LID_DEBOUNCE_US (30 * MSEC)
#endif

/*
 * Support for turning the lightbar power rails on briefly when the AP is off.
 * Enabling this requires implementing the board-specific lb_power() function
 * to do it (see lb_common.h).
 */

/*
 * For tap sequence, show the last segment in dim to give a better idea of
 * battery percentage.
 */

/*
 * Adds a console command for testing the long long shift right ABI on Cortex-m4
 * (Cr50).
 */

/* Program memory offset for little firmware loader. */

/* Size of little firmware loader. */

/* Little firmware loader storage offset. */

/*
 * Low power idle options. These are disabled by default and all boards that
 * want to use low power idle must define it. When using the LFIOSC, the low
 * frequency clock will be used to conserve even more power when possible.
 *
 * GPIOs which need to trigger interrupts in low power idle must specify the
 * GPIO_INT_DSLEEP flag in gpio_list[].
 *
 * Note that for some processors (e.g. LM4), an active JTAG connection will
 * prevent the EC from using low-power idle.
 */

/* Allows us to enable/disable low power idle mode in runtime. */

/*
 * Enable deep sleep during S0 (ignores SLEEP_MASK_AP_RUN).
 */

/* DMA paging between SRAM and DRAM */

/*
 * Enable HID subsystem using HECI on Intel ISH (Integrated Sensor Hub)
 */

/* Support host command interface over HECI */

/*
 * EC supports x86 host communication with AP. This can either be through LPC
 * or eSPI. The CONFIG_HOSTCMD_X86 will get automatically defined if either
 * CONFIG_HOST_INTERFACE_LPC or CONFIG_HOST_INTERFACE_ESPI are defined.
 * LPC and eSPI are mutually exclusive.
 */
/* Support host command interface over LPC bus. */
/* Support host command interface over eSPI bus. */
/* Support host command interface over USB. */

/*
 * SLP signals (SLP_S3, SLP_S4, and SLP_S5) use virtual wires instead of
 * physical pins with eSPI interface.
 */

/* MCHP next two items are EC eSPI slave configuration */
/* Maximum clock frequence eSPI EC slave advertises
 * Values in MHz are 20, 25, 33, 50, and 66
 */

/* EC eSPI slave advertises IO lanes
 * 0 = Single
 * 1 = Single and Dual
 * 2 = Single and Quad
 * 3 = Single, Dual, and Quad
 */

/* Bit map of eSPI channels EC advertises
 * bit[0] = 1 Peripheral channel
 * bit[1] = 1 Virtual Wire channel
 * bit[2] = 1 OOB channel
 * bit[3] = 1 Flash channel
 */

/*
 * Background information (from Intel eSPI Compatibility Specification):
 * eSPI_Reset# may be asserted as part of:
 * (1) a normal Deep-Sx entry:
 *    A normal eSPI_Reset# assertion is preceded by {Host,OOB}_Reset_Warn/Ack
 *    handshakes (using tunneled VWs) between the PCH/SoC and the EC/BMC.
 *    The eSPI Specification states that the SLP_* signals are reset based on
 *    eSPI_Reset#. However, for platforms that support Deep Sleep Well (DSW),
 *    the SLP_{S3,S4,S5,LAN,WLAN}# signals reside in the DSW power well and are
 *    reset by DSW_PWROK.
 *    In PCH/SoC, the states of these pins will be communicated to the EC/BMC
 *    as Virtual Wires over the eSPI interface. As a result, the EC/BMC needs
 *    to handle/maintain these pins' states during Deep-Sx.
 *
 * (2) a Global Reset event:
 *    It could happen in the middle of an on-going eSPI transaction, which is
 *    immediately truncated. All tunneled VWs, including
 *    SLP_{S3,S4,S5,LAN,WLAN}#, are returned to their default reset default
 *    state upon entry into Global Reset. Note that in the case of a Global
 *    Reset event, eSPI Virtual Wire messages deasserting the states of these
 *    wires will not be issued by the eSPI-MC. The eSPI Slave device is
 *    responsible for resetting the states of all its VWs at the appropriate
 *    platform reset events.
 *
 * Enable this config to reset SLP* VW when eSPI_RST is asserted for the Global
 * Reset event case.
 * Don't enable this config if the platform implements the Deep-Sx entry as EC
 * needs to maintain these pins' states per request.
 */

/* Base address of low power RAM. */

/* Size of low power RAM. */

/* Provide rudimentary malloc/free like services for shared memory. */

/* Need for a math library */

/* Include code to do online compass calibration */

/* Microchip LPC enable debug messages */

/* Microchip I2C controller slave addresses */

/* Microchip EC SRAM start address */

/* Microchip EC SRAM end address */

/* Microchip EC SRAM size */

/*
 * Define Megachips DisplayPort to HDMI protocol converter/level shifter serial
 * interface.
 */

/* Minute-IA watchdog timer vector number. */
#ifndef CONFIG_MIA_WDT_VEC
#define CONFIG_MIA_WDT_VEC 0xFF
#endif

/*
 * ISL9241 Configures the switching frequency and overrides the default
 * switching frequency set by PROG pin. The valid frequency settings are
 * find in driver/charger/isl9241.h.
 */

/*
 * ISL9238C disable the CMOUT latch function.
 */

/*
 * ISL9238C input voltage setting.
 * Set the input voltage for the ISL9238C charger. Setting -1 means use
 * the default setting defined by the chip.  The ISL9238C input voltage
 * is configured using 341.3 mV steps.  The value specified is rounded
 * down.
 */
#ifndef CONFIG_ISL9238C_INPUT_VOLTAGE_MV
#define CONFIG_ISL9238C_INPUT_VOLTAGE_MV -1
#endif

/*
 * ISL9238C enable Force Buck mode.
 */

/* ISL9238C adjusts phase comparator threshold offset */
#ifndef CONFIG_ISL9238C_BUCK_PHASE_VOLTAGE
#define CONFIG_ISL9238C_BUCK_PHASE_VOLTAGE 0
#endif

/* Support MKBP event */

/* MKBP events are sent by using host event */

/* MKBP events are sent by using GPIO */

/* MKBP events GPIO is active high */

/*
 * MKBP events are notified by using both a GPIO and a host event.
 *
 * You should use this if you are using a GPIO to notify the AP of an MKBP
 * event, and you need an MKBP event to wake the AP in suspend and the AP cannot
 * wake from the GPIO.  Since you are using both a GPIO and a hostevent for the
 * notification, make sure that the S0 hostevent mask does NOT include MKBP
 * events.  Otherwise, you will have multiple consumers for a single event.
 * However, make sure to configure the host event *sleep* mask in coreboot to
 * include MKBP events.  In order to prevent all MKBP events from waking the AP,
 * use CONFIG_MKBP_EVENT_WAKEUP_MASK to filter the events.
 */

/* MKBP events are sent by using HECI on an ISH */

/* MKBP events are sent by using custom method */

/*
 * If using MKBP to send host events, with this option, we can define the host
 * events that should wake the system in suspend.  Some examples are:
 *
 *    EC_HOST_EVENT_MASK(EC_HOST_EVENT_LID_OPEN)
 *    EC_HOST_EVENT_MASK(EC_HOST_EVENT_KEY_PRESSED)
 *
 * The only things that should be in this mask are:
 *    EC_HOST_EVENT_MASK(EC_HOST_EVENT_*)
 */

/*
 * Define which MKBP events should wakeup the system in suspend.  Some examples
 * are:
 *
 *    EC_MKBP_EVENT_KEY_MATRIX
 *    EC_MKBP_EVENT_SWITCH
 *
 * The only things that should be in this mask are EC_MKBP_EVENT_*.
 */

/*
 * Send button, switch and sysrq events via MKBP protocol to the host.
 */

/* Support RAM write/fetch protection */

/* Do not try hold I/O pins at frozen level during deep sleep */

/* Support one-wire interface */

/* Use OTP as a source of key material. */

/*
 * Address to store persistent panic data at. By default, this will be
 * at the end of RAM, and have a size of sizeof(struct panic_data)
 */

/* Common code for PECI interface to x86 processor */

/*
 * Maximum operating temperature in degrees Celcius used on some x86
 * processors. CPU chip temperature is reported relative to this value and
 * is never reported greater than this value. Processor asserts PROCHOT#
 * and starts throttling frequency and voltage at this temp. Operation may
 * become unreliable if temperature exceeds this limit.
 */

/*
 * Enable peripheral charge manager (e.g. NFC/WLC, WPC Qi)
 */

/*
 * Enable CTN730 driver
 *
 * CTN730 is NXP's NFC/WLC power transmitter (a.k.a. poller).
 */

/*****************************************************************************/
/* PMU config */

/*
 * Enable hard-resetting the PMU from the EC.  The implementation is rather
 * hacky; it simply shorts out the 3.3V rail to force the PMIC to panic.  We
 * need this unfortunate hack because it's the only way to reset the I2C engine
 * inside the PMU.
 */

/*
 * Enable this config to make console UART self sufficient (no other
 * initialization required before uart_init(), no interrupts, uart_tx_char()
 * does not exit until character finished transmitting).
 *
 * This is useful during early hardware bringup, each platform needs to
 * implement its own code to support this.
 */

/* Define length of history buffer for port80 messages. */
#ifndef CONFIG_PORT80_HISTORY_LEN
#define CONFIG_PORT80_HISTORY_LEN 128
#endif

/*
 * Enable/Disable printing of port80 messages in interrupt context. By default,
 * this is disabled.
 */
#ifndef CONFIG_PORT80_PRINT_IN_INT
#define CONFIG_PORT80_PRINT_IN_INT 0
#endif

/*
 * Allow Port80 common layer to dump 4-byte Port80 code. This is only supported
 * on NPCX9 (and latter) chips.
 */

/* MAX695x 7 segment driver */

/* Config for power states and port80 message to be displayed on 7 -segment */

/* Compile common code to support power button debouncing */

/* Configure power button. e.g. BUTTON_FLAG_ACTIVE_HIGH */

/* Allow the power button to send events while the lid is closed */

/* Support sending the power button signal to x86 chipsets */

/* Set power button state idle at init. Implemented only for npcx. */

/* Timeout before power button task gives up starting system */
#ifndef CONFIG_POWER_BUTTON_INIT_TIMEOUT
#define CONFIG_POWER_BUTTON_INIT_TIMEOUT 1
#endif

/*
 * Enable delay between DSW_PWROK and PWRBTN assertion.
 * If enabled, DSW_PWROK_TO_PWRBTN_US and get_time_dsw_pwrok must be defined
 * as well.
 */

/*
 * The time in usec required for PMC to be ready to detect power button press.
 * Refer to the timing diagram for G3 to S0 on PDG for details.
 */
#ifndef CONFIG_DSW_PWROK_TO_PWRBTN_US
#define CONFIG_DSW_PWROK_TO_PWRBTN_US (95 * MSEC)
#endif

/*
 * Board provides board_pwrbtn_to_pch function instead of GPIO_PCH_PWRBTN_L
 * as the means for asserting power button signal to PCH.
 */

/*
 * Enable power button press host command.
 */

/* Compile common code for AP power state machine */

/* Enable a task-safe way to control the PP5000 rail. */

/* Support stopping in S5 on shutdown */

/*
 * Detect power signal interrupt storms, defined as more than
 * CONFIG_POWER_SIGNAL_INTERRUPT_STORM_DETECT_THRESHOLD occurences of a single
 * power signal interrupt within one second.
 */

/* Use part of the EC's data EEPROM to hold persistent storage for the AP. */

/* Support S0ix */

/* Advertise S4 residency */

/* Support detecting failure to enter a sleep state (S0ix/S3) */

/*
 * Allow the host to self-report its sleep state, in case there is some delay
 * between the host beginning to enter the sleep state and power signals
 * actually reflecting the new state.
 */

/*
 * Allow the use of the "long" printf length modifier ('l') to be in 32-bit
 * systems along with any supported conversion specifiers. Note that this also
 * reenables support for the 'i' printf format. This config will only take
 * effect if sizeof(long) == sizeof(uint32_t).
 */

/*
 * On x86 systems, define this option if the CPU_PROCHOT signal is active low.
 * This setting also applies to monitoring the PROCHOT input if provided by
 * the board.
 */

/*
 * When the AP enters C10, the power rails VCCIO, VCCSTG, and VCCPLL_OC may be
 * turned off by the board.  If the PROCHOT# signal is pulled up by any of
 * these rails, PROCHOT cannot be relied upon while C10 is active.
 * Enable this option to gate PROCHOT detection when C10 is active.
 */

/* Support Power Sourcing Equipment */

/*
 * Define this option to enable programmable voltage detector which will
 * trigger an interrupt when the voltage drops below a threshold specified
 * by the PVD_THRESHOLD which is a chip specific voltage threshold that
 * must be defined in board.h.
 */

/*****************************************************************************/

/* Define clock input to PWM module. */

/*****************************************************************************/
/* Support PWM output to display backlight */

/*
 * Support keyboard backlight control
 *
 * You need to define board_kblight_init unless CONFIG_PWM_KBLIGHT is used.
 * For example, lm3509 can be registered as a driver in board_kblight_init.
 */

/*
 * Support PWM output to keyboard backlight
 *
 * This implies CONFIG_KEYBOARD_BACKLIGHT.
 */

/*
 * Support a GPIO enable pin (set as GPIO_EN_KEYBOARD_BACKLIGHT) for the
 * keyboard backlight.
 */

/*
 * Call keyboard backlight init function during init hook instead of start-up
 */

/* Size of each RAM bank in chip, default is CONFIG_RAM_SIZE */

/*
 * Number of RAM banks in chip, default is
 * CONFIG_RAM_SIZE / CONFIG_RAM_BANK_SIZE
 */

/* Base address of RAM for the chip */

/*
 * Base address of ROM for the chip. Only used in no physical flash case (
 * !CONFIG_FLASH_PHYSICAL).
 */

/*
 * CONFIG_DATA_RAM_SIZE and CONFIG_RAM_SIZE indicate size of all data RAM
 * available on the chip in bytes and size of data RAM available for EC in
 * bytes, respectively.
 * Usually, CONFIG_DATA_RAM_SIZE = CONFIG_RAM_SIZE but some chips need to
 * allocate RAM for the mask ROM. Then CONFIG_DATA_RAM_SIZE > CONFIG_RAM_SIZE.
 *
 * CONFIG_CODE_RAM_SIZE indicates the size of all code RAM available on the chip
 * in bytes.  This is needed when a chip with external storage where stored with
 * code section, or a chip without an internal flash but need to protect its
 * code section by MPU.
 * Usually, CONFIG_CODE_RAM_SIZE = CONFIG_RO_SIZE.  However, some chips may
 * have other value, e.g. mt_scp which doesn't have RO image, and the code RAM
 * size is actually its CONFIG_ROM_SIZE plus a reserved memory space.
 *
 * CONFIG_ROM_SIZE indicates the size of ROM allocated by a linker script.  This
 * is only needed when no physical flash present (!CONFIG_FLASH_PHYSICAL).  The
 * ROM region will place common RO sections, e.g. .text, .rodata, .data LMA etc.
 */

/* Support IR357x Link voltage regulator debugging / reprogramming */

/* Support RMA auth challenge-response */

/*
 * Use the p256 curve for RMA challenge-response calculations (x21559 is used
 * by default).
 */

/* Enable hardware Random Number generator support */

/* Support verifying 2048-bit RSA signature */

/* Define the RSA key size. */

/* Use RSA exponent 3 instead of F4 (65537) */

/*
 * Adjust the compiler optimization flags for the RSA code to get a speed-up
 * at the expense of a small code size delta.
 */

/*
 * Verify the RW firmware using the RSA signature.
 * (for accessories without software sync)
 */

/*
 * Disable rwsig jump when the reset source is hard pin-reset. This only work
 * for the case where rwsig task is not used.
 */

/*
 * When RWSIG verification is performed as a task, time to wait from signature
 * verification to an automatic jump to RW (if AP does not request the wait to
 * be interrupted).
 */
#ifndef CONFIG_RWSIG_JUMP_TIMEOUT
#define CONFIG_RWSIG_JUMP_TIMEOUT (1000 * MSEC)
#endif

/*
 * Defines what type of futility signature type should be used.
 * RWSIG should be used for new designs.
 * Old adapters use the USBPD1 futility signature type.
 */

/*
 * By default the pubkey and sig are put at the end of the first and second
 * half of the total flash, and take up the minimum space possible. You can
 * override those defaults with these.
 */

/* Size of the serial number if needed */

/* Support programmable Mac address field. */

/* Size of the MAC address field if needed. */

/* Support programmable device poweron config. */

/* Size of the poweron config field if needed. */

/* Allow the board to use a GPIO for the SCI# signal. */

/* Support computing of other hash sizes (without the VBOOT code) */

/* Compute SHA256 by using chip's hardware accelerator */

/* Unroll some loops in SHA256_transform for better performance. */

/* Emulate the CLZ (Count Leading Zeros) in software for CPU lacking support */

/* Emulate the CLZ (Count Trailing Zeros) in software for CPU lacking support */

/* Support smbus interface */
/*
 * Deprecated in
 * https://chromium-review.googlesource.com/c/chromiumos/platform/ec/+/1704279
 *
 * It hasn't been used in over 2 years
 * https://chromium-review.googlesource.com/c/chromiumos/platform/ec/+/452459/
 * and was only used by one board (pyro).
 *
 * I2C and SMBus are compatible at the physical layer. The data transfer
 * paradigm is different. Some of our batteries are using SMbus style
 * transfers now, they are just using i2cxfer directly to accomplish it.
 *
 * I doubt the SMBus code will get revived, but we do have it in revision
 * history if we ever need it.
 */
/* #undef CONFIG_SMBUS */

/* Define the SPI port to use to access SPI accelerometer */

/* Support SPI flash */

/* Support SPI flash protection register translation */

/* Define the SPI port to use to access the flash */

/* Select any of the following SPI flash configs that your board uses. */

/* SPI flash part supports SR2 register */

/* Define the SPI port to use to access the fingerprint sensor */

/* Enable SPI_NOR debugging providing additional console output while
 * initializing Serial NOR Flash devices including SFDP discovery. */

/* Maximum Serial NOR flash command size, in Bytes */

/* Maximum Serial NOR flash read size, in Bytes */

/* Maximum Serial NOR flash write size, in Bytes. Note this must be a power of
 * two. */

/* If defined will enable block (64KiB) erase operations. */

/* If defined will read the sector/block to be erased first and only initiate
 * the erase operation if not already in an erased state. The read operation
 * (performed in CONFIG_SPI_NOR_MAX_READ_SIZE chunks) is aborted early if a
 * non "0xff" byte is encountered.
 * !! Make sure there is enough stack space to host a
 * !! CONFIG_SPI_NOR_MAX_READ_SIZE sized buffer before enabling.
 */

/* SPI controller feature */

/* SPI controller halfduplex/3-wire mode */

/* Support STM32 SPI1 as controller. */

/* Support MCHP MEC family GP-SPI master(s)
 * Define to 0x01 for GPSPI0 only.
 * Define to 0x02 for GPSPI1 only.
 * Define to 0x03 for both controllers.
 */

/*
 * Configure SPI flash read wait time as 1ms
 * Chip or board can redefine it per design
 */
#ifndef CONFIG_SPI_FLASH_READ_WAIT_MS
#define CONFIG_SPI_FLASH_READ_WAIT_MS 1
#endif

/*
 * Allow modification to e.g. clock divisor or other fields of spi_devices[].
 */

/* Default stack size to use for tasks, in bytes */

/* Compile charger detect for STM32 */

/* Fake hibernate mode */

/*
 * Compile common code to handle simple switch inputs such as the recovery
 * button input from the servo debug interface.
 */

/* Support dedicated recovery signal from servo board */

/*
 * System should remain unlocked even if write protect is enabled.
 *
 * NOTE: This should ONLY be defined during bringup, and should never be
 * defined on a shipping / released platform.
 *
 * When defined, CBI allows ectool to reprogram all the fields. Once undefined,
 * it refuses to change certain fields. (e.g. board version, OEM ID)
 *
 * Also, this will enable PD in RO for TCPMv2.
 */
/*
 * Some systems decouple the CBI eeprom write protection from the
 * H1_FLASH_WP_ODL via the hardware change. Adds this config to
 * bypass the cbi eeprom write protection check.
 */

/*
 * Device can be a tablet as well as a clamshell.
 */

/*
 * Add a virtual switch to indicate when we are in tablet mode.
 */

/*
 * Config to identify what devices use GMR sensor to detect tablet mode. If a
 * board selects this config, it also needs to provide GPIO_TABLET_MODE_L
 * and direct its interrupt to gmr_tablet_switch_isr.
 */

/*
 * Board provides board_sensor_at_360 method instead of GPIO_TABLET_MODE_L
 * as the means for determining the state of the flipped-360-degree mode.
 */

/*
 * GMR tablet mode sensor debounce time.
 */
#ifndef CONFIG_GMR_SENSOR_DEBOUNCE_US
#define CONFIG_GMR_SENSOR_DEBOUNCE_US (CONFIG_LID_DEBOUNCE_US + 10 * MSEC)
#endif

/*
 * Add a virtual switch to indicate when detachable device has
 * base attached.
 */

/*
 * Add a virtual switch to indicate whether a nearby object is present in front
 * of the device.
 */

/*
 * Microchip Trace FIFO Debug Port
 */

/*****************************************************************************/
/* Task config */

/*
 * List of test tasks for Zephyr tests using CONFIG_HAS_TEST_TASKS. Same
 * format as CROS_EC_TASK_LIST. See zephyr/shim/include/shimmed_task_id.h.
 */

/*****************************************************************************/
/* Mock config */

/*
 * List of mock implementations to pull into the build.
 *
 * This should contain a flat list of MOCK(the-mock-name) elements.
 *
 * This is defined in the following two files:
 * test/{testname}.mocklist
 */

/*****************************************************************************/
/* Temperature sensor config */

/* Compile common code for temperature sensor support */

/* Support particular temperature sensor chips */

/* Compile common code for thermistor support */

/* Support particular thermistors */

/*
 * If defined, image includes lookup tables and helper functions that convert
 * thermistor ADC readings into degrees K based off of various circuit
 * configurations.
 */

/*
 * If defined, active-high GPIO which indicates temperature sensor chips are
 * powered. The GPIO pin must be defined as GPIO_TEMP_SENSOR_POWER.
 * If not defined, temperature sensors are assumed to be always
 * powered.
 */

/* AMD STT (Skin Temperature Tracking) */

/* Compile common code for throttling the CPU based on the temp sensors */

/*
 * Throttle the CPU when battery discharge current is too high. When
 * this feature is enabled, BAT_MAX_DISCHG_CURRENT must be defined in board.h.
 */

/*
 * Throttle the CPU when battery voltage drops below a defined threshold
 * where the board still boots but some components don't function perfectly.
 * When this feature is enabled, BAT_LOW_VOLTAGE_THRESH must be defined in
 * board.h.
 */

/*
 * If defined, dptf is enabled to manage thermals.
 *
 * NOTE: This doesn't mean that thermal control is completely taken care by
 * DPTF. We have some hybrid solutions where the EC still manages the fans.
 */

/* If defined, dptf debug prints will print to EC console */

/*
 * If defined, this indicates to the motion lid driver that the board does not
 * have any GMR sensor and hence DPTF profile selection is required to be done
 * based on lid angle.
 */

/*
 * If defined, device supports multiple DPTF profiles depending upon device mode
 * e.g. clamshell v/s 360-degree flipped mode or base detached v/s attached
 * mode.
 *
 * This config can be used by any driver that does lid angle calculation or base
 * state detection to determine if different profile numbers need to be
 * indicated to the host.
 */

/*
 * Sometime EC was already driver thermal sensor power pin to high, but sensor
 * power is still not ready. That cause the system will thermal shutdown when
 * first boot EC.
 *
 * This config can be used to delay thermal sensor read in the first time.
 */

/*****************************************************************************/
/* Touchpad config */

/* Enable touchpad. (You must pick a driver from the options below.) */

/* Enable Elan driver */

/* Enable Goodix GT7288 driver */

/* Enable ST driver */

/* Set I2C port and address (7-bit) */

/*
 * Enable touchpad FW update over USB update protocol, and define touchpad
 * virtual address and size.
 */

/*
 * Include hashes of the touchpad FW in the EC image, passed as TOUCHPAD_FW
 * parameter to make command.
 */

/*****************************************************************************/
/* USART stream config */

/*
 * Each USART stream can be individually enabled and accessible using the
 * stream interface provided in the usart_config struct.
 */

/*****************************************************************************/
/* USB stream config */

/*****************************************************************************/
/* UART HOST COMMAND config */

/* Includes USART as host command interface */

/* Pointer to USART HW config of physical instance */

/*
 * USART baudrate for host command interface.
 * Typically configured at 3000000 to handle use cases
 * like firmware download and big packets in a reasonable time.
 */

/*****************************************************************************/
/* UART config */

/* Baud rate for UARTs */
#ifndef CONFIG_UART_BAUD_RATE
#define CONFIG_UART_BAUD_RATE 115200
#endif

/* UART index (number) for host UART, if present */

/* Use uart_input_filter() to filter UART input. See prototype in uart.h */

/*
 * Allow switching the EC console UART to an alternate pad. This must be
 * used for short transactions only, and EC is only able to receive data on
 * that alternate pad after it has been explicitly switched.
 */

/**
 * Preserve EC reset logs and console logs on SRAM/FLASH so that the logs will
 * be preserved after EC shutting down or sysjumped. It will keep the contents
 * across EC resets, so we have more information about system states. The
 * contents on SRAM will be cleared when checksum or validity check fails.
 */

/*
 * UART receive buffer size in bytes.  Must be a power of 2 for macros in
 * common/uart_buffering.c to work properly.  Must be larger than
 * CONFIG_CONSOLE_INPUT_LINE_SIZE to copy and paste scripts.
 */
#ifndef CONFIG_UART_RX_BUF_SIZE
#define CONFIG_UART_RX_BUF_SIZE 128
#endif

/* Use DMA for UART input */

/*
 * On some platforms, UART receive DMA can't trigger an interrupt when a single
 * character is received.  Those platforms poll for characters every HOOK_TICK.
 * When a character is received, make this many additional checks between then
 * and the next HOOK_TICK, to increase responsiveness of the console to input.
 */
#ifndef CONFIG_UART_RX_DMA_RECHECKS
#define CONFIG_UART_RX_DMA_RECHECKS 5
#endif

/*
 * UART transmit buffer size in bytes.  Must be a power of 2 for macros in
 * common/uart_buffering.c to work properly.
 */
#ifndef CONFIG_UART_TX_BUF_SIZE
#define CONFIG_UART_TX_BUF_SIZE 512
#endif

/* Use DMA for UART output */

/* The DMA channel for UART.  If not defined, default to UART1. */

/* The DMA peripheral request signal for UART TX. STM32 only. */

/* The DMA channel mapping config for stm32f4. */

/* Driver of LN9310 switchcap */

/* Use this to include support for MP4245 buck boost converter */

/* Use this to include support for MP2964 IMVP9.1 PMIC */

/*****************************************************************************/
/* USB PD config */

/* Config is enabled, if PD interrupt tasks are used. */

/*
 * Enables USB Power Delivery
 *
 * When this config option is enabled, one of the following must be enabled:
 *	CONFIG_USB_PD_TCPMV2 - current power delivery state machine
 *	CONFIG_USB_PD_CONTROLLER - power delivery controller state machine
 * TCPMv1 is not supported in platform/ec (see platform/ec-legacy)
 */

/*
 * Enables Version 2 of the Power Delivery state machine
 *
 * Along with CONFIG_USB_PD_TCPMV2, you must ensure a device type is also
 * enabled otherwise an error will be emitted.
 */

/*
 * Enables the Power Delivery Controller state machine.
 */

/*
 * Enable dynamic PDO selection.
 *
 * DPS picks a power efficient voltage regarding to the battery configuration
 * and the system loading. It monitors PIn (Power In), so VBUS/IBUS ADC
 * should be supported on the platform.
 */

/*
 * Device Types for TCPMv2.
 *
 * Exactly one must be defined when CONFIG_USB_PD_TCPMV2 is defined.
 *
 * VPD - Vconn Powered Device
 * CTVPD - Charge Through Vconn Powered Device
 * DRP_ACC_TRYSRC - Dual Role Port, Audio Accessory, and Try.SRC Device
 */

/* Enables PD Console commands */
#ifndef CONFIG_USB_PD_CONSOLE_CMD
#define CONFIG_USB_PD_CONSOLE_CMD
#endif

/* Enables PD Host commands */
#ifdef CONFIG_HAS_HOSTCMD
#ifndef CONFIG_USB_PD_HOST_CMD
#define CONFIG_USB_PD_HOST_CMD
#endif
#endif

/* Support for USB PD alternate mode entry */

/* Support for USB PD alternate mode entry by a Downward Facing Port */

/* Support for USB PD alternate mode entry from an Upward Facing Port */

/* Support for automatic USB PD Discovery VDM probing and storage */

/*
 * Do not enter USB PD alternate modes or USB4 automatically. Wait for the AP to
 * direct the EC to enter a mode. This requires AP software support.
 */

/* Allow the AP to compose VDMs for us to send */

/* Supports DP as UFP-D and requires HPD to DP_ATTEN converter */

/* HPD is sent to the GPU from the EC via a GPIO */

/*
 * HPD is sent to the GPU from the EC via a GPIO, and the HPD GPIO level has
 * to be handled separately.
 */

/* Check if max voltage request is allowed before each request */

/* Default state of PD communication disabled flag */

/*
 * Define this if a board needs custom SNK and/or SRC PDOs.
 *
 * The default SRC PDO is a fixed 5V/1.5A with PDO_FIXED_FLAGS indicating
 * Dual-Role power, USB Communication Capable, and Dual-Role data.
 *
 * The default SNK PDOs are:
 * - Fixed 5V/500mA with the same PDO_FIXED_FLAGS
 * - Variable (non-battery) min 4.75V, max CONFIG_USB_PD_MAX_VOLTAGE_MV,
 *   operational current 3A
 * - Battery min 4.75V, max CONFIG_USB_PD_MAX_VOLTAGE_MV, operational power 15W
 */

/*
 * Do not enable PD communication in RO as a security measure.
 * We don't want to allow communication to outside world until
 * we jump to RW. This can by overridden with the removal of
 * the write protect screw to allow for easier testing.
 *
 * Note: this is assumed for TCPMv2. See also CONFIG_BRINGUP for enabling PD in
 * RO.
 */

/* Default USB data role when a USB PD debug accessory is seen */
#ifndef CONFIG_USB_PD_DEBUG_DR
#define CONFIG_USB_PD_DEBUG_DR PD_ROLE_DFP
#endif

/*
 * Define to have a fixed PD Task debug level.
 * Undef to allow runtime change via console command.
 */

/* Build a framework to record custom time intervals and print them out at
 * convenient points with respect to each USB-C port. This config does not
 * directly enable any recording. The developer must define the intervals and
 * choose when to print the results.
 */

/*
 * Set to a nonzero value to delay PD task startup by the given
 * amount of time.
 */
#ifndef CONFIG_USB_PD_STARTUP_DELAY_MS
#define CONFIG_USB_PD_STARTUP_DELAY_MS 0
#endif

/*
 * Define if this board is using runtime flags instead of build time configs
 * to control USB PD properties.
 */
#ifndef CONFIG_USB_PD_FLAGS
#define CONFIG_USB_PD_FLAGS
#endif

/*
 * Define to enable the PD Data Reset Message. This is mandatory for
 * USB4 and optional for USB 3.2
 */

/*
 * Define if this board can enable VBUS discharge (eg. through a GPIO-controlled
 * discharge circuit, or through port controller registers) to discharge VBUS
 * rapidly on disconnect. Will be defined automatically when one of the below
 * options is defined.
 */

/* Define if discharge circuit is EC GPIO-controlled. */

/* Define if discharge circuit is using PD discharge registers on TCPC. */

/* Define if discharge circuit is using PD discharge registers on PPC. */

/* Define if this board can act as a dual-role PD port (source and sink) */

/* Define if this board can used TCPC-controlled DRP toggle */

/* Define to reduces VBUS droop caused by inrush current during charging */

/* Initial DRP / toggle policy */
#ifndef CONFIG_USB_PD_INITIAL_DRP_STATE
#define CONFIG_USB_PD_INITIAL_DRP_STATE PD_DRP_TOGGLE_OFF
#endif

/*
 * Define if VBUS source GPIOs (GPIO_USB_C*_5V_EN) are active-low (and named
 * (..._L) rather than default active-high.
 */

/* Ask charger if VBUS is enabled on a source port, instead of using GPIO */

/*
 * If defined, use a custom function to determine if VBUS is enabled on a
 * source port. The custom function is board_is_sourcing_vbus(port).
 */

/* Enable Displayport 2.1 Capability */

/* Support USB PD flash. */

/* Check whether PD is the sole power source before flash erase operation */

/* Define if this board, operating as a sink, can give power back to a source */

/*
 * PD Rev2.0 functionality is enabled by default. Defining this macro
 * enables PD Rev3.0 functionality.
 */

/*
 * If enabled, the PD selection logic will select the PDO offering the lower
 * voltage if multiple PDOs provide the same power.
 */

/*
 * If enabled, the PD selection logic will select the PDO offering the higher
 * voltage if multiple PDOs provide the same power.
 */

/* Defined automatically based on on maximum PD revision supported. */

/*
 * Support USB PD 3.0 Extended Messages. This will only take effect if
 * CONFIG_USB_PD_REV30 is also enabled. Note that Chromebooks disabling this
 * config item are non-compliant with PD 3.0, because they have batteries but do
 * not support Get_Battery_Cap or Get_Battery_Status.
 */
#ifndef CONFIG_USB_PD_EXTENDED_MESSAGES
#define CONFIG_USB_PD_EXTENDED_MESSAGES
#endif

/* Major and Minor ChromeOS specific PD device Hardware IDs. */

/* HW & SW version for alternate mode discover identity response (4bits each) */

/* USB PD MCU I2C address for host commands */
#ifndef CONFIG_USB_PD_I2C_ADDR_FLAGS
#define CONFIG_USB_PD_I2C_ADDR_FLAGS 0x1E
#endif

/* Define if using internal comparator for PD receive */

/* Record main PD events in a circular buffer */

/*
 * Record PRL state transitions in a ring buffer, readable via the `prllog`
 * console command.
 */
/*
 * Number of events that can be stored in the PRL log (after this many, the
 * oldest entries will be replaced with new ones).
 */
#ifndef CONFIG_USB_PD_PRL_EVENT_LOG_CAPACITY
#define CONFIG_USB_PD_PRL_EVENT_LOG_CAPACITY 128
#endif

/* The size in bytes of the FIFO used for event logging */
#ifndef CONFIG_EVENT_LOG_SIZE
#define CONFIG_EVENT_LOG_SIZE 512
#endif

/* Event-driven CC detection */

/* Save power by waking up on VBUS rather than polling CC */
#ifndef CONFIG_USB_PD_LOW_POWER
#define CONFIG_USB_PD_LOW_POWER
#endif

/* Allow chip to go into low power idle even when a PD device is attached */

/* Number of USB PD ports */

/*
 * Number of ITE USB PD active ports
 * NOTE: The active port usage should follow the order of ITE TCPC port index.
 */

/* Simple DFP, such as power adapter, will not send discovery VDM on connect */

/* Use comparator module for PD RX interrupt */
#ifndef CONFIG_USB_PD_RX_COMP_IRQ
#define CONFIG_USB_PD_RX_COMP_IRQ
#endif

/* Use TCPC module (type-C port controller) */

/* Board provides specific TCPC init function */

/* Enable TCPC to enter low power mode */

/*
 * Default debounce when exiting low-power mode before checking CC status.
 * Some TCPCs need additional time following a VBUS change to internally
 * debounce the CC line status and updating the CC_STATUS register.
 */
#ifndef CONFIG_USB_PD_TCPC_LPM_EXIT_DEBOUNCE
#define CONFIG_USB_PD_TCPC_LPM_EXIT_DEBOUNCE (25 * MSEC)
#endif

/* Define EC and TCPC modules are in one integrated chip */

/* If VCONN is enabled, the TCPC will provide VCONN */
#ifndef CONFIG_USB_PD_TCPC_VCONN
#define CONFIG_USB_PD_TCPC_VCONN
#endif

/* Enable the encoding of msg SOP* in bits 31-28 of 32-bit msg header type */

/* Enable to support DisplayPort mode from the EC */

/*
 * The USB4 specification defines compatibility support for USB4 products to
 * interact with existing Thunderbolt 3 products. Enable this config to enter
 * into Thunderbolt-compatible mode between two port partners.
 */

/* Enable to enter into USB4 mode between two port partners */

/* Enable if port is cable of operating as an USB4 device */

/* Enable if port is cable of operating as an USB3.2 device */

/* Enable if the board is Thunderbolt Gen 3 capable */

/* Enable PCIE tunneling if Thunderbolt-Compatible mode is enabled*/

/* Enable Power Path Control from PD */

/*
 * The following two macros are ASCII text strings that matches what appears
 * in the USB-IF Product Registration form for this device. These macros are
 * used during VIF generation and they form the product name in the
 * USB Integrator’s List.
 */

/*
 * Should be defined if the device is a TypeC Alt Mode Adapter. This macro
 * is used during VIF generation.
 */

/*
 * A text string, provided by the USB-IF. This macro is used during VIF
 * generation.
 */

/*
 * An ASCII text string that must correspond with the port label given on the
 * device picture submitted to USB-IF by the Vendor along with the VIF. This
 * macro is used during VIF generation.
 */

/*
 * Define if Get_Manufacturer_Info request PD message is supported.
 * Used during VIF generation.
 */

/*
 * Define if both Security_Request and Security_Response PD messages are
 * supported. Used during VIF generation.
 */

/*
 * The number of non-removable batteries in the device. Used duing VIF
 * generation.
 */

/*
 * Track VBUS level in TCPC module. This will only be needed if we're acting
 * as an external TCPC.
 */

/* Enable runtime config the TCPC */

/*
 * Choose one of the following TCPMs (type-C port manager) to manage TCPC. The
 * TCPM stub is used to make direct function calls to TCPC when TCPC is on
 * the same MCU. The TCPCI TCPM uses the standard TCPCI i2c interface to TCPC.
 */

/* PS8XXX series are all supported by a single driver with a build time config
 * listed below (CONFIG_USB_PD_TCPM_PS*) defined to enable the specific product.
 *
 * If a board with the same EC FW is expected to support multiple products here
 * then CONFIG_USB_PD_TCPM_MULTI_PS8XXX MUST be defined then we can enable more
 * than one product config to support them in the runtime. In this case, board
 * is responsible to override function of board_get_ps8xxx_product_id in order
 * to provide the product id per port.
 */

/*
 * Enable PS8751 custom mux driver. It was designed to make use of Low Power
 * Mode on PS8751 TCPC/MUX chip when running as MUX only (CC lines are not
 * connected, eg. Ampton).
 *
 * If your PS8751 is working in the ordinary way (as TCPC and MUX) or you don't
 * need to take advantage of Low Power Mode when working as MUX only, standard
 * TCPC MUX driver (CONFIG_USB_PD_TCPM_MUX) will work fine.
 */

/*
 * Defined automatically by chip and depends on chip. This guards the onboard
 * TCPM driver, but CONFIG_USB_PD_TCPM_ITE_ON_CHIP needs to be defined in
 * board.h for either of these driver to actually be included in the final
 * image.
 */

/*
 * Type-C retimer drivers to be used.
 */

/*
 * DP redriver drivers to be used.
 */

/*
 * Define this to enable Type-C retimer firmware update. Each Type-C retimer
 * indicates its capability of supporting firmware update in usb_mux_driver.
 * This feature is available to TCPMv2 PD stack, also requires
 * CONFIG_USBC_SS_MUX is enabled.
 * This feature includes changes in EC, Coreboot and Kernel. During AP boot
 * up, AP scans each PD port for retimers if no Type-C device attached;
 * and firmware update can be performed on retimers showing up in AP
 * thunderbolt device entries. If PD port has device attached, no retimer
 * scan on that port.
 */

/* Prevent enabling LPM of NB7V904M */

/* Enable retimer TUSB544 tune EQ setting by register  */

/* Allow run-time configuration of the Burnside Bridge driver structure */

/* Enable vPro support for Intel Burnside Bridge on vPro supported platform */

/* Require manual configuration of the KB800x crossbar mapping. */

/* Enables debug console commands for the STM32 UCPD driver */

/*
 * Adds an EC console command to erase the ANX7447 OCM flash.
 * Note: this is intended to be a temporary option and
 * won't be needed when ANX7447 are put on boards with OCM already erased
 */

/*
 * Use this config option to enable and internal pullup resistor on the AUX_N
 * and internal pulldown resistor on the AUX_P line. Only use this config
 * option if there are no external pu/pd resistors on these signals. This
 * configuration should be used to avoid noise issues on the DDI1_AUX_N &
 * DDI1_AUX_P signals (b/122873171)
 */

/*
 * Use this to override the TCPCI Device ID value to be 0x0002 for
 * chip rev A3. Early A3 firmware misreports the DID as 0x0001.
 */

/*
 * Use this to override the TCPCI Device ID value to be 0x0002 for
 * chip rev A1. Early A1 firmware misreports the DID as 0x0001.
 */

/*
 * Use this option if the TCPC port controller supports the optional register
 * 18h CONFIG_STANDARD_OUTPUT to steer the high-speed muxes.
 */

/*
 * Some PD chips have integrated port protection for SBU lines.
 * If the switches to enable those SBU lines are controlled by the PD
 * chip, enable this config.
 */

/*
 * The TCPM must know whether VBUS is present in order to make proper state
 * transitions. In addition, charge_manager must know about VBUS presence in
 * order to make charging decisions. VBUS state can be determined by various
 * methods:
 * - Some TCPCs can detect and report the presence of VBUS.
 * - In some configurations, charger ICs can report the presence of VBUS.
 * - On some boards, dedicated VBUS interrupt pins are available.
 * - Some power path controllers (PPC) can report the presence of VBUS.
 *
 * Exactly one of these should be defined for all boards that run the PD
 * state machine.
 */

/* Define if the there is a separate ADC channel for each USB-C Vbus voltage */

/* Define if the there is no hardware to measure Vbus voltage */

/* Define if charger on the board supports VBUS measurement */

/* Define if tcpc on the board supports VBUS measurement */

/* Define if there is a specific method to measure Vbus voltage */

/* Define if the PDC on the board supports VBUS measurement */

/* Define the type-c port controller I2C base address. */
#ifndef CONFIG_TCPC_I2C_BASE_ADDR_FLAGS
#define CONFIG_TCPC_I2C_BASE_ADDR_FLAGS 0x4E
#endif

/* Use this option to enable Try.SRC mode for Dual Role devices */

/* Set the default minimum battery percentage for Try.Src to be enabled */
#ifndef CONFIG_USB_PD_TRY_SRC_MIN_BATT_SOC
#define CONFIG_USB_PD_TRY_SRC_MIN_BATT_SOC 5
#endif

/* Index for temperature sensor used in PD messages. Defaults to 0. */
#ifndef CONFIG_USB_PD_TEMP_SENSOR
#define CONFIG_USB_PD_TEMP_SENSOR 0
#endif

/*
 * Time limit in ms for a USB PD power button press to be considered a short
 * press
 */
#ifndef CONFIG_USB_PD_SHORT_PRESS_MAX_MS
#define CONFIG_USB_PD_SHORT_PRESS_MAX_MS 4000
#endif

/* Time limit in ms for a USB PD power button press to be considered valid. */
#ifndef CONFIG_USB_PD_LONG_PRESS_MAX_MS
#define CONFIG_USB_PD_LONG_PRESS_MAX_MS 8000
#endif

/*
 * Set the minimum battery percentage to allow a PD port to send resets as a
 * sink (and risk a hard reset, losing Vbus).  Note this may cause a high-power
 * charger to appear as only a low-power 15W charger until a reset is sent to
 * re-start PD negotiation.
 */

/*
 * Workaround for power_state:rec with cros_ec_softrec_power on chromeboxes.
 * cros_ec_softrec works by running `reboot wait-ext ap-off-in-ro`. If a
 * chromebox is powered by Type-C only, the EC reset will result in a PD hard
 * reset and the device will brown out. When it boots again the ap-off and
 * stay-in-ro flags are lost so recovery fails. To work around this, we preserve
 * the flags across a PD reset.
 *
 * This doesn't affect manual recovery on user devices, since it uses the
 * recovery signal from the GSC, not the reset flags.
 *
 * This should only be enabled on chromeboxes which don't have servo micro and
 * therefore can't use cros_ec_hardrec_power. See b/293545949 and b/295363809.
 */

/* Alternative configuration keeping only the TX part of PHY */

/* Use DAC as reference for comparator at 850mV. */

/*
 * The Fast Role Swap trigger can be implemented in either the TCPC or PPC
 * driver. If either CONFIG_USB_PD_FRS_TCPC or CONFIG_USB_PD_FRS_PPC is set,
 * CONFIG_USB_FRS will be set automatically to enable the protocol-side of FRS.
 */

/*
 * Enable USB-PD extended power range.
 */

/*
 * USB Product ID. Each platform (e.g. baseboard set) should have a single
 * VID/PID combination. If there is a big enough change within a platform,
 * then we can differentiate USB topologies by varying the HW version field
 * in the Sink and Source Capabilities Extended messages.
 *
 * To reserve a new PID, use go/usb-pid.
 */

/*
 * USB Vendor ID used for USB endpoints.
 */
#ifndef CONFIG_USB_VID
#define CONFIG_USB_VID USB_VID_GOOGLE
#endif

/*
 * Track overcurrent events for sinking partners coming from some component on
 * the board.  Auto-enabled for drivers which contain support for this feature.
 */

/* PPC needs to be informed of CC polarity */

/*
 * Disable charging from Default(USB) Rp as a type-c supplier. If your device
 * can detect such a supplier by BC 1.2, define this to get more current
 * from a BC 1.2 supplier.
 */

/* USB Type-C Power Path Controllers (PPC) */

/*
 * NX20P348x 5V SRC RCP trigger level at 10mV. Define to enable 5V SRC RCP
 * mask for can't trigger interrupt signal.
 */

/*
 * NX20P348x control vbus discharge with SRC_EN
 * in case we don't have hardware logic.
 * When SRC_EN goes low, we want to control vbus discharge earlier.
 * It is to speed up the speed at which vbus falls to zero.
 */

/*
 * Setting SYV682X OVP to 15v power profile application
 */

/*
 * SYV682x PPC high voltage power path current limit. The hardware default is
 * 3.3A, but this results in spurious OCP events. Default to 5.5A in software.
 * See b/349015641 for details. See the syv682x header file for possible values.
 */
#ifndef CONFIG_SYV682X_HV_ILIM
#define CONFIG_SYV682X_HV_ILIM SYV682X_HV_ILIM_5_50
#endif

/* SYV682 does not pass through CC, instead it bypasses to the TCPC */

/* PPC is capable of gating the SBU lines. */

/* PPC is capable of providing VCONN */

/* PPC has level interrupts and has a dedicated interrupt pin to check */

/* Enable logging related to the PPC. Undefine to reduce EC image size */
#ifndef CONFIG_USBC_PPC_LOGGING
#define CONFIG_USBC_PPC_LOGGING
#endif

/* Support for USB type-c superspeed mux */

/*
 * Only configure USB type-c superspeed mux when DFP (for chipsets that
 * don't support being a UFP)
 */

/* Only configure USB type-c superspeed mux when UFP */

/* Support v1.1 type-C connection state machine */

/* Support for USB type-c vconn. Not needed for captive cables. */

/* Support VCONN swap */

/*
 * The amount of time in microseconds that the board takes to turn VCONN on or
 * off after being directed to do so. Typically a property of the PPC. Default
 * to 5 ms.
 */
#ifndef CONFIG_USBC_VCONN_SWAP_DELAY_US
#define CONFIG_USBC_VCONN_SWAP_DELAY_US 5000
#endif

/* USB Binary device Object Store support */

/* USB Device version of product */

/*
 * Intel Reference Validation Platform's (RVP) Modular Embedded Control
 * Card (MECC) versions
 */

/*****************************************************************************/

/* Compile chip support for the USB device controller */

/* Support USB isochronous handler */

/* Common USB / BC1.2 charger detection routines */

/* Only allow PI3USB9201 to advertise itself as BC1.2 client */

/*
 * Used for bc1.2 chips that need to be triggered from data role swaps instead
 * of just VBUS changes.
 */

/*
 * Board only needs one bc12 driver. This includes the case that has multiple
 * chips that use the same driver. Enabled by default.
 *
 * If undefined, board should define a bc12_ports array which associates
 * each port to its bc12 driver.
 */
#ifndef CONFIG_BC12_SINGLE_DRIVER
#define CONFIG_BC12_SINGLE_DRIVER
#endif

/* External BC1.2 charger detection devices. */
/* Number of Pericom PI3USB9281 chips present in system */
/* The delay in ms from power off to power on for MAX14637 */
#ifndef CONFIG_BC12_MAX14637_DELAY_FROM_OFF_TO_ON_MS
#define CONFIG_BC12_MAX14637_DELAY_FROM_OFF_TO_ON_MS 1
#endif

/*
 * Max. size of packets for the control endpoint, valid values: 8, 16, 32, 64.
 * Defaults to the largest supported value.
 */

/* Enable USB serial console module. */

/*
 * Enable USB serial console module using usb stream config.
 * NOTE: CONFIG_USB_CONSOLE and CONFIG_USB_CONSOLE_STREAM should be defined
 * exclusively each other.
 */

/* USB serial console transmit buffer size in bytes. */
#ifndef CONFIG_USB_CONSOLE_TX_BUF_SIZE
#define CONFIG_USB_CONSOLE_TX_BUF_SIZE 2048
#endif

/*
 * Enable USB serial console crc32 computation.
 * Also makes console output block on overrun.
 */

/* Support USB HID interface. */

/* Support USB HID keyboard interface. */

/* Support USB HID keyboard backlight. */

/*
 * Support vivaldi compatible HID keyboard.
 * If defined, the board must implement a function board_vivaldi_keybd_config(),
 * and define a macro CONFIG_USB_HID_KB_NUM_TOP_ROW_KEYS which is equal to
 * board_vivaldi_keybd_config()->num_top_row_keys.
 */

/* Support USB HID touchpad interface. */

/* HID touchpad logical dimensions */
/* HID touchpad physical dimensions (tenth of mm) */

/* USB device buffers and descriptors */

/* Disable automatic connection of USB peripheral */

/* Disable automatic initialization of USB peripheral */

/* Support simple control of power to the device's USB ports */

/*
 * Let board customize the timing to enable/disable usb port, instead
 * of using the default S3 hook.
 */

/*
 * Support smart power control to the device's USB ports, using
 * dedicated power control chips.  This potentially enables automatic
 * negotiation of supplying more power to peripherals.
 */

/*
 * GPIOs to enable USB port power have non-const configuration.
 */

/*
 * Support smart power control to the device's USB ports, however only CDP and
 * SDP are supported.  Usually this is the case if all the control lines to the
 * charging port controller are hard-wired.
 */

/*
 * Override the default charging mode for USB smart power control.
 * Value is selected from usb_charge_mode in include/usb_charge.h
 */

/*
 * Smart USB power control can use a full set of control signals to the USB
 * port power chip, or a reduced set.  If this is defined, use the reduced set.
 */

/*  Number of smart USB power ports. */
#ifndef CONFIG_USB_PORT_POWER_SMART_PORT_COUNT
#define CONFIG_USB_PORT_POWER_SMART_PORT_COUNT 2
#endif

/*
 * Smart USB power control current limit pins may be inverted.  In this case
 * they are active low and the GPIO names will be GPIO_USBn_ILIM_SEL_L.
 */

/*
 * Support waking up host by setting the K-state on the data lines (requires
 * CONFIG_USB_SUSPEND to be set as well).
 */

/* Support programmable USB device iSerial field. */

/* Support reporting of configuration bMaxPower in mA */
#ifndef CONFIG_USB_MAXPOWER_MA
#define CONFIG_USB_MAXPOWER_MA 500
#endif

/* Support correct handling of USB suspend (host-initiated). */

/*
 * Enable this config for a USB-EP device that needs to interop properly with a
 * MS Windows OS host machine. This config will enable a special string
 * descriptor so Windows OS will know to retreieve an Extended Compat ID OS
 * Feature descriptor.
 */

/* Default pull-up value on the USB-C ports when they are used as source. */
#ifndef CONFIG_USB_PD_PULLUP
#define CONFIG_USB_PD_PULLUP TYPEC_RP_1A5
#endif

/*
 * Ignore all non-fixed PDOs received from a src_caps message. Enable this for
 * boards (like servo_v4) which only support FIXED PDO types.
 */

/*
 * Maximum number of interrupts in a second. Exceeding this limit
 * will cause the TCPM to break the PD connection to avoid a
 * watchdog timeout crash.
 *
 * The default of 500 (one every 2ms) is chosen to cover normal PD
 * behavior while mitigating interrupt storms.
 */
#ifndef CONFIG_USB_PD_INT_STORM_MAX
#define CONFIG_USB_PD_INT_STORM_MAX 500
#endif

/******************************************************************************/
/* stm32f4 dwc usb configs. */

/* Set USB speed to FS rather than HS */

/******************************************************************************/
/* USB port switch */

/* Allow run-time completion of the usb mux driver structure */

/* Allow the AP to send commands for mux control */

/* Support the AMD FP5 USB/DP Mux */

/* Support the AMD FP6 USB/DP Mux */

/*
 * Support the Analogix ANX3443 USB Type-C Active mux (6x4) with
 * Integrated Re-timers for USB3.2/DisplayPort.
 */

/*
 * Support the Analogix ANX7440 USB Type-C Active mux with
 * Integrated Re-timers for USB3.1/DisplayPort.
 */

/*
 * Support the Analogix ANX7451 10G Active Mux (4x4) with
 * Integrated Re-timers for USB3.2/DisplayPort
 */

/* Support the ITE IT5205 Type-C USB alternate mode mux. */

/* Support the Pericom PI3USB30532 USB3.0/DP1.2 Matrix Switch */

/* Support the Pericom PI3USB31532 USB3.1/DP1.4 Matrix Switch */

/* Support the Parade PS8740 Type-C Redriving Switch */

/* Support the Parade PS8742 Type-C Redriving Switch */

/* Support the Parade PS8743 Type-C Redriving Switch */

/* Config to enable TUSB1044 Type-c USB redriver */

/* Support the Texas Instrument TUSB1064 Type-C Redriving Switch (UFP) */

/*
 * Support TI TUSB546 USB Type-C DP ALT Mode Linear Redriver Crosspoint
 * Switch
 */

/* Support the Parade PS8822 Type-C Redriving Demux Switch */

/* 'Virtual' USB mux under host (not EC) control */

/* Enable IT5205H SBU protection switch */

/*
 * Enable to inform the AP that an ACK is needed on configuring the TCSS mux.
 * The config is enabled automatically when the board has CONFIG_USB_MUX_VIRTUAL
 * and CONFIG_USBC_RETIMER_INTEL_BB enabled.
 */

/*****************************************************************************/
/* USB GPIO config */

/*****************************************************************************/
/* USB SPI config */

/*
 * Use when you want the SPI subsystem to be enabled even when the USB SPI
 * endpoint is not enabled by the host. This means that when this firmware
 * enables SPI, then the HW SPI module is enabled (i.e. SPE bit is set) until
 * this firmware disables the SPI module; it ignores the host's enables state.
 */

/*****************************************************************************/
/* USB I2C config */

/* Allowed read/write count for USB over I2C */
#ifndef CONFIG_USB_I2C_MAX_WRITE_COUNT
#define CONFIG_USB_I2C_MAX_WRITE_COUNT 60
#endif
#ifndef CONFIG_USB_I2C_MAX_READ_COUNT
#define CONFIG_USB_I2C_MAX_READ_COUNT 60
#endif

/*****************************************************************************/
/* USB Power monitoring interface config */

/*****************************************************************************/
/*
 * USB stream signing config. This allows data read over UART or SPI
 * to have a signature generated that can be used to validate the data
 * offline based on H1's registered key. Used by mn50.
 */

/*****************************************************************************/

/*
 * Support early firmware selection
 *
 * EFS1 is being deprecated. EFS2 is faster, doesn't need two slots, and
 * supports rollback protection.
 *
 * EFS2 runs in the system task (a.k.a. main) and the hook task (for shutdown
 * hook). Their stack sizes must be big enough for sha256.
 */

/* Offset of RW-A image in writable storage when using EFS. */
/* Offset of RW-A signature. */
/* Offset of RW-B image in writable storage when using EFS. */
/* Offset of RW-B signature. */

/* Support computing hash of code for verified boot */

/* Support for secure temporary storage for verified boot */

/* Number of supported slots for secure temporary storage */

/*****************************************************************************/
/* Watchdog config */

/*
 * Compile watchdog timer support.  The watchdog timer will reboot the system
 * if the hook task (which is the lowest-priority task on the system) gets
 * starved for CPU time and isn't able to fire its HOOK_TICK event.
 */

/*
 * Try to detect a watchdog that is about to fire, and print a trace.  This is
 * required on chips such as STM32 where the watchdog timer simply reboots the
 * system without any early warning.
 */

/*
 * The maximum number of times that the watchdog timer may reset
 * before halting the system (or taking some sort of other
 * chip-dependent corrective action).
 */
#ifndef CONFIG_WATCHDOG_MAX_RETRIES
#define CONFIG_WATCHDOG_MAX_RETRIES 4
#endif

/* Watchdog period in ms; see also AUX_TIMER_PERIOD_MS */
#ifndef CONFIG_WATCHDOG_PERIOD_MS
#define CONFIG_WATCHDOG_PERIOD_MS 1600
#endif

/* The leading time of watchdog warning timer. */
#ifndef CONFIG_WATCHDOG_WARNING_LEADING_TIME_MS
#define CONFIG_WATCHDOG_WARNING_LEADING_TIME_MS 500
#endif

/*
 * Fire auxiliary timer before watchdog timer expires. This leaves some time for
 * debug trace to be printed.
 */
#ifndef CONFIG_AUX_TIMER_PERIOD_MS
#define CONFIG_AUX_TIMER_PERIOD_MS \
	(CONFIG_WATCHDOG_PERIOD_MS - CONFIG_WATCHDOG_WARNING_LEADING_TIME_MS)
#endif

/*****************************************************************************/
/* WebUSB config */

/*
 * Enable the WebUSB support and define its URL.
 * Export a WebUSB Platform Descriptor in the Binary Object Store descriptor.
 * The WebUSB landing page URL is equal to 'CONFIG_WEBUSB_URL' plus the
 * https:// prefix.
 * This requires CONFIG_USB_BOS.
 */

/*****************************************************************************/

/*
 * Support controlling power to WiFi, WWAN (3G/LTE), and/or bluetooth modules.
 */

/*
 * Support for WiFi devices that must remain powered in suspend.  Set to the
 * combination of EC_WIRELESS_SWITCH flags (from ec_commands.h) which should
 * be set in suspend.
 */

/* WiFi power control signal is active-low. */

/*
 * Write protect signal is active-high.  If this is defined, there must be a
 * GPIO named GPIO_WP; if not defined, there must be a GPIO names GPIO_WP_L.
 */

/*
 * The write protect signal is always asserted,
 * independently of the GPIO existence or current value.
 */

/* Firmware upgrade options. */
/* A different config for the same update. TODO(vbendeb): dedupe these */

/* Add support for pairing over the USB update interface. */

/* Add support for reading UART buffer from USB update interface. */

/* PDU size for fw update over USB (or TPM). */
#ifndef CONFIG_UPDATE_PDU_SIZE
#define CONFIG_UPDATE_PDU_SIZE 1024
#endif

/* DFU firmware upgrade options */
/*
 * Enables DFU USB Runtime identifier.
 */

/*
 * Indicates this region is a DFU Boot Manager and is a minimal runtime.
 */
/*
 * Enables DFU Boot Manager reboot loop protection. When unexpected reboots
 * occur, a counter is incremented which will enter DFU once it exceeds
 * the value defined. This parameter should only be enabled on setups which
 * can issue the command to exit DFU.
 */

/*
 * Enables access to shared utilities required for the application
 * and DFU Boot Manager. This allows the application to enter DFU.
 */

/*
 * If defined, led_pwr_get_state returns a special status if battery is
 * discharging and battery is nearly full.
 */

/*
 * Define this if a chip needs to add some information to the common 'version'
 * command output.
 */

/*
 * Include CROS_FWID in version output.
 */
#ifndef CONFIG_CROS_FWID_VERSION
#define CONFIG_CROS_FWID_VERSION
#endif

/*
 * Define this to support Cros Board Info from EEPROM. I2C_PORT_EEPROM
 * and I2C_ADDR_EEPROM_FLAGS must be defined as well.
 */

/*
 * Define this if the EC has exclusive control over the CBI EEPROM WP signal.
 * The accompanying hardware must ensure that the CBI WP gets latched and is
 * only reset when EC_RST_ODL is asserted.  GPIO_EC_CBI_WP must be set up for
 * the board.
 */

/* Define this to support Cros Board Info from GPIO. */

/* Define this to support Cros Board Info from EC flash. */

/*****************************************************************************/
/*
 * ISH config defaults
 */
/*
 * This will be automatically defined below if the board supports power
 * modes that will require the AONTASK functionality.
 */

/*
 * Define the following if the power state support is required.
 */

/*
 * Define the following if the ip accessible power gating is required.
 */

/*
 * Define the following to the number of uSeconds of elapsed time that is
 * required to enter D0I2 and D0I3, if they are supported
 */

/*
 * Define the following if the new specific power management processing
 * after ISH 5.4 is used.
 */

/*
 * Define the following in order to perform power management reset
 * prep IRQ setup when entering a new state
 */

/*
 * Define the following if combined ISR is required for ipc communication
 * between host and ISH.
 */

/*
 * Define the following if there is need to clear ISH fabric error.
 */

/*
 * Define the following if the version of ISH uses Synopsys Designware uart.
 */

/*
 * TEST ONLY defines (CONFIG_TEST_*)
 *
 * Used to include files for unit and other builds tests.
 */

/* Define to enable USB State Machine framework. */

/*
 * Define the following to drive CCD_MODE_ODL when a DTS accessory is
 * connected to the CCD USBC port.
 *
 * GPIO_CCD_MODE_ODL should be configured with GPIO_ODR_HIGH flag
 */

/*
 * The USB port used for CCD. Defaults to 0/C0.
 */
#ifndef CONFIG_CCD_USBC_PORT_NUMBER
#define CONFIG_CCD_USBC_PORT_NUMBER 0
#endif

/*
 * The historical default SCI pulse width to the host is 65 microseconds, but
 * some chipsets may require different widths.
 */
#ifndef CONFIG_HOST_INTERFACE_ESPI_DEFAULT_VW_WIDTH_US
#define CONFIG_HOST_INTERFACE_ESPI_DEFAULT_VW_WIDTH_US 65
#endif

/*
 * Build and link *test* images with googletest.
 */

/*
 * When this option is enabled, some of the experimental features (aka finch)
 * will be enabled for CROS_EC.
 */

/*****************************************************************************/
/*
 * Include board and core configs, since those hold the CONFIG_ constants for a
 * given configuration.  This guarantees they get included everywhere, and
 * fixes a fairly common bug where we gate out code with #ifndef
 * CONFIG_SOMETHING and but forget to include both of these.
 *
 * Board is included after chip, so that chip defaults can be overridden on a
 * per-board basis as needed.
 */

/* Default hcdebug mode, e.g. HCDEBUG_OFF or HCDEBUG_NORMAL */
#if !defined(CONFIG_HOSTCMD_DEBUG_MODE)
#if defined(CONFIG_BRINGUP)
#define CONFIG_HOSTCMD_DEBUG_MODE HCDEBUG_NORMAL
#else
#define CONFIG_HOSTCMD_DEBUG_MODE HCDEBUG_OFF
#endif /* defined(CONFIG_BRINGUP) */
#endif /* !defined(CONFIG_HOSTCMD_DEBUG_MODE) */

/*
 * Define CONFIG_HOST_ESPI_VW_POWER_SIGNAL if any power signals from the host
 * are configured as virtual wires.
 */
#if defined(CONFIG_HOST_INTERFACE_ESPI_VW_SLP_S3) ||     \
	defined(CONFIG_HOST_INTERFACE_ESPI_VW_SLP_S4) || \
	defined(CONFIG_HOST_INTERFACE_ESPI_VW_SLP_S5)
#define CONFIG_HOST_ESPI_VW_POWER_SIGNAL
#endif

/*
 * S4 residency works by observing SLP_S5 via virtual wire (as SLP_S5 has not
 * traditionally been routed to the EC). If the board family wants S4 residency,
 * they need to use ECs that support eSPI. Note that S4 residency is not
 * strictly a requirement to support suspend-to-disk, except on Intel platforms
 * with Key Locker support (TGL+).
 */
#if defined(CONFIG_POWER_S4_RESIDENCY) && \
	!defined(CONFIG_HOST_INTERFACE_ESPI_VW_SLP_S5)
#error "S4_RESIDENCY needs eSPI support or SLP_S5 routed"
#endif

/*
 * Note that in Zephyr OS, eSPI can be enabled for virtual wires
 * without using eSPI for host commands.
 */

/******************************************************************************/
/*
 * If CONFIG_USB_POWER_DELIVERY is enabled, make sure either
 * CONFIG_USB_PD_CONTROLLER or CONFIG_USB_PD_TCPMV2 is enabled but not both.
 * Also make sure CONFIG_USB_PD_DECODE_SOP is enabled with CONFIG_USB_PD_TCPMV2
 */
#ifdef CONFIG_USB_POWER_DELIVERY
#if defined(CONFIG_USB_PD_TCPMV2) == defined(CONFIG_USB_PD_CONTROLLER)
#error Enable exactly one of CONFIG_USB_PD_TCPMV2 or CONFIG_USB_PD_CONTROLLER
#endif

#if defined(CONFIG_USB_PD_TCPMV2) && !defined(CONFIG_USB_PD_DECODE_SOP)
#error CONFIG_USB_PD_DECODE_SOP must be enabled with the TCPMV2 PD state machine
#endif
#endif /* CONFIG_USB_POWER_DELIVERY */

/******************************************************************************/
/*
 * If CONFIG_USB_PD_USB4 is enabled, make sure CONFIG_USBC_SS_MUX and
 * CONFIG_USB_PD_ALT_MODE_DFP is enabled for TCPM configs
 */
#ifdef CONFIG_USB_PD_USB4
#if !defined(CONFIG_USBC_SS_MUX) && !defined(CONFIG_USB_PD_CONTROLLER)
#error CONFIG_USBC_SS_MUX must be enabled for TCPM USB4 mode support
#endif
#endif

/******************************************************************************/
/*
 * If CONFIG_USBC_SS_MUX_DFP_ONLY is enabled, make sure
 * CONFIG_USB_PD_ALT_MODE_UFP is not enabled
 */
#if defined(CONFIG_USBC_SS_MUX_DFP_ONLY) && defined(CONFIG_USB_PD_ALT_MODE_UFP)
#error port cannot be UFP when CONFIG_USBC_SS_MUX_DFP_ONLY is enabled
#endif

/******************************************************************************/
/*
 * Automatically define CONFIG_USB_PD_FRS if FRS is enabled in the TCPC or PPC
 */
#if defined(CONFIG_USB_PD_FRS_PPC) || defined(CONFIG_USB_PD_FRS_TCPC)
#define CONFIG_USB_PD_FRS
#endif

/******************************************************************************/
/* Disable extended message support if PD 3.0 support is disabled. */
#ifndef CONFIG_USB_PD_REV30
#undef CONFIG_USB_PD_EXTENDED_MESSAGES
#endif

/******************************************************************************/
/*
 * PD 3.0 only retries in TCPC hardware twice (for a total of 3 attempts), while
 * PD 2.0 retires three times (for a total of 4 attempts).
 *
 * Note must be [0-3] since it must fit within 2 bits.
 * TODO(b/175236718): Set retry count dynamically based on active spec revision.
 */
#ifdef CONFIG_USB_PD_REV30
#define CONFIG_PD_RETRY_COUNT 2
#else
#define CONFIG_PD_RETRY_COUNT 3
#endif

/******************************************************************************/
/*
 * Ensure that CONFIG_USB_PD_TCPMV2 is being used with exactly one device type
 */
#ifdef CONFIG_USB_PD_TCPMV2
#if defined(CONFIG_USB_VPD) + defined(CONFIG_USB_CTVPD) + \
		defined(CONFIG_USB_DRP_ACC_TRYSRC) !=     \
	1
#error Must define exactly one CONFIG_USB_ device type.
#endif
#endif

/******************************************************************************/
/*
 * Ensure that CONFIG_USB_PD_TCPMV2 is not being used with charge_manager source
 * defines, and define a default number of 3.0 A ports if not selected.  Note
 * that the functionality of this default of 1 is equivalent to both previous
 * defines, which only ever allocated one 3.0 A port.
 *
 * To turn off the TCPMv2 3.0 A current allocation from the DPM, set
 * CONFIG_USB_PD_3A_PORTS to 0.
 */
#ifdef CONFIG_USB_PD_TCPMV2
#ifndef CONFIG_USB_PD_3A_PORTS
#define CONFIG_USB_PD_3A_PORTS 1
#endif
/* USB4 support requires at least one port providing 3.0 A */
#if defined(CONFIG_USB_PD_USB4) && CONFIG_USB_PD_3A_PORTS == 0
#error USB4 support requires at least one 3.0 A port
#endif
#endif

/******************************************************************************/
/*
 * Ensure CONFIG_USB_PD_TCPMV2 and CONFIG_USBC_SS_MUX, or
 * CONFIG_PLATFORM_EC_USB_PD_CONTROLLER are defined.
 * USBC retimer firmware update feature requires one of these.
 */
#if (defined(CONFIG_USBC_RETIMER_FW_UPDATE) &&                            \
     (!((defined(CONFIG_USB_PD_TCPMV2) && defined(CONFIG_USBC_SS_MUX)) || \
	defined(CONFIG_PLATFORM_EC_USB_PD_CONTROLLER))))
#error "Retimer firmware update requires TCPMv2 and USBC_SS_MUX, or " \
	"USB PD controller."
#endif

/* Set default USB PD power levels unless already defined by the platform. */
/*
 * Base configuration for PD power operating power value, which is used
 * in PD negotiation. The final PD parameter used in negotiation is
 * affected by CONFIG_USB_PD_MAX_POWER_MW,
 * CONFIG_USB_PD_MAX_CURRENT_MA, and CONFIG_USB_PD_MAX_VOLTAGE_MV.
 * Increase this value is the system requires more than 15 watts to boot
 * without a battery.
 */
#ifndef CONFIG_USB_PD_OPERATING_POWER_MW
#define CONFIG_USB_PD_OPERATING_POWER_MW 15000
#endif

/*
 * The maximum PD negotiated current for the system.
 */
#ifndef CONFIG_USB_PD_MAX_CURRENT_MA
#define CONFIG_USB_PD_MAX_CURRENT_MA 3000
#endif

/*
 * The maximum PD negotiated voltage for the system.
 */
#ifndef CONFIG_USB_PD_MAX_VOLTAGE_MV
#define CONFIG_USB_PD_MAX_VOLTAGE_MV 20000
#endif

/*
 * The maximum PD negotiated power for the system. By default, this
 * is derived from CONFIG_USB_PD_MAX_CURRENT_MA and CONFIG_PD_MAX_CURRENT_MV.
 */
#ifndef CONFIG_USB_PD_MAX_POWER_MW
#define CONFIG_USB_PD_MAX_POWER_MW \
	((CONFIG_USB_PD_MAX_CURRENT_MA * CONFIG_USB_PD_MAX_VOLTAGE_MV) / 1000)
#endif

/******************************************************************************/
/*
 * Automatically define CONFIG_HOSTCMD_X86 if either child option is defined.
 * Ensure LPC and eSPI are mutually exclusive
 */
#if defined(CONFIG_HOST_INTERFACE_LPC) || defined(CONFIG_HOST_INTERFACE_ESPI)
#define CONFIG_HOSTCMD_X86
#endif

#if defined(CONFIG_HOST_INTERFACE_LPC) && defined(CONFIG_HOST_INTERFACE_ESPI)
#error Must select only one type of host communication bus.
#endif

#if defined(CONFIG_HOSTCMD_X86) && !defined(CONFIG_HOST_INTERFACE_LPC) && \
	!defined(CONFIG_HOST_INTERFACE_ESPI)
#error Must select one type of host communication bus.
#endif

/******************************************************************************/
/*
 * Set default code ram size unless it's customized by the chip.
 */
#ifndef CONFIG_CODE_RAM_SIZE
#define CONFIG_CODE_RAM_SIZE CONFIG_RO_SIZE
#endif

/******************************************************************************/
/*
 * Set default data ram size unless it's customized by the chip.
 */
#ifndef CONFIG_DATA_RAM_SIZE
#define CONFIG_DATA_RAM_SIZE CONFIG_RAM_SIZE
#endif

/* Automatic configuration of RAM banks **************************************/
/* Assume one RAM bank if not specified, auto-compute number of banks        */
#ifndef CONFIG_RAM_BANK_SIZE
#define CONFIG_RAM_BANK_SIZE CONFIG_RAM_SIZE
#endif

#ifndef CONFIG_RAM_BANKS
#define CONFIG_RAM_BANKS (CONFIG_RAM_SIZE / CONFIG_RAM_BANK_SIZE)
#endif

/******************************************************************************/
/*
 * Store panic data at end of memory by default, unless otherwise
 * configured.  This is safe because we don't context switch away from
 * the panic handler before rebooting, and stacks and data start at
 * the beginning of RAM.
 */
#ifndef CONFIG_PANIC_DATA_SIZE
#define CONFIG_PANIC_DATA_SIZE sizeof(struct panic_data)
#endif

#ifndef CONFIG_PANIC_DATA_BASE
#define CONFIG_PANIC_DATA_BASE \
	(CONFIG_RAM_BASE + CONFIG_RAM_SIZE - CONFIG_PANIC_DATA_SIZE)
#endif

/******************************************************************************/
/*
 * Set minimum shared memory size, unless it is defined in board file.
 */
#ifndef CONFIG_SHAREDMEM_MINIMUM_SIZE
#ifdef CONFIG_COMMON_RUNTIME
/* If RWSIG is used, we may need more space. */
#if defined(CONFIG_RWSIG)
#define CONFIG_SHAREDMEM_MINIMUM_SIZE_RWSIG (CONFIG_RSA_KEY_SIZE / 8 * 3)
#else
#define CONFIG_SHAREDMEM_MINIMUM_SIZE_RWSIG 0
#endif

/*
 * We can't use the "MAX" function here, as it is too smart and BUILD_ASSERT
 * calls do not allow it as parameter. BUILD_MAX below works for both compiler
 * and linker.
 */
#define BUILD_MAX(x, y) ((x) > (y) ? (x) : (y))

/* Minimum: 1kb */
#define CONFIG_SHAREDMEM_MINIMUM_SIZE \
	BUILD_MAX(1024, CONFIG_SHAREDMEM_MINIMUM_SIZE_RWSIG)
#else /* !CONFIG_COMMON_RUNTIME */
/* Without common runtime, we do not have support for shared memory. */
#define CONFIG_SHAREDMEM_MINIMUM_SIZE 0
#endif
#endif /* !CONFIG_SHAREDMEM_MINIMUM_SIZE */

/******************************************************************************/
/*
 * Thermal throttling AP must have temperature sensor enabled to get
 * the temperature readings.
 */
#if defined(CONFIG_THROTTLE_AP) && !defined(CONFIG_TEMP_SENSOR)
#define CONFIG_TEMP_SENSOR
#endif

/******************************************************************************/
/*
 * DPTF must have temperature sensor enabled to get the readings for
 * generating DPTF thresholds events.
 */
#if defined(CONFIG_DPTF) && !defined(CONFIG_TEMP_SENSOR)
#define CONFIG_TEMP_SENSOR
#endif

/******************************************************************************/
/* The Matrix Keyboard Protocol depends on MKBP input devices and events. */
#ifdef CONFIG_KEYBOARD_PROTOCOL_MKBP
#define CONFIG_MKBP_INPUT_DEVICES
#endif

#if defined(CONFIG_KEYBOARD_PROTOCOL_MKBP) || defined(CONFIG_MKBP_INPUT_DEVICES)
#define CONFIG_MKBP_EVENT
#endif

/******************************************************************************/
/* MKBP events delivery methods. */
#ifdef CONFIG_MKBP_EVENT
/* clang-format off */
#if !defined(CONFIG_MKBP_USE_CUSTOM) &&                  \
	!defined(CONFIG_MKBP_USE_HOST_EVENT) &&          \
	!defined(CONFIG_MKBP_USE_GPIO) &&                \
	!defined(CONFIG_MKBP_USE_GPIO_AND_HOST_EVENT) && \
	!defined(CONFIG_MKBP_USE_HECI) &&                \
	!defined(CONFIG_MKBP_USE_USB)
#error Please define one of CONFIG_MKBP_USE_* macro.
#endif
/* clang-format on */

#if defined(CONFIG_MKBP_USE_CUSTOM) + defined(CONFIG_MKBP_USE_GPIO) + \
		defined(CONFIG_MKBP_USE_HOST_EVENT) +                 \
		defined(CONFIG_MKBP_USE_HOST_HECI) +                  \
		defined(CONFIG_MKBP_USE_HOST_USB) >                   \
	1
#error Must select only one type of MKBP event delivery method.
#endif
#endif /* CONFIG_MKBP_EVENT */

/******************************************************************************/
/* Set generic orientation config if a specific orientation config is set. */
#if defined(CONFIG_KX022_ORIENTATION_SENSOR) || \
	defined(CONFIG_BMI_ORIENTATION_SENSOR)
#ifndef CONFIG_ACCEL_FIFO
#error CONFIG_ACCEL_FIFO must be defined to use hw orientation sensor support
#endif
#define CONFIG_ORIENTATION_SENSOR
#endif

/*****************************************************************************/
/* Define CONFIG_BATTERY if board has a battery. */
#if defined(CONFIG_BATTERY_BQ20Z453) || defined(CONFIG_BATTERY_BQ27541) ||    \
	defined(CONFIG_BATTERY_BQ27621) || defined(CONFIG_BATTERY_BQ4050) ||  \
	defined(CONFIG_BATTERY_MAX17055) || defined(CONFIG_BATTERY_MM8013) || \
	defined(CONFIG_BATTERY_SMART)
#define CONFIG_BATTERY
#endif

#if defined(CONFIG_BATTERY_CUTOFF_VOL_UP_DISABLED) && \
	!defined(CONFIG_VOLUME_BUTTONS)
#error "VOLUME_BUTTONS must be defined to use BATTERY_CUTOFF_VOL_UP_DISABLED"
#endif

#if defined(CONFIG_CBI_EEPROM) || defined(CONFIG_CBI_FLASH)
#if defined(CONFIG_BATTERY) && defined(CONFIG_BATTERY_FUEL_GAUGE)
#define CONFIG_BATTERY_CONFIG_IN_CBI
#endif
#endif

/******************************************************************************/
/*
 * Ensure CONFIG_USB_PD_RESET_PRESERVE_RECOVERY_FLAGS is only used on
 * chromeboxes.
 */
#if defined(CONFIG_USB_PD_RESET_PRESERVE_RECOVERY_FLAGS) && \
	defined(CONFIG_BATTERY)
#error Only use CONFIG_USB_PD_RESET_PRESERVE_RECOVERY_FLAGS on chromeboxes.
#endif

/*****************************************************************************/
/* Define CONFIG_USBC_PPC if board has a USB Type-C Power Path Controller. */
#if defined(CONFIG_USBC_PPC_AOZ1380) || defined(CONFIG_USBC_PPC_NX20P3483) || \
	defined(CONFIG_USBC_PPC_SN5S330) || defined(CONFIG_USBC_PPC_TCPCI)
#define CONFIG_USBC_PPC
#endif /* "has a PPC" */

/* Following chips use Power Path Control information from TCPC chip */
#if defined(CONFIG_USBC_PPC_AOZ1380) || defined(CONFIG_USBC_PPC_NX20P3481) || \
	defined(CONFIG_USBC_PPC_NX20P3483) || defined(CONFIG_USBC_PPC_TCPCI)
#define CONFIG_USB_PD_PPC
#endif

/* The TI SN5S330 supports VCONN and needs to be informed of CC polarity */
#if defined(CONFIG_USBC_PPC_SN5S330)
#define CONFIG_USBC_PPC_POLARITY
#define CONFIG_USBC_PPC_SBU
#define CONFIG_USBC_PPC_VCONN
#endif

/*****************************************************************************/
/* PPC SYV682C is a subset of SYV682X. */
#if defined(CONFIG_USBC_PPC_SYV682C)
#define CONFIG_USBC_PPC_SYV682X
#endif

/*
 * The SYV682X supports VCONN and needs to be informed of CC polarity.
 * There is a 3.6V limit on the HOST_CC signals, so the TCPC should not source
 * 5V VCONN.
 *
 * For the ITE integrated TCPC, it wants to be notified of VCONN but won't
 * source VCONN itself, so is safe to keep enabled.
 */
#if defined(CONFIG_USBC_PPC_SYV682X)
#define CONFIG_USBC_PPC_POLARITY
#define CONFIG_USBC_PPC_VCONN
#if !defined(CONFIG_USB_PD_TCPM_ITE_ON_CHIP) && \
	!defined(CONFIG_USBC_PPC_SYV682X_NO_CC)
#undef CONFIG_USB_PD_TCPC_VCONN
#endif
#endif

/* CCGXXF standard default defines */
#if defined(CONFIG_USB_PD_TCPM_CCGXXF)
#define CONFIG_USB_PD_DISCHARGE_TCPC
#define CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE
#define CONFIG_USB_PD_PPC
#define CONFIG_USB_PD_TCPM_SBU
#define CONFIG_USB_PD_TCPC_LOW_POWER
#define CONFIG_USB_PD_TCPM_TCPCI
#define CONFIG_USB_PD_VBUS_DETECT_TCPC
#endif

/*****************************************************************************/
/* Define CONFIG_USBC_OCP if a component can detect overcurrent */
#if defined(CONFIG_USBC_PPC_AOZ1380) || defined(CONFIG_USBC_PPC_KTU1125) ||   \
	defined(CONFIG_USBC_PPC_NX20P3481) ||                                 \
	defined(CONFIG_USBC_PPC_NX20P3483) ||                                 \
	defined(CONFIG_USBC_PPC_SN5S330) ||                                   \
	defined(CONFIG_USBC_PPC_SYV682X) || defined(CONFIG_CHARGER_SM5803) || \
	defined(CONFIG_USB_PD_TCPM_TCPCI) ||                                  \
	defined(CONFIG_USB_PD_TCPM_ANX7406)
#define CONFIG_USBC_OCP
#endif

/*****************************************************************************/
/*
 * Define CONFIG_USB_PD_VBUS_MEASURE_TCPC if the tcpc on the board supports
 * VBUS measurement.
 */
#if defined(CONFIG_USB_PD_TCPM_FUSB302) && \
	!defined(CONFIG_USB_PD_VBUS_MEASURE_CHARGER)
#define CONFIG_USB_PD_VBUS_MEASURE_TCPC
#endif

/*****************************************************************************/
/*
 * Define CONFIG_USB_PD_TCPC_ON_CHIP if we use ITE series TCPM driver
 * on the board.
 *
 * NOTE: If we don't use all the ITE pd ports on a board, then we need to
 *       start from port0 to use the ITE pd port. If we start from port1,
 *       then port1 HOOK function never works.
 */
#ifdef CONFIG_USB_PD_TCPM_ITE_ON_CHIP
#define CONFIG_USB_PD_TCPC_ON_CHIP
#if !defined(CONFIG_USB_PD_TCPM_DRIVER_IT8XXX2) && \
	!defined(CONFIG_USB_PD_TCPM_DRIVER_IT83XX)
#error "No drivers for ITE ON CHIP"
#endif
#endif

/*****************************************************************************/
/*
 * Define CONFIG_CHARGER_NARROW_VDC for chargers that use a Narrow VDC power
 * architecture.
 */
#if defined(CONFIG_CHARGER_ISL9237) || defined(CONFIG_CHARGER_ISL9238) ||      \
	defined(CONFIG_CHARGER_ISL9238C) || defined(CONFIG_CHARGER_ISL9241) || \
	defined(CONFIG_CHARGER_RAA489000) || defined(CONFIG_CHARGER_SM5803) || \
	defined(CONFIG_CHARGER_BQ25710) || defined(CONFIG_CHARGER_BQ25720) ||  \
	defined(CONFIG_CHARGER_BQ25770) ||                                     \
	defined(CONFIG_CHARGER_RAA489110) || defined(CONFIG_CHARGER_RT9490) || \
	defined(CONFIG_CHARGER_RT9478)
#define CONFIG_CHARGER_NARROW_VDC
#endif

/*****************************************************************************/
/*
 * Define CONFIG_PRECHARGE_DELAY_MS 150ms which is the debounce
 * time after VADP >3.2V for the first time adapter plugged in.
 */
#ifdef CONFIG_CHARGER_ISL9238
#ifndef CONFIG_PRECHARGE_DELAY_MS
#define CONFIG_PRECHARGE_DELAY_MS 150
#endif
#endif

/*****************************************************************************/
/*
 * Define CONFIG_BUTTON_TRIGGERED_RECOVERY if a board has a dedicated recovery
 * button.
 */
#ifdef CONFIG_DEDICATED_RECOVERY_BUTTON
#define CONFIG_BUTTON_TRIGGERED_RECOVERY
#endif /* defined(CONFIG_DEDICATED_RECOVERY_BUTTON) */

#ifdef CONFIG_LED_PWM_ACTIVE_CHARGE_PORT_ONLY
#define CONFIG_LED_PWM_CHARGE_STATE_ONLY
#endif

/* Define for to turn off power LED in suspend for boards shipped after 2022 */
#undef CONFIG_LED_PWM_OFF_IN_SUSPEND

/*****************************************************************************/
/*
 * Define derived configuration options for EC-EC communication
 */
#ifdef CONFIG_EC_EC_COMM_BATTERY
#ifdef CONFIG_EC_EC_COMM_CLIENT
#define CONFIG_EC_EC_COMM_BATTERY_CLIENT
#define CONFIG_BATTERY_INFO
#define CONFIG_BATTERY_COUNT 2
#endif

#ifdef CONFIG_EC_EC_COMM_SERVER
#define CONFIG_EC_EC_COMM_BATTERY_SERVER
#define CONFIG_BATTERY_INFO
#define CONFIG_BATTERY_COUNT 1
#endif
#endif /* CONFIG_EC_EC_COMM_BATTERY */

/*****************************************************************************/
/* Auto-enable battery v2 module with a single battery if battery is defined. */
#if defined(CONFIG_BATTERY) && !defined(CONFIG_BATTERY_INFO)
#define CONFIG_BATTERY_COUNT 1
#define CONFIG_BATTERY_INFO
#endif

/*
 * Check the specific battery status to judge whether the battery is
 * initialized and stable when the battery wakes up from ship mode.
 * Use two MASKs to provide logical AND and logical OR options for different
 * status. For example:
 *
 * Logical OR -- just check one of TCA/TDA mask:
 *   #define CONFIG_BATT_ALARM_MASK1 \
 *       (STATUS_TERMINATE_CHARGE_ALARM | STATUS_TERMINATE_DISCHARGE_ALARM)
 *   #define CONFIG_BATT_ALARM_MASK2 0xFFFF
 *
 * Logical AND -- check both TCA/TDA mask:
 *   #define CONFIG_BATT_ALARM_MASK1 STATUS_TERMINATE_CHARGE_ALARM
 *   #define CONFIG_BATT_ALARM_MASK2 STATUS_TERMINATE_DISCHARGE_ALARM
 *
 * The default configuration is logical OR.
 */
#ifdef CONFIG_BATTERY_STBL_STAT
#ifndef CONFIG_BATT_ALARM_MASK1
#define CONFIG_BATT_ALARM_MASK1 \
	(STATUS_TERMINATE_CHARGE_ALARM | STATUS_TERMINATE_DISCHARGE_ALARM)
#endif
#ifndef CONFIG_BATT_ALARM_MASK2
#define CONFIG_BATT_ALARM_MASK2 0xFFFF
#endif
#endif

/*****************************************************************************/
/* Define derived USB PD Discharge common path */
#if defined(CONFIG_USB_PD_DISCHARGE_GPIO) ||     \
	defined(CONFIG_USB_PD_DISCHARGE_TCPC) || \
	defined(CONFIG_USB_PD_DISCHARGE_PPC)
#define CONFIG_USB_PD_DISCHARGE
#endif

/*****************************************************************************/
/* Define derived config options for DP HPD GPIO */
#ifdef CONFIG_USB_PD_DP_HPD_GPIO_CUSTOM
#define CONFIG_USB_PD_DP_HPD_GPIO
#endif

/*****************************************************************************/
/* Define derived thermistor common path */
#ifdef CONFIG_THERMISTOR_NCP15WB
#define CONFIG_THERMISTOR
#endif

/*****************************************************************************/
/* Define derived config options for BC1.2 detection */
#ifdef CONFIG_BC12_DETECT_PI3USB9201
#define CONFIG_BC12_DETECT_DATA_ROLE_TRIGGER
#undef CONFIG_BC12_CLIENT_MODE_ONLY_PI3USB9201
#endif

/*****************************************************************************/
/*
 * Handle task-dependent configs.
 *
 * This prevent sub-modules from being compiled when the task and parent module
 * are not present.
 */

#ifndef HAS_TASK_CHIPSET
#undef CONFIG_AP_HANG_DETECT
#undef CONFIG_CHIPSET_ALDERLAKE
#undef CONFIG_CHIPSET_APOLLOLAKE
#undef CONFIG_CHIPSET_CANNONLAKE
#undef CONFIG_CHIPSET_COMETLAKE
#undef CONFIG_CHIPSET_GEMINILAKE
#undef CONFIG_CHIPSET_ICELAKE
#undef CONFIG_CHIPSET_JASPERLAKE
#undef CONFIG_CHIPSET_MT817X
#undef CONFIG_CHIPSET_MT8183
#undef CONFIG_CHIPSET_MT8192
#undef CONFIG_CHIPSET_CEZANNE
#undef CONFIG_CHIPSET_SDM845
#undef CONFIG_CHIPSET_SKYLAKE
#undef CONFIG_CHIPSET_STONEY
#undef CONFIG_CHIPSET_TIGERLAKE
#undef CONFIG_POWER_COMMON
#endif

/*
 * If a board has a chipset task, set the minimum charger power required for
 * powering on to 15W.  This is also the highest power discovered over Type-C by
 * analog signaling.  The EC normally does not communicate using USB PD when the
 * system is locked and in RO, so it would not be able to tell if higher power
 * is available.  However, if a 15W charger is discovered, it's likely that the
 * charger does speak USB PD and we would be able to negotiate more power after
 * booting the AP and jumping to EC RW.
 *
 * If a board needs more or less power to power on, they can re-define this
 * value in their board.h file.
 */
#ifdef HAS_TASK_CHIPSET
#ifndef CONFIG_CHARGER_MIN_POWER_MW_FOR_POWER_ON
#define CONFIG_CHARGER_MIN_POWER_MW_FOR_POWER_ON 15000
#endif /* !defined(CONFIG_CHARGER_MIN_POWER_MW_FOR_POWER_ON) */
#endif /* defined(HAS_TASK_CHIPSET) */

#ifdef CONFIG_CHARGER_LIMIT_POWER_THRESH_CHG_MW
#ifndef CONFIG_CHARGER_LIMIT_POWER_THRESH_BAT_PCT
#define CONFIG_CHARGER_LIMIT_POWER_THRESH_BAT_PCT \
	(CONFIG_CHARGER_MIN_BAT_PCT_FOR_POWER_ON)
#endif
#endif

#ifndef CONFIG_CHARGER_MIN_BAT_PCT_IMBALANCED_POWER_ON
/*
 * The function of MEASURE_BATTERY_IMBALANCE and these variables is to prevent a
 * battery brownout when the management IC reports a state of charge that is
 * higher than CHARGER_MIN_BAT_PCT_FOR_POWER_ON, but an individual cell is lower
 * than the rest of the pack.  The critical term is MAX_IMBALANCE_MV, which must
 * be small enough to ensure that the system can reliably boot even when the
 * battery total state of charge barely passes the
 * CHARGER_MIN_BAT_PCT_FOR_POWER_ON threshold.
 *
 * Lowering CHARGER_MIN_BAT_PCT_IMBALANCED_POWER_ON below
 * CHARGER_MIN_BAT_PCT_FOR_POWER_ON disables this check.  Raising it too high
 * may needlessly prevent boot when the lowest cell can still support the
 * system.
 *
 * As this term is lowered and BATTERY_MAX_IMBALANCE_MV is raised, the risk of
 * cell-undervoltage brownout during startup increases.  Raising this term and
 * lowering MAX_IMBALANCE_MV increases the risk of poor UX when the user must
 * wait longer to turn on their device.
 */
#define CONFIG_CHARGER_MIN_BAT_PCT_IMBALANCED_POWER_ON 5
#endif

#ifndef CONFIG_BATTERY_MAX_IMBALANCE_MV
/*
 * WAG.  Imbalanced battery packs in this situation appear to have balanced
 * charge very quickly after beginning the charging cycle, since dV/dQ rapidly
 * decreases as the cell is charged out of deep discharge.  Increasing the value
 * of CHARGER_MIN_BAT_PCT_IMBALANCED_POWER_ON will make a system tolerant of
 * larger values of BATTERY_MAX_IMBALANCE_MV.
 */
#define CONFIG_BATTERY_MAX_IMBALANCE_MV 200
#endif

#ifndef HAS_TASK_KEYPROTO
#undef CONFIG_KEYBOARD_PROTOCOL_8042
/*
 * Note that we don't undef CONFIG_KEYBOARD_PROTOCOL_MKBP, because it doesn't
 * have its own task.
 */
#endif

#ifndef HAS_TASK_PDCMD
#undef CONFIG_HOSTCMD_PD
#endif

#if defined(HAS_TASK_PD_INT_C0) || defined(HAS_TASK_PD_INT_C1) || \
	defined(HAS_TASK_PD_INT_C2) || defined(HAS_TASK_PD_INT_C3)
#define CONFIG_HAS_TASK_PD_INT
#endif

#if defined(HAS_TASK_PDCMD) && defined(CONFIG_HAS_TASK_PD_INT)
#error Should not use PDCMD task with PD INT tasks
#endif

/* Certain console cmds are irrelevant without parent modules. */
#ifndef CONFIG_BATTERY
#undef CONFIG_CMD_PWR_AVG
#endif

#ifndef CONFIG_ADC
#undef CONFIG_CMD_ADC
#endif

/*****************************************************************************/
/* Define derived Chipset configs */
#if defined(CONFIG_CHIPSET_APOLLOLAKE) || defined(CONFIG_CHIPSET_GEMINILAKE)
#define CONFIG_CHIPSET_APL_GLK
#endif

#if defined(CONFIG_CHIPSET_JASPERLAKE) || defined(CONFIG_CHIPSET_TIGERLAKE) || \
	defined(CONFIG_CHIPSET_ALDERLAKE)
#define CONFIG_CHIPSET_ICELAKE
#endif

#if defined(CONFIG_CHIPSET_APL_GLK)
#define CONFIG_CHIPSET_HAS_PRE_INIT_CALLBACK
#define CONFIG_CHIPSET_X86_RSMRST_AFTER_S5
#endif

#if defined(CONFIG_CHIPSET_ALDERLAKE_SLG4BD44540) ||  \
	defined(CONFIG_CHIPSET_APOLLOLAKE) ||         \
	defined(CONFIG_CHIPSET_CANNONLAKE) ||         \
	defined(CONFIG_CHIPSET_COMETLAKE) ||          \
	defined(CONFIG_CHIPSET_COMETLAKE_DISCRETE) || \
	defined(CONFIG_CHIPSET_GEMINILAKE) ||         \
	defined(CONFIG_CHIPSET_ICELAKE) || defined(CONFIG_CHIPSET_SKYLAKE)
#define CONFIG_POWER_COMMON
#endif

#if defined(CONFIG_CHIPSET_ALDERLAKE_SLG4BD44540) || \
	defined(CONFIG_CHIPSET_CANNONLAKE) ||        \
	defined(CONFIG_CHIPSET_ICELAKE) || defined(CONFIG_CHIPSET_SKYLAKE)
#define CONFIG_CHIPSET_X86_RSMRST_DELAY
#endif

#if defined(CONFIG_HOST_INTERFACE_ESPI_VW_SLP_S3) && \
	defined(CONFIG_CHIPSET_SLP_S3_L_OVERRIDE)
#error "Cannot use CONFIG_CHIPSET_SLP_S3_L_OVERRIDE if SLP_S3 is a virtual wire"
#endif

#if defined(CONFIG_POWER_S0IX) && !defined(CONFIG_POWER_TRACK_HOST_SLEEP_STATE)
#error "Must enable CONFIG_POWER_TRACK_HOST_SLEEP_STATE for S0ix"
#endif

#if defined(CONFIG_CHIPSET_SC7180) || defined(CONFIG_CHIPSET_SC7280) || \
	defined(CONFIG_CHIPSET_QC_EXP)
#if defined(CONFIG_POWER_SLEEP_FAILURE_DETECTION) && \
	!defined(CONFIG_CHIPSET_RESUME_INIT_HOOK)
#error "Require resume init hook to enable sleep failure detection"
#endif
#if !defined(CONFIG_POWER_SLEEP_FAILURE_DETECTION) && \
	defined(CONFIG_CHIPSET_RESUME_INIT_HOOK)
#error "Don't enable resume init hook unless for sleep failure detection"
#endif
#endif

/*****************************************************************************/

/*
 * Automatically define CONFIG_ACCEL_LIS2D_COMMON if a child option is defined.
 */
#if defined(CONFIG_ACCEL_LIS2DH) || defined(CONFIG_ACCEL_LIS2DE) || \
	defined(CONFIG_ACCEL_LNG2DM)
#define CONFIG_ACCEL_LIS2D_COMMON
#endif

/*
 * Automatically define CONFIG_ACCEL_LIS2DW_COMMON if a child option is defined.
 */
#if defined(CONFIG_ACCEL_LIS2DW12) || defined(CONFIG_ACCEL_LIS2DWL)
#define CONFIG_ACCEL_LIS2DW_COMMON
#endif

/*
 * CONFIG_ACCEL_LIS2DW12 and CONFIG_ACCEL_LIS2DWL can't be defined at the same
 * time.
 */
#if defined(CONFIG_ACCEL_LIS2DW12) && defined(CONFIG_ACCEL_LIS2DWL)
#error "Define only one of CONFIG_ACCEL_LIS2DW12 and CONFIG_ACCEL_LIS2DWL"
#endif

/*****************************************************************************/
/* Define derived seven segment display common path */
#ifdef CONFIG_MAX695X_SEVEN_SEGMENT_DISPLAY
#define CONFIG_SEVEN_SEG_DISPLAY
#endif /* CONFIG_MAX695X_SEVEN_SEGMENT_DISPLAY */

/*****************************************************************************/
/* Enable PCIE tunneling if the board supports Thunderbolt-Compatible mode */
#ifdef CONFIG_USB_PD_TBT_COMPAT_MODE
#define CONFIG_USB_PD_PCIE_TUNNELING
#define CONFIG_USB_PD_TBT_GEN3_CAPABLE
#endif /* CONFIG_USB_PD_TBT_COMPAT_MODE */

/*
 * CONFIG_CHIP_INIT_ROM_REGION requires that the chip has defined a
 * ROM resident region to store the .init_rom section.
 *
 * These sections must also not be zero bytes, which will happen if
 * the program size is the same as the flash size.
 */
#ifdef CONFIG_CHIP_INIT_ROM_REGION

#ifndef CONFIG_FLASH_CROS
#error CONFIG_CHIP_INIT_ROM_REGION requires CONFIG_FLASH_CROS
#endif

#ifndef CONFIG_RO_ROM_RESIDENT_SIZE
#error CONFIG_CHIP_INIT_ROM_REGION requires CONFIG_RO_ROM_RESIDENT_SIZE
#endif

#ifndef CONFIG_RW_ROM_RESIDENT_SIZE
#error CONFIG_CHIP_INIT_ROM_REGION requires CONFIG_RW_ROM_RESIDENT_SIZE
#endif

#if (CONFIG_RO_ROM_RESIDENT_SIZE == 0)
#error CONFIG_RO_ROM_RESIDENT_SIZE is 0 with CONFIG_CHIP_INIT_ROM_REGION defined
#endif

#if (CONFIG_RW_ROM_RESIDENT_SIZE == 0)
#error CONFIG_RW_ROM_RESIDENT_SIZE is 0 with CONFIG_CHIP_INIT_ROM_REGION defined
#endif

/*
 * By default, enable storing the .data section on the ROM resident area to
 * save flash space.
 */
#ifdef CONFIG_MAPPED_STORAGE
#define CONFIG_CHIP_DATA_IN_INIT_ROM
#endif
#endif /* CONFIG_CHIP_INIT_ROM_REGION */

/*
 * By default, enable a request for an ACK from AP, on setting the mux, if the
 * board supports Intel retimer.
 */
#if (defined(CONFIG_USBC_RETIMER_INTEL_BB) ||  \
     defined(CONFIG_USBC_RETIMER_INTEL_HB)) && \
	defined(CONFIG_USB_MUX_VIRTUAL)
#define CONFIG_USB_MUX_AP_ACK_REQUEST
#endif /* CONFIG_USBC_RETIMER_INTEL_BB || CONFIG_USBC_RETIMER_INTEL_HB */

/* Enable retimer console command */
#if (defined(CONFIG_USBC_RETIMER_INTEL_BB) || \
     defined(CONFIG_USBC_RETIMER_KB800X))
#define CONFIG_CMD_RETIMER
#endif

/**
 * CONFIG_CMD_CRASH_NESTED depends on CONFIG_CMD_CRASH
 */
#if !defined(CONFIG_CMD_CRASH) && defined(CONFIG_CMD_CRASH_NESTED)
#error "CONFIG_CMD_CRASH_NESTED depends on CONFIG_CMD_CRASH"
#endif

/*****************************************************************************/

/*
 * Apply test config overrides last, since tests need to
 * override some of the config flags in non-standard ways to mock only parts of
 * the system.
 */
#ifdef TEST_BUILD
#include "test_config.h"
#endif

/*
 * Validity checks to make sure some of the configs above make sense.
 */

/*
 * Chromium ec uses hook tick to reload the watchdog. The interval between
 * reloads of the watchdog timer should be less than half of the watchdog
 * period.
 */
#ifdef CONFIG_WATCHDOG
#if (CONFIG_AUX_TIMER_PERIOD_MS) < ((HOOK_TICK_INTERVAL_MS) * 2)
#error "CONFIG_AUX_TIMER_PERIOD_MS must be at least 2x HOOK_TICK_INTERVAL_MS"
#endif
#endif

#ifdef CONFIG_USB_SERIALNO
#define CONFIG_SERIALNO_LEN 28
#endif

#ifdef CONFIG_MAC_ADDR
#define CONFIG_MAC_ADDR_LEN 20
#endif

#ifndef CONFIG_EC_MAX_SENSOR_FREQ_MILLIHZ
#define CONFIG_EC_MAX_SENSOR_FREQ_MILLIHZ \
	CONFIG_EC_MAX_SENSOR_FREQ_DEFAULT_MILLIHZ
#endif

/* Enable BMI secondary port if needed. */
#if defined(CONFIG_MAG_BMI_BMM150) || defined(CONFIG_MAG_BMI_LIS2MDL)
#define CONFIG_BMI_SEC_I2C
#endif

/* Enable LSM2MDL secondary port if needed. */
#if defined(CONFIG_MAG_LSM6DSM_BMM150) || defined(CONFIG_MAG_LSM6DSM_LIS2MDL)
#define CONFIG_LSM6DSM_SEC_I2C
#endif

/* Load LIS2MDL driver if needed */
#if defined(CONFIG_MAG_BMI_LIS2MDL) || defined(CONFIG_MAG_LSM6DSM_LIS2MDL)
#define CONFIG_MAG_LIS2MDL
#ifndef CONFIG_ACCELGYRO_SEC_ADDR_FLAGS
#error "The i2c address of the magnetometer is not set."
#endif
#endif

/* Load BMM150 driver if needed */
#if defined(CONFIG_MAG_BMI_BMM150) || defined(CONFIG_MAG_LSM6DSM_BMM150)
#define CONFIG_MAG_BMM150
#ifndef CONFIG_ACCELGYRO_SEC_ADDR_FLAGS
#error "The i2c address of the magnetometer is not set."
#endif
#endif

/* Verify sensorhub is enabled */
#ifdef CONFIG_MAG_LSM6DSM_LIS2MDL
#ifndef CONFIG_SENSORHUB_LSM6DSM
#error "Enable SENSORHUB_LSM6DSM."
#endif
#endif

/* Fill LPC sense data on X86 architecture. */
#ifdef CONFIG_HOSTCMD_X86
#define CONFIG_MOTION_FILL_LPC_SENSE_DATA
#endif

/*
 * TODO(crbug.com/888109): Makes sure RDP as PSTATE is only enabled where it
 * makes sense.
 */
#ifdef CONFIG_FLASH_READOUT_PROTECTION_AS_PSTATE
#ifdef CONFIG_FLASH_PSTATE
#error "Flash readout protection and PSTATE may not work as intended."
#endif

#if !defined(CHIP_FAMILY_STM32H7) && !defined(CHIP_FAMILY_STM32F4) && \
	!defined(CHIP_FAMILY_NPCX9)
#error "Flash readout protection only implemented on STM32H7, STM32F4 and NPCX9"
#endif
#endif /* CONFIG_FLASH_READOUT_PROTECTION_AS_PSTATE */

#if defined(CONFIG_USB_PD_TCPM_ANX3429) ||     \
	defined(CONFIG_USB_PD_TCPM_ANX740X) || \
	defined(CONFIG_USB_PD_TCPM_ANX7471)
/* Note: ANX7447 is handled by its own driver, not ANX74XX. */
#define CONFIG_USB_PD_TCPM_ANX74XX
#endif

#if defined(CONFIG_DPTF_MULTI_PROFILE) && !defined(CONFIG_DPTF)
#error "CONFIG_DPTF_MULTI_PROFILE can be set only when CONFIG_DPTF is set."
#endif /* CONFIG_DPTF_MULTI_PROFILE && !CONFIG_DPTF */

/*
 * The EC monitors the AP suspend/resume process using:
 * - EC_CMD_HOST_SLEEP_EVENT (0x00A9)
 * - SLP_S0 signal
 *
 * When the AP starts the suspend process, it sends EC_CMD_HOST_SLEEP_EVENT to
 * signal to the EC that a suspend has begun. This starts the EC's timer, which
 * uses CONFIG_SLEEP_TIMEOUT_MS to determine how long to wait for the suspend to
 * complete (by monitoring SLP_S0) before considering the AP "hung". Similarly,
 * when a resume is begun, the EC starts a timer using the same
 * CONFIG_SLEEP_TIMEOUT_MS value and waits for the AP to send
 * EC_CMD_HOST_SLEEP_EVENT to indicate the resume has completed.
 *
 * For AMD Systems:
 * If the EC hits the timeout value CONFIG_SLEEP_TIMEOUT_MS, the AP is
 * considered "hung" and the EC begins the recovery process. If
 * CONFIG_POWER_SLEEP_FAILURE_DETECTION is enabled for the board, the EC will
 * send the Host Event EC_HOST_EVENT_HANG_DETECT, possibly triggering recovery
 * within the AP, and then start a timer to wait CONFIG_HARD_SLEEP_HANG_TIMEOUT.
 * If the AP fails to complete the sleep step within
 * CONFIG_HARD_SLEEP_HANG_TIMEOUT, the EC will forcefully reset the AP to
 * complete recovery.
 */

/*
 * Define the timeout in milliseconds between when the EC receives a suspend
 * command and when the EC times out and asserts wake because the sleep signal
 * SLP_S0 did not assert.
 */
#ifndef CONFIG_SLEEP_TIMEOUT_MS
#define CONFIG_SLEEP_TIMEOUT_MS 10000
#endif

/*
 * Define the timeout in milliseconds between when the EC |SysRq| to the AP
 * and when the AP is forcibly reset because it didn't reboot on its own.
 */
#ifndef CONFIG_HARD_SLEEP_HANG_TIMEOUT
#define CONFIG_HARD_SLEEP_HANG_TIMEOUT 10000
#endif

#ifdef CONFIG_PWM_KBLIGHT
#define CONFIG_KEYBOARD_BACKLIGHT
#endif

/*****************************************************************************/
/* ISH power management related definitions */
#if defined(CONFIG_ISH_PM_D0I2) || defined(CONFIG_ISH_PM_D0I3) || \
	defined(CONFIG_ISH_PM_D3) || defined(CONFIG_ISH_PM_RESET_PREP)

#ifndef CONFIG_LOW_POWER_IDLE
#error "Must define CONFIG_LOW_POWER_IDLE if enable ISH low power states"
#endif

#define CONFIG_ISH_PM_AONTASK

#endif

#ifdef CONFIG_ACCEL_FIFO
#if !defined(CONFIG_ACCEL_FIFO_SIZE) || !defined(CONFIG_ACCEL_FIFO_THRES)
#error "Using CONFIG_ACCEL_FIFO, must define _SIZE and _THRES"
#endif

#endif /* CONFIG_ACCEL_FIFO */

/*
 * If USB PD Discharge is enabled, verify that CONFIG_USB_PD_DISCHARGE_GPIO
 * and CONFIG_USB_PD_PORT_MAX_COUNT, CONFIG_USB_PD_DISCHARGE_TCPC, or
 * CONFIG_USB_PD_DISCHARGE_PPC is defined.
 */
#ifndef CONFIG_TEST_ENABLE_USB_PD_DISCHARGE
#ifdef CONFIG_USB_PD_DISCHARGE
#ifdef CONFIG_USB_PD_DISCHARGE_GPIO
#if !defined(CONFIG_USB_PD_PORT_MAX_COUNT)
#error "PD discharge port not defined"
#endif
#else
#if !defined(CONFIG_USB_PD_DISCHARGE_TCPC) && \
	!defined(CONFIG_USB_PD_DISCHARGE_PPC)
#error "PD discharge implementation not defined"
#endif
#endif /* CONFIG_USB_PD_DISCHARGE_GPIO */
#endif /* CONFIG_USB_PD_DISCHARGE */
#endif /* CONFIG_TEST_ENABLE_USB_PD_DISCHARGE */

/* Chargesplash defaults */
#ifdef CONFIG_CHARGESPLASH
#ifndef CONFIG_CHARGESPLASH_PERIOD
#define CONFIG_CHARGESPLASH_PERIOD 900
#endif
#ifndef CONFIG_CHARGESPLASH_MAX_REQUESTS_PER_PERIOD
#define CONFIG_CHARGESPLASH_MAX_REQUESTS_PER_PERIOD 5
#endif
#endif

/* EC Codec Wake-on-Voice related definitions */
#ifdef CONFIG_AUDIO_CODEC_WOV
#define CONFIG_SHA256_SW
#endif

#ifdef CONFIG_SMBUS_PEC
#define CONFIG_CRC8_CROS
#endif

/*
 *  Vivaldi keyboard code to be enabled only if board has selected
 *  CONFIG_KEYBOARD_PROTOCOL_8042 and not disabled CONFIG_KEYBOARD_VIVALDI
 *  explicitly
 */
#ifndef CONFIG_KEYBOARD_PROTOCOL_8042
#undef CONFIG_KEYBOARD_VIVALDI
#endif

#if defined(CONFIG_USB_PD_TCPM_MULTI_PS8XXX)
#if defined(CONFIG_USB_PD_TCPM_PS8705) + defined(CONFIG_USB_PD_TCPM_PS8751) + \
		defined(CONFIG_USB_PD_TCPM_PS8755) +                          \
		defined(CONFIG_USB_PD_TCPM_PS8805) +                          \
		defined(CONFIG_USB_PD_TCPM_PS8815) <                          \
	2
#error "Must select 2 CONFIG_USB_PD_TCPM_PS8* or above if " \
	"CONFIG_USB_PD_TCPM_MULTI_PS8XXX is defined."
#endif
#endif /* CONFIG_USB_PD_TCPM_MULTI_PS8XXX  */

#if defined(CONFIG_USB_PD_TCPM_PS8705) + defined(CONFIG_USB_PD_TCPM_PS8751) + \
		defined(CONFIG_USB_PD_TCPM_PS8755) +                          \
		defined(CONFIG_USB_PD_TCPM_PS8805) +                          \
		defined(CONFIG_USB_PD_TCPM_PS8815) >                          \
	1
#if !defined(CONFIG_USB_PD_TCPM_MULTI_PS8XXX)
#error "CONFIG_USB_PD_TCPM_MULTI_PS8XXX MUST be defined if more than one " \
	"CONFIG_USB_PD_TCPM_PS8* are intended to support in a board."
#endif
#endif /* defined(CONFIG_USB_PD_TCPM_PS8705) + ... */

/******************************************************************************/
/* Check body detection setup */
#if defined(CONFIG_BODY_DETECTION)
#ifndef CONFIG_BODY_DETECTION_SENSOR
#error CONFIG_BODY_DETECTION_SENSOR must be defined to use body detection
#endif /* ifndef(CONFIG_BODY_DETECTION_SENSOR) */

#ifndef CONFIG_BODY_DETECTION_MAX_WINDOW_SIZE
#define CONFIG_BODY_DETECTION_MAX_WINDOW_SIZE 250 /* max sensor odr (Hz) */
#endif
#ifndef CONFIG_BODY_DETECTION_VAR_THRESHOLD
#define CONFIG_BODY_DETECTION_VAR_THRESHOLD 550 /* (mm/s^2)^2 */
#endif
#ifndef CONFIG_BODY_DETECTION_CONFIDENCE_DELTA
#define CONFIG_BODY_DETECTION_CONFIDENCE_DELTA 525 /* (mm/s^2)^2 */
#endif
#ifndef CONFIG_BODY_DETECTION_VAR_NOISE_FACTOR
#define CONFIG_BODY_DETECTION_VAR_NOISE_FACTOR 120 /* % */
#endif
#ifndef CONFIG_BODY_DETECTION_ON_BODY_CON
#define CONFIG_BODY_DETECTION_ON_BODY_CON 50 /* % */
#endif
#ifndef CONFIG_BODY_DETECTION_OFF_BODY_CON
#define CONFIG_BODY_DETECTION_OFF_BODY_CON 10 /* % */
#endif
#ifndef CONFIG_BODY_DETECTION_STATIONARY_DURATION
#define CONFIG_BODY_DETECTION_STATIONARY_DURATION 15 /* second */
#endif

#else /* CONFIG_BODY_DETECTION */
#ifdef CONFIG_BODY_DETECTION_SENSOR
#error "Unexpected body detection property set"
#else
#define CONFIG_BODY_DETECTION_SENSOR 0
#endif
#endif /* CONFIG_BODY_DETECTION */

/*
 * Set parameters to dummy values to use IS_ENABLED().
 * If a parameter is already set, it will trigger a compilatin error.
 */

/* To be able to use IS_ENABLED(CONFIG_GESTURE_SENSOR_DOUBLE_TAP) */
#ifndef CONFIG_GESTURE_SENSOR_DOUBLE_TAP
#define CONFIG_GESTURE_TAP_THRES_MG 0
#define CONFIG_GESTURE_TAP_MAX_INTERSTICE_T 0
#define CONFIG_GESTURE_TAP_SENSOR 0
#endif /* CONFIG_GESTURE_SENSOR_DOUBLE_TAP */

#ifndef CONFIG_ACCEL_FIFO
#define CONFIG_ACCEL_FIFO_SIZE 0
#endif

#ifndef CONFIG_GESTURE_DETECTION
#define CONFIG_GESTURE_DETECTION_MASK 0
#endif /* CONFIG_GESTURE_DETECTION */

#ifndef CONFIG_GESTURE_SIGMO
#define CONFIG_GESTURE_SIGMO_SENSOR 0
#endif /* CONFIG_GESTURE_SIGMO */

#ifdef CONFIG_LID_ANGLE
#if !defined(CONFIG_LID_ANGLE_SENSOR_BASE) || \
	!defined(CONFIG_LID_ANGLE_SENSOR_LID)
#error "Sensors must be identified for calculating lid angle."
#endif
#else /* CONFIG_LID_ANGLE */
#define CONFIG_LID_ANGLE_SENSOR_BASE 0
#define CONFIG_LID_ANGLE_SENSOR_LID 0
#endif /* CONFIG_LID_ANGLE */

#if defined(CONFIG_LID_ANGLE_UPDATE) && !defined(CONFIG_LID_ANGLE)
#error "CONFIG_LID_ANGLE is needed for CONFIG_LID_ANGLE_UPDATE."
#endif

#ifndef CONFIG_ALS
#define ALS_COUNT 0
#endif /* CONFIG_ALS */

#if defined(CONFIG_EEPROM_CBI_WP) && !defined(CONFIG_CBI_EEPROM)
#error "CONFIG_EEPROM_CBI_WP requires CONFIG_CBI_EEPROM to be defined!"
#endif

#if defined(CONFIG_BYPASS_CBI_EEPROM_WP_CHECK) && \
	!defined(CONFIG_SYSTEM_UNLOCKED)
#error "CONFIG_BYPASS_CBI_EEPROM_WP_CHECK is only permitted " \
	"when CONFIG_SYSTEM_UNLOCK is also enabled."
#endif /* CONFIG_BYPASS_CBI_EEPROM_WP_CHECK && !CONFIG_SYSTEM_UNLOCK */

#if defined(CONFIG_BOARD_VERSION_CBI) && defined(CONFIG_BOARD_VERSION_GPIO)
#error "CONFIG_BOARD_VERSION_CBI and CONFIG_BOARD_VERSION_GPIO " \
	"are mutually exclusive. "
#endif /* CONFIG_BOARD_VERSION_CBI && CONFIG_BOARD_VERSION_GPIO */

#if defined(CONFIG_CBI_EEPROM) && defined(CONFIG_CBI_GPIO)
#error "CONFIG_CBI_EEPROM and CONFIG_CBI_GPIO are mutually exclusive."
#endif

#if defined(CONFIG_CBI_FLASH) && defined(CONFIG_CBI_GPIO)
#error "CONFIG_CBI_FLASH and CONFIG_CBI_GPIO are mutually exclusive."
#endif

/* AMD STT requires AMD SB-RMI to be enabled */
#if defined(CONFIG_AMD_STT) && !defined(CONFIG_AMD_SB_RMI)
#define CONFIG_AMD_SB_RMI
#endif

/*
 * Default timeout value for which EC has to wait for system to exit from S5
 * before performing RTC reset and moving the system to G3.
 */
#if defined(CONFIG_BOARD_HAS_RTC_RESET) && !defined(CONFIG_S5_EXIT_WAIT)
#define CONFIG_S5_EXIT_WAIT 4
#endif

/* HAS_GPU_DRIVER enables D-Notify and throttling. */
#if defined(CONFIG_GPU_NVIDIA)
#define HAS_GPU_DRIVER
#endif

/* Default to 1024 for end of ram data (panic and jump data) */
#ifndef CONFIG_PRESERVED_END_OF_RAM_SIZE
#define CONFIG_PRESERVED_END_OF_RAM_SIZE 1024
#endif

/* Select CONFIG_IT83XX_EXTENDED_ILM automatically */
#if defined(CONFIG_IT83XX_EXTENDED_ILM_HWTIMER_DRIVER) ||     \
	defined(CONFIG_IT83XX_EXTENDED_ILM_I2C_CONTROLLER) || \
	defined(CONFIG_IT83XX_EXTENDED_ILM_I2C_DRIVER) ||     \
	defined(CONFIG_IT83XX_EXTENDED_ILM_IRQ_DRIVER) ||     \
	defined(CONFIG_IT83XX_EXTENDED_ILM_UART_DRIVER) ||    \
	defined(CONFIG_IT83XX_EXTENDED_ILM_USBC_PD_TIMER) ||  \
	defined(CONFIG_IT83XX_EXTENDED_ILM_TCPCI_DRIVER) ||   \
	defined(CONFIG_IT83XX_EXTENDED_ILM_TIMER) ||          \
	defined(CONFIG_IT83XX_EXTENDED_ILM_RAA489000_DRIVER)
#define CONFIG_IT83XX_EXTENDED_ILM
#endif

#ifdef HAVE_PRIVATE
#include "private_config.h"
#endif /* HAVE_PRIVATE */

#endif /* !HOST_TOOLS_BUILD */

#endif /* PLATFORM_EC_INCLUDE_CONFIG_H_ */
