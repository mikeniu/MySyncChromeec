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

#define DT_DRV_COMPAT richtek_rtq3700fh

#define RTQ3700FH_PAGE_INVALID 0xFF
#define RTQ3700FH_LAST_PAGE RTQ3700FH_PAGE_VCC_LP_ECORE /* 0x03 */

/* Standard PMBus registers go up to 0xFF, specific limit depends on IC */
#define RTQ3700FH_LAST_PAGE_REGISTER 0xFF

BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) == 1,
	     "only one 'richtek,rtq3700fh' compatible node may be present");

/* Get raw data from devicetree (Expected Format: 0x[Page][Reg][Val_Hi][Val_Lo])
 */
static const uint32_t raw_data[] = DT_PROP(DT_DRV_INST(0), update_data);

struct rtq3700fh_data_t {
	uint8_t cur_page;
};

struct rtq3700fh_config_t {
	struct i2c_dt_spec i2c;
	struct gpio_dt_spec enable_gpio;
};

const struct rtq3700fh_config_t *config;

static struct rtq3700fh_data_t data0 = {
	.cur_page = RTQ3700FH_PAGE_INVALID,
};

static const struct rtq3700fh_config_t config0 = {
	.i2c = I2C_DT_SPEC_INST_GET(0),
	.enable_gpio = GPIO_DT_SPEC_GET(DT_DRV_INST(0), enable_gpios),
};

#ifdef CONFIG_IMVP_FACTORY_UPDATE
static void imvp_init_cb(const struct device *dev,
			 const enum ap_pwrseq_state entry,
			 const enum ap_pwrseq_state exit);
#endif

static int rtq3700fh_update(const struct device *dev);

static int rtq3700fh_init(const struct device *dev)
{
	config = dev->config;

#if defined(CONFIG_IMVP_FACTORY_UPDATE)
	static struct ap_pwrseq_state_callback ap_pwrseq_imvp_cb;
	const struct device *ap_pwrseq_dev = ap_pwrseq_get_instance();

	ap_pwrseq_imvp_cb.cb = imvp_init_cb;
	ap_pwrseq_imvp_cb.states_bit_mask = BIT(AP_POWER_STATE_G3);
	ap_pwrseq_register_state_exit_callback(ap_pwrseq_dev,
					       &ap_pwrseq_imvp_cb);
#endif

	return 0;
}

DEVICE_DT_INST_DEFINE(0, rtq3700fh_init, NULL, &data0, &config0, POST_KERNEL,
		      CONFIG_APPLICATION_INIT_PRIORITY, NULL);

static const struct device *rtq3700fh_dev = DEVICE_DT_GET(DT_DRV_INST(0));

static int rtq3700fh_apply_update_data(const struct device *dev)
{
	int rv = 0;
	int prev_page = -1;
	struct rtq3700fh_info update_entries;

	if (!chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
		return -EINVAL;
	}

	/* Apply each register update from the raw_data array */
	for (size_t i = 0; i < ARRAY_SIZE(raw_data); i++) {
		/* Unpack 32-bit entry: [Page: 8][Reg: 8][Val: 16] */
		update_entries.page = (raw_data[i] >> 24) & 0xFF;
		update_entries.reg = (raw_data[i] >> 16) & 0xFF;
		update_entries.val = raw_data[i] & 0xFFFF;

		if (prev_page != update_entries.page) {
			k_msleep(1);

			rv = rtq3700fh_set_page(dev, update_entries.page);
			if (rv)
				return rv;
			prev_page = update_entries.page;
		}

		rv = rtq3700fh_write_reg(dev, update_entries.reg,
					 update_entries.val);
		if (rv)
			return rv;

		LOG_DBG("page:0x%x Reg:0x%x Data:0x%04x ", update_entries.page,
			update_entries.reg, update_entries.val);
	}

	return rv;
}

static int rtq3700fh_crc_check(const struct device *dev)
{
	uint16_t nvm_stat;
	int rv;

	if (!chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
		return -EINVAL;
	}

	/* RTQ3700FH Checksum fail flag is BIT(0) of 0xED */
	rv = rtq3700fh_read_reg(dev, NVM_STAT_REG, &nvm_stat);
	if (rv < 0) {
		return rv;
	}

	if ((nvm_stat & BIT(0)) == 0) {
		LOG_DBG("Good NVM Checksum");
		return EC_SUCCESS;
	}

	LOG_ERR("NVM Checksum Failed! (0xED = 0x%04x)", nvm_stat);
	return -EINVAL;
}

