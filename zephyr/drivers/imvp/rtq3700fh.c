/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "chipset.h"
#include "rtq3700fh.h"

#ifdef CONFIG_AP_PWRSEQ_DRIVER
#include <ap_power/ap_pwrseq_sm.h>
#endif

#include <ctype.h>
#include <stdlib.h>

#include <zephyr/drivers/i2c.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/byteorder.h>

LOG_MODULE_REGISTER(imvp_rtq3700fh, LOG_LEVEL_INF);

/* Device tree compatibility string */
#define DT_DRV_COMPAT richtek_rtq3700fh

/* PMBus Page Constants */
#define RTQ3700FH_PAGE_INVALID 0xFF
#define RTQ3700FH_PAGE_NVM 0x50
#define RTQ3700FH_LAST_PAGE 0x58
#define RTQ3700FH_LAST_PAGE_REGISTER 0xFF

#define RTQ3700FH_UNLOCK 0x2454

BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) == 1,
	     "Only one 'richtek,rtq3700fh' node allowed");

static const uint32_t raw_data[] = DT_PROP(DT_DRV_INST(0), update_data);
static const uint16_t expected_crc = (uint16_t)DT_INST_PROP(0, update_crc);

struct rtq3700fh_data_t {
	uint8_t cur_page;
};

struct rtq3700fh_config_t {
	struct i2c_dt_spec i2c;
	struct gpio_dt_spec enable_gpio;
};

static struct rtq3700fh_data_t data0 = {
	.cur_page = RTQ3700FH_PAGE_INVALID,
};

static const struct rtq3700fh_config_t config0 = {
	.i2c = I2C_DT_SPEC_INST_GET(0),
	.enable_gpio = GPIO_DT_SPEC_GET(DT_DRV_INST(0), enable_gpios),
};

static const struct device *rtq3700fh_dev = DEVICE_DT_GET(DT_DRV_INST(0));

static int rtq3700fh_read_reg(const struct device *dev, uint8_t reg,
			      uint16_t *val)
{
	const struct rtq3700fh_config_t *cfg = dev->config;
	uint8_t buf[2];
	int ret;

	ret = i2c_burst_read_dt(&cfg->i2c, reg, buf, sizeof(buf));
	if (ret == 0) {
		*val = sys_get_le16(buf);
	}
	return ret;
}

static int rtq3700fh_write_reg(const struct device *dev, uint8_t reg,
			       uint16_t val)
{
	const struct rtq3700fh_config_t *cfg = dev->config;
	uint8_t buf[3];

	buf[0] = reg;
	sys_put_le16(val, &buf[1]);

	return i2c_write_dt(&cfg->i2c, buf, sizeof(buf));
}

static int rtq3700fh_read_pmbus_block_8(const struct device *dev, uint8_t reg,
					uint8_t *val)
{
	const struct rtq3700fh_config_t *cfg = dev->config;
	uint8_t buf[2];
	int ret;

	ret = i2c_burst_read_dt(&cfg->i2c, reg, buf, sizeof(buf));
	if (ret) {
		return ret;
	}

	if (buf[0] < 1) {
		return -ENODATA;
	}

	*val = buf[1];

	return 0;
}

static int rtq3700fh_read_pmbus_block_16(const struct device *dev, uint8_t reg,
					 uint16_t *val)
{
	const struct rtq3700fh_config_t *cfg = dev->config;
	uint8_t buf[3];
	int ret;

	ret = i2c_burst_read_dt(&cfg->i2c, reg, buf, sizeof(buf));
	if (ret) {
		return ret;
	}

	if (buf[0] < 2) {
		return -ENODATA;
	}

	*val = sys_get_le16(&buf[1]);

	return 0;
}

