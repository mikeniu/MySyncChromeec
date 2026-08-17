/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* DTS-configured I2C passthru driver for Zephyr EC */

#define DT_DRV_COMPAT cros_ec_i2c_passthru

#include "battery.h"
#include "console.h"
#include "host_command.h"
#include "i2c.h"
#include "i2c_msg_queue.h"
#include "system.h"
#include "util.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(i2c_dts_passthru, CONFIG_I2C_DTS_PASSTHRU_LOG_LEVEL);

#define I2C_PASSTHRU_MAX_MSGS 16

#define I2C_PASSTHRU_DEV                                      \
	COND_CODE_1(DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT), \
		    (DEVICE_DT_GET_OR_NULL(DT_DRV_INST(0))), (NULL))

struct i2c_passthru_port_data {
	bool port_protected;
};

struct i2c_passthru_port_config {
	int remote_port;
	const struct device *i2c_dev;
	const uint8_t *devices;
	size_t num_devices;
	struct i2c_passthru_port_data *data;
};

struct i2c_passthru_config {
	const struct i2c_passthru_port_config *ports;
	size_t num_ports;
};

static const struct i2c_passthru_port_config *
find_i2c_passthru_port_config(const struct device *dev, int remote_port)
{
	if (!dev)
		return NULL;

	const struct i2c_passthru_config *cfg = dev->config;

	for (size_t i = 0; i < cfg->num_ports; i++) {
		if (cfg->ports[i].remote_port == remote_port)
			return &cfg->ports[i];
	}
	return NULL;
}

static int i2c_allow_passthru(const struct device *dev,
			      const struct i2c_passthru_port_config *port_cfg,
			      const struct ec_params_i2c_passthru *params)
{
	if (!system_is_locked())
		return 1;

	if (!dev || !port_cfg)
		return 0;

	if (port_cfg->data->port_protected)
		return 0;

	if (port_cfg->num_devices > 0) {
		for (int i = 0; i < params->num_msgs; i++) {
			uint8_t target_addr =
				I2C_STRIP_FLAGS(params->msg[i].addr_flags);
			bool found = false;

			for (size_t d = 0; d < port_cfg->num_devices; d++) {
				if (port_cfg->devices[d] == target_addr) {
					found = true;
					break;
				}
			}
			if (!found)
				return 0;
		}
	}

	return 1;
}

static int check_i2c_params(const struct host_cmd_handler_args *args)
{
	const struct ec_params_i2c_passthru *params = args->params;
	struct msg_queue_t msg_queue;
	unsigned int size;

	if (args->params_size < sizeof(*params)) {
		LOG_DBG("no params, params_size=%d, need at least %d",
			args->params_size, sizeof(*params));
		return EC_RES_INVALID_PARAM;
	}
	size = sizeof(*params) + params->num_msgs * sizeof(*params->msg);
	if (args->params_size < size) {
		LOG_DBG("params_size=%d, need at least %d", args->params_size,
			size);
		return EC_RES_INVALID_PARAM;
	}

	/* Loop and process messages */
	for (msg_queue = msg_queue_create(params);
	     msg_queue.msg < msg_queue.msg_end;) {
		const struct msg_value_t val = msg_queue_front(&msg_queue);
		msg_queue_pop_front(&msg_queue, &val);
	}

	/* Check there is room for the data */
	if (args->response_max <
	    sizeof(struct ec_response_i2c_passthru) + msg_queue.in_len) {
		LOG_DBG("overflow1");
		return EC_RES_INVALID_PARAM;
	}

	/* Must have bytes to write */
	if (args->params_size < size + msg_queue.out_len) {
		LOG_DBG("overflow2");
		return EC_RES_INVALID_PARAM;
	}

	return EC_RES_SUCCESS;
}

static enum ec_status i2c_command_passthru(struct host_cmd_handler_args *args)
{
	const struct ec_params_i2c_passthru *params =
		(const struct ec_params_i2c_passthru *)args->params;
	const uint8_t remote_port = params->port;
	const struct device *dev = I2C_PASSTHRU_DEV;
	const struct i2c_passthru_port_config *port_cfg =
		find_i2c_passthru_port_config(dev, remote_port);
	const struct device *i2c_dev = NULL;
	struct ec_response_i2c_passthru *resp = args->response;
	struct msg_queue_t msg_queue;
	struct i2c_msg msgs[I2C_PASSTHRU_MAX_MSGS];
	int ret;