static int rtq3700fh_lock_nvm(const struct device *dev)
{
	/* Implement Richtek specific lock sequence if applicable */
	LOG_DBG("NVM Lock Placeholder");
	return 0;
}

static int rtq3700fh_nvm_program_status(const struct device *dev, int stat_bit)
{
	int retry_cnt = 0;
	int retry_max;
	int rv = 1;
	uint16_t status_reg;

	if (stat_bit == NVM_RELOAD_STAT_BIT || stat_bit == -1)
		retry_max = 4;
	else
		retry_max = 20;

	while (retry_cnt < retry_max) {
		rv = rtq3700fh_read_reg(dev, NVM_STAT_REG, &status_reg);

		if (!rv) {
			if (stat_bit == -1) { /* Full Ready Check */
				if ((status_reg & 0xFF) == NVM_READY_STAT_VAL) {
					LOG_DBG("Match to NVM_READY_STAT_VAL (0xE0)");
					return EC_SUCCESS;
				}
			} else {
				/* Note: Guide says BIT=0 means NOT done. So
				 * BIT=1 is done. */
				if (status_reg & BIT(stat_bit)) {
					LOG_DBG("NVM Action %d Done", stat_bit);
					return EC_SUCCESS;
				}
			}
			rv = -EINVAL;
		}
		retry_cnt++;
		k_msleep(1);
	}

	return rv;
}

static int rtq3700fh_update(const struct device *dev)
{
	int rv = 0;
	uint16_t reg_val = 0;

	/* Mock unlock seq, adapt to actual spec */
	const uint8_t config_seq[] = { 0x24, 0x54, 0x02 };

	/* 1. Check Reload Done (BIT 7) */
	if (rtq3700fh_nvm_program_status(dev, NVM_RELOAD_STAT_BIT) !=
	    EC_SUCCESS)
		return -EINVAL;

	if (rtq3700fh_set_cfg_mode(dev, config_seq, 3) != EC_SUCCESS) {
		LOG_ERR("Unlock IMVP Failure");
		return -EINVAL;
	}

	k_msleep(1);

	/* 2. Check Device ID */
	rv = rtq3700fh_read_reg(dev, PRODUCT_ID_REG, &reg_val);
	if (rv || reg_val != PRODUCT_ID) {
		LOG_ERR("Wrong Product Id: 0x%04x", reg_val);
		goto lock_imvp;
	}

	k_msleep(1);

	/* 3. Check CRC/NVM status */
	rv = rtq3700fh_crc_check(dev);
	if (!rv) {
		LOG_INF(" No IMVP update needed");
		goto lock_imvp;
	}

	/* 4. Apply Updates */
	rv = rtq3700fh_apply_update_data(dev);
	if (rv) {
		LOG_ERR("Data update Failed");
		goto lock_imvp;
	}

	/* 5. Program NVM */
	rtq3700fh_store_config(dev);
	k_msleep(800);
	rv = rtq3700fh_nvm_program_status(dev, NVM_PRGRM_FINISH_STAT_BIT);
	if (rv) {
		LOG_ERR("NVM Programming Failed");
		goto lock_imvp;
	}

	/* 6. Restore/Reload NVM */
	rtq3700fh_load_config(dev);
	k_msleep(100);

	/* -1 checks for exact 0xE0 status */
	rv = rtq3700fh_nvm_program_status(dev, -1);
	if (rv) {
		LOG_ERR("NVM Relaoding Failed");
		goto lock_imvp;
	}

	rv = rtq3700fh_crc_check(dev);
	if (rv)
		LOG_ERR("CRC Match Failed post-update");

lock_imvp:
	if (rtq3700fh_lock_nvm(dev))
		LOG_ERR("Lock_Faied");

	return rv;
}