static int rtq3700fh_set_page(const struct device *dev, uint8_t page)
{
	const struct rtq3700fh_config_t *cfg = dev->config;
	struct rtq3700fh_data_t *data = dev->data;
	int rv;

	if (!chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
		return -EINVAL;
	}
	rv = i2c_reg_write_byte_dt(&cfg->i2c, PAGE_SET_REG, page);
	if (rv) {
		return rv;
	}

	data->cur_page = page;
	return rv;
}

static int rtq3700fh_set_cfg_mode(const struct device *dev, uint16_t password)
{
	if (!chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
		return -EINVAL;
	}
	return rtq3700fh_write_reg(dev, LOCK_UNLOCK_REG, password);
}

static int rtq3700fh_load_config(const struct device *dev)
{
	const struct rtq3700fh_config_t *cfg = dev->config;
	uint8_t cmd = RESTORE_USER_ALL_REG;

	if (!chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
		return -EINVAL;
	}
	rtq3700fh_set_page(dev, RTQ3700FH_PAGE_NVM);

	return i2c_write_dt(&cfg->i2c, &cmd, 1);
}

static int rtq3700fh_store_config(const struct device *dev)
{
	const struct rtq3700fh_config_t *cfg = dev->config;
	uint8_t cmd = STORE_USER_ALL_REG;

	if (!chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
		return -EINVAL;
	}
	rtq3700fh_set_page(dev, RTQ3700FH_PAGE_NVM);

	return i2c_write_dt(&cfg->i2c, &cmd, 1);
}

/**
 * @brief Parse the raw update data from devicetree and write to IC registers.
 */
static int rtq3700fh_apply_update_data(const struct device *dev)
{
	const struct rtq3700fh_config_t *cfg = dev->config;
	int rv;
	int prev_page = -1;
	size_t i = 0;

	if (!chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
		return -EINVAL;
	}

	while (i < ARRAY_SIZE(raw_data)) {
		if (i + 3 > ARRAY_SIZE(raw_data)) {
			LOG_ERR("Incomplete data entry");
			return -EINVAL;
		}

		uint8_t page = (uint8_t)raw_data[i++];
		uint8_t reg = (uint8_t)raw_data[i++];
		uint8_t dlen = (uint8_t)raw_data[i++];

		uint8_t buf[33];

		if (dlen > (sizeof(buf) - 1) ||
		    i + dlen > ARRAY_SIZE(raw_data)) {
			LOG_ERR("Invalid data length %d", dlen);
			return -EINVAL;
		}

		buf[0] = reg;

		for (int j = 0; j < dlen; j++) {
			buf[j + 1] = (uint8_t)raw_data[i++];
		}

		if (prev_page != page) {
			k_msleep(1);
			rv = rtq3700fh_set_page(dev, page);
			if (rv) {
				return rv;
			}
			prev_page = page;
		}

		rv = i2c_write_dt(&cfg->i2c, buf, dlen + 1);
		if (rv) {
			LOG_ERR("Write failed at P:0x%x R:0x%x (rv=%d)", page,
				reg, rv);
			return rv;
		}
		k_usleep(100);
	}

	return 0;
}

/*
 * @brief Read 32-bit checksum from IC and compare against the expected CRC.
 */
static int rtq3700fh_crc_check(const struct device *dev)
{
	const struct rtq3700fh_config_t *cfg = dev->config;
	uint8_t buf[4];
	uint32_t read_crc32;
	uint16_t read_crc16;
	int rv;

	if (!chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
		return -EINVAL;
	}

	rv = rtq3700fh_set_page(dev, 0x00);
	if (rv) {
		return rv;
	}

	rv = i2c_burst_read_dt(&cfg->i2c, NVM_CHECKSUM_REG, buf, sizeof(buf));
	if (rv) {
		LOG_ERR("Failed to read CRC register (rv=%d)", rv);
		return rv;
	}

	read_crc32 = sys_get_le32(buf);
	read_crc16 = read_crc32 & 0xFFFF;

	if (read_crc16 != expected_crc) {
		LOG_WRN("CRC Mismatch! Exp:0x%04x, Read:0x%04x", expected_crc,
			read_crc16);
		return -EINVAL;
	}

	return 0;
}

