/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "cros/reg_audit.h"

#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#include <cmsis_core.h>
#include <reg/reg_acpi.h>
#include <reg/reg_adc.h>
#include <reg/reg_emi.h>
#include <reg/reg_espi.h>
#include <reg/reg_gpio.h>
#include <reg/reg_kbc.h>
#include <reg/reg_kbm.h>
#include <reg/reg_port80.h>
#include <reg/reg_pwm.h>
#include <reg/reg_rtc.h>
#include <reg/reg_rtmr.h>
#include <reg/reg_slwtmr.h>
#include <reg/reg_spi.h>
#include <reg/reg_spic.h>
#include <reg/reg_system.h>
#include <reg/reg_tacho.h>
#include <reg/reg_timer.h>
#include <reg/reg_wdt.h>

/*
 * Encode RTS5912 clock gate register offset (within SYSTEM_Type @ 0x40010000)
 * and enable bit into a 16-bit clk_id:
 *   bits [15:8] = byte offset in SYSTEM_Type
 *   bits [4:0]  = bit index (0..31)
 */
#define RTK_CLK_ID(_sys_off, _bit) \
	(((uint16_t)(_sys_off) << 8) | ((_bit) & 0x1F))
#define RTK_CLK_OFF(_id) (((_id) >> 8) & 0xFF)
#define RTK_CLK_BIT(_id) ((_id) & 0x1F)

#define RTK_SYS_BASE 0x40020000UL
#define OFF_PERICLKPWR0 offsetof(SYSTEM_Type, PERICLKPWR0)
#define OFF_PERICLKPWR1 offsetof(SYSTEM_Type, PERICLKPWR1)
#define OFF_PERICLKPWR2 offsetof(SYSTEM_Type, PERICLKPWR2)
#define OFF_I2CCLK offsetof(SYSTEM_Type, I2CCLK)
#define OFF_UARTCLK offsetof(SYSTEM_Type, UARTCLK)
#define OFF_ADCCLK offsetof(SYSTEM_Type, ADCCLK)

/* 1. SYSTEM (SCCON @ 0x40010000) */
static const struct reg_field_desc rtk_system_fields[] = {
	REG_FIELD(SYSTEM_Type, I3CCLK, 0),
	REG_FIELD(SYSTEM_Type, I2CCLK, 0),
	REG_FIELD(SYSTEM_Type, TMRCLK, 0),
	REG_FIELD(SYSTEM_Type, PERICLKPWR0, 0),
	REG_FIELD(SYSTEM_Type, UARTCLK, 0),
	REG_FIELD(SYSTEM_Type, SYSCLK, 0),
	REG_FIELD(SYSTEM_Type, ADCCLK, 0),
	REG_FIELD(SYSTEM_Type, PERICLKPWR1, 0),
	REG_FIELD(SYSTEM_Type, SLPCTRL, 0),
	REG_FIELD(SYSTEM_Type, VIVOCTRL, 0),
	REG_FIELD(SYSTEM_Type, LDOCTRL, 0),
	REG_FIELD(SYSTEM_Type, RC25MCTRL, 0),
	REG_FIELD(SYSTEM_Type, PLLCTRL, 0),
	REG_FIELD(SYSTEM_Type, RC32KCTRL, 0),
	REG_FIELD(SYSTEM_Type, PERICLKPWR2, 0),
};
static const struct reg_block_type rtk_system_type =
	REG_BLOCK_TYPE("SYSTEM", rtk_system_fields);

/* 2. GPIO (132 GCR registers @ 0x40090000) */
static const struct reg_field_desc rtk_gpio_fields[] = {
	REG_FIELD_ARR(GPIO_Type, GCR, 0),
};
static const struct reg_block_type rtk_gpio_type =
	REG_BLOCK_TYPE("GPIO", rtk_gpio_fields);

/* 3. WDT (@ 0x40002400) */
static const struct reg_field_desc rtk_wdt_fields[] = {
	REG_FIELD(WDT_Type, CTRL, 0),
	REG_FIELD(WDT_Type, STS, 0),
	REG_FIELD(WDT_Type, CNT, REG_FLAG_VOLATILE),
	REG_FIELD(WDT_Type, DIV, 0),
	REG_FIELD(WDT_Type, INTEN, 0),
};
static const struct reg_block_type rtk_wdt_type =
	REG_BLOCK_TYPE("WDT", rtk_wdt_fields);

