/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "cros/reg_audit.h"

#include <zephyr/devicetree.h>
#include <zephyr/dt-bindings/clock/npcx_clock.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#include <cmsis_core.h>
#include <soc.h>

/* Encode NPCX_PWDWN_CTL(ctl_idx, bit) into a 16-bit clk_id */
#define NPCX_CLK_ID(_ctl_idx, _bit) \
	(((uint16_t)(_ctl_idx) << 8) | ((_bit) & 0x07))
#define NPCX_CLK_CTL_IDX(_id) (((_id) >> 8) & 0xFF)
#define NPCX_CLK_BIT(_id) ((_id) & 0x07)

/* 1. PMC (Power Management Controller) */
static const struct reg_field_desc npcx_pmc_fields[] = {
	REG_FIELD(struct pmc_reg, PMCSR, 0),
	REG_FIELD(struct pmc_reg, ENSLP_CTL, 0),
	REG_FIELD(struct pmc_reg, DISIDL_CTL, 0),
	REG_FIELD(struct pmc_reg, DISIDL_CTL1, 0),
	REG_FIELD_ARR(struct pmc_reg, PWDWN_CTL1, 0),
	REG_FIELD_ARR(struct pmc_reg, RAM_PD, 0),
	REG_FIELD_ARR(struct pmc_reg, PWDWN_CTL7, 0),
};
static const struct reg_block_type npcx_pmc_type =
	REG_BLOCK_TYPE("PMC", npcx_pmc_fields);

/* 2. CDCG (Core Domain Clock Generator) */
static const struct reg_field_desc npcx_cdcg_fields[] = {
	REG_FIELD(struct cdcg_reg, HFCGCTRL, 0),
	REG_FIELD(struct cdcg_reg, HFCGML, 0),
	REG_FIELD(struct cdcg_reg, HFCGMH, 0),
	REG_FIELD(struct cdcg_reg, HFCGN, 0),
	REG_FIELD(struct cdcg_reg, HFCGP, 0),
	REG_FIELD(struct cdcg_reg, HFCBCD, 0),
	REG_FIELD(struct cdcg_reg, HFCBCD1, 0),
	REG_FIELD(struct cdcg_reg, HFCBCD2, 0),
	REG_FIELD(struct cdcg_reg, HFCBCD3, 0),
	REG_FIELD(struct cdcg_reg, LFCGCTL, 0),
	REG_FIELD(struct cdcg_reg, HFRDI, 0),
	REG_FIELD(struct cdcg_reg, HFRDF, 0),
	REG_FIELD(struct cdcg_reg, FRCDIV, 0),
	REG_FIELD(struct cdcg_reg, DIVCOR1, 0),
	REG_FIELD(struct cdcg_reg, DIVCOR2, 0),
	REG_FIELD(struct cdcg_reg, LFCGCTL2, 0),
};
static const struct reg_block_type npcx_cdcg_type =
	REG_BLOCK_TYPE("CDCG", npcx_cdcg_fields);

/* 3. SCFG (System Configuration + DEVALT + LV_GPIO_CTL + DEVALT_LK) */
static const struct reg_field_desc npcx_scfg_fields[] = {
	REG_FIELD(struct scfg_reg, DEVCNT, 0),
	REG_FIELD(struct scfg_reg, STRPST, 0),
	REG_FIELD(struct scfg_reg, RSTCTL, 0),
	REG_FIELD(struct scfg_reg, DEV_CTL3, 0),
	REG_FIELD(struct scfg_reg, DEV_CTL4, 0),
	REG_FIELD_ARR(struct scfg_reg, DEVALT0, 0),
	REG_FIELD_ARR(struct scfg_reg, LV_GPIO_CTL5, 0),
	REG_FIELD(struct scfg_reg, PUPD_EN0, 0),
	REG_FIELD(struct scfg_reg, PUPD_EN1, 0),
	REG_FIELD_ARR(struct scfg_reg, LV_GPIO_CTL0, 0),
	REG_RAW_ARR("DEVALT_B", 0x0B0, 1, 5, 1, 0),
	REG_RAW_ARR("LV_GPIO_CTL_EXT", 0x115, 1, 12, 1, 0),
	REG_RAW_ARR("DEVALT_LK", 0x210, 1, 19, 1, 0),
};
static const struct reg_block_type npcx_scfg_type =
	REG_BLOCK_TYPE("SCFG", npcx_scfg_fields);

/* 4. GLUE (System Glue) */
static const struct reg_field_desc npcx_glue_fields[] = {
	REG_FIELD(struct glue_reg, SMB_SBD, 0),
	REG_FIELD(struct glue_reg, SMB_EEN, 0),
	REG_FIELD(struct glue_reg, SDPD0, 0),
	REG_FIELD(struct glue_reg, SDPD1, 0),
	REG_FIELD(struct glue_reg, SDP_CTS, 0),
	REG_FIELD(struct glue_reg, SMB_SEL, 0),
	REG_FIELD(struct glue_reg, PSL_CTS, 0),
};
static const struct reg_block_type npcx_glue_type =
	REG_BLOCK_TYPE("GLUE", npcx_glue_fields);

/* 5. BBRAM (0x400af100 BKUP_STS) */
static const struct reg_field_desc npcx_bbram_fields[] = {
	REG_RAW("BKUP_STS", 0x100, 1, 0),
};
static const struct reg_block_type npcx_bbram_type =
	REG_BLOCK_TYPE("BBRAM", npcx_bbram_fields);

