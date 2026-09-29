/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "cros/reg_audit.h"

#include <zephyr/arch/riscv/csr.h>
#include <zephyr/devicetree.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#include <chip_chipregs.h>
#include <soc.h>

#if defined(CONFIG_SOC_IT8XXX2_REG_SET_V2)
#define IT8XXX2_REG_BASE 0x00F00000UL
#else
#define IT8XXX2_REG_BASE 0xFFF00000UL
#endif

#define IT8XXX2_ADDR(_off) (IT8XXX2_REG_BASE + (_off))

/* 1. ECPM (Clock & Power Management, 0x1E00) */
static const struct reg_field_desc it8xxx2_ecpm_fields[] = {
	REG_RAW("CGCTRL1R", 0x01, 1, 0), REG_RAW("CGCTRL2R", 0x02, 1, 0),
	REG_RAW("PLLCTRL", 0x03, 1, 0),	 REG_RAW("AUTOCG", 0x04, 1, 0),
	REG_RAW("CGCTRL3R", 0x05, 1, 0), REG_RAW("PLLFREQR", 0x06, 1, 0),
	REG_RAW("PLLSSCR", 0x07, 1, 0),	 REG_RAW("PLLCSS", 0x08, 1, 0),
	REG_RAW("CGCTRL4R", 0x09, 1, 0), REG_RAW("SCDCR0", 0x0C, 1, 0),
	REG_RAW("SCDCR1", 0x0D, 1, 0),	 REG_RAW("SCDCR2", 0x0E, 1, 0),
	REG_RAW("SCDCR3", 0x0F, 1, 0),	 REG_RAW("SCDCR4", 0x10, 1, 0),
};
static const struct reg_block_type it8xxx2_ecpm_type =
	REG_BLOCK_TYPE("ECPM", it8xxx2_ecpm_fields);

/* 2. GCTRL (Global Control, 0x2000) */
static const struct reg_field_desc it8xxx2_gctrl_fields[] = {
	REG_FIELD(struct gctrl_it8xxx2_regs, GCTRL_ECHIPVER, 0),
	REG_FIELD(struct gctrl_it8xxx2_regs, GCTRL_DBGROS, 0),
	REG_FIELD(struct gctrl_it8xxx2_regs, GCTRL_RSTS, 0),
	REG_FIELD(struct gctrl_it8xxx2_regs, GCTRL_BADRSEL, 0),
	REG_FIELD(struct gctrl_it8xxx2_regs, GCTRL_WNCKR, REG_FLAG_WRITE_ONLY),
	REG_FIELD(struct gctrl_it8xxx2_regs, GCTRL_SPCTRL1, 0),
	REG_FIELD(struct gctrl_it8xxx2_regs, GCTRL_RSTDMMC, 0),
	REG_FIELD(struct gctrl_it8xxx2_regs, GCTRL_RSTC4, 0),
	REG_FIELD(struct gctrl_it8xxx2_regs, GCTRL_SPCTRL4, 0),
	REG_FIELD(struct gctrl_it8xxx2_regs, GCTRL_MCCR3, 0),
	REG_FIELD(struct gctrl_it8xxx2_regs, GCTRL_RSTC5, 0),
	REG_FIELD(struct gctrl_it8xxx2_regs, GCTRL_MCCR, 0),
	REG_FIELD(struct gctrl_it8xxx2_regs, GCTRL_EIDSR, 0),
	REG_FIELD(struct gctrl_it8xxx2_regs, gctrl_pmer2, 0),
	REG_FIELD(struct gctrl_it8xxx2_regs, GCTRL_EPLR, 0),
	REG_FIELD(struct gctrl_it8xxx2_regs, GCTRL_IVTBAR, 0),
	REG_FIELD(struct gctrl_it8xxx2_regs, GCTRL_MCCR2, 0),
	REG_FIELD(struct gctrl_it8xxx2_regs, GCTRL_PMER3, 0),
	REG_FIELD(struct gctrl_it8xxx2_regs, GCTRL_ETWDUARTCR, 0),
	REG_FIELD(struct gctrl_it8xxx2_regs, GCTRL_WMCR, 0),
	REG_FIELD(struct gctrl_it8xxx2_regs, GCTRL_P80H81HSR, 0),
	REG_FIELD(struct gctrl_it8xxx2_regs, GCTRL_P80HDR, 0),
	REG_FIELD(struct gctrl_it8xxx2_regs, GCTRL_P81HDR, 0),
	REG_FIELD(struct gctrl_it8xxx2_regs, GCTRL_H2ROFSR, 0),
	REG_FIELD(struct gctrl_it8xxx2_regs, GCTRL_RVILMCR0, 0),
	REG_FIELD(struct gctrl_it8xxx2_regs, GCTRL_ECHIPID1, 0),
	REG_FIELD(struct gctrl_it8xxx2_regs, GCTRL_ECHIPID2, 0),
	REG_FIELD(struct gctrl_it8xxx2_regs, GCTRL_ECHIPID3, 0),
};
static const struct reg_block_type it8xxx2_gctrl_type =
	REG_BLOCK_TYPE("GCTRL", it8xxx2_gctrl_fields);