	if (port_cfg != NULL) {
		i2c_dev = port_cfg->i2c_dev;
	} else {
		/* TODO: Add remote mapping for i2c_device that is not in the
		 * list. Unlocked system should allow passthru even if
		 * unconfigured in DTS
		 */
		return EC_RES_ACCESS_DENIED;
	}

#ifdef CONFIG_BATTERY_CUT_OFF
	/* Some batteries would wake up after cut-off if we talk to it. */
	if (battery_is_cut_off())
		return EC_RES_ACCESS_DENIED;
#endif

	ret = check_i2c_params(args);
	if (ret)
		return ret;

	if (!i2c_allow_passthru(dev, port_cfg, params))
		return EC_RES_ACCESS_DENIED;

	/* Loop and process messages */
	resp->i2c_status = 0;
	resp->num_msgs = 0;

	msg_queue = msg_queue_create(params);

	ret = zephyr_i2c_xfer_i2c_message_queue(i2c_dev, &msg_queue, msgs,
						ARRAY_SIZE(msgs), resp->data);
	switch (ret) {
	case EC_SUCCESS:
		resp->num_msgs = params->num_msgs;
		break;
	case EC_ERROR_TIMEOUT:
		msg_queue.in_len = 0;
		resp->i2c_status = EC_I2C_STATUS_TIMEOUT;
		break;
	default:
		msg_queue.in_len = 0;
		resp->i2c_status = EC_I2C_STATUS_NAK;
		break;
	}

	args->response_size = sizeof(*resp) + msg_queue.in_len;
	/*
	 * Return success even if transfer failed so response is sent.  Host
	 * will check message status to determine the transfer result.
	 */
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_I2C_PASSTHRU, i2c_command_passthru, EC_VER_MASK(0));

__test_only void i2c_passthru_protect_reset(void)
{
	const struct device *dev = I2C_PASSTHRU_DEV;

	if (!dev)
		return;

	const struct i2c_passthru_config *cfg = dev->config;

	for (size_t i = 0; i < cfg->num_ports; i++) {
		cfg->ports[i].data->port_protected = false;
	}
}

static void i2c_passthru_protect_all_ports(const struct device *dev)
{
	if (!dev)
		return;

	const struct i2c_passthru_config *cfg = dev->config;

	for (size_t i = 0; i < cfg->num_ports; i++) {
		cfg->ports[i].data->port_protected = true;
	}
}

static enum ec_status i2c_passthru_protect_port(const struct device *dev,
						int remote_port)
{
	const struct i2c_passthru_port_config *port_cfg =
		find_i2c_passthru_port_config(dev, remote_port);

	if (port_cfg != NULL) {
		port_cfg->data->port_protected = true;
		return EC_RES_SUCCESS;
	} else {
		LOG_DBG("Invalid I2C port %d to be protected", remote_port);
		return EC_RES_INVALID_PARAM;
	}
}

static enum ec_status
i2c_command_passthru_protect(struct host_cmd_handler_args *args)
{
	const struct ec_params_i2c_passthru_protect *params =
		(const struct ec_params_i2c_passthru_protect *)args->params;
	const uint8_t remote_port = params->port;
	struct ec_response_i2c_passthru_protect *resp = args->response;
	const struct device *dev = I2C_PASSTHRU_DEV;

	if (args->params_size < sizeof(*params)) {
		LOG_DBG("protect no params, params_size=%d", args->params_size);
		return EC_RES_INVALID_PARAM;
	}

	switch (params->subcmd) {
	case EC_CMD_I2C_PASSTHRU_PROTECT_ENABLE_ALL:
		i2c_passthru_protect_all_ports(dev);
		return EC_RES_SUCCESS;

	case EC_CMD_I2C_PASSTHRU_PROTECT_ENABLE:
		return i2c_passthru_protect_port(dev, remote_port);

	case EC_CMD_I2C_PASSTHRU_PROTECT_STATUS: {
		if (args->response_max < sizeof(*resp)) {
			LOG_DBG("protect no response, "
				"response_max=%d, need at least %d",
				args->response_max, sizeof(*resp));
			return EC_RES_INVALID_PARAM;
		}

		const struct i2c_passthru_port_config *port_cfg =
			find_i2c_passthru_port_config(dev, remote_port);
		if (port_cfg == NULL) {
			LOG_DBG("remote port invalid %d", remote_port);
			return EC_RES_INVALID_PARAM;
		}

		resp->status = port_cfg->data->port_protected;
		args->response_size = sizeof(*resp);
		return EC_RES_SUCCESS;
	}

	default:
		return EC_RES_INVALID_COMMAND;
	}
}
DECLARE_HOST_COMMAND(EC_CMD_I2C_PASSTHRU_PROTECT, i2c_command_passthru_protect,
		     EC_VER_MASK(0));

