/* Copyright 2018 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * TI bq25710 battery charger driver.
 */

#ifndef __CROS_EC_BQ25710_H
#define __CROS_EC_BQ25710_H

/* SMBUS Interface */
#define BQ25710_SMBUS_ADDR1_FLAGS 0x09

#define BQ25710_BC12_MIN_VOLTAGE_MV 1408

/* Charger parameters */
#if defined(CONFIG_CHARGER_BQ25710) || defined(CONFIG_CHARGER_BQ25720)
#define BQ_CHARGER_NAME "bq25710"
#define BQ_CHARGE_V_MAX 19200
#define BQ_CHARGE_V_MIN 1024
#define BQ_CHARGE_V_STEP 8
#define BQ_CHARGE_I_MAX 8128
#define BQ_CHARGE_I_MIN 64
#define BQ_CHARGE_I_STEP 64
#define BQ_INPUT_I_MAX 6400
#define BQ_INPUT_I_MIN 50
#define BQ_INPUT_I_STEP 50
#elif defined(CONFIG_CHARGER_BQ25770)
#define BQ_CHARGER_NAME "bq25770"
#define BQ_CHARGE_V_MAX 23000
#define BQ_CHARGE_V_MIN 5000
#define BQ_CHARGE_V_STEP 4
#if CONFIG_CHARGER_BQ25770_SENSE_RESISTOR == 5
#define BQ_CHARGE_I_MAX 16320
#elif CONFIG_CHARGER_BQ25770_SENSE_RESISTOR == 2
#define BQ_CHARGE_I_MAX 12000 /* 30A */
#endif
#define BQ_CHARGE_I_MIN 128
#define BQ_CHARGE_I_STEP 8
#define BQ_INPUT_I_MAX 8200
#define BQ_INPUT_I_MIN 400
#define BQ_INPUT_I_STEP 25
#endif

/* Registers */
#define BQ25710_REG_CHARGE_OPTION_0 0x12
#define BQ25710_REG_CHARGE_CURRENT 0x14
#define BQ25710_REG_MAX_CHARGE_VOLTAGE 0x15
#define BQ25710_REG_CHARGER_STATUS 0x20
#define BQ25710_REG_PROCHOT_STATUS 0x21
#define BQ25710_REG_IIN_DPM 0x22
#define BQ25710_REG_ADC_VBUS_PSYS 0x23
#define BQ25710_REG_ADC_IBAT 0x24
#define BQ25710_REG_ADC_CMPIN_IIN 0x25
#define BQ25710_REG_ADC_VSYS_VBAT 0x26
#define BQ25710_REG_CHARGE_OPTION_1 0x30
#define BQ25710_REG_CHARGE_OPTION_2 0x31
#define BQ25710_REG_CHARGE_OPTION_3 0x32
#define BQ25710_REG_PROCHOT_OPTION_0 0x33
#define BQ25710_REG_PROCHOT_OPTION_1 0x34
#define BQ25710_REG_ADC_OPTION 0x35
#define BQ25720_REG_CHARGE_OPTION_4 0x36
#define BQ25720_REG_VMIN_ACTIVE_PROTECTION 0x37
#define BQ25710_REG_OTG_VOLTAGE 0x3B
#define BQ25710_REG_OTG_CURRENT 0x3C
#define BQ25710_REG_INPUT_VOLTAGE 0x3D
#define BQ25710_REG_MIN_SYSTEM_VOLTAGE 0x3E
#define BQ25710_REG_IIN_HOST 0x3F
#define BQ25710_REG_MANUFACTURER_ID 0xFE
#define BQ25710_REG_DEVICE_ADDRESS 0xFF

#define BQ25770_REG_CHARGE_PROFILE 0x17
#define BQ25770_REG_GATE_DRIVE 0x18
#define BQ25770_REG_CHARGE_OPTION_5 0x19
#define BQ25770_REG_AUTO_CHARGE 0x1A
#define BQ25770_REG_CHARGER_STATUS_0 0x1B
#define BQ25770_REG_CHARGER_STATUS_1 0x20
#define BQ25770_REG_ADC_VBUS 0x23
#define BQ25770_REG_ADC_IIN 0x25
#define BQ25770_REG_ADC_VSYS 0x26
#define BQ25770_REG_ADC_VBAT 0x27
#define BQ25770_REG_ADC_PSYS 0x28
#define BQ25770_REG_ADC_CMPIN_TR 0x29
#define BQ25770_REG_VIRTUAL_CONTROL 0xFD

/* ADC conversion time ins ms */
#if defined(CONFIG_CHARGER_BQ25720)
#define BQ25710_ADC_OPTION_ADC_CONV_MS 25
#elif defined(CONFIG_CHARGER_BQ25710)
#define BQ25710_ADC_OPTION_ADC_CONV_MS 10
#elif defined(CONFIG_CHARGER_BQ25770)
#define BQ25710_ADC_OPTION_ADC_CONV_MS 12
#else
#error Only the BQ25720 and BQ25710 and BQ25770 are supported by bq25710 driver.
#endif

/* ADCVBUS/PSYS Register */
#if defined(CONFIG_CHARGER_BQ25720)
#define BQ25720_ADC_VBUS_STEP_MV 96
#elif defined(CONFIG_CHARGER_BQ25710)
#define BQ25710_ADC_VBUS_STEP_MV 64
#define BQ25710_ADC_VBUS_BASE_MV 3200
#elif defined(CONFIG_CHARGER_BQ25770)
#define BQ25770_ADC_VBUS_STEP_MV 2
#else
#error Only the BQ25720 and BQ25710 and BQ25770 are supported by bq25710 driver.
#endif

/* Min System Voltage Register */
#define BQ25710_MIN_SYSTEM_VOLTAGE_STEP_MV 256
#define BQ25720_VSYS_MIN_VOLTAGE_STEP_MV 100
#define BQ25770_VSYS_MIN_VOLTAGE_STEP_MV 5

extern const struct charger_drv bq25710_drv;

/**
 * Get a charger register option
 *
 * @param chgnum: Index into charger chips
 * @param reg: Register to read
 * @param option: The register's option
 * @return EC_SUCCESS or error
 */
enum ec_error_list bq257x0_get_option_reg(int chgnum, int reg, int *option);

/**
 * Set a charger register option
 *
 * @param chgnum: Index into charger chips
 * @param reg: Register to write
 * @param option: The register's option
 * @return EC_SUCCESS or error
 */
enum ec_error_list bq257x0_set_option_reg(int chgnum, int reg, int option);

/**
 * Set VSYS_MIN
 *
 * @param chgnum: Index into charger chips
 * @param mv: min system voltage in mV
 * @return EC_SUCCESS or error
 */
int bq25710_set_min_system_voltage(int chgnum, int mv);

#endif /* __CROS_EC_BQ25710_H */