/* 3. INTC (Interrupt Controller) */
#if defined(CONFIG_SOC_IT8XXX2_REG_SET_V1)
static const struct reg_field_desc it8xxx2_intc_fields[] = {
	REG_RAW_ARR("ISR", 0x00, 1, 24, 4, REG_FLAG_VOLATILE),
	REG_RAW_ARR("IER", 0x01, 1, 24, 4, 0),
	REG_RAW_ARR("IELMR", 0x02, 1, 24, 4, 0),
	REG_RAW_ARR("IPOLR", 0x03, 1, 24, 4, 0),
	REG_RAW("IVECT", 0x10, 1, REG_FLAG_VOLATILE),
};
#else
static const struct reg_field_desc it8xxx2_intc_fields[] = {
	REG_RAW("IVECT", 0x00, 1, REG_FLAG_VOLATILE),
	REG_RAW_ARR("ISR", 0x40, 1, 28, 4, REG_FLAG_VOLATILE),
	REG_RAW_ARR("IER", 0x41, 1, 28, 4, 0),
	REG_RAW_ARR("IELMR", 0x42, 1, 28, 4, 0),
	REG_RAW_ARR("IPOLR", 0x43, 1, 28, 4, 0),
};
#endif
static const struct reg_block_type it8xxx2_intc_type =
	REG_BLOCK_TYPE("INTC", it8xxx2_intc_fields);

/* 4. GPIO Global & Pin Control (0x1600) */
#if defined(CONFIG_SOC_IT8XXX2_REG_SET_V1)
static const struct reg_field_desc it8xxx2_gpio_fields[] = {
	REG_RAW("GCR", 0x00, 1, 0),
	REG_RAW_ARR("GPDR", 0x01, 1, 13, 1, 0),
	REG_RAW_ARR("GPCR", 0x10, 1, 80, 1, 0),
	REG_RAW_ARR("GPDMR", 0x61, 1, 13, 1, REG_FLAG_VOLATILE),
	REG_RAW_ARR("GPOTR", 0x71, 1, 13, 1, 0),
	REG_RAW_ARR("GPCR_KLM", 0x90, 1, 24, 1, 0),
	REG_RAW_ARR("GCR_EXT", 0xE0, 1, 10, 1, 0),
	REG_RAW_ARR("GCR_CFG", 0xF0, 1, 16, 1, 0),
};
#else
static const struct reg_field_desc it8xxx2_gpio_fields[] = {
	REG_RAW("GCR", 0x00, 1, 0),
	REG_RAW_ARR("GPDR", 0x01, 1, 13, 1, 0),
	REG_RAW_ARR("GPDMR", 0x18, 1, 13, 1, REG_FLAG_VOLATILE),
	REG_RAW_ARR("GPOTR", 0x30, 1, 13, 1, 0),
	REG_RAW_ARR("P18SCR", 0x48, 1, 13, 1, 0),
	REG_RAW_ARR("GPCR", 0x60, 1, 104, 1, 0),
};
#endif
static const struct reg_block_type it8xxx2_gpio_type =
	REG_BLOCK_TYPE("GPIO", it8xxx2_gpio_fields);

