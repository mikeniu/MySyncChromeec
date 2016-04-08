/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Reef board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/*
 * Allow dangerous commands.
 * TODO: Remove this config before production.
 */
#define CONFIG_SYSTEM_UNLOCKED

/* Optional features */
#define CONFIG_ACCELGYRO_BMI160
#define CONFIG_ACCEL_KX022
/* FIXME: Need to add BMP280 barometer */

/* FIXME(dhendrix): ADC not included for now */
/* #define CONFIG_ADC */
#undef CONFIG_ADC

#define CONFIG_ALS
#define CONFIG_ALS_OPT3001
#if 0
/* FIXME(dhendrix): Add battery stuff, probably import from amenia */
#define CONFIG_BATTERY_CUT_OFF
/* #define CONFIG_BATTERY_PRESENT_GPIO GPIO_BAT_PRESENT_L */
#define CONFIG_BATTERY_SMART
#endif
#define CONFIG_BOARD_VERSION
#define CONFIG_BOARD_SPECIFIC_VERSION
#define CONFIG_BUTTON_COUNT 2

/* Charger / PD */
#define CONFIG_CHARGER_BD99955
#define CONFIG_CHARGER
#define CONFIG_CHARGER_INPUT_CURRENT 512
#define CONFIG_CHARGER_V2
#define CONFIG_CHARGE_MANAGER

/* USB PD config */
/* #define CONFIG_USB_CHARGER */ /* FIXME(dhendrix): not on kevin */
#define CONFIG_USB_POWER_DELIVERY
#define CONFIG_USB_PD_ALT_MODE
#define CONFIG_USB_PD_ALT_MODE_DFP
#define CONFIG_USB_PD_CUSTOM_VDM
#define CONFIG_USB_PD_DUAL_ROLE
#define CONFIG_USB_PD_LOGGING
#define CONFIG_USB_PD_LOG_SIZE 512
#define CONFIG_USB_PD_PORT_COUNT 2
#define CONFIG_USB_PD_TCPM_VBUS
/* TODO: Enable TRY_SRC (dhendrix: this was copied from Kevin)*/
#undef CONFIG_USB_PD_TRY_SRC

#define CONFIG_BATTERY_CUT_OFF
#define CONFIG_BATTERY_PRESENT_GPIO GPIO_EC_BATT_PRES_L
#define CONFIG_BATTERY_SMART

#define CONFIG_CHIPSET_APOLLOLAKE
/* #define CONFIG_CHIPSET_RESET_HOOK */
/* #define CONFIG_CLOCK_CRYSTAL */
/* #define CONFIG_EXTPOWER_GPIO */

#define CONFIG_FPU
#define CONFIG_HOSTCMD_PD
#define CONFIG_HOSTCMD_PD_PANIC
#define CONFIG_I2C
#define CONFIG_I2C_MASTER
#define CONFIG_LPC
#define CONFIG_KEYBOARD_PROTOCOL_8042
/* #define CONFIG_LED_COMMON */	/* FIXME(dhendrix): maybe? */
/* #define CONFIG_LID_ANGLE */	/* FIXME(dhendrix): maybe? */
/* #define CONFIG_LID_ANGLE_SENSOR_BASE 0 */	/* FIXME(dhendrix): maybe? */
/* #define CONFIG_LID_ANGLE_SENSOR_LID 2 */	/* FIXME(dhendrix): maybe? */
#define CONFIG_LID_SWITCH	/* FIXME(dhendrix): Alps HGDEDM013A */
/* #define CONFIG_LOW_POWER_IDLE */
#define CONFIG_LTO
#define CONFIG_POWER_BUTTON		/* FIXME(dhendrix): need to verify */
#define CONFIG_POWER_BUTTON_X86		/* FIXME(dhendrix): need to verify */
#define CONFIG_POWER_COMMON		/* FIXME(dhendrix): need to verify */
#define CONFIG_POWER_SIGNAL_INTERRUPT_STORM_DETECT_THRESHOLD 30
/* All data won't fit in data RAM.  So, moving boundary slightly. */
/* #undef CONFIG_RO_SIZE */
/* #define CONFIG_RO_SIZE (104 * 1024) */
/* #define CONFIG_SCI_GPIO GPIO_PCH_SCI_L */
#define CONFIG_UART_HOST 0	/* FIXME(dhendrix): need to verify */
/* #undef CONFIG_UART_TX_BUF_SIZE */
/* #define CONFIG_UART_TX_BUF_SIZE 512 */

#define CONFIG_USB_PD_TCPM_MUX	/* for both PS8751 and ANX3429 */

/* FIXME(dhendrix): power delivery handled by BD99956, so maybe
 * we can omit this all together? */
