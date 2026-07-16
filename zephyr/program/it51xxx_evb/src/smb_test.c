/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "hooks.h"
#include "task.h"
#include "i2c.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/sys/printk.h>

#include <zephyr/drivers/smbus.h>
#define SMBUS_DEV_NODE DT_NODELABEL(smbus2)

#undef HOST_NOTIFY_TEST
#define PCALL_CMD_PEC_TEST
#undef BLOCK_CMD_TEST
#undef SMBALERT_TEST
#undef WORD_DATA_TEST

#ifdef HOST_NOTIFY_TEST
#define I2C_DEV_NODE DT_NODELABEL(i2c4)
//#define SMBUS_DEV_NODE_HOST DT_NODELABEL(smbus4)
static void host_notify_cb_handler(const struct device *dev, struct smbus_callback *cb, uint8_t addr)
{
	ARG_UNUSED(cb);

	//printk("\n%s: Host Notify fired, addr=0x%02x\n", dev->name, addr);
}

static int smbus_host_notify_scenario(const struct device *smbus, const struct device *i2c)
{
	int ret;
	//uint8_t byte;
	uint16_t word;
	uint8_t count;
	uint8_t buf[32];

	/* Word Data Read */
	//ret = smbus_word_data_read(smbus, 0x52, 0x12, &word);
	//if (ret) {
	//	return ret;
	//}

	/* Host Notify #1 */
	uint8_t notify1[] = {0xa0, 0x11, 0x22};
	ret = i2c_write(i2c, notify1, sizeof(notify1), 0x08);
	//ret = smbus_word_data_write(smb_host, 0x08, 0xa0, 0x1122);
	if (ret) {
		return ret;
	}

	/* Byte Data Read */
	//ret = smbus_byte_data_read(smbus, 0x52, 0x13, &byte);
	//ret = smbus_byte_data_write(smbus, 0x52, 0x13, 0x77);
	//ret = smbus_byte_read(smbus, 0x52, &byte);
	ret = smbus_byte_write(smbus, 0x52, 0x77);
	if (ret) {
		return ret;
	}

	/* Host Notify #2 */
	uint8_t notify2[] = {0xa0, 0x33, 0x44};
	ret = i2c_write(i2c, notify2, sizeof(notify2), 0x08);
	//ret = smbus_word_data_write(smb_host, 0x08, 0xa0, 0x3344);
	if (ret) {
		return ret;
	}
#if 1
	/* Process Call */
	ret = smbus_pcall(smbus, 0x52, 0x20, 0x5678, &word);
	if (ret) {
		return ret;
	}

	/* Host Notify #3 */
	uint8_t notify3[] = {0xa0, 0x55, 0x66};
	ret = i2c_write(i2c, notify3, sizeof(notify3), 0x08);
	if (ret) {
		return ret;
	}

	/* Block Read */
	ret = smbus_block_read(smbus, 0x52, 0x30, &count, buf);
	if (ret) {
		return ret;
	}

	/* Host Notify #4 */
	uint8_t notify4[] = {0xa0, 0x77, 0x88};
	ret = i2c_write(i2c, notify4, sizeof(notify4), 0x08);
#endif
	return ret;
}
#endif

#ifdef SMBALERT_TEST
static void host_alert_cb_handler(const struct device *dev, struct smbus_callback *cb, uint8_t addr)
{
	ARG_UNUSED(cb);

	printk("%s: Host alert fired, addr=0x%02x\n", dev->name, addr);
}
#endif

static int test_smbus_host(void)
{
	const struct device *smbus_dev = DEVICE_DT_GET(SMBUS_DEV_NODE);
	int ret = 0;

#ifdef WORD_DATA_TEST
	uint16_t rx_word;

	ret = smbus_word_data_read(smbus_dev, 0x52, 0x12, &rx_word);
	printk("SMBus: rx_word: 0x%04x\n",rx_word);
	if (ret < 0) {
		printk("SMBus: Failed to read from periph\n");
	}

	smbus_configure(smbus_dev, SMBUS_MODE_CONTROLLER);
#endif

#ifdef HOST_NOTIFY_TEST
	const struct device *i2c_dev = DEVICE_DT_GET(I2C_DEV_NODE);
	//const struct device *smb_host_dev = DEVICE_DT_GET(SMBUS_DEV_NODE_HOST);

	static struct smbus_callback notify_cb = {
		.handler = host_notify_cb_handler,
		.addr = 0x50,
	};

	smbus_host_notify_remove_cb(smbus_dev, &notify_cb);
	smbus_host_notify_set_cb(smbus_dev, &notify_cb);

	printk("SMBus: host notify test\n");

	smbus_configure(smbus_dev, SMBUS_MODE_CONTROLLER | SMBUS_MODE_HOST_NOTIFY);

	ret = smbus_host_notify_scenario(smbus_dev, i2c_dev);
	if (ret < 0) {
		printk("SMBus: Failed test host notify=%d\n",ret);
	}

	//smbus_configure(smbus_dev, SMBUS_MODE_CONTROLLER);

#endif

#ifdef PCALL_CMD_PEC_TEST
	uint16_t rx_word;
	//uint8_t out;

	smbus_configure(smbus_dev, SMBUS_MODE_CONTROLLER | SMBUS_MODE_PEC);

	//ret = smbus_byte_data_read(smbus_dev, 0x52, 0x12, &out);
	//printk("SMBus: out: 0x%02x\n",out);

	ret = smbus_pcall(smbus_dev, 0x52, 0x12, 0x5634, &rx_word);
	printk("SMBus: pcall rx_word: 0x%04x\n",rx_word);
	if (ret < 0) {
		printk("SMBus: Failed to read from periph\n");
	}

	smbus_configure(smbus_dev, SMBUS_MODE_CONTROLLER);
#endif

#ifdef BLOCK_CMD_TEST
	uint8_t out_buf[32];
	uint8_t out_count = 0;

	ret = smbus_block_read(smbus_dev, 0x52, 0x12, &out_count, out_buf);
	if (ret < 0) {
		printk("SMBus: Failed to read from periph\n");
	}
#endif
#ifdef SMBALERT_TEST
	const struct device *smbus_dev = DEVICE_DT_GET(SMBUS_DEV_NODE);
	static struct smbus_callback cb = {
		.handler = host_alert_cb_handler,
		.addr = 0x52,
	};

	smbus_smbalert_remove_cb(smbus_dev, &cb);
	smbus_smbalert_set_cb(smbus_dev, &cb);
	smbus_configure(smbus_dev, SMBUS_MODE_CONTROLLER | SMBUS_MODE_SMBALERT);
#endif
	return ret;
}

static void board_init(void)
{
	int ret;

	printf("[main]===test_smbus_host to target===\n");
	ret = test_smbus_host();
	if(ret)
		printf("[main]fail to access\n");
	else
		printf("[main]smbus transfer sucess!\n\n\n");

}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_POST_I2C+1);