/* 5. WUC (Wake-Up Controller, 0x1B00) */
static const struct reg_field_desc it8xxx2_wuc_fields[] = {
	REG_RAW("WUEMR1", 0x00, 1, 0),
	REG_RAW("WUEMR2", 0x01, 1, 0),
	REG_RAW("WUEMR3", 0x02, 1, 0),
	REG_RAW("WUEMR4", 0x03, 1, 0),
	REG_RAW("WUESR1", 0x04, 1, REG_FLAG_VOLATILE),
	REG_RAW("WUESR2", 0x05, 1, REG_FLAG_VOLATILE),
	REG_RAW("WUESR3", 0x06, 1, REG_FLAG_VOLATILE),
	REG_RAW("WUESR4", 0x07, 1, REG_FLAG_VOLATILE),
	REG_RAW("WUENR1", 0x08, 1, 0),
	REG_RAW("WUENR2", 0x09, 1, 0),
	REG_RAW("WUENR3", 0x0A, 1, 0),
	REG_RAW("WUENR4", 0x0B, 1, 0),
	REG_RAW("WUEMR6", 0x10, 1, 0),
	REG_RAW("WUESR6", 0x11, 1, REG_FLAG_VOLATILE),
	REG_RAW("WUEMR7", 0x14, 1, 0),
	REG_RAW("WUESR7", 0x15, 1, REG_FLAG_VOLATILE),
	REG_RAW("WUENR7", 0x16, 1, 0),
	REG_RAW("WUEMR8", 0x18, 1, 0),
	REG_RAW("WUESR8", 0x19, 1, REG_FLAG_VOLATILE),
	REG_RAW("WUEMR9", 0x1C, 1, 0),
	REG_RAW("WUESR9", 0x1D, 1, REG_FLAG_VOLATILE),
	REG_RAW("WUEMR10", 0x20, 1, 0),
	REG_RAW("WUESR10", 0x21, 1, REG_FLAG_VOLATILE),
	REG_RAW("WUEMR11", 0x24, 1, 0),
	REG_RAW("WUESR11", 0x25, 1, REG_FLAG_VOLATILE),
	REG_RAW("WUEMR12", 0x28, 1, 0),
	REG_RAW("WUESR12", 0x29, 1, REG_FLAG_VOLATILE),
	REG_RAW("WUEMR13", 0x2C, 1, 0),
	REG_RAW("WUESR13", 0x2D, 1, REG_FLAG_VOLATILE),
	REG_RAW("WUEMR14", 0x30, 1, 0),
	REG_RAW("WUESR14", 0x31, 1, REG_FLAG_VOLATILE),
};
static const struct reg_block_type it8xxx2_wuc_type =
	REG_BLOCK_TYPE("WUC", it8xxx2_wuc_fields);

/* 6. SMFI (Shared Memory Flash Interface, 0x1000) */
static const struct reg_field_desc it8xxx2_smfi_fields[] = {
	REG_RAW("FBCFG", 0x00, 1, 0),
	REG_RAW("FPCFG", 0x01, 1, 0),
	REG_RAW("FECBSR", 0x05, 1, 0),
	REG_RAW("FMSSR", 0x07, 1, 0),
	REG_RAW("SMECCS", 0x20, 1, 0),
	REG_RAW("SMIHAS", 0x22, 1, 0),
	REG_RAW("FLHCTRL1R", 0x31, 1, 0),
	REG_RAW("FLHCTRL2R", 0x32, 1, 0),
	REG_RAW("DCACHE_CTRL", 0x33, 1, 0),
	REG_RAW("UCCTRLR", 0x34, 1, 0),
	REG_RAW("HCTRL2R", 0x36, 1, 0),
	REG_FIELD(struct smfi_it8xxx2_regs, SMFI_ECINDAR0, 0),
	REG_FIELD(struct smfi_it8xxx2_regs, SMFI_ECINDAR1, 0),
	REG_FIELD(struct smfi_it8xxx2_regs, SMFI_ECINDAR2, 0),
	REG_FIELD(struct smfi_it8xxx2_regs, SMFI_ECINDAR3, 0),
	REG_FIELD(struct smfi_it8xxx2_regs, SMFI_ECINDDR, REG_FLAG_SIDE_EFFECT),
	REG_FIELD(struct smfi_it8xxx2_regs, SMFI_SCAR0L, 0),
	REG_FIELD(struct smfi_it8xxx2_regs, SMFI_SCAR0M, 0),
	REG_FIELD(struct smfi_it8xxx2_regs, SMFI_SCAR0H, 0),
	REG_RAW_ARR("SCAR_1_4", 0x43, 1, 12, 1, 0),
	REG_FIELD(struct smfi_it8xxx2_regs, SMFI_HRAMWC, 0),
	REG_FIELD(struct smfi_it8xxx2_regs, SMFI_HRAMW0BA, 0),
	REG_FIELD(struct smfi_it8xxx2_regs, SMFI_HRAMW1BA, 0),
	REG_FIELD(struct smfi_it8xxx2_regs, SMFI_HRAMW0AAS, 0),
	REG_FIELD(struct smfi_it8xxx2_regs, SMFI_HRAMW1AAS, 0),
	REG_FIELD(struct smfi_it8xxx2_regs, SMFI_FLHCTRL6R, 0),
};
static const struct reg_block_type it8xxx2_smfi_type =
	REG_BLOCK_TYPE("SMFI", it8xxx2_smfi_fields);