#ifdef CONFIG_CMD_I2C_DTS_PROTECT
static int command_i2cprotect(int argc, const char **argv)
{
	const struct device *dev = I2C_PASSTHRU_DEV;

	if (!dev) {
		ccprintf("I2C passthru device not found\n");
		return EC_ERROR_UNKNOWN;
	}

	const struct i2c_passthru_config *cfg = dev->config;

	if (argc == 1) {
		for (size_t index = 0; index < cfg->num_ports; index++) {
			const struct i2c_passthru_port_config *port_cfg =
				&cfg->ports[index];
			ccprintf("Remote Port %d Port %p: %sprotected\n",
				 port_cfg->remote_port, port_cfg->i2c_dev,
				 port_cfg->data->port_protected ? "" : "un");
		}
		return EC_SUCCESS;
	} else if (argc == 2) {
		enum ec_status ret;
		int remote_port;
		char *e;

		remote_port = strtoi(argv[1], &e, 0);
		if (*e)
			return EC_ERROR_PARAM1;

		ret = i2c_passthru_protect_port(dev, remote_port);
		if (ret != 0) {
			ccprintf(
				"i2c passthru protect invalid remote_port %d\n",
				remote_port);
			return EC_ERROR_PARAM1;
		}
		return EC_SUCCESS;
	} else {
		return EC_ERROR_PARAM_COUNT;
	}
}
DECLARE_CONSOLE_COMMAND(i2cprotect, command_i2cprotect, "[remote_port]",
			"Protect I2C bus");
#endif

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

#define PASSTHRU_CHILD_DEVICES(node_id)                         \
	COND_CODE_1(DT_NODE_HAS_PROP(node_id, devices),         \
		    (static const uint8_t devices_##node_id[] = \
			     DT_PROP(node_id, devices);),       \
		    ())

#define PASSTHRU_CHILD_PORT_DATA(node_id) \
	static struct i2c_passthru_port_data port_data_##node_id;

#define PASSTHRU_CHILD_PORT_CONFIG(node_id)                                    \
	{                                                                      \
		.remote_port = COND_CODE_1(DT_NODE_HAS_PROP(node_id, reg),     \
					   (DT_REG_ADDR(node_id)), (-1)),      \
		.i2c_dev = DEVICE_DT_GET(DT_PHANDLE(node_id, i2c_port)),       \
		.devices = COND_CODE_1(DT_NODE_HAS_PROP(node_id, devices),     \
				       (devices_##node_id), (NULL)),           \
		.num_devices = COND_CODE_1(DT_NODE_HAS_PROP(node_id, devices), \
					   (DT_PROP_LEN(node_id, devices)),    \
					   (0)),                               \
		.data = &port_data_##node_id,                                  \
	},

#define I2C_DTS_PASSTHRU_INST_DEFINE(inst)                                  \
	DT_INST_FOREACH_CHILD_STATUS_OKAY(inst, PASSTHRU_CHILD_DEVICES)     \
	DT_INST_FOREACH_CHILD_STATUS_OKAY(inst, PASSTHRU_CHILD_PORT_DATA)   \
	static const struct i2c_passthru_port_config                        \
		i2c_passthru_ports_##inst[] = {                             \
			DT_INST_FOREACH_CHILD_STATUS_OKAY(                  \
				inst, PASSTHRU_CHILD_PORT_CONFIG)           \
		};                                                          \
	static const struct i2c_passthru_config i2c_passthru_cfg_##inst = { \
		.ports = i2c_passthru_ports_##inst,                         \
		.num_ports = ARRAY_SIZE(i2c_passthru_ports_##inst),         \
	};                                                                  \
	static int i2c_dts_passthru_init(const struct device *dev)          \
	{                                                                   \
		ARG_UNUSED(dev);                                            \
		return 0;                                                   \
	}                                                                   \
	DEVICE_DT_INST_DEFINE(inst, i2c_dts_passthru_init, NULL, NULL,      \
			      &i2c_passthru_cfg_##inst, POST_KERNEL,        \
			      CONFIG_KERNEL_INIT_PRIORITY_DEVICE, NULL);

DT_INST_FOREACH_STATUS_OKAY(I2C_DTS_PASSTHRU_INST_DEFINE)

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