/* #define CONFIG_USB_POWER_DELIVERY */

/* FIXME(dhendrix): confirm all this USB_PD_* stuff */
/* #define CONFIG_USB_PD_ALT_MODE */
/* #define CONFIG_USB_PD_ALT_MODE_DFP */
/* #define CONFIG_USB_PD_CUSTOM_VDM */
#if 0
#define CONFIG_USB_PD_DUAL_ROLE	/* BD99956 handles this? */
#define CONFIG_USB_PD_LOGGING
#define CONFIG_USB_PD_LOG_SIZE 512
#endif
#define CONFIG_USB_PD_PORT_COUNT 2
#define CONFIG_USB_PD_TCPM_TCPCI
#define CONFIG_USB_PD_TCPM_ANX74XX
/* #define CONFIG_USB_PD_TRY_SRC */
/* #define CONFIG_USB_SWITCH_PI3USB9281 */
/* #define CONFIG_USB_SWITCH_PI3USB9281_CHIP_COUNT 2 */
/* #define CONFIG_USBC_SS_MUX */
/* #define CONFIG_USBC_SS_MUX_DFP_ONLY */
/* #define CONFIG_USBC_VCONN */
/* #define CONFIG_USBC_VCONN_SWAP */
#define CONFIG_VBOOT_HASH

#define CONFIG_FLASH_SIZE 524288
#define CONFIG_SPI_FLASH_W25Q40	/* FIXME: Should be GD25LQ40 */

/* #define CONFIG_TEMP_SENSOR */
/* #define CONFIG_TEMP_SENSOR_BD99992GW */
/* #define CONFIG_THERMISTOR_NCP15WB */

/*
 * Enable 1 slot of secure temporary storage to support
 * suspend/resume with read/write memory training.
 */
/* #define CONFIG_VSTORE */
/* #define CONFIG_VSTORE_SLOT_COUNT 1 */

/* #define CONFIG_WATCHDOG_HELP	*/ /* seems to be for stm32f and mec1322? */

/* Optional feature - used by nuvoton */
/* FIXME(dhendrix): NPCX_I2C0_BUS2 option obsolete? We use GPIOs B4/B5 *and* B2/B3. */
/* #define NPCX_I2C0_BUS2    0 */ /* 0:GPIOB4/B5 1:GPIOB2/B3 as I2C0 */
#define NPCX_UART_MODULE2    1 /* 0:GPIO10/11 1:GPIO64/65 as UART */
#define NPCX_JTAG_MODULE2    0 /* 0:GPIO21/17/16/20 1:GPIOD5/E2/D4/E5 as JTAG*/
/* FIXME(dhendrix): these pins are just normal GPIOs on Reef. Do we need
 * to change some other setting to put them in GPIO mode? */
#define NPCX_TACH_SEL2       0 /* 0:GPIO40/A4 1:GPIO93/D3 as TACH */

#if 0
#define CONFIG_WIRELESS
#define CONFIG_WIRELESS_SUSPEND \
	(EC_WIRELESS_SWITCH_WLAN | EC_WIRELESS_SWITCH_WLAN_POWER)

/* Wireless signals */
#define WIRELESS_GPIO_WLAN GPIO_WLAN_OFF_L
#define WIRELESS_GPIO_WLAN_POWER GPIO_PP3300_WLAN_EN
#endif

/* LED signals */
#define GPIO_BAT_LED_RED GPIO_CHARGE_LED_1
#define GPIO_BAT_LED_GREEN GPIO_CHARGE_LED_2

/*
 * I2C ports
 * I2C0: USB PD
 * I2C1: power (ROHM BD99955, also connected to PCH_I2C_PMIC (multi-master?))
 * I2C2: sensors:
 *	bmp280 barometric pressure (barometer, 7-bit addr 0x76)
 *	kx022: accelerometer (7-bit addr 0x3e)
 *	bmi160 intertial measurement (gyro, 7-bit addr = 0x68)
 *		- EC_I2C_SENSOR_S_SDA/SCL are unstuffed. It's just there in
 *		  case the BMI160 <-> BMM150 connection doesn't work.
 *		- This is the master of the compass (BMM150 via COMPASS_I2C_*)
 *		  and presents the data in its own registers. Access BMM150
 *		  thru BMI160 only.
 *	bmm150 geomagnetic sensor (compass, 7-bit addr = 0x10)
 *		- The master is the BMI160. Configuration is done indirectly
 *		  thru BMI160..
 *		- EC_I2C_SENSOR_S_SDA/SCL are unstuffed. It's just there in
 *		  case the BMI160 <-> BMM150 connection doesn't work.
 * I2C3: gyro (7-bit addr = 0x68)
 */