/* 7. PWM & TACH (0x1800) */
static const struct reg_field_desc it8xxx2_pwm_fields[] = {
	REG_FIELD(struct pwm_it8xxx2_regs, C0CPRS, 0),
	REG_FIELD(struct pwm_it8xxx2_regs, CTR, 0),
	REG_RAW_ARR("DCR", 0x02, 1, 8, 1, 0),
	REG_RAW("PWMPOL", 0x0A, 1, 0),
	REG_FIELD(struct pwm_it8xxx2_regs, PCFSR, 0),
	REG_RAW("PCSSGL", 0x0C, 1, 0),
	REG_RAW("PCSSGH", 0x0D, 1, 0),
	REG_RAW("CR256PCSSG", 0x0E, 1, 0),
	REG_RAW("PCSGR", 0x0F, 1, 0),
	REG_FIELD(struct pwm_it8xxx2_regs, CTR1M, 0),
	REG_RAW("F1TLRR", 0x1E, 1, REG_FLAG_VOLATILE),
	REG_RAW("F1TMRR", 0x1F, 1, REG_FLAG_VOLATILE),
	REG_RAW("F2TLRR", 0x20, 1, REG_FLAG_VOLATILE),
	REG_RAW("F2TMRR", 0x21, 1, REG_FLAG_VOLATILE),
	REG_FIELD(struct pwm_it8xxx2_regs, ZTIER, 0),
	REG_FIELD(struct pwm_it8xxx2_regs, C4CPRS, 0),
	REG_FIELD(struct pwm_it8xxx2_regs, C4MCPRS, 0),
	REG_FIELD(struct pwm_it8xxx2_regs, C6CPRS, 0),
	REG_FIELD(struct pwm_it8xxx2_regs, C6MCPRS, 0),
	REG_FIELD(struct pwm_it8xxx2_regs, C7CPRS, 0),
	REG_FIELD(struct pwm_it8xxx2_regs, C7MCPRS, 0),
	REG_FIELD(struct pwm_it8xxx2_regs, CTR1, 0),
	REG_FIELD(struct pwm_it8xxx2_regs, CTR2, 0),
	REG_FIELD(struct pwm_it8xxx2_regs, CTR3, 0),
	REG_RAW("TSWCTLR", 0x48, 1, 0),
	REG_FIELD(struct pwm_it8xxx2_regs, PWMODENR, 0),
};
static const struct reg_block_type it8xxx2_pwm_type =
	REG_BLOCK_TYPE("PWM", it8xxx2_pwm_fields);