static void rtq3700fh_initiate_update(const struct device *dev)
{
	gpio_pin_configure_dt(&config->enable_gpio, GPIO_OUTPUT);
	gpio_pin_set_dt(&config->enable_gpio, 1);

	k_msleep(30);

	if (!rtq3700fh_update(rtq3700fh_dev))
		LOG_INF("IMVP Update Success!");
	else
		LOG_ERR("IMVP update Failed!");
}

#if defined(CONFIG_IMVP_FACTORY_UPDATE)
static void imvp_init_cb(const struct device *dev,
			 const enum ap_pwrseq_state entry,
			 const enum ap_pwrseq_state exit)
{
	if (entry == AP_POWER_STATE_S5) {
		rtq3700fh_initiate_update(rtq3700fh_dev);
	}
}
#endif

/* PMBus is 16-bit (Word) oriented */
int rtq3700fh_read_reg(const struct device *dev, uint8_t reg, uint16_t *val)
{
	const struct rtq3700fh_config_t *config = dev->config;
	uint8_t buf[2];
	int ret;

	ret = i2c_burst_read_dt(&config->i2c, reg, buf, sizeof(buf));
	if (ret == 0) {
		*val = sys_get_le16(buf);
	}
	return ret;
}

int rtq3700fh_write_reg(const struct device *dev, uint8_t reg, uint16_t val)
{
	const struct rtq3700fh_config_t *config = dev->config;
	uint8_t buf[3];

	buf[0] = reg;
	sys_put_le16(val, &buf[1]);

	return i2c_write_dt(&config->i2c, buf, sizeof(buf));
}

int rtq3700fh_set_cfg_mode(const struct device *dev, const uint8_t *seq,
			   uint8_t seq_len)
{
	/* Custom implementation as per exact spec */
	return 0;
}

int rtq3700fh_set_page(const struct device *dev, uint8_t page)
{
	struct rtq3700fh_data_t *data = dev->data;
	int rv;

	if (!chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
		return -EINVAL;
	}

	/* PAGE register is generally an 8-bit command in PMBus */
	rv = i2c_reg_write_byte_dt(&config->i2c, PAGE_SET_REG, page);
	if (rv) {
		return rv;
	}

	data->cur_page = page;
	return rv;
}

int rtq3700fh_load_config(const struct device *dev)
{
	if (!chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
		return -EINVAL;
	}
	/* Use PMBus RESTORE_USER_ALL (0x12) or Richtek custom 0xED command */
	return i2c_reg_write_byte_dt(&config->i2c, 0x12, 0x00);
}

int rtq3700fh_store_config(const struct device *dev)
{
	if (!chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
		return -EINVAL;
	}
	/* Use PMBus STORE_USER_ALL (0x15) or Richtek custom 0xED command */
	return i2c_reg_write_byte_dt(&config->i2c, 0x15, 0x00);
}

#ifdef CONFIG_IMVP_RT3645_CONSOLE

static int cmd_rtq3700fh_enter_cfg_mode(const struct shell *sh, size_t argc,
					char **argv)
{
	uint8_t unlock_seq[4];
	uint8_t seq_len;

	if (!chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
		shell_error(sh,
			    "Can not change regsiters while higher than S5\n");
		return -EINVAL;
	}

	seq_len = 0;
	for (int i = 1; i < argc; i++, seq_len++) {
		unlock_seq[i - 1] = strtol(argv[i], NULL, 0);
	}
	return rtq3700fh_set_cfg_mode(rtq3700fh_dev, unlock_seq, seq_len);
}

static int cmd_rtq3700fh_load(const struct shell *sh, size_t argc, char **argv)
{
	if (!chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
		shell_error(sh,
			    "Can not change regsiters while higher than S5");
		return -EINVAL;
	}
	return rtq3700fh_load_config(rtq3700fh_dev);
}

static int cmd_rtq3700fh_store(const struct shell *sh, size_t argc, char **argv)
{
	if (!chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
		shell_error(sh,
			    "Can not change regsiters while higher than S5");
		return -EINVAL;
	}
	return rtq3700fh_store_config(rtq3700fh_dev);
}