/**
 * @brief Poll the NVM status register for specific ready/finish bits.
 */
static int rtq3700fh_nvm_program_status(const struct device *dev, int stat_bit)
{
	const struct rtq3700fh_config_t *cfg = dev->config;
	int retry_cnt = 0;
	int retry_max;
	int delay_ms;
	int rv = 1;
	uint8_t status_reg = 0;

	if (stat_bit == NVM_RELOAD_STAT_BIT) {
		/* Step 3: NVM Restore Flag. Wait 1ms, retry 2 times */
		retry_max = 3;
		delay_ms = 1;
	} else if (stat_bit == NVM_PRGRM_FINISH_STAT_BIT) {
		/* Step 11: NVM Program Finish. Wait 100ms, retry 3 times */
		retry_max = 4;
		delay_ms = 100;
	} else if (stat_bit == -1) {
		/* Step 14: Check NVM 0xE0. Wait 1ms, retry 2 times */
		retry_max = 3;
		delay_ms = 1;
	} else {
		retry_max = 4;
		delay_ms = 1;
	}

	rtq3700fh_set_page(dev, RTQ3700FH_PAGE_NVM);

	while (retry_cnt < retry_max) {
		rv = i2c_reg_read_byte_dt(&cfg->i2c, NVM_STAT_REG, &status_reg);

		if (!rv) {
			LOG_INF("Polling NVM_STAT_REG (Attempt %d/%d): read 0x%02x",
				retry_cnt + 1, retry_max, status_reg);

			if (stat_bit == -1) {
				if (status_reg == NVM_READY_STAT_VAL) {
					return 0;
				}
			} else {
				if (status_reg & BIT(stat_bit)) {
					LOG_INF("Target bit %d is SET! Success.",
						stat_bit);
					return 0;
				}
			}
			rv = -EINVAL;
		} else {
			LOG_ERR("I2C read failed at NVM_STAT_REG (rv=%d)", rv);
		}
		retry_cnt++;
		if (retry_cnt < retry_max) {
			k_msleep(delay_ms);
		}
	}

	LOG_ERR("Timeout waiting for stat_bit %d. Final status_reg: 0x%02x",
		stat_bit, status_reg);
	return rv;
}

/**
 * @brief The main NVM update state machine.
 */