/* 4. RTMR (@ 0x40001800) */
static const struct reg_field_desc rtk_rtmr_fields[] = {
	REG_FIELD(RTOSTMR_Type, LDCNT, 0),
	REG_FIELD(RTOSTMR_Type, CNT, REG_FLAG_VOLATILE),
	REG_FIELD(RTOSTMR_Type, CTRL, 0),
	REG_FIELD(RTOSTMR_Type, INTSTS, 0),
};
static const struct reg_block_type rtk_rtmr_type =
	REG_BLOCK_TYPE("RTMR", rtk_rtmr_fields);

/* 5. SLWTMR (@ 0x40001A00 / 0x40001C00) */
static const struct reg_field_desc rtk_slwtmr_fields[] = {
	REG_FIELD(struct slwtmr_type, ldcnt, 0),
	REG_FIELD(struct slwtmr_type, cnt, REG_FLAG_VOLATILE),
	REG_FIELD(struct slwtmr_type, ctrl, 0),
	REG_FIELD(struct slwtmr_type, insts, 0),
};
static const struct reg_block_type rtk_slwtmr_type =
	REG_BLOCK_TYPE("SLWTMR", rtk_slwtmr_fields);

/* 6. TIMER32 (@ 0x40000000..0x40001400) */
static const struct reg_field_desc rtk_timer_fields[] = {
	REG_FIELD(struct timer32_type, ldcnt, 0),
	REG_FIELD(struct timer32_type, cnt, REG_FLAG_VOLATILE),
	REG_FIELD(struct timer32_type, ctrl, 0),
	REG_FIELD(struct timer32_type, intclr, REG_FLAG_WRITE_ONLY),
	REG_FIELD(struct timer32_type, intsts, 0),
};
static const struct reg_block_type rtk_timer_type =
	REG_BLOCK_TYPE("TIMER32", rtk_timer_fields);

/* 7. RTC (@ 0x40002800) */
static const struct reg_field_desc rtk_rtc_fields[] = {
	REG_FIELD(RTC_Type, SEC, REG_FLAG_VOLATILE),
	REG_FIELD(RTC_Type, SECALARM, 0),
	REG_FIELD(RTC_Type, MIN, REG_FLAG_VOLATILE),
	REG_FIELD(RTC_Type, MINALARM, 0),
	REG_FIELD(RTC_Type, HR, REG_FLAG_VOLATILE),
	REG_FIELD(RTC_Type, HRALARM, 0),
	REG_FIELD(RTC_Type, DAYWEEK, REG_FLAG_VOLATILE),
	REG_FIELD(RTC_Type, DAYMONTH, REG_FLAG_VOLATILE),
	REG_FIELD(RTC_Type, MONTH, REG_FLAG_VOLATILE),
	REG_FIELD(RTC_Type, YEAR, REG_FLAG_VOLATILE),
	REG_FIELD(RTC_Type, CTRL0, 0),
	REG_FIELD(RTC_Type, CTRL1, 0),
	REG_FIELD(RTC_Type, CTRL2, 0),
	REG_FIELD(RTC_Type, DAYWEEKALARM, 0),
	REG_FIELD(RTC_Type, DAYMONTHALARM, 0),
	REG_FIELD(RTC_Type, DLSFW, 0),
	REG_FIELD(RTC_Type, DLSBW, 0),
	REG_FIELD(RTC_Type, WEEK, REG_FLAG_VOLATILE),
};
static const struct reg_block_type rtk_rtc_type =
	REG_BLOCK_TYPE("RTC", rtk_rtc_fields);

/* 8. PWM (@ 0x40004000..0x400042C0) */
static const struct reg_field_desc rtk_pwm_fields[] = {
	REG_FIELD(struct pwm_regs, duty, 0),
	REG_FIELD(struct pwm_regs, div, 0),
	REG_FIELD(struct pwm_regs, ctrl, 0),
};
static const struct reg_block_type rtk_pwm_type =
	REG_BLOCK_TYPE("PWM", rtk_pwm_fields);

/* 9. TACHO (@ 0x40006000..0x40006300) */
static const struct reg_field_desc rtk_tacho_fields[] = {
	REG_FIELD(struct tacho_regs, ctrl, 0),
	REG_FIELD(struct tacho_regs, status, REG_FLAG_VOLATILE),
	REG_FIELD(struct tacho_regs, limit_high, 0),
	REG_FIELD(struct tacho_regs, limit_low, 0),
	REG_FIELD(struct tacho_regs, int_en, 0),
};
static const struct reg_block_type rtk_tacho_type =
	REG_BLOCK_TYPE("TACHO", rtk_tacho_fields);