/* 6. MSWC (Mobile System Wake-Up Control) */
static const struct reg_field_desc npcx_mswc_fields[] = {
	REG_FIELD(struct mswc_reg, MSWCTL1, 0),
	REG_FIELD(struct mswc_reg, MSWCTL2, 0),
	REG_FIELD(struct mswc_reg, HCBAL, 0),
	REG_FIELD(struct mswc_reg, HCBAH, 0),
	REG_FIELD(struct mswc_reg, MSIEN2, 0),
	REG_FIELD(struct mswc_reg, MSHES0, 0),
	REG_FIELD(struct mswc_reg, MSHEIE0, 0),
	REG_FIELD(struct mswc_reg, HOST_CTL, 0),
	REG_FIELD(struct mswc_reg, SMIP_LEN, 0),
	REG_FIELD(struct mswc_reg, SCIP_LEN, 0),
	REG_FIELD(struct mswc_reg, SRID_CR, 0),
	REG_FIELD(struct mswc_reg, SID_CR, 0),
	REG_FIELD(struct mswc_reg, DEVICE_ID_CR, 0),
	REG_FIELD(struct mswc_reg, CHPREV_CR, 0),
	REG_FIELD(struct mswc_reg, VW_SLPST1, 0),
};
static const struct reg_block_type npcx_mswc_type =
	REG_BLOCK_TYPE("MSWC", npcx_mswc_fields);

/* 7. LCT (Long Countdown Timer) */
static const struct reg_field_desc npcx_lct_fields[] = {
	REG_FIELD(struct lct_reg, LCTCONT, 0),
	REG_FIELD(struct lct_reg, LCTSTAT, 0),
	REG_FIELD(struct lct_reg, LCTSECOND, REG_FLAG_VOLATILE),
	REG_FIELD(struct lct_reg, LCTMINUTE, REG_FLAG_VOLATILE),
	REG_FIELD(struct lct_reg, LCTHOUR, REG_FLAG_VOLATILE),
	REG_FIELD(struct lct_reg, LCTDAY, REG_FLAG_VOLATILE),
	REG_FIELD(struct lct_reg, LCTWEEK, REG_FLAG_VOLATILE),
};
static const struct reg_block_type npcx_lct_type =
	REG_BLOCK_TYPE("LCT", npcx_lct_fields);

/* 8. FIU (Flash Interface Unit) */
static const struct reg_field_desc npcx_fiu_fields[] = {
	REG_FIELD(struct fiu_reg, BURST_CFG, 0),
	REG_FIELD(struct fiu_reg, RESP_CFG, 0),
	REG_FIELD(struct fiu_reg, SPI_FL_CFG, 0),
	REG_FIELD(struct fiu_reg, UMA_CODE, 0),
	REG_FIELD(struct fiu_reg, UMA_AB0, 0),
	REG_FIELD(struct fiu_reg, UMA_AB1, 0),
	REG_FIELD(struct fiu_reg, UMA_AB2, 0),
	REG_FIELD(struct fiu_reg, UMA_DB0, 0),
	REG_FIELD(struct fiu_reg, UMA_DB1, 0),
	REG_FIELD(struct fiu_reg, UMA_DB2, 0),
	REG_FIELD(struct fiu_reg, UMA_DB3, 0),
	REG_FIELD(struct fiu_reg, UMA_CTS, 0),
	REG_FIELD(struct fiu_reg, UMA_ECTS, 0),
	REG_FIELD(struct fiu_reg, UMA_DB0_3, 0),
	REG_FIELD(struct fiu_reg, CRCCON, 0),
	REG_FIELD(struct fiu_reg, CRCENT, 0),
	REG_FIELD(struct fiu_reg, CRCRSLT, 0),
	REG_FIELD(struct fiu_reg, FIU_RD_CMD, 0),
	REG_FIELD(struct fiu_reg, FIU_DMM_CYC, 0),
	REG_FIELD(struct fiu_reg, FIU_EXT_CFG, 0),
};
static const struct reg_block_type npcx_fiu_type =
	REG_BLOCK_TYPE("FIU", npcx_fiu_fields);

/* 9. SHM (Shared Memory) */
static const struct reg_field_desc npcx_shm_fields[] = {
	REG_FIELD(struct shm_reg, SMC_STS, 0),
	REG_FIELD(struct shm_reg, SMC_CTL, 0),
	REG_FIELD(struct shm_reg, SHM_CTL, 0),
	REG_FIELD(struct shm_reg, IMA_WIN_SIZE, 0),
	REG_FIELD(struct shm_reg, WIN_SIZE, 0),
	REG_FIELD(struct shm_reg, SHAW1_SEM, 0),
	REG_FIELD(struct shm_reg, SHAW2_SEM, 0),
	REG_FIELD(struct shm_reg, IMA_SEM, 0),
	REG_FIELD(struct shm_reg, SHCFG, 0),
	REG_FIELD(struct shm_reg, WIN1_WR_PROT, 0),
	REG_FIELD(struct shm_reg, WIN1_RD_PROT, 0),
	REG_FIELD(struct shm_reg, WIN2_WR_PROT, 0),
	REG_FIELD(struct shm_reg, WIN2_RD_PROT, 0),
	REG_FIELD(struct shm_reg, IMA_WR_PROT, 0),
	REG_FIELD(struct shm_reg, IMA_RD_PROT, 0),
	REG_FIELD(struct shm_reg, WIN_BASE1, 0),
	REG_FIELD(struct shm_reg, WIN_BASE2, 0),
	REG_FIELD(struct shm_reg, IMA_BASE, 0),
	REG_FIELD(struct shm_reg, RST_CFG, 0),
	REG_FIELD(struct shm_reg, DP80BUF, REG_FLAG_SIDE_EFFECT),
	REG_FIELD(struct shm_reg, DP80STS, 0),
	REG_FIELD(struct shm_reg, DP80CTL, 0),
	REG_FIELD(struct shm_reg, HOFS_STS, 0),
	REG_FIELD(struct shm_reg, HOFS_CTL, 0),
	REG_FIELD(struct shm_reg, COFS2, 0),
	REG_FIELD(struct shm_reg, COFS1, 0),
};
static const struct reg_block_type npcx_shm_type =
	REG_BLOCK_TYPE("SHM", npcx_shm_fields);