/* 8. ADC (0x1900) */
static const struct reg_field_desc it8xxx2_adc_fields[] = {
	REG_FIELD(struct adc_it8xxx2_regs, ADCSTS, 0),
	REG_FIELD(struct adc_it8xxx2_regs, ADCCFG, 0),
	REG_FIELD(struct adc_it8xxx2_regs, ADCCTL, 0),
	REG_FIELD(struct adc_it8xxx2_regs, ADCGCR, 0),
	REG_FIELD(struct adc_it8xxx2_regs, VCH0CTL, 0),
	REG_FIELD(struct adc_it8xxx2_regs, KDCTL, 0),
	REG_RAW_ARR("VCH1_3", 0x06, 1, 9, 1, REG_FLAG_VOLATILE),
	REG_FIELD(struct adc_it8xxx2_regs, VCH0DATL, REG_FLAG_VOLATILE),
	REG_FIELD(struct adc_it8xxx2_regs, VCH0DATM, REG_FLAG_VOLATILE),
	REG_RAW_ARR("VCH4_7", 0x38, 1, 12, 1, REG_FLAG_VOLATILE),
	REG_FIELD(struct adc_it8xxx2_regs, ADCDVSTS, 0),
	REG_FIELD(struct adc_it8xxx2_regs, ADCIVMFSCS1, 0),
	REG_FIELD(struct adc_it8xxx2_regs, ADCIVMFSCS2, 0),
	REG_FIELD(struct adc_it8xxx2_regs, ADCIVMFSCS3, 0),
	REG_FIELD(struct adc_it8xxx2_regs, ADCDVSTS2, 0),
	REG_FIELD(struct adc_it8xxx2_regs, ADCCTL1, 0),
};
static const struct reg_block_type it8xxx2_adc_type =
	REG_BLOCK_TYPE("ADC", it8xxx2_adc_fields);

/* 9. KSCAN (Keyboard Scan, 0x1D00) */
static const struct reg_field_desc it8xxx2_kbs_fields[] = {
	REG_FIELD(struct kscan_it8xxx2_regs, KBS_KSOL, 0),
	REG_FIELD(struct kscan_it8xxx2_regs, KBS_KSOH1, 0),
	REG_FIELD(struct kscan_it8xxx2_regs, KBS_KSOCTRL, 0),
	REG_FIELD(struct kscan_it8xxx2_regs, KBS_KSOH2, 0),
	REG_FIELD(struct kscan_it8xxx2_regs, KBS_KSI, REG_FLAG_VOLATILE),
	REG_FIELD(struct kscan_it8xxx2_regs, KBS_KSICTRL, 0),
	REG_FIELD(struct kscan_it8xxx2_regs, KBS_KSIGCTRL, 0),
	REG_FIELD(struct kscan_it8xxx2_regs, KBS_KSIGOEN, 0),
	REG_FIELD(struct kscan_it8xxx2_regs, KBS_KSIGDAT, 0),
	REG_FIELD(struct kscan_it8xxx2_regs, KBS_KSIGDMRR, 0),
	REG_FIELD(struct kscan_it8xxx2_regs, KBS_KSOHGCTRL, 0),
	REG_FIELD(struct kscan_it8xxx2_regs, KBS_KSOHGOEN, 0),
	REG_FIELD(struct kscan_it8xxx2_regs, KBS_KSOHGDMRR, 0),
	REG_FIELD(struct kscan_it8xxx2_regs, KBS_KSOLGCTRL, 0),
	REG_FIELD(struct kscan_it8xxx2_regs, KBS_KSOLGOEN, 0),
};
static const struct reg_block_type it8xxx2_kbs_type =
	REG_BLOCK_TYPE("KBS", it8xxx2_kbs_fields);

/* 10. WDT & External Timers (0x1F00) */
static const struct reg_field_desc it8xxx2_wdt_timer_fields[] = {
	REG_FIELD(struct wdt_it8xxx2_regs, ETWCFG, 0),
	REG_FIELD(struct wdt_it8xxx2_regs, ET1PSR, 0),
	REG_FIELD(struct wdt_it8xxx2_regs, ET1CNTLHR, 0),
	REG_FIELD(struct wdt_it8xxx2_regs, ET1CNTLLR, 0),
	REG_FIELD(struct wdt_it8xxx2_regs, ETWCTRL, 0),
	REG_FIELD(struct wdt_it8xxx2_regs, EWDCNTLR, 0),
	REG_FIELD(struct wdt_it8xxx2_regs, EWDKEYR, REG_FLAG_WRITE_ONLY),
	REG_FIELD(struct wdt_it8xxx2_regs, EWDCNTHR, 0),
	REG_FIELD(struct wdt_it8xxx2_regs, ET2PSR, 0),
	REG_FIELD(struct wdt_it8xxx2_regs, ET2CNTLHR, 0),
	REG_FIELD(struct wdt_it8xxx2_regs, ET2CNTLLR, 0),
	REG_FIELD(struct wdt_it8xxx2_regs, ET2CNTLH2R, 0),
	/* External timers 3..8 (8 bytes each starting at 0x10) */
	REG_RAW_ARR("EXT_TMR_CTRL", 0x10, 1, 6, 8, 0),
	REG_RAW_ARR("EXT_TMR_PSR", 0x11, 1, 6, 8, 0),
	REG_RAW_ARR("EXT_TMR_CNT", 0x14, 4, 6, 8, 0),
	REG_RAW_ARR("EXT_TMR_OBS", 0x48, 4, 6, 4, REG_FLAG_VOLATILE),
};
static const struct reg_block_type it8xxx2_wdt_timer_type =
	REG_BLOCK_TYPE("WDT_TMR", it8xxx2_wdt_timer_fields);