/* 10. ADC (@ 0x40084000) */
static const struct reg_field_desc rtk_adc_fields[] = {
	REG_FIELD(struct adc_regs, ctrl, 0),
	REG_FIELD(struct adc_regs, chctrl, 0),
	REG_FIELD(struct adc_regs, sts, 0),
	REG_FIELD_ARR(struct adc_regs, chdata, REG_FLAG_VOLATILE),
	REG_FIELD(struct adc_regs, coeffa, 0),
	REG_FIELD(struct adc_regs, coeffb, 0),
};
static const struct reg_block_type rtk_adc_type =
	REG_BLOCK_TYPE("ADC", rtk_adc_fields);

/* 11. ESPI (@ 0x4000A000) */
static const struct reg_field_desc rtk_espi_fields[] = {
	REG_FIELD(struct espi_reg, EPSTS, 0),
	REG_FIELD(struct espi_reg, EPCFG, 0),
	REG_FIELD(struct espi_reg, EPINTEN, 0),
	REG_FIELD(struct espi_reg, EPRXINFO, 0),
	REG_FIELD(struct espi_reg, EPRXADRH, 0),
	REG_FIELD(struct espi_reg, EPRXADRL, 0),
	REG_FIELD(struct espi_reg, EPCPRADR, 0),
	REG_FIELD(struct espi_reg, EPCPRVLD, 0),
	REG_FIELD(struct espi_reg, EPBUF, REG_FLAG_SIDE_EFFECT),
	REG_FIELD(struct espi_reg, EPBUFSZ, 0),
	REG_FIELD(struct espi_reg, EPPTRCLR, 0),
	REG_FIELD(struct espi_reg, ELMSG, 0),
	REG_FIELD(struct espi_reg, EPMRDA, REG_FLAG_SIDE_EFFECT),
	REG_FIELD(struct espi_reg, EPMRADRL, 0),
	REG_FIELD(struct espi_reg, EPMRADRH, 0),
	REG_FIELD(struct espi_reg, EPMRLEN, 0),
	REG_FIELD(struct espi_reg, EVSTS, 0),
	REG_FIELD(struct espi_reg, EVCFG, 0),
	REG_FIELD(struct espi_reg, EVIDX2, 0),
	REG_FIELD(struct espi_reg, EVIDX3, 0),
	REG_FIELD(struct espi_reg, EVIDX7, 0),
	REG_FIELD(struct espi_reg, EVIDX41, 0),
	REG_FIELD(struct espi_reg, EVIDX42, 0),
	REG_FIELD(struct espi_reg, EVIDX43, 0),
	REG_FIELD(struct espi_reg, EVIDX44, 0),
	REG_FIELD(struct espi_reg, EVIDX47, 0),
	REG_FIELD(struct espi_reg, EVIDX4A, 0),
	REG_FIELD(struct espi_reg, EVTXDAT, REG_FLAG_SIDE_EFFECT),
	REG_FIELD(struct espi_reg, EVPVIDX, 0),
	REG_FIELD(struct espi_reg, EVRXINTEN, 0),
	REG_FIELD(struct espi_reg, EVTXINTEN, 0),
	REG_FIELD(struct espi_reg, EOSTS, 0),
	REG_FIELD(struct espi_reg, EOCFG, 0),
	REG_FIELD(struct espi_reg, EORXINTEN, 0),
	REG_FIELD(struct espi_reg, EORXBUF, REG_FLAG_SIDE_EFFECT),
	REG_FIELD(struct espi_reg, EORXLEN, 0),
	REG_FIELD(struct espi_reg, EOTXBUF, REG_FLAG_SIDE_EFFECT),
	REG_FIELD(struct espi_reg, EOTXLEN, 0),
	REG_FIELD(struct espi_reg, EOTXCTRL, 0),
	REG_FIELD(struct espi_reg, EOTXINTEN, 0),
	REG_FIELD(struct espi_reg, EFSTS, 0),
	REG_FIELD(struct espi_reg, EFCONF, 0),
	REG_FIELD(struct espi_reg, EMADR, 0),
	REG_FIELD(struct espi_reg, EMTRLEN, 0),
	REG_FIELD(struct espi_reg, EMBUF, REG_FLAG_SIDE_EFFECT),
	REG_FIELD(struct espi_reg, EMCTRL, 0),
	REG_FIELD(struct espi_reg, EMINTEN, 0),
	REG_FIELD(struct espi_reg, ESBUF, REG_FLAG_SIDE_EFFECT),
	REG_FIELD(struct espi_reg, ESINTEN, 0),
	REG_FIELD(struct espi_reg, ESRXADR, 0),
	REG_FIELD(struct espi_reg, ESRXLEN, 0),
	REG_FIELD(struct espi_reg, ESPICFG, 0),
	REG_FIELD(struct espi_reg, ERSTCFG, 0),
	REG_FIELD(struct espi_reg, EVIDX51, 0),
	REG_FIELD(struct espi_reg, EVIDX61, 0),
	REG_FIELD(struct espi_reg, ESRXC, 0),
	REG_FIELD(struct espi_reg, EFCFG2, 0),
	REG_FIELD(struct espi_reg, EFCFG3, 0),
	REG_FIELD(struct espi_reg, EFCFG4, 0),
};
static const struct reg_block_type rtk_espi_type =
	REG_BLOCK_TYPE("ESPI", rtk_espi_fields);