static int cmd_rtq3700fh_set_page(const struct shell *sh, size_t argc,
				  char **argv)
{
	unsigned int arg;

	if (!chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
		shell_error(sh,
			    "Can not change regsiters while higher than S5");
		return -EINVAL;
	}
	arg = (unsigned int)strtol(argv[1], NULL, 0);

	if (arg > RTQ3700FH_LAST_PAGE) {
		shell_error(sh, "Page number out of range");
		return -EINVAL;
	}

	return rtq3700fh_set_page(rtq3700fh_dev, arg);
}

static void dump_reg_range(const struct shell *sh, int low, int high)
{
	uint8_t reg;
	uint16_t regval;
	int rv;

	for (reg = low; reg <= high; reg++) {
		rv = rtq3700fh_read_reg(rtq3700fh_dev, reg, &regval);
		if (!rv)
			shell_fprintf(sh, SHELL_INFO, "[%02Xh] = 0x%04X\n", reg,
				      regval);
		else
			shell_fprintf(sh, SHELL_INFO, "ERROR [%Xh]\n", reg);
	}
}

static int cmd_rtq3700fh_dump_regs(const struct shell *sh, size_t argc,
				   char **argv)
{
	struct rtq3700fh_data_t *data = rtq3700fh_dev->data;

	dump_reg_range(sh, 0x99, 0x99); /* MFR_ID */
	dump_reg_range(sh, 0xAD, 0xAD); /* PRODUCT_ID */
	dump_reg_range(sh, 0xED, 0xED); /* NVM_STAT */

	if (!chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
		shell_print(sh,
			    "To access paged registers put system on G3/S5");
		return -EINVAL;
	}

	if (data->cur_page == RTQ3700FH_PAGE_INVALID) {
		shell_print(sh, "No active page set");
		return 0;
	}

	shell_fprintf(sh, SHELL_INFO, "Page: 0x%X\n", (int)data->cur_page);
	dump_reg_range(sh, 0x00, RTQ3700FH_LAST_PAGE_REGISTER);

	return 0;
}

static int cmd_rtq3700fh_set_regs(const struct shell *sh, size_t argc,
				  char **argv)
{
	struct rtq3700fh_data_t *data = rtq3700fh_dev->data;
	uint8_t reg;
	uint16_t val;
	int rv = 0;

	if (!chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
		shell_error(sh,
			    "Can not change regsiters while higher than S5");
		return -EINVAL;
	}

	if (data->cur_page == RTQ3700FH_PAGE_INVALID) {
		shell_error(sh, "Not active page set");
		return -EINVAL;
	}

	reg = strtol(argv[1], NULL, 0);

	for (int i = 2; (i < argc) && (reg <= RTQ3700FH_LAST_PAGE_REGISTER);
	     i++, reg++) {
		if (*argv[i] == '-') {
			continue;
		}

		val = strtol(argv[i], NULL, 0);
		rv = rtq3700fh_write_reg(rtq3700fh_dev, reg, val);
		if (rv)
			break;
	}

	return rv;
}

static int cmd_rtq3700fh_update(const struct shell *sh, size_t argc,
				char **argv)
{
	chipset_force_shutdown(CHIPSET_SHUTDOWN_G3);
	LOG_INF("System shutting_down for IMVP update");

	k_msleep(100);

	rtq3700fh_initiate_update(rtq3700fh_dev);

	gpio_pin_set_dt(&config->enable_gpio, 0);

	LOG_INF("Press powerbutton/enter 'powerbtn' to boot system");

	return EC_SUCCESS;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	sub_imvp_cmds,
	SHELL_CMD_ARG(cfg_mode, NULL,
		      SHELL_HELP("Enter password to set IMVP chip in config mode"
				 "<Password sequence>",
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
	SHELL_CMD_ARG(set_regs, NULL,
		      SHELL_HELP("Sets chip paged registers",
				 "<start_reg> [value | - ]"),
		      cmd_rtq3700fh_set_regs, 2, 13),
	SHELL_CMD(dump_regs, NULL,
		  "Dump registers, content depends on current page\n",
		  cmd_rtq3700fh_dump_regs),
	SHELL_CMD(update, NULL, "Update IMVP controller\n",
		  cmd_rtq3700fh_update),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(imvp_rtq3700fh, &sub_imvp_cmds, "RTQ3700FH commands", NULL);
#endif /* CONFIG_IMVP_RT3645_CONSOLE */
