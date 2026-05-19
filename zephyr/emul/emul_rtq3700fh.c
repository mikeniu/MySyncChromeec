/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "emul/emul_common_i2c.h"
#include "emul/emul_rtq3700fh.h"

#include <zephyr/device.h>
#include <zephyr/drivers/emul_stub_device.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(emul_rtq3700fh, LOG_LEVEL_INF);

#define DT_DRV_COMPAT richtek_rtq3700fh

#define RTQ3700FH_PAGE_MAX 0x58
#define RTQ3700FH_PAGE_REGS_MAX 0xFF

struct rtq3700fh_page_regs {
	uint8_t data[RTQ3700FH_PAGE_MAX + 1][RTQ3700FH_PAGE_REGS_MAX + 1];
};

struct rtq3700fh_regs {
	uint8_t page;
	uint8_t nvm_status;
	uint16_t fw_version;
	uint32_t checksum;
	struct rtq3700fh_page_regs page_regs;
};

struct rtq3700fh_data {
	struct i2c_common_emul_data common;
	struct rtq3700fh_regs regs;
	uint16_t unlock_val_tmp;
	bool is_unlocked;
};

void rtq3700fh_emul_reset_regs(const struct emul *emul)
{
	struct rtq3700fh_data *data = emul->data;
	struct rtq3700fh_regs *regs = &data->regs;

	regs->page = 0;
	data->is_unlocked = false;
	data->unlock_val_tmp = 0;

	/* Setup defaults to pass the update state machine Happy Path */
	regs->nvm_status = 0xE0; /* NVM_READY_STAT_VAL */
	regs->fw_version = 0x0000;
	regs->checksum = 0x01E8; /* Default matched CRC from DTS */

	memset(&regs->page_regs, 0, sizeof(struct rtq3700fh_page_regs));
}

void rtq3700fh_emul_set_nvm_status(const struct emul *emul, uint8_t status)
{
	struct rtq3700fh_data *data = emul->data;
	data->regs.nvm_status = status;
}

void rtq3700fh_emul_set_fw_version(const struct emul *emul, uint16_t version)
{
	struct rtq3700fh_data *data = emul->data;
	data->regs.fw_version = version;
}

void rtq3700fh_emul_set_checksum(const struct emul *emul, uint32_t checksum)
{
	struct rtq3700fh_data *data = emul->data;
	data->regs.checksum = checksum;
}

bool rtq3700fh_emul_is_unlocked(const struct emul *emul)
{
	struct rtq3700fh_data *data = emul->data;
	return data->is_unlocked;
}

static int rtq3700fh_emul_read(const struct emul *emul, int reg, uint8_t *val,
			       int bytes, void *unused_data)
{
	struct rtq3700fh_data *data = emul->data;
	struct rtq3700fh_regs *regs = &data->regs;
	uint8_t page = regs->page;

	if (val == NULL) {
		return -EINVAL;
	}

	if (reg == 0x00) { /* PAGE_SET_REG */
		*val = regs->page;
		return 0;
	}

	if (page == 0x00) {
		switch (reg) {
		case 0x99: /* MFR_ID_REG (PMBus Block 16) */
			if (bytes == 1)
				*val = 2; /* Length */
			else if (bytes == 2)
				*val = 0x52; /* LSB */
			else if (bytes == 3)
				*val = 0x54; /* MSB */
			return 0;
		case 0x9A: /* MFR_MODEL_REG (PMBus Block 16) */
			if (bytes == 1)
				*val = 2;
			else if (bytes == 2)
				*val = 0x46;
			else if (bytes == 3)
				*val = 0x48;
			return 0;
		case 0x9B: /* MFR_REVISION_REG (PMBus Block 16) */
			if (bytes == 1)
				*val = 2;
			else if (bytes == 2)
				*val = 0x30;
			else if (bytes == 3)
				*val = 0x32;
			return 0;
		case 0x9E: /* MFR_CONFIG_ID_REG (PMBus Block 8) */
			if (bytes == 1)
				*val = 1;
			else if (bytes == 2)
				*val = 0x00;
			return 0;
		case 0x9F: /* MFR_PROJECT_CODE_REG (PMBus Block 16) */
			if (bytes == 1)
				*val = 2;
			else if (bytes == 2)
				*val = 0x00;
			else if (bytes == 3)
				*val = 0x00;
			return 0;
		case 0xEF: /* FW_VERSION_REG (Word Read, No Length) */
			if (bytes == 1)
				*val = (regs->fw_version & 0xFF);
			else if (bytes == 2)
				*val = ((regs->fw_version >> 8) & 0xFF);
			return 0;
		case 0xF0: /* NVM_CHECKSUM_REG (32-bit Read) */
			if (bytes == 1)
				*val = (regs->checksum & 0xFF);
			else if (bytes == 2)
				*val = ((regs->checksum >> 8) & 0xFF);
			else if (bytes == 3)
				*val = ((regs->checksum >> 16) & 0xFF);
			else if (bytes == 4)
				*val = ((regs->checksum >> 24) & 0xFF);
			return 0;
		}
	} else if (page == 0x50) {
		if (reg == 0xED) { /* NVM_STAT_REG */
			*val = regs->nvm_status;
			return 0;
		}
	}

	/* Fallback: Read from simulated memory */
	if (page <= RTQ3700FH_PAGE_MAX) {
		uint16_t target_reg = reg + bytes - 1;
		if (target_reg <= RTQ3700FH_PAGE_REGS_MAX) {
			*val = regs->page_regs.data[page][target_reg];
			return 0;
		}
	}

	*val = 0;
	return 0;
}

