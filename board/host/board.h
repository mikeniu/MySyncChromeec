/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Emulator board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/* Optional features */
#define CONFIG_EXTPOWER_GPIO
#undef CONFIG_FMAP
#define CONFIG_POWER_BUTTON
#undef CONFIG_WATCHDOG
#define CONFIG_SWITCH
#define CONFIG_INDUCTIVE_CHARGING

#undef CONFIG_CONSOLE_HISTORY
#define CONFIG_CONSOLE_HISTORY 4

#define CONFIG_WP_ACTIVE_HIGH

#include "gpio_signal.h"

enum temp_sensor_id {
	TEMP_SENSOR_CPU = 0,
	TEMP_SENSOR_BOARD,
	TEMP_SENSOR_CASE,
	TEMP_SENSOR_BATTERY,

	TEMP_SENSOR_COUNT
};

enum adc_channel {
	ADC_CH_CHARGER_CURRENT,
	ADC_AC_ADAPTER_ID_VOLTAGE,

	ADC_CH_COUNT
};

/* Fake test charge suppliers */
enum {
	CHARGE_SUPPLIER_TEST1,
	CHARGE_SUPPLIER_TEST2,
	CHARGE_SUPPLIER_TEST3,
	CHARGE_SUPPLIER_TEST4,
	CHARGE_SUPPLIER_TEST5,
	CHARGE_SUPPLIER_TEST6,
	CHARGE_SUPPLIER_TEST7,
	CHARGE_SUPPLIER_TEST8,
	CHARGE_SUPPLIER_TEST9,
	CHARGE_SUPPLIER_TEST_COUNT
};

/* Custom charge_manager priority table is defined in test code */
extern const int supplier_priority[];

/* Standard-current Rp */
#define PD_SRC_VNC           PD_SRC_DEF_VNC_MV
#define PD_SRC_RD_THRESHOLD  PD_SRC_DEF_RD_THRESH_MV

/* delay necessary for the voltage transition on the power supply */
#define PD_POWER_SUPPLY_TURN_ON_DELAY  20000 /* us */
#define PD_POWER_SUPPLY_TURN_OFF_DELAY 20000 /* us */

/* Define typical operating power and max power */
#define PD_OPERATING_POWER_MW 15000
#define PD_MAX_POWER_MW       60000
#define PD_MAX_CURRENT_MA     3000
#define PD_MAX_VOLTAGE_MV     20000

#define PD_MIN_CURRENT_MA     500
#define PD_MIN_POWER_MW       7500

#ifdef HAS_TASK_TPM

/**
 * Set up a deferred call to update CCD state.
 *
 * This will enable/disable UARTs, SPI, I2C, etc. as needed.
 */
void ccd_update_state(void);

int board_use_plt_rst(void);
int board_rst_pullup_needed(void);
int board_tpm_uses_i2c(void);
int board_tpm_uses_spi(void);
int board_id_is_mismatched(void);
/* Allow for deep sleep to be enabled on AP shutdown */
int board_deep_sleep_allowed(void);

void power_button_record(void);

/**
 * Enable/disable power button release interrupt.
 *
 * @param enable	Enable (!=0) or disable (==0)
 */
void power_button_release_enable_interrupt(int enable);

/* Functions needed by CCD config */
int board_battery_is_present(void);
int board_fwmp_allows_unlock(void);
int board_vboot_dev_mode_enabled(void);
void board_reboot_ap(void);
int board_wipe_tpm(void);
int board_is_first_factory_boot(void);

int usb_i2c_board_enable(void);
void usb_i2c_board_disable(void);

void print_ap_state(void);
void print_ap_uart_state(void);
void print_ec_state(void);
void print_servo_state(void);

int ap_is_on(void);
int ap_uart_is_on(void);
int ec_is_on(void);
int ec_is_rx_allowed(void);
int servo_is_connected(void);

void set_ap_on(void);

int chip_factory_mode(void);
#endif

#endif /* __CROS_EC_BOARD_H */