/* 11. SMB / I2C (0x1C00) */
static const struct reg_field_desc it8xxx2_smb_fields[] = {
	REG_RAW_ARR("SMB_CFG", 0x00, 1, 18, 1, 0),
	REG_RAW_ARR("CH_A", 0x40, 1, 18, 1, 0),
	REG_RAW_ARR("CH_B", 0x80, 1, 18, 1, 0),
	REG_RAW_ARR("CH_C", 0xC0, 1, 18, 1, 0),
};
static const struct reg_block_type it8xxx2_smb_type =
	REG_BLOCK_TYPE("SMB", it8xxx2_smb_fields);

/* 12. Enhanced I2C (I2C D/E/F, 0x3500 / 0x3580 / 0x3680) */
static const struct reg_field_desc it8xxx2_i2c_enh_fields[] = {
	REG_RAW("DRR", 0x00, 1, REG_FLAG_SIDE_EFFECT),
	REG_RAW("PSR", 0x01, 1, 0),
	REG_RAW("HSPR", 0x02, 1, 0),
	REG_RAW("STR", 0x03, 1, REG_FLAG_VOLATILE),
	REG_RAW("DHTR", 0x04, 1, 0),
	REG_RAW("TOR", 0x05, 1, 0),
	REG_RAW("IDR", 0x06, 1, 0),
	REG_RAW("TOS", 0x07, 1, REG_FLAG_VOLATILE),
	REG_RAW("DTR", 0x08, 1, REG_FLAG_SIDE_EFFECT),
	REG_RAW("CTR", 0x09, 1, 0),
	REG_RAW("CTR1", 0x0A, 1, 0),
	REG_RAW("BYTE_CNT_H", 0x0B, 1, 0),
	REG_RAW("BYTE_CNT_L", 0x0C, 1, 0),
	REG_RAW("IRQ_ST", 0x0D, 1, REG_FLAG_VOLATILE),
	REG_RAW("CTR2", 0x20, 1, 0),
};
static const struct reg_block_type it8xxx2_i2c_enh_type =
	REG_BLOCK_TYPE("I2C_ENH", it8xxx2_i2c_enh_fields);

/* 13. KBC (0x1300) */
static const struct reg_field_desc it8xxx2_kbc_fields[] = {
	REG_RAW("KBHICR", 0x00, 1, 0),
	REG_RAW("KBIRQR", 0x02, 1, 0),
	REG_RAW("KBHISR", 0x04, 1, 0),
	REG_RAW("KBHIKDOR", 0x06, 1, 0),
	REG_RAW("KBHIMDOR", 0x08, 1, 0),
	REG_RAW("KBHIDIR", 0x0A, 1, REG_FLAG_SIDE_EFFECT),
};
static const struct reg_block_type it8xxx2_kbc_type =
	REG_BLOCK_TYPE("KBC", it8xxx2_kbc_fields);