static int rtq3700fh_update(const struct device *dev)
{
	int rv = 0;
	uint16_t reg_val_16 = 0;
	uint8_t reg_val_8 = 0;
	int program_retry_cnt = 0;

	k_msleep(3);

	rv = rtq3700fh_nvm_program_status(dev, NVM_RELOAD_STAT_BIT);
	if (rv) {
		LOG_ERR("Step 3: NVM Restore Flag Not Ready");
		return -EINVAL;
	}

	rtq3700fh_set_page(dev, 0x00);
	rv = rtq3700fh_read_pmbus_block_16(dev, MFR_ID_REG, &reg_val_16);
	if (rv || reg_val_16 != MFR_ID) {
		LOG_ERR("Step 4-1: MFR_ID Mismatch (0x%04x)", reg_val_16);
		return -ENODEV;
	}

	rv = rtq3700fh_read_pmbus_block_16(dev, MFR_MODEL_REG, &reg_val_16);
	if (rv || reg_val_16 != MFR_MODEL) {
		LOG_ERR("Step 4-2: MFR_MODEL Mismatch (0x%04x)", reg_val_16);
		return -ENODEV;
	}

	rv = rtq3700fh_read_pmbus_block_16(dev, MFR_REVISION_REG, &reg_val_16);
	if (rv || reg_val_16 != MFR_REVISION) {
		LOG_ERR("Step 4-3: MFR_REVISION Mismatch (0x%04x)", reg_val_16);
		return -ENODEV;
	}

	rv = rtq3700fh_read_pmbus_block_8(dev, MFR_CONFIG_ID_REG, &reg_val_8);
	if (rv || reg_val_8 != MFR_CONFIG_ID) {
		LOG_ERR("Step 5-1: MFR_CONFIG_ID Mismatch (0x%04x)", reg_val_8);
		return -ENODEV;
	}

	rv = rtq3700fh_read_pmbus_block_16(dev, MFR_PROJECT_CODE_REG,
					   &reg_val_16);
	if (rv || reg_val_16 != MFR_PROJECT_CODE) {
		LOG_ERR("Step 5-2: MFR_PROJECT_CODE Mismatch (0x%04x)",
			reg_val_16);
		return -ENODEV;
	}

	bool need_update = false;

	rv = rtq3700fh_read_reg(dev, FW_VERSION_REG, &reg_val_16);
	if (rv == 0) {
		LOG_INF("Step 6-1: Current FW_VERSION = 0x%04x", reg_val_16);
		if (reg_val_16 != FW_VERSION) {
			LOG_INF("-> Version mismatch (Expected: 0x%04x)",
				FW_VERSION);
			need_update = true;
		}
	} else {
		need_update = true;
	}

	rv = rtq3700fh_crc_check(dev);
	if (rv != 0) {
		LOG_ERR("Step 6-2: Checksum error or mismatch detected.");
		need_update = true;
	}

	if (!need_update) {
		return 0;
	}

	rv = rtq3700fh_set_cfg_mode(dev, RTQ3700FH_UNLOCK);
	if (rv) {
		LOG_ERR("Step 7: Unlock IMVP Failure");
		return rv;
	} else {
		LOG_INF("Step 7: IMVP Unlocked successfully. Ready to write.");
	}

step_8_retry:
	if (program_retry_cnt >= 3) {
		LOG_ERR("Step 15: Verification failed after 3 retries");
		rv = -EIO;
		goto lock_imvp;
	}

	LOG_INF("Step 8: Applying data (Attempt %d)...", program_retry_cnt + 1);
	rv = rtq3700fh_apply_update_data(dev);
	if (rv)
		goto lock_imvp;

	rv = rtq3700fh_store_config(dev);
	if (rv)
		goto lock_imvp;

	k_msleep(500);

	rv = rtq3700fh_nvm_program_status(dev, NVM_PRGRM_FINISH_STAT_BIT);
	if (rv) {
		LOG_ERR("Step 11: Store NVM Timeout");
		goto lock_imvp;
	}

	rv = rtq3700fh_load_config(dev);
	if (rv) {
		goto lock_imvp;
	}
	k_msleep(1);

	rv = rtq3700fh_nvm_program_status(dev, -1);
	if (rv) {
		LOG_ERR("Step 14: NVM Restore Check Failed");
		goto lock_imvp;
	}

	rv = rtq3700fh_crc_check(dev);
	if (rv != 0) {
		LOG_WRN("Step 15: Checksum verify failed. Retrying...");
		program_retry_cnt++;
		goto step_8_retry;
	}

lock_imvp:
	LOG_INF("Step 16: Locking NVM...");
	rtq3700fh_set_page(dev, 0x00);
	rtq3700fh_set_cfg_mode(dev, 0x0000);

	return rv;
}

static void rtq3700fh_initiate_update(const struct device *dev)
{
	const struct rtq3700fh_config_t *cfg = dev->config;

	gpio_pin_configure_dt(&cfg->enable_gpio, GPIO_OUTPUT);
	gpio_pin_set_dt(&cfg->enable_gpio, 1);

	k_msleep(30);

	if (!rtq3700fh_update(rtq3700fh_dev)) {
		LOG_INF("IMVP Update Success!");
	} else {
		LOG_ERR("IMVP update Failed!");
	}
}

static int rtq3700fh_init(const struct device *dev)
{
	return 0;
}