/* 10. KBC (Keyboard & Mouse Controller) */
static const struct reg_field_desc npcx_kbc_fields[] = {
	REG_FIELD(struct kbc_reg, HICTRL, 0),
	REG_FIELD(struct kbc_reg, HIIRQC, 0),
	REG_FIELD(struct kbc_reg, HIKMST, 0),
	REG_FIELD(struct kbc_reg, HIKDO, 0),
	REG_FIELD(struct kbc_reg, HIMDO, 0),
	REG_FIELD(struct kbc_reg, HIKMDI, REG_FLAG_SIDE_EFFECT),
	REG_FIELD(struct kbc_reg, SHIKMDI, 0),
};
static const struct reg_block_type npcx_kbc_type =
	REG_BLOCK_TYPE("KBC", npcx_kbc_fields);

/* 11. PMCH (Power Management Channel) */
static const struct reg_field_desc npcx_pmch_fields[] = {
	REG_FIELD(struct pmch_reg, HIPMST, 0),
	REG_FIELD(struct pmch_reg, HIPMDO, 0),
	REG_FIELD(struct pmch_reg, HIPMDI, REG_FLAG_SIDE_EFFECT),
	REG_FIELD(struct pmch_reg, SHIPMDI, 0),
	REG_FIELD(struct pmch_reg, HIPMDOC, 0),
	REG_FIELD(struct pmch_reg, HIPMDOM, 0),
	REG_FIELD(struct pmch_reg, HIPMDIC, REG_FLAG_SIDE_EFFECT),
	REG_FIELD(struct pmch_reg, HIPMCTL, 0),
	REG_FIELD(struct pmch_reg, HIPMCTL2, 0),
	REG_FIELD(struct pmch_reg, HIPMIC, 0),
	REG_FIELD(struct pmch_reg, HIPMIE, 0),
};
static const struct reg_block_type npcx_pmch_type =
	REG_BLOCK_TYPE("PMCH", npcx_pmch_fields);

/* 12. C2H (Core Access to Host) */
static const struct reg_field_desc npcx_c2h_fields[] = {
	REG_FIELD(struct c2h_reg, IHIOA, 0),
	REG_FIELD(struct c2h_reg, IHD, REG_FLAG_SIDE_EFFECT),
	REG_FIELD(struct c2h_reg, LKSIOHA, 0),
	REG_FIELD(struct c2h_reg, SIOLV, 0),
	REG_FIELD(struct c2h_reg, CRSMAE, 0),
	REG_FIELD(struct c2h_reg, SIBCTRL, 0),
};
static const struct reg_block_type npcx_c2h_type =
	REG_BLOCK_TYPE("C2H", npcx_c2h_fields);

/* 13. SMB (SMBus / I2C Controller) */
static const struct reg_field_desc npcx_smb_fields[] = {
	REG_FIELD(struct smb_reg, SMBSDA, REG_FLAG_SIDE_EFFECT),
	REG_FIELD(struct smb_reg, SMBST, 0),
	REG_FIELD(struct smb_reg, SMBCST, 0),
	REG_FIELD(struct smb_reg, SMBCTL1, 0),
	REG_FIELD(struct smb_reg, SMBADDR1, 0),
	REG_FIELD(struct smb_reg, SMBCTL2, 0),
	REG_FIELD(struct smb_reg, SMBADDR2, 0),
	REG_FIELD(struct smb_reg, SMBCTL3, 0),
	REG_FIELD(struct smb_reg, SMBT_OUT, 0),
	REG_FIELD(struct smb_reg, SMBADDR3, 0),
	REG_FIELD(struct smb_reg, SMBADDR7, 0),
	REG_FIELD(struct smb_reg, SMBADDR4, 0),
	REG_FIELD(struct smb_reg, SMBADDR8, 0),
	REG_FIELD(struct smb_reg, SMBADDR5, 0),
	REG_FIELD(struct smb_reg, SMBADDR6, 0),
	REG_FIELD(struct smb_reg, SMBCST2, 0),
	REG_FIELD(struct smb_reg, SMBCST3, 0),
	REG_FIELD(struct smb_reg, SMBCTL4, 0),
	REG_FIELD(struct smb_reg, SMBSCLLT, 0),
	REG_FIELD(struct smb_reg, SMBFIF_CTL, 0),
	REG_FIELD(struct smb_reg, SMBSCLHT, 0),
};
static const struct reg_block_type npcx_smb_type =
	REG_BLOCK_TYPE("SMB", npcx_smb_fields);

/* 14. ITIM32 / ITIM64 (Internal Timers) */
static const struct reg_field_desc npcx_itim32_fields[] = {
	REG_FIELD(struct itim32_reg, ITPRE32, 0),
	REG_FIELD(struct itim32_reg, ITCTS32, 0),
	REG_FIELD(struct itim32_reg, ITCNT32, REG_FLAG_VOLATILE),
};
static const struct reg_block_type npcx_itim32_type =
	REG_BLOCK_TYPE("ITIM32", npcx_itim32_fields);