/* 14. PMC (0x1500) */
static const struct reg_field_desc it8xxx2_pmc_fields[] = {
	REG_RAW("PM1STS", 0x00, 1, 0),
	REG_RAW("PM1DO", 0x01, 1, 0),
	REG_RAW("PM1DI", 0x04, 1, REG_FLAG_SIDE_EFFECT),
	REG_RAW("PM1CTL", 0x06, 1, 0),
	REG_RAW("PM1IC", 0x07, 1, 0),
	REG_RAW("PM1IE", 0x08, 1, 0),
	REG_RAW("PM2STS", 0x10, 1, 0),
	REG_RAW("PM2DO", 0x11, 1, 0),
	REG_RAW("PM2DI", 0x14, 1, REG_FLAG_SIDE_EFFECT),
	REG_RAW("PM2CTL", 0x16, 1, 0),
	REG_RAW("PM2IC", 0x17, 1, 0),
	REG_RAW("PM2IE", 0x18, 1, 0),
	REG_RAW("PM3STS", 0x20, 1, 0),
	REG_RAW("PM3DO", 0x21, 1, 0),
	REG_RAW("PM3DI", 0x22, 1, REG_FLAG_SIDE_EFFECT),
	REG_RAW("PM3CTL", 0x23, 1, 0),
	REG_RAW("PM4STS", 0x30, 1, 0),
	REG_RAW("PM4DO", 0x31, 1, 0),
	REG_RAW("PM4DI", 0x32, 1, REG_FLAG_SIDE_EFFECT),
	REG_RAW("PM4CTL", 0x33, 1, 0),
	REG_RAW("PM5STS", 0x40, 1, 0),
	REG_RAW("PM5DO", 0x41, 1, 0),
	REG_RAW("PM5DI", 0x42, 1, REG_FLAG_SIDE_EFFECT),
	REG_RAW("PM5CTL", 0x43, 1, 0),
	REG_RAW("MBXCTRL", 0x50, 1, 0),
};
static const struct reg_block_type it8xxx2_pmc_type =
	REG_BLOCK_TYPE("PMC", it8xxx2_pmc_fields);

/* 15. EC2I (0x1200) */
static const struct reg_field_desc it8xxx2_ec2i_fields[] = {
	REG_RAW("IHIOA", 0x00, 1, 0),
	REG_RAW("IHD", 0x01, 1, REG_FLAG_SIDE_EFFECT),
	REG_RAW("LSIOHA", 0x02, 1, 0),
	REG_RAW("SIOLV", 0x03, 1, 0),
	REG_RAW("IBMAE", 0x04, 1, 0),
	REG_RAW("IBCTL", 0x05, 1, 0),
};
static const struct reg_block_type it8xxx2_ec2i_type =
	REG_BLOCK_TYPE("EC2I", it8xxx2_ec2i_fields);

/* 16. eSPI Slave & Virtual Wire (0x3100 & 0x3200) */
static const struct reg_field_desc it8xxx2_espi_fields[] = {
	REG_RAW("GCACAP0", 0x04, 1, 0),
	REG_RAW("GCACAP1", 0x05, 1, 0),
	REG_RAW("GCACFG0", 0x08, 1, 0),
	REG_RAW("GCACFG1", 0x09, 1, 0),
	REG_RAW("GCACFG2", 0x0A, 1, 0),
	REG_RAW("GCACFG3", 0x0B, 1, 0),
	REG_RAW_ARR("ESPI_CTRL", 0x90, 1, 16, 1, 0),
	REG_RAW_ARR("ESPI_UPCFG", 0xA0, 1, 16, 1, 0),
};
static const struct reg_block_type it8xxx2_espi_type =
	REG_BLOCK_TYPE("ESPI", it8xxx2_espi_fields);

static const struct reg_field_desc it8xxx2_espi_vw_fields[] = {
	REG_RAW_ARR("VW_INDEX", 0x00, 1, 64, 1, 0),
	REG_RAW_ARR("VW_CTRL", 0x90, 1, 12, 1, 0),
};
static const struct reg_block_type it8xxx2_espi_vw_type =
	REG_BLOCK_TYPE("ESPI_VW", it8xxx2_espi_vw_fields);

/* 17. UART (0x2700 / 0x2800) */
static const struct reg_field_desc it8xxx2_uart_fields[] = {
	REG_RAW("RBR_THR_DLL", 0x00, 1, REG_FLAG_SIDE_EFFECT),
	REG_RAW("IER_DLM", 0x01, 1, 0),
	REG_RAW("IIR_FCR", 0x02, 1, REG_FLAG_SIDE_EFFECT),
	REG_RAW("LCR", 0x03, 1, 0),
	REG_RAW("MCR", 0x04, 1, 0),
	REG_RAW("LSR", 0x05, 1, REG_FLAG_VOLATILE),
	REG_RAW("MSR", 0x06, 1, REG_FLAG_VOLATILE),
	REG_RAW("SCR", 0x07, 1, 0),
	REG_RAW("ECSPMR", 0x08, 1, 0),
};
static const struct reg_block_type it8xxx2_uart_type =
	REG_BLOCK_TYPE("UART", it8xxx2_uart_fields);