/* FIXME(dhendrix): Clean this up. */
#define I2C_PORT_PMIC                   NPCX_I2C_PORT0_0
#define I2C_PORT_ANX3429		NPCX_I2C_PORT0_0
#define I2C_PORT_PS8751			NPCX_I2C_PORT0_1
#define I2C_PORT_USB_CHARGER_2          NPCX_I2C_PORT0_0
#define I2C_PORT_PD_MCU                 NPCX_I2C_PORT1
#define I2C_PORT_TCPC                   NPCX_I2C_PORT1
#define I2C_PORT_ALS                    NPCX_I2C_PORT2
#define I2C_PORT_ACCEL                  NPCX_I2C_PORT2
#define I2C_PORT_BATTERY                NPCX_I2C_PORT3
#define I2C_PORT_CHARGER                NPCX_I2C_PORT3
#define I2C_PORT_THERMAL                NPCX_I2C_PORT3

#define BMM150_I2C_ADDRESS BMM150_ADDR0	/* 8-bit address */
#define CONFIG_MAG_BMI160_BMM150
#define   BMM150_I2C_ADDRESS BMM150_ADDR0
#define CONFIG_MAG_CALIBRATE

/* Thermal sensors read through PMIC ADC interface */
/* #define I2C_PORT_THERMAL I2C_PORT_PMIC */

/* Ambient Light Sensor address */
/* ALS on Reef is a TI OPT3001 integrated in the camera module and is
 * which is connected to EC_I2C_SENSOR_U */
/* FIXME(dhendrix): This assumes the ADDR pin is grounded (need to verify) */
#define OPT3001_I2C_ADDR OPT3001_I2C_ADDR1

/* Modules we want to exclude */
/* #undef CONFIG_CMD_HASH */
/* #undef CONFIG_CMD_TEMP_SENSOR */
/* #undef CONFIG_CMD_TIMERINFO */
/* #undef CONFIG_CONSOLE_CMDHELP */
#undef CONFIG_PECI

#undef DEFERRABLE_MAX_COUNT
#define DEFERRABLE_MAX_COUNT 15

#ifndef __ASSEMBLER__

#include "gpio_signal.h"
#include "registers.h"

/* ADC signal */
enum adc_channel {
	ADC_VBUS = -1,	/* FIXME(dhendrix): hack, see comment in charge_manager.c:275 */
//	ADC_AMON_BMON,
//	ADC_PSYS,
	/* Number of ADC channels */
	ADC_CH_COUNT
};

enum power_signal {
	X86_RSMRST_N = 0,
	X86_SLP_S0_N,
	X86_SLP_S3_N,
	X86_SLP_S4_N,
	X86_SUSPWRDNACK,
//	X86_SUS_STAT_N,

	X86_ALL_SYS_PG,		/* PMIC_EC_PWROK_OD */
	X86_PGOOD_PP3300,	/* GPIO_PP3300_PG */
	X86_PGOOD_PP5000,	/* GPIO_PP5000_PG */

	/* Number of X86 signals */
	POWER_SIGNAL_COUNT
};

enum temp_sensor_id {
	TEMP_SENSOR_BATTERY,	/* only readable in S0? */
//	TEMP_SENSOR_AMBIENT,
//	TEMP_SENSOR_CHARGER,

	TEMP_SENSOR_COUNT
};

/* Light sensors */
enum als_id {
	ALS_OPT3001 = 0,

	ALS_COUNT
};

/* Motion sensors */
enum sensor_id {
	LID_ACCEL = 0,
	LID_GYRO,
	LID_MAG,
	BASE_ACCEL,
};

/* start as a sink in case we have no other power supply/battery */
#define PD_DEFAULT_STATE PD_STATE_SNK_DISCONNECTED

/* TODO: determine the following board specific type-C power constants */
/* FIXME(dhendrix): verify all of the below PD_* numbers */
/*
 * delay to turn on the power supply max is ~16ms.
 * delay to turn off the power supply max is about ~180ms.
 */
#define PD_POWER_SUPPLY_TURN_ON_DELAY  30000  /* us */
#define PD_POWER_SUPPLY_TURN_OFF_DELAY 250000 /* us */

/* delay to turn on/off vconn */
#define PD_VCONN_SWAP_DELAY 5000 /* us */

/* Define typical operating power and max power */
#define PD_OPERATING_POWER_MW 15000
#define PD_MAX_POWER_MW       45000
#define PD_MAX_CURRENT_MA     3000
#define PD_MAX_VOLTAGE_MV     20000

/* Reset PD MCU */
void board_reset_pd_mcu(void);

int board_get_version(void);

void board_set_tcpc_power_mode(int port, int mode);

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
