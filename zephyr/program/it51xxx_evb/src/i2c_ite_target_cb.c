/*
 * Copyright (c) 2025 ITE Corporation. All Rights Reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT ite_target_i2c

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>
#include <zephyr/pm/policy.h>
#include <errno.h>
#include <soc.h>
#include <soc_dt.h>
#include <zephyr/sys/util.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(i2c_ite_target, 4);

struct i2c_target_dev_config {
	/* I2C alternate configuration */
	struct i2c_dt_spec bus;
};

struct i2c_target_data {
	struct i2c_target_config config;
	uint32_t buffer_idx;
	uint8_t w_buffer[256];
	uint8_t r_buffer[256];
	uint8_t first_write;
};

static int i2c_ite_target_write_requested(struct i2c_target_config *config)
{
	struct i2c_target_data *data = CONTAINER_OF(config, struct i2c_target_data, config);

	//LOG_DBG("[target]: write req");

	data->first_write = true;

	return 0;
}

static int i2c_ite_target_read_requested(struct i2c_target_config *config, uint8_t *val)
{
	struct i2c_target_data *data = CONTAINER_OF(config, struct i2c_target_data, config);

	//LOG_DBG("[target]: read req");

	*val = data->r_buffer[data->buffer_idx++];

	return 0;
}

static int i2c_ite_target_write_received(struct i2c_target_config *config, uint8_t val)
{
	struct i2c_target_data *data = CONTAINER_OF(config, struct i2c_target_data, config);

	if (data->first_write) {
		//LOG_DBG("[target]target_write_received: val=%x, address=%x",val,config->address);
		data->first_write = false;
	} else {
		//printk("[i2c cb]val=%x\n",val);
		data->w_buffer[data->buffer_idx++] = val;
	}

	return 0;
}

static int i2c_ite_target_read_processed(struct i2c_target_config *config, uint8_t *val)
{
	struct i2c_target_data *data = CONTAINER_OF(config, struct i2c_target_data, config);

	*val = data->r_buffer[data->buffer_idx++];

	//LOG_DBG("[target]target_read_processed: read done, val=0x%x", *val);

	return 0;
}

static int i2c_ite_target_stop(struct i2c_target_config *config)
{
	struct i2c_target_data *data = CONTAINER_OF(config, struct i2c_target_data, config);

	//LOG_DBG("[target]: stop");

	data->buffer_idx = 0;
	data->first_write = true;

	return 0;
}

#ifdef CONFIG_I2C_TARGET_BUFFER_MODE
static void i2c_ite_target_buf_write_received(struct i2c_target_config *config, uint8_t *ptr,
					      uint32_t len)
{
	//LOG_DBG("[target]: w-ptr=%p, len=%d", ptr, len);
}
static int i2c_ite_target_buf_read_requested(struct i2c_target_config *config, uint8_t **ptr,
					     uint32_t *len)
{
	struct i2c_target_data *data = CONTAINER_OF(config, struct i2c_target_data, config);

	*len = sizeof(data->r_buffer);
	*ptr = data->r_buffer;

	//LOG_DBG("[target]: r-ptr=%p, len=%d", *ptr,*len);

	return 0;
}
#endif

static const struct i2c_target_callbacks target_callbacks = {
	.write_requested = i2c_ite_target_write_requested,
	.read_requested = i2c_ite_target_read_requested,
	.write_received = i2c_ite_target_write_received,
	.read_processed = i2c_ite_target_read_processed,
#ifdef CONFIG_I2C_TARGET_BUFFER_MODE
	.buf_write_received = i2c_ite_target_buf_write_received,
	.buf_read_requested = i2c_ite_target_buf_read_requested,
#endif
	.stop = i2c_ite_target_stop,
};

static int i2c_ite_target_register(const struct device *dev)
{
	const struct i2c_target_dev_config *cfg = dev->config;
	struct i2c_target_data *data = dev->data;

	//LOG_DBG("[target]i2c target register");

	return i2c_target_register(cfg->bus.bus, &data->config);
}

static int i2c_ite_target_unregister(const struct device *dev)
{
	const struct i2c_target_dev_config *cfg = dev->config;
	struct i2c_target_data *data = dev->data;

	//LOG_DBG("[target]i2c target unregister");

	return i2c_target_unregister(cfg->bus.bus, &data->config);
}

static const struct i2c_target_driver_api api_funcs = {
	.driver_register = i2c_ite_target_register,
	.driver_unregister = i2c_ite_target_unregister,
};

static int i2c_ite_target_init(const struct device *dev)
{
	const struct i2c_target_dev_config *cfg = dev->config;
	struct i2c_target_data *data = dev->data;

	//LOG_DBG("[target]i2c_target_init i2c target bus=%p",cfg->bus.bus); //I2C5 node
	//LOG_DBG("[target]i2c_target_init i2c target dev=%p",dev); //I2C target
	//LOG_DBG("[target]i2c_target_init i2c target data config=%p",&data->config);
	LOG_DBG("[target]i2c_target_init i2c target addr=%x",cfg->bus.addr);

	/* Check I2C controller ready. */
	if (!device_is_ready(cfg->bus.bus)) {
		LOG_ERR("I2C controller device not ready");
		return -ENODEV;
	}

	data->config.address = cfg->bus.addr;
	data->config.callbacks = &target_callbacks;

	LOG_DBG("[target]w_buffer=%p",data->w_buffer);
	LOG_DBG("[target]r_buffer=%p",data->r_buffer);

	for (int i = 0; i < 256; i++) {

		data->r_buffer[i] = 0xff-i;

	}

	return 0;
}

#define I2C_ITE_TARGET_INIT(inst)                                                                  \
	static const struct i2c_target_dev_config i2c_target_cfg_##inst = {                        \
		.bus = I2C_DT_SPEC_INST_GET(inst),			                           \
	};                                                                                         \
										                   \
	static struct i2c_target_data i2c_target_data_##inst;                                      \
										                   \
	I2C_DEVICE_DT_INST_DEFINE(inst, i2c_ite_target_init, NULL, &i2c_target_data_##inst,        \
				  &i2c_target_cfg_##inst, POST_KERNEL,                             \
				  CONFIG_I2C_TARGET_INIT_PRIORITY, &api_funcs);                    \

DT_INST_FOREACH_STATUS_OKAY(I2C_ITE_TARGET_INIT)
