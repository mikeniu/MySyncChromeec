/* -*- mode:c -*-
 *
 * Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Declare symbolic names for all the GPIOs that we care about.
 * Note: Those with interrupt handlers must be declared first. */

/* FIXME(dhendrix): ACOK_OD -> AC_PRESENT for common code */
/* FIXME(dhendrix): Do nothing with ACOK_OD? Kevin doesn't seem to. BD99955 handles 
 * plug/unplug events and VBUS/VCC status is read via I2C (bd99955_extpower_is_present) */
//GPIO_INT(ACOK_OD,		PIN(C, 1), GPIO_INT_BOTH | GPIO_INPUT, extpower_interrupt) /* from BD99955 */
//GPIO_INT(AC_PRESENT,		PIN(C, 1), GPIO_INT_BOTH | GPIO_INPUT, extpower_interrupt) /* from BD99955 */

/* FIXME(dhendrix): Do we need to select voltage for C0 and C1 PD interrupt pins? */
GPIO_INT(USB_C0_PD_INT,		PIN(3, 7), GPIO_INT_HIGH | GPIO_INPUT, tcpc_alert_event) /* from Analogix TCPC */
GPIO_INT(USB_C1_PD_INT_ODL,	PIN(D, 2), GPIO_INT_LOW | GPIO_INPUT, tcpc_alert_event)	/* from Parade TCPC */

/* FIXME(dhendrix): CHARGER_EC_INT_ODL needs to be named PD_MCU_INT for common code in host_command_pd.c */
//GPIO_INT(CHARGER_EC_INT_ODL,	PIN(3, 3), GPIO_INT_LOW | GPIO_INPUT, pd_mcu_interrupt) /* from BD99955 */
GPIO_INT(PD_MCU_INT,		PIN(3, 3), GPIO_INT_FALLING | GPIO_INPUT, pd_mcu_interrupt) /* from BD99955 */

/* FIXME(dhendrix): SLP_Sn_L -> PCH_SLP_Sn_L for apollolake common code */
//GPIO_INT(SLP_S4_L, 		PIN(8, 6), GPIO_INT_BOTH | GPIO_INPUT, power_signal_interrupt)
//GPIO_INT(SLP_S3_L,		PIN(7, 3), GPIO_INT_BOTH | GPIO_INPUT, power_signal_interrupt)
//GPIO_INT(SLP_S0_L,		PIN(7, 5), GPIO_INT_BOTH | GPIO_INPUT, power_signal_interrupt)
GPIO_INT(PCH_SLP_S4_L, 		PIN(8, 6), GPIO_INT_BOTH | GPIO_INPUT, power_signal_interrupt)
GPIO_INT(PCH_SLP_S3_L,		PIN(7, 3), GPIO_INT_BOTH | GPIO_INPUT, power_signal_interrupt)
GPIO_INT(PCH_SLP_S0_L,		PIN(7, 5), GPIO_INT_BOTH | GPIO_INPUT, power_signal_interrupt)
GPIO_INT(SUSPWRNACK,		PIN(7, 2), GPIO_INT_BOTH | GPIO_INPUT, power_signal_interrupt)
/* FIXME(dhendrix): PMIC_EC_RSMRST_ODL -> RSMRST_L_PGOOD for apollolake common code */
//GPIO_INT(PMIC_EC_RSMRST_ODL,	PIN(6, 0), GPIO_INT_BOTH | GPIO_INPUT, power_signal_interrupt)
GPIO_INT(RSMRST_L_PGOOD,	PIN(6, 0), GPIO_INT_BOTH | GPIO_INPUT, power_signal_interrupt)
/* FIXME(dhendrix): PMIC_EC_PWROK_OD -> ALL_SYS_PGOOD for apollolake common code */
GPIO_INT(ALL_SYS_PGOOD,		PIN(5, 0), GPIO_INT_BOTH | GPIO_INPUT, power_signal_interrupt)
//GPIO_INT(PMIC_EC_PWROK_OD,	PIN(5, 0), GPIO_INT_BOTH | GPIO_INPUT, power_signal_interrupt)

/* FIXME(dhendrix): KB_PWR_BTN_ODL needs to be named POWER_BUTTON_L for common code */
/* FIXME(dhendrix): We might remove external pull-up in EVT */
//GPIO(MECH_PWR_BTN_ODL,		PIN(0, 4), GPIO_INPUT)	/* FIXME: should enable pull-up? */
GPIO_INT(POWER_BUTTON_L,	PIN(0, 4), GPIO_INT_BOTH | GPIO_INPUT, power_button_interrupt)
GPIO_INT(EC_VOLDN_BTN_L,	PIN(8, 3), GPIO_INPUT | GPIO_PULL_UP, button_interrupt)
GPIO_INT(EC_VOLUP_BTN_L,	PIN(8, 2), GPIO_INPUT | GPIO_PULL_UP, button_interrupt)