static const struct reg_field_desc npcx_itim64_fields[] = {
	REG_FIELD(struct itim64_reg, ITPRE64, 0),
	REG_FIELD(struct itim64_reg, ITCTS64, 0),
	REG_FIELD(struct itim64_reg, ITCNT64L, REG_FLAG_VOLATILE),
	REG_FIELD(struct itim64_reg, ITCNT64H, REG_FLAG_VOLATILE),
};
static const struct reg_block_type npcx_itim64_type =
	REG_BLOCK_TYPE("ITIM64", npcx_itim64_fields);

/* 15. TACH (Tachometer) */
static const struct reg_field_desc npcx_tach_fields[] = {
	REG_FIELD(struct tach_reg, TCNT1, REG_FLAG_VOLATILE),
	REG_FIELD(struct tach_reg, TCRA, 0),
	REG_FIELD(struct tach_reg, TCRB, 0),
	REG_FIELD(struct tach_reg, TCNT2, REG_FLAG_VOLATILE),
	REG_FIELD(struct tach_reg, TPRSC, 0),
	REG_FIELD(struct tach_reg, TCKC, 0),
	REG_FIELD(struct tach_reg, TMCTRL, 0),
	REG_FIELD(struct tach_reg, TECTRL, 0),
	REG_FIELD(struct tach_reg, TECLR, REG_FLAG_WRITE_ONLY),
	REG_FIELD(struct tach_reg, TIEN, 0),
	REG_FIELD(struct tach_reg, TCPA, 0),
	REG_FIELD(struct tach_reg, TCPB, 0),
	REG_FIELD(struct tach_reg, TCPCFG, 0),
	REG_FIELD(struct tach_reg, TWUEN, 0),
	REG_FIELD(struct tach_reg, TCFG, 0),
};
static const struct reg_block_type npcx_tach_type =
	REG_BLOCK_TYPE("TACH", npcx_tach_fields);

/* 16. PWM */
static const struct reg_field_desc npcx_pwm_fields[] = {
	REG_FIELD(struct pwm_reg, PRSC, 0),
	REG_FIELD(struct pwm_reg, CTR, 0),
	REG_FIELD(struct pwm_reg, PWMCTL, 0),
	REG_FIELD(struct pwm_reg, DCR, 0),
	REG_FIELD(struct pwm_reg, PWMCTLEX, 0),
};
static const struct reg_block_type npcx_pwm_type =
	REG_BLOCK_TYPE("PWM", npcx_pwm_fields);

/* 17. ESPI */
static const struct reg_field_desc npcx_espi_fields[] = {
	REG_FIELD(struct espi_reg, ESPIID, 0),
	REG_FIELD(struct espi_reg, ESPICFG, 0),
	REG_FIELD(struct espi_reg, ESPISTS, 0),
	REG_FIELD(struct espi_reg, ESPIIE, 0),
	REG_FIELD(struct espi_reg, ESPIWE, 0),
	REG_FIELD(struct espi_reg, VWREGIDX, 0),
	REG_FIELD(struct espi_reg, VWREGDATA, 0),
	REG_FIELD(struct espi_reg, OOBRXRDHEAD, 0),
	REG_FIELD(struct espi_reg, OOBTXWRHEAD, 0),
	REG_FIELD(struct espi_reg, OOBCTL, 0),
	REG_FIELD(struct espi_reg, FLASHRXRDHEAD, 0),
	REG_FIELD(struct espi_reg, FLASHTXWRHEAD, 0),
	REG_FIELD(struct espi_reg, FLASHCFG, 0),
	REG_FIELD(struct espi_reg, FLASHCTL, 0),
	REG_FIELD(struct espi_reg, ESPIERR, 0),
	REG_FIELD(struct espi_reg, PBMRXRDHEAD, 0),
	REG_FIELD(struct espi_reg, PBMTXWRHEAD, 0),
	REG_FIELD(struct espi_reg, PERCFG, 0),
	REG_FIELD(struct espi_reg, PERCTL, 0),
	REG_FIELD(struct espi_reg, STATUS_IMG, 0),
	REG_FIELD(struct espi_reg, NPCX_ONLY_ESPI_REG1, 0),
	REG_FIELD(struct espi_reg, NPCX_ONLY_ESPI_REG2, 0),
	REG_FIELD_ARR(struct espi_reg, VWEVSM, 0),
	REG_FIELD_ARR(struct espi_reg, VWEVMS, 0),
	REG_FIELD_ARR(struct espi_reg, VWGPSM, 0),
	REG_FIELD(struct espi_reg, VWCTL, 0),
	REG_FIELD(struct espi_reg, OOBCTL_DIRECT, 0),
	REG_FIELD(struct espi_reg, FLASHCTL_DIRECT, 0),
	REG_FIELD_ARR(struct espi_reg, FLASH_PRTR_BADDR, 0),
	REG_FIELD_ARR(struct espi_reg, FLASH_PRTR_HADDR, 0),
	REG_FIELD_ARR(struct espi_reg, FLASH_RGN_TAG_OVR, 0),
	REG_FIELD(struct espi_reg, FLASH_RPMC_CFG_1, 0),
	REG_FIELD(struct espi_reg, FLASH_RPMC_CFG_2, 0),
	REG_FIELD(struct espi_reg, RMAP_FLASH_OFFS, 0),
	REG_FIELD(struct espi_reg, RMAP_DST_BASE, 0),
	REG_FIELD(struct espi_reg, RMAP_WIN_SIZE, 0),
	REG_FIELD(struct espi_reg, FLASHBASE, 0),
};
static const struct reg_block_type npcx_espi_type =
	REG_BLOCK_TYPE("ESPI", npcx_espi_fields);