/* Instantiated modules on ITE IT8xxx2 */
static const struct reg_module_inst it8xxx2_modules[] = {
	{ "ECPM", IT8XXX2_ADDR(0x1E00), &it8xxx2_ecpm_type, REG_CLK_NONE },
	{ "GCTRL", IT8XXX2_ADDR(0x2000), &it8xxx2_gctrl_type, REG_CLK_NONE },
#if defined(CONFIG_SOC_IT8XXX2_REG_SET_V1)
	{ "INTC", IT8XXX2_ADDR(0x3F00), &it8xxx2_intc_type, REG_CLK_NONE },
#else
	{ "INTC", IT8XXX2_ADDR(0x1100), &it8xxx2_intc_type, REG_CLK_NONE },
#endif
	{ "GPIO", IT8XXX2_ADDR(0x1600), &it8xxx2_gpio_type, REG_CLK_NONE },
	{ "WUC", IT8XXX2_ADDR(0x1B00), &it8xxx2_wuc_type, REG_CLK_NONE },
	{ "SMFI", IT8XXX2_ADDR(0x1000), &it8xxx2_smfi_type, REG_CLK_NONE },
	{ "PWM", IT8XXX2_ADDR(0x1800), &it8xxx2_pwm_type, REG_CLK_NONE },
	{ "ADC", IT8XXX2_ADDR(0x1900), &it8xxx2_adc_type, REG_CLK_NONE },
	{ "KBS", IT8XXX2_ADDR(0x1D00), &it8xxx2_kbs_type, REG_CLK_NONE },
	{ "WDT_TMR", IT8XXX2_ADDR(0x1F00), &it8xxx2_wdt_timer_type,
	  REG_CLK_NONE },
	{ "SMB", IT8XXX2_ADDR(0x1C00), &it8xxx2_smb_type, REG_CLK_NONE },
	{ "I2C_E", IT8XXX2_ADDR(0x3500), &it8xxx2_i2c_enh_type, REG_CLK_NONE },
	{ "I2C_F", IT8XXX2_ADDR(0x3580), &it8xxx2_i2c_enh_type, REG_CLK_NONE },
	{ "KBC", IT8XXX2_ADDR(0x1300), &it8xxx2_kbc_type, REG_CLK_NONE },
	{ "PMC", IT8XXX2_ADDR(0x1500), &it8xxx2_pmc_type, REG_CLK_NONE },
	{ "EC2I", IT8XXX2_ADDR(0x1200), &it8xxx2_ec2i_type, REG_CLK_NONE },
	{ "ESPI", IT8XXX2_ADDR(0x3100), &it8xxx2_espi_type, REG_CLK_NONE },
	{ "ESPI_VW", IT8XXX2_ADDR(0x3200), &it8xxx2_espi_vw_type,
	  REG_CLK_NONE },
	{ "UART1", IT8XXX2_ADDR(0x2700), &it8xxx2_uart_type, REG_CLK_NONE },
	{ "UART2", IT8XXX2_ADDR(0x2800), &it8xxx2_uart_type, REG_CLK_NONE },
};

const struct reg_module_inst *reg_audit_chip_get_modules(size_t *num_modules)
{
	*num_modules = ARRAY_SIZE(it8xxx2_modules);
	return it8xxx2_modules;
}

int reg_audit_chip_module_enter(const struct reg_module_inst *mod,
				int *was_clocked, uint32_t *saved_state)
{
	ARG_UNUSED(mod);
	*was_clocked = 1;
	*saved_state = 0;
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
	unsigned long mstatus = csr_read(mstatus);
	unsigned long mie = csr_read(mie);
	unsigned long mip = csr_read(mip);
	unsigned long mtvec = csr_read(mtvec);

	printk("CORE|%s|mstatus=0x%08lx|mie=0x%08lx|mip=0x%08lx|mtvec=0x%08lx\n",
	       stage_tag, mstatus, mie, mip, mtvec);
}