DEVICE_DT_INST_DEFINE(0, rtq3700fh_init, NULL, &data0, &config0, POST_KERNEL,
		      CONFIG_APPLICATION_INIT_PRIORITY, NULL);

#if defined(CONFIG_IMVP_FACTORY_UPDATE)
static void imvp_init_cb(const struct device *dev,
			 const enum ap_pwrseq_state entry,
			 const enum ap_pwrseq_state exit)
{
	if (entry == AP_POWER_STATE_S5) {
		rtq3700fh_initiate_update(rtq3700fh_dev);
	}
}
AP_PWRSEQ_STATE_EXIT_CALLBACK_DEFINE(imvp_init_cb, AP_POWER_STATE_G3);
#endif

#ifdef CONFIG_IMVP_RTQ3700FH_CONSOLE

static int cmd_rtq3700fh_enter_cfg_mode(const struct shell *sh, size_t argc,
					char **argv)
{
	uint16_t password;
	const struct rtq3700fh_config_t *cfg = rtq3700fh_dev->config;
	int rv;

	if (!chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
		shell_error(sh, "Not in S5/G3 state\n");
		return -EINVAL;
	}

	gpio_pin_set_dt(&cfg->enable_gpio, 1);
	k_msleep(30);

	password = (uint16_t)strtol(argv[1], NULL, 0);

	rv = rtq3700fh_set_cfg_mode(rtq3700fh_dev, password);

	if (rv) {
		shell_error(sh, "Failed to write password (rv=%d)", rv);
		return rv;
	}

	if (password == RTQ3700FH_UNLOCK) {
		shell_print(sh, "IMVP unlocked successfully");
	} else {
		shell_print(sh, "IMVP locked (Password cleared/invalid)");
	}

	return 0;
}

static int cmd_rtq3700fh_load(const struct shell *sh, size_t argc, char **argv)
{
	const struct rtq3700fh_config_t *cfg = rtq3700fh_dev->config;
	int rv;

	if (!chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
		return -EINVAL;
	}

	gpio_pin_set_dt(&cfg->enable_gpio, 1);
	k_msleep(30);

	rv = rtq3700fh_load_config(rtq3700fh_dev);

	gpio_pin_set_dt(&cfg->enable_gpio, 0);

	return rv;
}

static int cmd_rtq3700fh_store(const struct shell *sh, size_t argc, char **argv)
{
	const struct rtq3700fh_config_t *cfg = rtq3700fh_dev->config;
	int rv;

	if (!chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
		return -EINVAL;
	}

	gpio_pin_set_dt(&cfg->enable_gpio, 1);
	k_msleep(30);

	rv = rtq3700fh_store_config(rtq3700fh_dev);

	gpio_pin_set_dt(&cfg->enable_gpio, 0);

	return rv;
}

static int cmd_rtq3700fh_set_page(const struct shell *sh, size_t argc,
				  char **argv)
{
	unsigned int arg;

	if (!chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
		shell_error(sh,
			    "To access paged registers put system on G3/S5");
		return -EINVAL;
	}

	arg = (unsigned int)strtol(argv[1], NULL, 0);

	if (arg > RTQ3700FH_LAST_PAGE) {
		shell_error(sh, "Invalid page: 0x%X (Max allowed: 0x%X)", arg,
			    RTQ3700FH_LAST_PAGE);
		return -EINVAL;
	}

	const struct rtq3700fh_config_t *cfg = rtq3700fh_dev->config;
	gpio_pin_set_dt(&cfg->enable_gpio, 1);
	k_msleep(30);

	rtq3700fh_set_page(rtq3700fh_dev, (uint8_t)arg);

	return 0;
}