/* 18. MDMA */
static const struct reg_field_desc npcx_mdma_fields[] = {
	REG_FIELD(struct mdma_reg, MDMA_CTL0, 0),
	REG_FIELD(struct mdma_reg, MDMA_SRCB0, 0),
	REG_FIELD(struct mdma_reg, MDMA_DSTB0, 0),
	REG_FIELD(struct mdma_reg, MDMA_TCNT0, 0),
	REG_FIELD(struct mdma_reg, MDMA_CDST0, REG_FLAG_VOLATILE),
	REG_FIELD(struct mdma_reg, MDMA_CTCNT0, REG_FLAG_VOLATILE),
	REG_FIELD(struct mdma_reg, MDMA_CTL1, 0),
	REG_FIELD(struct mdma_reg, MDMA_SRCB1, 0),
	REG_FIELD(struct mdma_reg, MDMA_DSTB1, 0),
	REG_FIELD(struct mdma_reg, MDMA_TCNT1, 0),
	REG_FIELD(struct mdma_reg, MDMA_CSRC1, REG_FLAG_VOLATILE),
	REG_FIELD(struct mdma_reg, MDMA_CTCNT1, REG_FLAG_VOLATILE),
};
static const struct reg_block_type npcx_mdma_type =
	REG_BLOCK_TYPE("MDMA", npcx_mdma_fields);