/* Actually this is EC_WP_ODL, but WP_L is the common name used in the EC codebase */
GPIO_INT(WP_L,			PIN(4, 0), GPIO_INT_BOTH | GPIO_INPUT | GPIO_SEL_1P8V, switch_interrupt)

/* FIXME(dhendrix): Implement interrupt handlers for lid, tablet mode, etc. */
//GPIO_INT(BASE_SIXAXIS_INT_L,	PIN(9, 3), GPIO_INT_LOW | GPIO_INPUT | GPIO_SEL_1P8V, gyro_interrupt)
GPIO(BASE_SIXAXIS_INT_L,	PIN(9, 3), GPIO_INT_LOW | GPIO_INPUT | GPIO_SEL_1P8V)
//GPIO_INT(LID_ACCEL_INT_L,	PIN(C, 7), GPIO_INT_LOW | GPIO_INPUT | GPIO_SEL_1P8V, lid_interrupt)
GPIO(LID_ACCEL_INT_L,	PIN(C, 7), GPIO_INPUT | GPIO_SEL_1P8V)
//GPIO(TABLET_MODE,		PIN(3, 6), GPIO_INT_BOTH | GPIO_INPUT | GPIO_PULL_HIGH, tablet_mode_interrupt)

GPIO(PCH_RSMRST_L,		PIN(7, 0), GPIO_OUT_LOW)

/* KB interrupt from EC to PCH */
ALTERNATE(PIN_MASK(5, 0x08), 0, MODULE_LPC, GPIO_INT_HIGH | GPIO_ODR_HIGH)	/* FIXME(dhendrix): LPC_SERIRQ, INT_HIGH? */
/* Use EC_PCH_KB_INT_ODL if SERIRQ doesn't work*/
/* GPIO(EC_PCH_KB_INT_ODL, PIN(B, 0), GPIO_OPEN_DRAIN) */

ALTERNATE(PIN_MASK(4, 0xc0), 1, MODULE_LPC, 0)
ALTERNATE(PIN_MASK(5, 0x7e), 1, MODULE_LPC, 0)

/* I2C GPIOs will be set to alt. function later */
GPIO(EC_I2C_GYRO_SDA,		PIN(D, 0), GPIO_INPUT | GPIO_SEL_1P8V)
GPIO(EC_I2C_GYRO_SCL,		PIN(D, 1), GPIO_INPUT | GPIO_SEL_1P8V)
GPIO(EC_I2C_SENSOR_SDA,		PIN(9, 1), GPIO_INPUT | GPIO_SEL_1P8V)
GPIO(EC_I2C_SENSOR_SCL,		PIN(A, 0), GPIO_INPUT | GPIO_SEL_1P8V)
GPIO(EC_I2C_USB_C0_PD_SDA,	PIN(B, 4), GPIO_INPUT | GPIO_SEL_1P8V)
GPIO(EC_I2C_USB_C0_PD_SCL,	PIN(B, 5), GPIO_INPUT | GPIO_SEL_1P8V)
GPIO(EC_I2C_USB_C1_PD_SDA,	PIN(B, 2), GPIO_INPUT | GPIO_SEL_1P8V)
GPIO(EC_I2C_USB_C1_PD_SCL,	PIN(B, 3), GPIO_INPUT | GPIO_SEL_1P8V)
GPIO(EC_I2C_POWER_SDA,		PIN(8, 7), GPIO_INPUT)
GPIO(EC_I2C_POWER_SCL,		PIN(9, 0), GPIO_INPUT)

/* FIXME: had GPIO_ODR_LOW here earlier, but should be GPIO_ODR_HIGH? */
GPIO(EC_SMI_ODL,		PIN(A, 6), GPIO_ODR_HIGH | GPIO_SEL_1P8V)
GPIO(EC_SCI_ODL,		PIN(A, 7), GPIO_ODR_HIGH | GPIO_SEL_1P8V)

/* Keyboard pins */
#define GPIO_KB_INPUT (GPIO_INPUT | GPIO_PULL_UP)
#define GPIO_KB_OUTPUT (GPIO_ODR_HIGH)
/* inputs */
ALTERNATE(PIN_MASK(3, 0x03), 0,  MODULE_KEYBOARD_SCAN, GPIO_KB_INPUT)
ALTERNATE(PIN_MASK(2, 0xfc), 0,  MODULE_KEYBOARD_SCAN, GPIO_KB_INPUT)
/* outputs */
ALTERNATE(PIN_MASK(2, 0x03), 0,  MODULE_KEYBOARD_SCAN, GPIO_KB_OUTPUT)
ALTERNATE(PIN_MASK(0, 0xe0), 0,  MODULE_KEYBOARD_SCAN, GPIO_KB_OUTPUT)
ALTERNATE(PIN_MASK(1, 0xff), 0,  MODULE_KEYBOARD_SCAN, GPIO_KB_OUTPUT)