static int rtq3700fh_emul_write(const struct emul *emul, int reg, uint8_t val,
				int bytes, void *unused_data)
{
	struct rtq3700fh_data *data = emul->data;
	struct rtq3700fh_regs *regs = &data->regs;
	uint8_t page = regs->page;

	if (reg == 0x00) { /* PAGE_SET_REG */
		if (bytes == 1 && val <= RTQ3700FH_PAGE_MAX) {
			regs->page = val;
		}
		return 0;
	}

	if (page == 0x00 && reg == 0xFC) { /* LOCK_UNLOCK_REG */
		if (bytes == 1) {
			data->unlock_val_tmp = val;
		} else if (bytes == 2) {
			uint16_t pw = data->unlock_val_tmp | (val << 8);
			if (pw == 0x2454) {
				data->is_unlocked = true;
			} else if (pw == 0x0000) {
				data->is_unlocked = false;
			}
		}
		return 0;
	}

	/* Fallback: Write directly into simulated memory map */
	if (page <= RTQ3700FH_PAGE_MAX) {
		/*
		 * By utilizing (reg + bytes - 1), we perfectly mimic Zephyr's
		 * i2c_burst_write behavior, effectively storing PMBus block
		 * lengths and sequential data chunks without extra parsing.
		 */
		uint16_t target_reg = reg + bytes - 1;
		if (target_reg <= RTQ3700FH_PAGE_REGS_MAX) {
			regs->page_regs.data[page][target_reg] = val;
		}
	}

	return 0;
}

static int rtq3700fh_emul_init(const struct emul *emul,
			       const struct device *parent)
{
	struct rtq3700fh_data *data = (struct rtq3700fh_data *)emul->data;
	struct i2c_common_emul_data *common_data = &data->common;

	i2c_common_emul_init(common_data);
	i2c_common_emul_set_read_func(common_data, rtq3700fh_emul_read, NULL);
	i2c_common_emul_set_write_func(common_data, rtq3700fh_emul_write, NULL);

	rtq3700fh_emul_reset_regs(emul);

	return 0;
}

#define INIT_RTQ3700FH_EMUL(n)                                           \
	static struct i2c_common_emul_cfg common_cfg_##n;                \
	static struct rtq3700fh_data rtq3700fh_data_##n;                 \
	static struct i2c_common_emul_cfg common_cfg_##n = {             \
		.dev_label = DT_NODE_FULL_NAME(DT_DRV_INST(n)),          \
		.data = &rtq3700fh_data_##n.common,                      \
		.addr = DT_INST_REG_ADDR(n)                              \
	};                                                               \
	static struct rtq3700fh_data rtq3700fh_data_##n = {              \
		.common = { .cfg = &common_cfg_##n }                     \
	};                                                               \
	EMUL_DT_INST_DEFINE(n, rtq3700fh_emul_init, &rtq3700fh_data_##n, \
			    &common_cfg_##n, &i2c_common_emul_api, NULL)

DT_INST_FOREACH_STATUS_OKAY(INIT_RTQ3700FH_EMUL)