/* 12. KBC (@ 0x4000B000) */
static const struct reg_field_desc rtk_kbc_fields[] = {
	REG_FIELD(struct kbc_reg, STS, 0),
	REG_FIELD(struct kbc_reg, IB, REG_FLAG_SIDE_EFFECT),
	REG_FIELD(struct kbc_reg, OB, 0),
	REG_FIELD(struct kbc_reg, PTADDR, 0),
	REG_FIELD(struct kbc_reg, INTEN, 0),
	REG_FIELD(struct kbc_reg, VWCTRL0, 0),
	REG_FIELD(struct kbc_reg, VWCTRL1, 0),
};
static const struct reg_block_type rtk_kbc_type =
	REG_BLOCK_TYPE("KBC", rtk_kbc_fields);

/* 13. ACPI (@ 0x4000B400..0x4000C400) */
static const struct reg_field_desc rtk_acpi_fields[] = {
	REG_FIELD(struct acpi_reg, STS, 0),
	REG_FIELD(struct acpi_reg, IB, REG_FLAG_SIDE_EFFECT),
	REG_FIELD(struct acpi_reg, OB, 0),
	REG_FIELD(struct acpi_reg, PTADDR, 0),
	REG_FIELD(struct acpi_reg, INTEN, 0),
	REG_FIELD(struct acpi_reg, VWCTRL0, 0),
	REG_FIELD(struct acpi_reg, VWCTRL1, 0),
};
static const struct reg_block_type rtk_acpi_type =
	REG_BLOCK_TYPE("ACPI", rtk_acpi_fields);

/* 14. EMI (@ 0x4000D000..0x4000EC00) */
static const struct reg_field_desc rtk_emi_fields[] = {
	REG_FIELD(struct emi_reg, CFG, 0),
	REG_FIELD(struct emi_reg, INTCTRL, 0),
	REG_FIELD(struct emi_reg, IRQNUM, 0),
	REG_FIELD(struct emi_reg, SAR, 0),
	REG_FIELD(struct emi_reg, INTSTS, 0),
	REG_FIELD(struct emi_reg, STS, 0),
};
static const struct reg_block_type rtk_emi_type =
	REG_BLOCK_TYPE("EMI", rtk_emi_fields);

/* 15. PORT80 (@ 0x4000C800 / 0x4000CC00) */
static const struct reg_field_desc rtk_port80_fields[] = {
	REG_FIELD(struct port80_reg, STS, 0),
	REG_FIELD(struct port80_reg, CFG, 0),
	REG_FIELD(struct port80_reg, INTEN, 0),
	REG_FIELD(struct port80_reg, DATA, REG_FLAG_SIDE_EFFECT),
	REG_FIELD(struct port80_reg, ADDR, 0),
};
static const struct reg_block_type rtk_port80_type =
	REG_BLOCK_TYPE("PORT80", rtk_port80_fields);

/* 16. KBM (@ 0x40098000) */
static const struct reg_field_desc rtk_kbm_fields[] = {
	REG_FIELD(struct kbm_regs, scan_out, 0),
	REG_FIELD(struct kbm_regs, scan_in, REG_FLAG_VOLATILE),
	REG_FIELD(struct kbm_regs, int_en, 0),
	REG_FIELD(struct kbm_regs, ctrl, 0),
};
static const struct reg_block_type rtk_kbm_type =
	REG_BLOCK_TYPE("KBM", rtk_kbm_fields);