/* FIXME(dhendrix): EC_BL_EN_OD -> ENABLE_BACKLIGHT for apollolake.c */
//GPIO(EC_BL_EN_OD,		PIN(9, 7), GPIO_ODR_HIGH | GPIO_SEL_1P8V)
GPIO(ENABLE_BACKLIGHT,		PIN(9, 7), GPIO_ODR_HIGH | GPIO_SEL_1P8V)

/*
 * BRD_ID1 is actually an ADC which will be used to measure multiple values.
 * Assert EC_BRD_ID_EN_ODL and then read BRD_ID1. Keep EC_BRD_ID_EN_ODL
 * as input when not in use to conserve power.
 */
//GPIO(BRD_ID1,			PIN(4, 3), GPIO_INPUT)
ALTERNATE(PIN_MASK(4, 0x08), 1, MODULE_ADC, 0)
/* Note: GPO35 is an output only */
GPIO(EC_BRD_ID_EN_ODL,		PIN(3, 5), GPIO_ODR_HIGH)

ALTERNATE(PIN(4, 4), 6, MODULE_ADC, 0)	/* TEMP_SENSOR_AMB (FIXME: alt function 6?) */

GPIO(CCD_MODE_ODL,		PIN(6, 3), GPIO_INPUT)
GPIO(EC_BATT_PRES_L,		PIN(3, 4), GPIO_INPUT)

GPIO(LID_OPEN,			PIN(6, 7), GPIO_INPUT)	/* FIXME: how does this tie-in with LID_ACCEL_INT_L? */
GPIO(EC_HAVEN_RESET_ODL,	PIN(0, 2), GPIO_ODR_HIGH)

ALTERNATE(PIN(8, 0), 4, MODULE_PWM, 0)	/* RED_LED (PWM3) */
ALTERNATE(PIN(C, 4), 4, MODULE_PWM, 0)	/* GREEN_LED (PWM2) */

/* FIXME(dhendrix): EC_ENTERING_RW -> ENTERING_RW for common code */
//GPIO(EC_ENTERING_RW,		PIN(7, 6), GPIO_OUTPUT)
GPIO(ENTERING_RW,		PIN(7, 6), GPIO_OUTPUT)

/* GPIO(EC_BRD_ID_EN_ODL, ___ , GPIO_ODR_HIGH) */ /* FIXME: This is on GPO35 */

/* FIXME: schematic asks "ARE PMON_PSYS AND CHARGER_IMON USED?" */
ALTERNATE(PIN(4, 5), 6, MODULE_ADC, 0)	/* TEMP_SENSOR_CHARGER (FIXME: alt function) */

GPIO(PP5000_OVER_TEMP_L,	PIN(C, 5), GPIO_INPUT)
/* FIXME(dhendrix): PMIC_A_RAILS_EN -> V5A_EN for apollolake common code */
//GPIO(PMIC_A_RAILS_EN,		PIN(8, 5), GPIO_OUT_LOW)
GPIO(V5A_EN,			PIN(8, 5), GPIO_OUT_LOW)
GPIO(EN_PP3300, 		PIN(C, 2), GPIO_OUT_LOW)
GPIO(PP3300_PG,			PIN(6, 2), GPIO_INPUT)
GPIO(EN_PP5000,			PIN(C, 6), GPIO_OUT_LOW)
GPIO(PP5000_PG,			PIN(7, 1), GPIO_INPUT)
/* GPIO(EN_P3300_TRACKPAD_ODL,	PIN(3, 2), GPIO_ODR_HIGH) */	/* FIXME(dhendrix): GPO32, not GPIO32 */

/* FIXME(dhendrix): EC_PCH_PWROK_OD -> PCH_SYS_PWROK for common apollolake code */
//GPIO(EC_PCH_PWROK_OD,		PIN(E, 7), GPIO_OUTPUT)
GPIO(PCH_SYS_PWROK,		PIN(E, 7), GPIO_OUTPUT)