/* 19. MIWU (Multi-Input Wake-Up Unit, 8 groups per table) */
#define MIWU_GRP_FIELDS(_g, _off)                                             \
	REG_RAW("WKEDG_" #_g, (_off) + 0x00, 1, 0),                           \
		REG_RAW("WKAEDG_" #_g, (_off) + 0x01, 1, 0),                  \
		REG_RAW("WKPND_" #_g, (_off) + 0x02, 1, REG_FLAG_VOLATILE),   \
		REG_RAW("WKPCL_" #_g, (_off) + 0x03, 1, REG_FLAG_WRITE_ONLY), \
		REG_RAW("WKEN_" #_g, (_off) + 0x04, 1, 0),                    \
		REG_RAW("WKST_" #_g, (_off) + 0x05, 1, REG_FLAG_VOLATILE),    \
		REG_RAW("WKINEN_" #_g, (_off) + 0x06, 1, 0),                  \
		REG_RAW("WKMOD_" #_g, (_off) + 0x07, 1, 0)

static const struct reg_field_desc npcx_miwu_fields[] = {
	MIWU_GRP_FIELDS(1, 0x00), MIWU_GRP_FIELDS(2, 0x10),
	MIWU_GRP_FIELDS(3, 0x20), MIWU_GRP_FIELDS(4, 0x30),
	MIWU_GRP_FIELDS(5, 0x40), MIWU_GRP_FIELDS(6, 0x50),
	MIWU_GRP_FIELDS(7, 0x60), MIWU_GRP_FIELDS(8, 0x70),
};
static const struct reg_block_type npcx_miwu_type =
	REG_BLOCK_TYPE("MIWU", npcx_miwu_fields);

/* 20. GPIO */
static const struct reg_field_desc npcx_gpio_fields[] = {
	REG_FIELD(struct gpio_reg, PDOUT, 0),
	REG_FIELD(struct gpio_reg, PDIN, REG_FLAG_VOLATILE),
	REG_FIELD(struct gpio_reg, PDIR, 0),
	REG_FIELD(struct gpio_reg, PPULL, 0),
	REG_FIELD(struct gpio_reg, PPUD, 0),
	REG_FIELD(struct gpio_reg, PENVDD, 0),
	REG_FIELD(struct gpio_reg, PTYPE, 0),
	REG_FIELD(struct gpio_reg, PLOCK_CTL, 0),
};
static const struct reg_block_type npcx_gpio_type =
	REG_BLOCK_TYPE("GPIO", npcx_gpio_fields);

/* 21. UART */
static const struct reg_field_desc npcx_uart_fields[] = {
	REG_FIELD(struct uart_reg, UTBUF, REG_FLAG_WRITE_ONLY),
	REG_FIELD(struct uart_reg, URBUF, REG_FLAG_SIDE_EFFECT),
	REG_FIELD(struct uart_reg, UICTRL, 0),
	REG_FIELD(struct uart_reg, USTAT, 0),
	REG_FIELD(struct uart_reg, UFRS, 0),
	REG_FIELD(struct uart_reg, UMDSL, 0),
	REG_FIELD(struct uart_reg, UBAUD, 0),
	REG_FIELD(struct uart_reg, UPSR, 0),
	REG_FIELD(struct uart_reg, UFTSTS, 0),
	REG_FIELD(struct uart_reg, UFRSTS, 0),
	REG_FIELD(struct uart_reg, UFTCTL, 0),
	REG_FIELD(struct uart_reg, UFRCTL, 0),
	REG_FIELD(struct uart_reg, UCNTL, 0),
};
static const struct reg_block_type npcx_uart_type =
	REG_BLOCK_TYPE("UART", npcx_uart_fields);

/* 22. KBS (Keyboard Scan) */
static const struct reg_field_desc npcx_kbs_fields[] = {
	REG_FIELD(struct kbs_reg, KBSIN, REG_FLAG_VOLATILE),
	REG_FIELD(struct kbs_reg, KBSINPU, 0),
	REG_FIELD(struct kbs_reg, KBSOUT0, 0),
	REG_FIELD(struct kbs_reg, KBSOUT1, 0),
	REG_FIELD(struct kbs_reg, KBS_BUF_INDX, 0),
	REG_FIELD(struct kbs_reg, KBS_BUF_DATA, REG_FLAG_SIDE_EFFECT),
	REG_FIELD(struct kbs_reg, KBSEVT, 0),
	REG_FIELD(struct kbs_reg, KBSCTL, 0),
	REG_FIELD(struct kbs_reg, KBS_CFG_INDX, 0),
	REG_FIELD(struct kbs_reg, KBS_CFG_DATA, 0),
};
static const struct reg_block_type npcx_kbs_type =
	REG_BLOCK_TYPE("KBS", npcx_kbs_fields);

/* 23. ADC */
static const struct reg_field_desc npcx_adc_fields[] = {
	REG_FIELD(struct adc_reg, ADCSTS, 0),
	REG_FIELD(struct adc_reg, ADCCNF, 0),
	REG_FIELD(struct adc_reg, ATCTL, 0),
	REG_FIELD(struct adc_reg, ASCADD, 0),
	REG_FIELD(struct adc_reg, ADCCS, 0),
	REG_FIELD(struct adc_reg, ADCCS2, 0),
	REG_FIELD(struct adc_reg, THRCTS, 0),
	REG_FIELD(struct adc_reg, ADCCNF2, 0),
	REG_FIELD(struct adc_reg, GENDLY, 0),
	REG_FIELD(struct adc_reg, MEAST, 0),
	REG_RAW_ARR("CHNDAT", 0x040, 2, 12, 2, REG_FLAG_VOLATILE),
	REG_RAW_ARR("THRCTL", 0x060, 2, 6, 2, 0),
};
static const struct reg_block_type npcx_adc_type =
	REG_BLOCK_TYPE("ADC", npcx_adc_fields);

/* 24. TWD (Timer Watchdog) */
static const struct reg_field_desc npcx_twd_fields[] = {
	REG_FIELD(struct twd_reg, TWCFG, 0),
	REG_FIELD(struct twd_reg, TWCP, 0),
	REG_FIELD(struct twd_reg, TWDT0, 0),
	REG_FIELD(struct twd_reg, T0CSR, 0),
	REG_FIELD(struct twd_reg, WDCNT, REG_FLAG_VOLATILE),
	REG_FIELD(struct twd_reg, WDSDM, REG_FLAG_WRITE_ONLY),
	REG_FIELD(struct twd_reg, TWMT0, REG_FLAG_VOLATILE),
	REG_FIELD(struct twd_reg, TWMWD, 0),
	REG_FIELD(struct twd_reg, WDCP, 0),
};
static const struct reg_block_type npcx_twd_type =
	REG_BLOCK_TYPE("TWD", npcx_twd_fields);

/* 25. SPIP (SPI Peripheral) */
static const struct reg_field_desc npcx_spip_fields[] = {
	REG_FIELD(struct spip_reg, SPIP_DATA, REG_FLAG_SIDE_EFFECT),
	REG_FIELD(struct spip_reg, SPIP_CTL1, 0),
	REG_FIELD(struct spip_reg, SPIP_STAT, 0),
};
static const struct reg_block_type npcx_spip_type =
	REG_BLOCK_TYPE("SPIP", npcx_spip_fields);

/* 26. ARM Cortex-M4 Core Peripherals (SCB, NVIC, SysTick) */
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
	REG_RAW_ARR("ISER", offsetof(NVIC_Type, ISER), 4, 3, 4, 0),
	REG_RAW_ARR("ISPR", offsetof(NVIC_Type, ISPR), 4, 3, 4,
		    REG_FLAG_VOLATILE),
	REG_RAW_ARR("IABR", offsetof(NVIC_Type, IABR), 4, 3, 4,
		    REG_FLAG_VOLATILE),
	REG_RAW_ARR("IPR", offsetof(NVIC_Type, IPR), 4, 24, 4, 0),
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

/* Instantiated modules on NPCX (PMC & CDCG first!) */
static const struct reg_module_inst npcx_modules[] = {
	/* Always-on / System Core */
	{ "PMC", 0x4000d000, &npcx_pmc_type, REG_CLK_NONE },
	{ "CDCG", 0x400b5000, &npcx_cdcg_type, REG_CLK_NONE },
	{ "SCFG", 0x400c3000, &npcx_scfg_type, REG_CLK_NONE },
	{ "GLUE", 0x400bd000, &npcx_glue_type, REG_CLK_NONE },
	{ "BBRAM", 0x400af000, &npcx_bbram_type, REG_CLK_NONE },
	{ "MSWC", 0x400c1000, &npcx_mswc_type, REG_CLK_NONE },
	{ "TWD", 0x400d8000, &npcx_twd_type, REG_CLK_NONE },
	{ "ARM_SCB", SCB_BASE, &arm_scb_type, REG_CLK_NONE },
	{ "ARM_NVIC", NVIC_BASE, &arm_nvic_type, REG_CLK_NONE },
	{ "ARM_SYSTICK", SysTick_BASE, &arm_systick_type, REG_CLK_NONE },

	/* MIWU 0..2 */
	{ "MIWU0", 0x400bb000, &npcx_miwu_type, REG_CLK_NONE },
	{ "MIWU1", 0x400bd000, &npcx_miwu_type, REG_CLK_NONE },
	{ "MIWU2", 0x400bf000, &npcx_miwu_type, REG_CLK_NONE },

	/* GPIO 0..F */
	{ "GPIO0", 0x40081000, &npcx_gpio_type, REG_CLK_NONE },
	{ "GPIO1", 0x40083000, &npcx_gpio_type, REG_CLK_NONE },
	{ "GPIO2", 0x40085000, &npcx_gpio_type, REG_CLK_NONE },
	{ "GPIO3", 0x40087000, &npcx_gpio_type, REG_CLK_NONE },
	{ "GPIO4", 0x40089000, &npcx_gpio_type, REG_CLK_NONE },
	{ "GPIO5", 0x4008b000, &npcx_gpio_type, REG_CLK_NONE },
	{ "GPIO6", 0x4008d000, &npcx_gpio_type, REG_CLK_NONE },
	{ "GPIO7", 0x4008f000, &npcx_gpio_type, REG_CLK_NONE },
	{ "GPIO8", 0x40091000, &npcx_gpio_type, REG_CLK_NONE },
	{ "GPIO9", 0x40093000, &npcx_gpio_type, REG_CLK_NONE },
	{ "GPIOA", 0x40095000, &npcx_gpio_type, REG_CLK_NONE },
	{ "GPIOB", 0x40097000, &npcx_gpio_type, REG_CLK_NONE },
	{ "GPIOC", 0x40099000, &npcx_gpio_type, REG_CLK_NONE },
	{ "GPIOD", 0x4009b000, &npcx_gpio_type, REG_CLK_NONE },
	{ "GPIOE", 0x4009d000, &npcx_gpio_type, REG_CLK_NONE },
	{ "GPIOF", 0x4009f000, &npcx_gpio_type, REG_CLK_NONE },

	/* Host & Flash Interfaces */
	{ "FIU", 0x40000000, &npcx_fiu_type, NPCX_CLK_ID(NPCX_PWDWN_CTL1, 2) },
	{ "SHM", 0x40010000, &npcx_shm_type, NPCX_CLK_ID(NPCX_PWDWN_CTL2, 5) },
	{ "KBC", 0x400c7000, &npcx_kbc_type, NPCX_CLK_ID(NPCX_PWDWN_CTL2, 4) },
	{ "PMCH1", 0x400c9000, &npcx_pmch_type,
	  NPCX_CLK_ID(NPCX_PWDWN_CTL2, 0) },
	{ "PMCH2", 0x400cb000, &npcx_pmch_type,
	  NPCX_CLK_ID(NPCX_PWDWN_CTL2, 1) },
	{ "PMCH3", 0x400cd000, &npcx_pmch_type,
	  NPCX_CLK_ID(NPCX_PWDWN_CTL2, 2) },
	{ "PMCH4", 0x400cf000, &npcx_pmch_type,
	  NPCX_CLK_ID(NPCX_PWDWN_CTL2, 3) },
	{ "C2H", 0x400c5000, &npcx_c2h_type, NPCX_CLK_ID(NPCX_PWDWN_CTL2, 7) },
	{ "ESPI", 0x4000a000, &npcx_espi_type,
	  NPCX_CLK_ID(NPCX_PWDWN_CTL6, 7) },
	{ "LCT", 0x400d7000, &npcx_lct_type, REG_CLK_NONE },

	/* SMB / I2C 0..7 */
	{ "SMB0", 0x40009000, &npcx_smb_type, NPCX_CLK_ID(NPCX_PWDWN_CTL3, 0) },
	{ "SMB1", 0x40009100, &npcx_smb_type, NPCX_CLK_ID(NPCX_PWDWN_CTL3, 1) },
	{ "SMB2", 0x40009200, &npcx_smb_type, NPCX_CLK_ID(NPCX_PWDWN_CTL3, 2) },
	{ "SMB3", 0x40009300, &npcx_smb_type, NPCX_CLK_ID(NPCX_PWDWN_CTL3, 3) },
	{ "SMB4", 0x40008000, &npcx_smb_type, NPCX_CLK_ID(NPCX_PWDWN_CTL3, 4) },
	{ "SMB5", 0x4000f000, &npcx_smb_type, NPCX_CLK_ID(NPCX_PWDWN_CTL7, 0) },
	{ "SMB6", 0x40013000, &npcx_smb_type, NPCX_CLK_ID(NPCX_PWDWN_CTL7, 1) },
	{ "SMB7", 0x40014000, &npcx_smb_type, NPCX_CLK_ID(NPCX_PWDWN_CTL7, 2) },

	/* Internal Timers */
	{ "ITIM1", 0x400b0000, &npcx_itim32_type,
	  NPCX_CLK_ID(NPCX_PWDWN_CTL4, 0) },
	{ "ITIM2", 0x400b2000, &npcx_itim32_type,
	  NPCX_CLK_ID(NPCX_PWDWN_CTL4, 1) },
	{ "ITIM3", 0x400b4000, &npcx_itim32_type,
	  NPCX_CLK_ID(NPCX_PWDWN_CTL4, 2) },
	{ "ITIM4", 0x400be000, &npcx_itim32_type,
	  NPCX_CLK_ID(NPCX_PWDWN_CTL6, 0) },
	{ "ITIM5", 0x400bc000, &npcx_itim32_type,
	  NPCX_CLK_ID(NPCX_PWDWN_CTL6, 1) },
	{ "ITIM6", 0x400ba000, &npcx_itim32_type,
	  NPCX_CLK_ID(NPCX_PWDWN_CTL6, 2) },
	{ "ITIM64", 0x400b6000, &npcx_itim64_type,
	  NPCX_CLK_ID(NPCX_PWDWN_CTL7, 5) },

	/* Tachometers */
	{ "TACH1", 0x400e1000, &npcx_tach_type,
	  NPCX_CLK_ID(NPCX_PWDWN_CTL5, 6) },
	{ "TACH2", 0x400e3000, &npcx_tach_type,
	  NPCX_CLK_ID(NPCX_PWDWN_CTL5, 7) },

	/* PWMs 0..7 */
	{ "PWM0", 0x40080000, &npcx_pwm_type, NPCX_CLK_ID(NPCX_PWDWN_CTL1, 3) },
	{ "PWM1", 0x40082000, &npcx_pwm_type, NPCX_CLK_ID(NPCX_PWDWN_CTL1, 4) },
	{ "PWM2", 0x40084000, &npcx_pwm_type, NPCX_CLK_ID(NPCX_PWDWN_CTL1, 5) },
	{ "PWM3", 0x40086000, &npcx_pwm_type, NPCX_CLK_ID(NPCX_PWDWN_CTL1, 6) },
	{ "PWM4", 0x40088000, &npcx_pwm_type, NPCX_CLK_ID(NPCX_PWDWN_CTL5, 0) },
	{ "PWM5", 0x4008a000, &npcx_pwm_type, NPCX_CLK_ID(NPCX_PWDWN_CTL5, 1) },
	{ "PWM6", 0x4008c000, &npcx_pwm_type, NPCX_CLK_ID(NPCX_PWDWN_CTL5, 2) },
	{ "PWM7", 0x4008e000, &npcx_pwm_type, NPCX_CLK_ID(NPCX_PWDWN_CTL5, 3) },

	/* UART 1..2, KBS, ADC, SPIP */
	{ "UART1", 0x400c4000, &npcx_uart_type,
	  NPCX_CLK_ID(NPCX_PWDWN_CTL4, 5) },
	{ "UART2", 0x400c6000, &npcx_uart_type,
	  NPCX_CLK_ID(NPCX_PWDWN_CTL7, 6) },
	{ "KBS", 0x400a3000, &npcx_kbs_type, NPCX_CLK_ID(NPCX_PWDWN_CTL1, 0) },
	{ "ADC0", 0x400d1000, &npcx_adc_type, NPCX_CLK_ID(NPCX_PWDWN_CTL4, 4) },
	{ "SPIP", 0x400d2000, &npcx_spip_type,
	  NPCX_CLK_ID(NPCX_PWDWN_CTL4, 7) },

	/* MDMA 1..5 */
	{ "MDMA1", 0x40011100, &npcx_mdma_type, REG_CLK_NONE },
	{ "MDMA2", 0x40011200, &npcx_mdma_type, REG_CLK_NONE },
	{ "MDMA3", 0x40011300, &npcx_mdma_type, REG_CLK_NONE },
	{ "MDMA4", 0x40011400, &npcx_mdma_type, REG_CLK_NONE },
	{ "MDMA5", 0x40011500, &npcx_mdma_type, REG_CLK_NONE },
};

const struct reg_module_inst *reg_audit_chip_get_modules(size_t *num_modules)
{
	*num_modules = ARRAY_SIZE(npcx_modules);
	return npcx_modules;
}

int reg_audit_chip_module_enter(const struct reg_module_inst *mod,
				int *was_clocked, uint32_t *saved_state)
{
	if (mod->clk_id == REG_CLK_NONE) {
		*was_clocked = 1;
		*saved_state = 0;
		return 0;
	}

	uint8_t ctl_idx = NPCX_CLK_CTL_IDX(mod->clk_id);
	uint8_t bit = NPCX_CLK_BIT(mod->clk_id);
	uintptr_t pmc_base = 0x4000d000;
	volatile uint8_t *reg = &NPCX_PWDWN_CTL(pmc_base, ctl_idx);
	uint8_t val = *reg;

	*saved_state = val;
	/* On NPCX, PWDWN_CTL bit == 1 means powered down (clock gated) */
	if (val & BIT(bit)) {
		*was_clocked = 0;
		*reg = val & ~BIT(bit);
		__DSB();
		__ISB();
	} else {
		*was_clocked = 1;
	}
	return 0;
}

void reg_audit_chip_module_exit(const struct reg_module_inst *mod,
				uint32_t saved_state)
{
	if (mod->clk_id == REG_CLK_NONE) {
		return;
	}

	uint8_t ctl_idx = NPCX_CLK_CTL_IDX(mod->clk_id);
	uintptr_t pmc_base = 0x4000d000;
	volatile uint8_t *reg = &NPCX_PWDWN_CTL(pmc_base, ctl_idx);

	*reg = (uint8_t)saved_state;
	__DSB();
	__ISB();
}

void reg_audit_chip_dump_core_regs(const char *stage_tag)
{
	printk("CORE|%s|CONTROL=0x%08x|PRIMASK=0x%08x|"
	       "BASEPRI=0x%08x|MSP=0x%08x|PSP=0x%08x\n",
	       stage_tag, (unsigned int)__get_CONTROL(),
	       (unsigned int)__get_PRIMASK(), (unsigned int)__get_BASEPRI(),
	       (unsigned int)__get_MSP(), (unsigned int)__get_PSP());
}