/* 17. DesignWare I2C (I2C0..7 @ 0x40081000..0x40082C00) */
static const struct reg_field_desc rtk_dw_i2c_fields[] = {
	REG_RAW("IC_CON", 0x00, 4, 0),
	REG_RAW("IC_TAR", 0x04, 4, 0),
	REG_RAW("IC_SAR", 0x08, 4, 0),
	REG_RAW("IC_DATA_CMD", 0x10, 4, REG_FLAG_SIDE_EFFECT),
	REG_RAW("IC_SS_SCL_HCNT", 0x14, 4, 0),
	REG_RAW("IC_SS_SCL_LCNT", 0x18, 4, 0),
	REG_RAW("IC_FS_SCL_HCNT", 0x1C, 4, 0),
	REG_RAW("IC_FS_SCL_LCNT", 0x20, 4, 0),
	REG_RAW("IC_INTR_STAT", 0x2C, 4, REG_FLAG_VOLATILE),
	REG_RAW("IC_INTR_MASK", 0x30, 4, 0),
	REG_RAW("IC_RAW_INTR_STAT", 0x34, 4, REG_FLAG_VOLATILE),
	REG_RAW("IC_RX_TL", 0x38, 4, 0),
	REG_RAW("IC_TX_TL", 0x3C, 4, 0),
	REG_RAW("IC_ENABLE", 0x6C, 4, 0),
	REG_RAW("IC_STATUS", 0x70, 4, REG_FLAG_VOLATILE),
	REG_RAW("IC_TXFLR", 0x74, 4, REG_FLAG_VOLATILE),
	REG_RAW("IC_RXFLR", 0x78, 4, REG_FLAG_VOLATILE),
	REG_RAW("IC_SDA_HOLD", 0x7C, 4, 0),
	REG_RAW("IC_TX_ABRT_SOURCE", 0x80, 4, 0),
	REG_RAW("IC_ENABLE_STATUS", 0x9C, 4, 0),
};
static const struct reg_block_type rtk_dw_i2c_type =
	REG_BLOCK_TYPE("DW_I2C", rtk_dw_i2c_fields);

/* 18. NS16550 UART0 (@ 0x40080000) */
static const struct reg_field_desc rtk_uart_fields[] = {
	REG_RAW("RBR_THR_DLL", 0x00, 4, REG_FLAG_SIDE_EFFECT),
	REG_RAW("IER_DLM", 0x04, 4, 0),
	REG_RAW("IIR_FCR", 0x08, 4, REG_FLAG_SIDE_EFFECT),
	REG_RAW("LCR", 0x0C, 4, 0),
	REG_RAW("MCR", 0x10, 4, 0),
	REG_RAW("LSR", 0x14, 4, REG_FLAG_VOLATILE),
	REG_RAW("MSR", 0x18, 4, REG_FLAG_VOLATILE),
	REG_RAW("SCR", 0x1C, 4, 0),
};
static const struct reg_block_type rtk_uart_type =
	REG_BLOCK_TYPE("UART", rtk_uart_fields);

/* 19. ARM Cortex-M33 Core Peripherals (SCB, NVIC, SysTick) */
static const struct reg_field_desc arm_scb_fields[] = {
	REG_FIELD(SCB_Type, CPUID, 0),
	REG_FIELD(SCB_Type, ICSR, REG_FLAG_VOLATILE),
	REG_FIELD(SCB_Type, VTOR, 0),
	REG_FIELD(SCB_Type, AIRCR, 0),
	REG_FIELD(SCB_Type, SCR, 0),
	REG_FIELD(SCB_Type, CCR, 0),
	REG_FIELD_ARR(SCB_Type, SHPR, 0),
	REG_FIELD(SCB_Type, SHCSR, 0),
	REG_FIELD(SCB_Type, CFSR, 0),
	REG_FIELD(SCB_Type, HFSR, 0),
	REG_FIELD(SCB_Type, CPACR, 0),
};
static const struct reg_block_type arm_scb_type =
	REG_BLOCK_TYPE("ARM_SCB", arm_scb_fields);

static const struct reg_field_desc arm_nvic_fields[] = {
	REG_RAW_ARR("ISER", offsetof(NVIC_Type, ISER), 4, 8, 4, 0),
	REG_RAW_ARR("ISPR", offsetof(NVIC_Type, ISPR), 4, 8, 4,
		    REG_FLAG_VOLATILE),
	REG_RAW_ARR("IABR", offsetof(NVIC_Type, IABR), 4, 8, 4,
		    REG_FLAG_VOLATILE),
	REG_RAW_ARR("IPR", offsetof(NVIC_Type, IPR), 4, 60, 4, 0),
};
static const struct reg_block_type arm_nvic_type =
	REG_BLOCK_TYPE("ARM_NVIC", arm_nvic_fields);