/* EN_PP3300_WLAN_ODL	GPO66/ARM#_x86	OUT	OD	*/ /* FIXME: GPO66 */ 
/* FIXME(dhendrix): EC_PCH_PWRBTN_ODL needs to be renamed for common code */
//GPIO(EC_PCH_PWR_BTN_ODL,	PIN(0, 1), GPIO_ODR_HIGH)
GPIO(PCH_PWRBTN_L,	PIN(0, 1), GPIO_ODR_HIGH)
GPIO(EC_PCH_PROCHOT_OVERRIDE_ODL,	PIN(A, 3), GPIO_ODR_HIGH | GPIO_SEL_1P8V)

/* FIXME(dhendrix): PCH_PROCHOT_ODL -> CPU_PROCHOT for apollolake common code */
//GPIO(PCH_PROCHOT_ODL,		PIN(7, 4), GPIO_INPUT | GPIO_SEL_1P8V)
GPIO(CPU_PROCHOT,		PIN(7, 4), GPIO_INPUT | GPIO_SEL_1P8V)
/* FIXME(dhendrix): EC->PCH->WAKE->ODL -> PCH_WAKE_L for lpc common code */
//GPIO(EC_PCH_WAKE_ODL,		PIN(8, 1), GPIO_ODR_HIGH)
GPIO(PCH_WAKE_L,		PIN(8, 1), GPIO_ODR_HIGH)
GPIO(USB_C0_HPD_1P8_ODL,	PIN(9, 4), GPIO_ODR_HIGH)
GPIO(USB_C1_HPD_1P8_ODL,	PIN(A, 5), GPIO_ODR_HIGH)

GPIO(USB2_OTG_ID,		PIN(A, 1), GPIO_OUTPUT) /* FIXME: what should this init to? */
GPIO(USB2_OTG_VBUSSENSE,	PIN(9, 5), GPIO_OUTPUT)
GPIO(EC_PCH_RTCRST,		PIN(B, 7), GPIO_OUTPUT)
/* FIXME(dhendrix): Renamed SYS_RST_ODL -> PCH_RCIN_L for apollolake common code */
//GPIO(SYS_RST_ODL,		PIN(6, 1), GPIO_ODR_HIGH)
GPIO(PCH_RCIN_L,		PIN(6, 1), GPIO_ODR_HIGH)

/* EC_RST_ODL on VCC1_RST# - Not a GPIO */

/* FIXME: Make UART RX an interrupt? */
ALTERNATE(PIN_MASK(6, 0x30), 0, MODULE_UART, 0)	/* UART from EC to Servo */

GPIO(CHARGER_RST_L,		PIN(C, 0), GPIO_OUT_HIGH)
GPIO(USB_A_CHARGE_EN_L,		PIN(4, 2), GPIO_OUT_HIGH)
GPIO(EN_USB_TCPC_PWR,		PIN(C, 3), GPIO_OUT_LOW)
GPIO(USB_PD_RST_ODL,		PIN(0, 3), GPIO_OUT_HIGH)
GPIO(EN_USB_A_5V,		PIN(4, 1), GPIO_OUT_LOW)
/* FIXME(dhendrix): EN_USB_Cn_5V_OUT -> USB_Cn_5V_EN for common code */
GPIO(USB_C0_5V_EN,		PIN(D, 3), GPIO_OUT_LOW)	/* Enable C0 */
GPIO(USB_C1_5V_EN,		PIN(B, 1), GPIO_OUT_LOW)	/* Enable C1 */

/* FIXME: need to set GPIO_SEL_1P8V for the I2C GPIOs? */
ALTERNATE(PIN_MASK(D, 0x03), 1, MODULE_I2C, 0)	/* EC_I2C_GYRO_SDA/SCL */
ALTERNATE(PIN_MASK(9, 0x06), 1, MODULE_I2C, 0)	/* EC_I2C_SENSOR_SDA */
ALTERNATE(PIN_MASK(B, 0x30), 1, MODULE_I2C, 0)	/* EC_I2C_USB_C0_PD_SDA/SCL */
ALTERNATE(PIN_MASK(B, 0x0C), 1, MODULE_I2C, 0)	/* EC_I2C_USB_C1_PD_SDA/SCL */
ALTERNATE(PIN_MASK(8, 0x08), 1, MODULE_I2C, 0)	/* EC_I2C_POWER_SDA */
ALTERNATE(PIN_MASK(9, 0x01), 1, MODULE_I2C, 0) /* EC_I2C_POWER_SCL (FIXME: not necessary due to above declaration for GPIO87?) */

/* Clear for non-HDI breakout, must be pulled high */
GPIO(NC1,			PIN(8, 4), GPIO_INPUT | GPIO_PULL_UP | GPIO_SEL_1P8V)
GPIO(NC2,			PIN(8, 4), GPIO_INPUT | GPIO_PULL_UP | GPIO_SEL_1P8V)