static int cmd_rtq3700fh_dump_reg(const struct shell *sh, size_t argc,
				  char **argv)
{
	const struct rtq3700fh_config_t *cfg = rtq3700fh_dev->config;
	struct rtq3700fh_data_t *data = rtq3700fh_dev->data;
	uint8_t reg;
	int rv = 0;

	if (!chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
		shell_error(sh, "Must be in S5/G3 state!");
		return -EINVAL;
	}

	reg = (uint8_t)strtol(argv[2], NULL, 0);

	gpio_pin_set_dt(&cfg->enable_gpio, 1);
	k_msleep(30);

	if (data->cur_page != RTQ3700FH_PAGE_INVALID) {
		rtq3700fh_set_page(rtq3700fh_dev, data->cur_page);
	}

	if (strcmp(argv[1], "-b") == 0) {
		uint8_t val;
		rv = i2c_reg_read_byte_dt(&cfg->i2c, reg, &val);
		if (!rv) {
			shell_print(sh, "Read Byte: [0x%02X] = 0x%02X", reg,
				    val);
		}

	} else if (strcmp(argv[1], "-w") == 0) {
		uint16_t val;
		rv = rtq3700fh_read_reg(rtq3700fh_dev, reg, &val);
		if (!rv) {
			shell_print(sh, "Read Word: [0x%02X] = 0x%04X", reg,
				    val);
		}

	} else if (strcmp(argv[1], "-blk") == 0) {
		uint8_t buf[33] = { 0 };
		rv = i2c_burst_read_dt(&cfg->i2c, reg, buf, sizeof(buf));

		if (!rv) {
			uint8_t dlen = buf[0];
			if (dlen == 0 || dlen > 32) {
				shell_error(sh,
					    "Invalid block length received: %d",
					    dlen);
				rv = -EINVAL;
			} else {
				shell_fprintf(
					sh, SHELL_INFO,
					"Read Block: [0x%02X] Len: %d bytes | Data: ",
					reg, dlen);
				for (int i = 1; i <= dlen; i++) {
					shell_fprintf(sh, SHELL_INFO, "%02X ",
						      buf[i]);
				}
				shell_fprintf(sh, SHELL_INFO, "\n");
			}
		}
	} else {
		shell_error(sh, "Unknown flag '%s'. Use -b, -w, or -blk.",
			    argv[1]);
		rv = -EINVAL;
	}

	if (rv && rv != -EINVAL) {
		shell_error(sh, "Read Failed (rv=%d) at Reg 0x%02X", rv, reg);
	}

	return rv;
}

static int cmd_rtq3700fh_update(const struct shell *sh, size_t argc,
				char **argv)
{
	const struct rtq3700fh_config_t *cfg = rtq3700fh_dev->config;

	chipset_force_shutdown(CHIPSET_SHUTDOWN_G3);
	LOG_INF("System shutting_down for IMVP update");

	k_msleep(3000);

	rtq3700fh_initiate_update(rtq3700fh_dev);

	gpio_pin_set_dt(&cfg->enable_gpio, 0);

	LOG_INF("Press powerbutton/enter 'powerbtn' to boot system");

	return EC_SUCCESS;
}