static const struct reg_field_desc arm_systick_fields[] = {
	REG_FIELD(SysTick_Type, CTRL, REG_FLAG_VOLATILE),
	REG_FIELD(SysTick_Type, LOAD, 0),
	REG_FIELD(SysTick_Type, VAL, REG_FLAG_VOLATILE),
};
static const struct reg_block_type arm_systick_type =
	REG_BLOCK_TYPE("ARM_SYSTICK", arm_systick_fields);

/* Instantiated modules on Realtek RTS5912 (SYSTEM first!) */
static const struct reg_module_inst rtk_modules[] = {
	{ "SYSTEM", 0x40020000, &rtk_system_type, REG_CLK_NONE },
	{ "ARM_SCB", SCB_BASE, &arm_scb_type, REG_CLK_NONE },
	{ "ARM_NVIC", NVIC_BASE, &arm_nvic_type, REG_CLK_NONE },
	{ "ARM_SYSTICK", SysTick_BASE, &arm_systick_type, REG_CLK_NONE },
	{ "GPIO", 0x40090000, &rtk_gpio_type,
	  RTK_CLK_ID(OFF_PERICLKPWR0, SYSTEM_PERICLKPWR0_GPIOCLKPWR_Pos) },
	{ "WDT", 0x4000C000, &rtk_wdt_type,
	  RTK_CLK_ID(OFF_PERICLKPWR2, SYSTEM_PERICLKPWR2_WDTCLKPWR_Pos) },
	{ "RTMR", 0x4000C500, &rtk_rtmr_type,
	  RTK_CLK_ID(OFF_PERICLKPWR1, SYSTEM_PERICLKPWR1_RTMRCLKPWR_Pos) },
	{ "SLWTMR0", 0x4000C200, &rtk_slwtmr_type,
	  RTK_CLK_ID(OFF_PERICLKPWR1, SYSTEM_PERICLKPWR1_SLWTMR0CLKPWR_Pos) },
	{ "SLWTMR1", 0x4000C220, &rtk_slwtmr_type,
	  RTK_CLK_ID(OFF_PERICLKPWR1, SYSTEM_PERICLKPWR1_SLWTMR1CLKPWR_Pos) },
	{ "TIMER0", 0x4000C300, &rtk_timer_type,
	  RTK_CLK_ID(OFF_PERICLKPWR1, SYSTEM_PERICLKPWR1_TMR0CLKPWR_Pos) },
	{ "TIMER1", 0x4000C314, &rtk_timer_type,
	  RTK_CLK_ID(OFF_PERICLKPWR1, SYSTEM_PERICLKPWR1_TMR1CLKPWR_Pos) },
	{ "TIMER2", 0x4000C328, &rtk_timer_type,
	  RTK_CLK_ID(OFF_PERICLKPWR1, SYSTEM_PERICLKPWR1_TMR2CLKPWR_Pos) },
	{ "TIMER3", 0x4000C33C, &rtk_timer_type,
	  RTK_CLK_ID(OFF_PERICLKPWR1, SYSTEM_PERICLKPWR1_TMR3CLKPWR_Pos) },
	{ "TIMER4", 0x4000C350, &rtk_timer_type,
	  RTK_CLK_ID(OFF_PERICLKPWR1, SYSTEM_PERICLKPWR1_TMR4CLKPWR_Pos) },
	{ "TIMER5", 0x4000C364, &rtk_timer_type,
	  RTK_CLK_ID(OFF_PERICLKPWR1, SYSTEM_PERICLKPWR1_TMR5CLKPWR_Pos) },
	{ "RTC", 0x4000C100, &rtk_rtc_type,
	  RTK_CLK_ID(OFF_PERICLKPWR2, SYSTEM_PERICLKPWR2_RTCCLKPWR_Pos) },

	/* Host Interfaces */
	{ "ESPI", 0x400B1000, &rtk_espi_type,
	  RTK_CLK_ID(OFF_PERICLKPWR0, SYSTEM_PERICLKPWR0_ESPICLKPWR_Pos) },
	{ "KBC", 0x400A0100, &rtk_kbc_type,
	  RTK_CLK_ID(OFF_PERICLKPWR0, SYSTEM_PERICLKPWR0_KBCCLKPWR_Pos) },
	{ "ACPI0", 0x400A0200, &rtk_acpi_type,
	  RTK_CLK_ID(OFF_PERICLKPWR0, SYSTEM_PERICLKPWR0_ACPICLKPWR_Pos) },
	{ "ACPI1", 0x400A021C, &rtk_acpi_type,
	  RTK_CLK_ID(OFF_PERICLKPWR0, SYSTEM_PERICLKPWR0_PMPORT0CLKPWR_Pos) },
	{ "ACPI2", 0x400A0238, &rtk_acpi_type,
	  RTK_CLK_ID(OFF_PERICLKPWR0, SYSTEM_PERICLKPWR0_PMPORT1CLKPWR_Pos) },
	{ "ACPI3", 0x400A0254, &rtk_acpi_type,
	  RTK_CLK_ID(OFF_PERICLKPWR0, SYSTEM_PERICLKPWR0_PMPORT2CLKPWR_Pos) },
	{ "ACPI4", 0x400A0270, &rtk_acpi_type,
	  RTK_CLK_ID(OFF_PERICLKPWR0, SYSTEM_PERICLKPWR0_PMPORT3CLKPWR_Pos) },
	{ "PORT80_0", 0x400A0400, &rtk_port80_type,
	  RTK_CLK_ID(OFF_PERICLKPWR0, SYSTEM_PERICLKPWR0_P80CLKPWR_Pos) },
	{ "EMI0", 0x40082000, &rtk_emi_type,
	  RTK_CLK_ID(OFF_PERICLKPWR0, SYSTEM_PERICLKPWR0_EMI0CLKPWR_Pos) },
	{ "EMI1", 0x40082020, &rtk_emi_type,
	  RTK_CLK_ID(OFF_PERICLKPWR0, SYSTEM_PERICLKPWR0_EMI1CLKPWR_Pos) },
	{ "EMI2", 0x40082040, &rtk_emi_type,
	  RTK_CLK_ID(OFF_PERICLKPWR1, SYSTEM_PERICLKPWR1_EMI2CLKPWR_Pos) },
	{ "EMI3", 0x40082060, &rtk_emi_type,
	  RTK_CLK_ID(OFF_PERICLKPWR1, SYSTEM_PERICLKPWR1_EMI3CLKPWR_Pos) },

	/* PWM 0..7 */
	{ "PWM0", 0x4000F000, &rtk_pwm_type,
	  RTK_CLK_ID(OFF_PERICLKPWR0, SYSTEM_PERICLKPWR0_PWM0CLKPWR_Pos) },
	{ "PWM1", 0x4000F00C, &rtk_pwm_type,
	  RTK_CLK_ID(OFF_PERICLKPWR0, SYSTEM_PERICLKPWR0_PWM1CLKPWR_Pos) },
	{ "PWM2", 0x4000F018, &rtk_pwm_type,
	  RTK_CLK_ID(OFF_PERICLKPWR0, SYSTEM_PERICLKPWR0_PWM2CLKPWR_Pos) },
	{ "PWM3", 0x4000F024, &rtk_pwm_type,
	  RTK_CLK_ID(OFF_PERICLKPWR0, SYSTEM_PERICLKPWR0_PWM3CLKPWR_Pos) },
	{ "PWM4", 0x4000F030, &rtk_pwm_type,
	  RTK_CLK_ID(OFF_PERICLKPWR0, SYSTEM_PERICLKPWR0_PWM4CLKPWR_Pos) },
	{ "PWM5", 0x4000F03C, &rtk_pwm_type,
	  RTK_CLK_ID(OFF_PERICLKPWR0, SYSTEM_PERICLKPWR0_PWM5CLKPWR_Pos) },
	{ "PWM6", 0x4000F048, &rtk_pwm_type,
	  RTK_CLK_ID(OFF_PERICLKPWR0, SYSTEM_PERICLKPWR0_PWM6CLKPWR_Pos) },
	{ "PWM7", 0x4000F054, &rtk_pwm_type,
	  RTK_CLK_ID(OFF_PERICLKPWR0, SYSTEM_PERICLKPWR0_PWM7CLKPWR_Pos) },

	/* TACHO 0..3 */
	{ "TACHO0", 0x4000FD00, &rtk_tacho_type,
	  RTK_CLK_ID(OFF_PERICLKPWR0, SYSTEM_PERICLKPWR0_TACHO0CLKPWR_Pos) },
	{ "TACHO1", 0x4000FD40, &rtk_tacho_type,
	  RTK_CLK_ID(OFF_PERICLKPWR0, SYSTEM_PERICLKPWR0_TACHO1CLKPWR_Pos) },
	{ "TACHO2", 0x4000FD80, &rtk_tacho_type,
	  RTK_CLK_ID(OFF_PERICLKPWR0, SYSTEM_PERICLKPWR0_TACHO2CLKPWR_Pos) },
	{ "TACHO3", 0x4000FDC0, &rtk_tacho_type,
	  RTK_CLK_ID(OFF_PERICLKPWR0, SYSTEM_PERICLKPWR0_TACHO3CLKPWR_Pos) },

	/* I2C 0..7, UART0, ADC0, KBM */
	{ "I2C0", 0x4000D000, &rtk_dw_i2c_type,
	  RTK_CLK_ID(OFF_I2CCLK, SYSTEM_I2CCLK_I2C0CLKPWR_Pos) },
	{ "I2C1", 0x4000D200, &rtk_dw_i2c_type,
	  RTK_CLK_ID(OFF_I2CCLK, SYSTEM_I2CCLK_I2C1CLKPWR_Pos) },
	{ "I2C2", 0x4000D400, &rtk_dw_i2c_type,
	  RTK_CLK_ID(OFF_I2CCLK, SYSTEM_I2CCLK_I2C2CLKPWR_Pos) },
	{ "I2C3", 0x4000D600, &rtk_dw_i2c_type,
	  RTK_CLK_ID(OFF_I2CCLK, SYSTEM_I2CCLK_I2C3CLKPWR_Pos) },
	{ "I2C4", 0x4000D800, &rtk_dw_i2c_type,
	  RTK_CLK_ID(OFF_I2CCLK, SYSTEM_I2CCLK_I2C4CLKPWR_Pos) },
	{ "I2C5", 0x4000DA00, &rtk_dw_i2c_type,
	  RTK_CLK_ID(OFF_I2CCLK, SYSTEM_I2CCLK_I2C5CLKPWR_Pos) },
	{ "I2C6", 0x4000DC00, &rtk_dw_i2c_type,
	  RTK_CLK_ID(OFF_I2CCLK, SYSTEM_I2CCLK_I2C6CLKPWR_Pos) },
	{ "I2C7", 0x4000DE00, &rtk_dw_i2c_type,
	  RTK_CLK_ID(OFF_I2CCLK, SYSTEM_I2CCLK_I2C7CLKPWR_Pos) },
	{ "UART0", 0x40010100, &rtk_uart_type,
	  RTK_CLK_ID(OFF_UARTCLK, SYSTEM_UARTCLK_PWR_Pos) },
	{ "ADC0", 0x4000FE00, &rtk_adc_type,
	  RTK_CLK_ID(OFF_ADCCLK, SYSTEM_ADCCLK_PWR_Pos) },
	{ "KBM", 0x40010000, &rtk_kbm_type,
	  RTK_CLK_ID(OFF_PERICLKPWR0, SYSTEM_PERICLKPWR0_KBMCLKPWR_Pos) },
};

const struct reg_module_inst *reg_audit_chip_get_modules(size_t *num_modules)
{
	*num_modules = ARRAY_SIZE(rtk_modules);
	return rtk_modules;
}

int reg_audit_chip_module_enter(const struct reg_module_inst *mod,
				int *was_clocked, uint32_t *saved_state)
{
	if (mod->clk_id == REG_CLK_NONE) {
		*was_clocked = 1;
		*saved_state = 0;
		return 0;
	}

	uint8_t off = RTK_CLK_OFF(mod->clk_id);
	uint8_t bit = RTK_CLK_BIT(mod->clk_id);
	volatile uint32_t *reg = (volatile uint32_t *)(RTK_SYS_BASE + off);
	uint32_t val = *reg;

	*saved_state = val;
	/*
	 * On RTS5912, clock/power bit == 1 means enabled, 0 means gated.
	 * Do not force-ungate unclocked peripherals at PRE_KERNEL_1 priority 0
	 * because functional clock sources (RC32K / PLL) may not be running
	 * yet, which stalls the APB bridge. Returning -EACCES records
	 * was_clocked=0 in mod_clocked and safely skips APB reads.
	 */
	if ((val & BIT(bit)) == 0) {
		*was_clocked = 0;
		return -EACCES;
	}

	*was_clocked = 1;
	return 0;
}

void reg_audit_chip_module_exit(const struct reg_module_inst *mod,
				uint32_t saved_state)
{
	ARG_UNUSED(mod);
	ARG_UNUSED(saved_state);
}

void reg_audit_chip_dump_core_regs(const char *stage_tag)
{
	printk("CORE|%s|CONTROL=0x%08x|PRIMASK=0x%08x|"
	       "BASEPRI=0x%08x|MSP=0x%08x|PSP=0x%08x\n",
	       stage_tag, (unsigned int)__get_CONTROL(),
	       (unsigned int)__get_PRIMASK(), (unsigned int)__get_BASEPRI(),
	       (unsigned int)__get_MSP(), (unsigned int)__get_PSP());
}