static int cmd_rtq3700fh_set_reg(const struct shell *sh, size_t argc,
				 char **argv)
{
	const struct rtq3700fh_config_t *cfg = rtq3700fh_dev->config;
	struct rtq3700fh_data_t *data = rtq3700fh_dev->data;
	uint8_t reg;
	int rv = 0;

	if (!chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
		shell_error(sh, "Must be in S5/G3 state!");
		return -EINVAL;
	}

	reg = (uint8_t)strtol(argv[2], NULL, 0);

	gpio_pin_set_dt(&cfg->enable_gpio, 1);
	k_msleep(30);

	if (data->cur_page != RTQ3700FH_PAGE_INVALID) {
		rtq3700fh_set_page(rtq3700fh_dev, data->cur_page);
	}

	if (strcmp(argv[1], "-b") == 0) {
		uint8_t val = (uint8_t)strtol(argv[3], NULL, 0);
		rv = i2c_reg_write_byte_dt(&cfg->i2c, reg, val);

	} else if (strcmp(argv[1], "-w") == 0) {
		uint16_t val = (uint16_t)strtol(argv[3], NULL, 0);
		rv = rtq3700fh_write_reg(rtq3700fh_dev, reg, val);

	} else if (strcmp(argv[1], "-blk") == 0) {
		uint8_t dlen = argc - 3;
		uint8_t buf[33] = { 0 };

		if (dlen == 0 || dlen > 32) {
			shell_error(sh,
				    "Invalid block length: %d (Must be 1~32)",
				    dlen);
			rv = -EINVAL;
		} else {
			buf[0] = dlen;
			for (int i = 0; i < dlen; i++) {
				buf[i + 1] =
					(uint8_t)strtol(argv[i + 3], NULL, 0);
			}
			rv = i2c_burst_write_dt(&cfg->i2c, reg, buf, dlen + 1);
		}
	} else {
		shell_error(sh, "Unknown flag '%s'. Use -b, -w, or -blk.",
			    argv[1]);
		rv = -EINVAL;
	}

	if (rv && rv != -EINVAL) {
		shell_error(sh, "Write Failed (rv=%d) at Reg 0x%02X", rv, reg);
	}

	return rv;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	sub_imvp_cmds,
	SHELL_CMD_ARG(
		cfg_mode, NULL,
		SHELL_HELP("Enter password to set IMVP chip in config mode\n"
			   "lock=0x0000,unlock=0x2454",
			   NULL),
		cmd_rtq3700fh_enter_cfg_mode, 1, 4),
	SHELL_CMD(load_cfg, NULL, SHELL_HELP("Load IMVP config from NVM", NULL),
		  cmd_rtq3700fh_load),
	SHELL_CMD(store_cfg, NULL,
		  SHELL_HELP("Store IMVP config into NVM", NULL),
		  cmd_rtq3700fh_store),
	SHELL_CMD_ARG(set_page, NULL,
		      SHELL_HELP("Sets active page for R/W",
				 "<page number in hex>"),
		      cmd_rtq3700fh_set_page, 2, 0),
	SHELL_CMD_ARG(set_reg, NULL,
		      "Universal write command for RTQ3700FH.\n"
		      "Usage: imvp set_reg <flag> <reg> <data...>\n"
		      "Flags:\n"
		      "  -b   : Byte set_reg (8-bit)\n"
		      "  -w   : Word set_reg (16-bit) [PMBus Default]\n"
		      "  -blk : Block set_reg (1~32 bytes)\n"
		      "Examples:\n"
		      "  imvp set_reg -b 0x01 0x80\n"
		      "  imvp set_reg -w 0x21 0x1234\n"
		      "  imvp set_reg -blk 0x90 0x00 0xC0",
		      cmd_rtq3700fh_set_reg, 4, 35),
	SHELL_CMD_ARG(dump_reg, NULL,
		      "Universal read command for RTQ3700FH.\n"
		      "Usage: imvp read <flag> <reg>\n"
		      "Flags:\n"
		      "  -b   : Byte dump_reg (8-bit)\n"
		      "  -w   : Word dump_reg (16-bit) [PMBus Default]\n"
		      "  -blk : Block dump_reg (dynamic length)\n"
		      "Examples:\n"
		      "  imvp dump_reg -b 0x00\n"
		      "  imvp dump_reg -w 0x21\n"
		      "  imvp dump_reg -blk 0x90",
		      cmd_rtq3700fh_dump_reg, 3, 0),
	SHELL_CMD(update, NULL, "Update IMVP controller\n",
		  cmd_rtq3700fh_update),
	SHELL_SUBCMD_SET_END /* Array terminated. */
);

SHELL_CMD_REGISTER(imvp, &sub_imvp_cmds, "RTQ3700FH commands", NULL);
#endif /* CONFIG_IMVP_RTQ3700FH_CONSOLE */
