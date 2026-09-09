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
#include "i2c_battery_parser.h"
#include "i2c_dts_passthru.h"
#include "i2c_msg_queue.h"
#include "system.h"
#include "util.h"
#include "virtual_battery.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(i2c_dts_passthru, CONFIG_I2C_DTS_PASSTHRU_LOG_LEVEL);

enum i2c_passthru_allow_type {
	ALLOW_IF_UNPROTECTED_BY_AP,
	VIRTUAL_BATTERY,
};

#define CHILD_HAS_VIRTUAL_BATTERY_OR(node_id)                        \
	|| COND_CODE_1(DT_NODE_HAS_PROP(node_id, allow_type),        \
		       (VIRTUAL_BATTERY ==                           \
			DT_STRING_UPPER_TOKEN(node_id, allow_type)), \
		       (0))

#define HAS_VIRTUAL_BATTERY                                     \
	(COND_CODE_1(DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT),  \
		     (0 DT_INST_FOREACH_CHILD_STATUS_OKAY(      \
			     0, CHILD_HAS_VIRTUAL_BATTERY_OR)), \
		     (0)))
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
	enum i2c_passthru_allow_type allow_type;
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

#define I2C_MAX_CONTROLLERS 16

static const struct device *i2c_controllers[I2C_MAX_CONTROLLERS];

static void init_i2c_controllers(void)
{
	const struct device *dev = I2C_PASSTHRU_DEV;

	if (dev != NULL) {
		const struct i2c_passthru_config *cfg = dev->config;

		for (size_t index = 0; index < cfg->num_ports; index++) {
			const struct i2c_passthru_port_config *port_cfg =
				&cfg->ports[index];

			if (port_cfg->remote_port >= 0 &&
			    port_cfg->remote_port <
				    ARRAY_SIZE(i2c_controllers)) {
				i2c_controllers[port_cfg->remote_port] =
					port_cfg->i2c_dev;
			}
		}
	}

	const struct device *devices;
	size_t num_devices = z_device_get_all_static(&devices);
	size_t controller_idx = 0;

	for (size_t i = 0; i < num_devices; i++) {
		const struct device *d = &devices[i];

		if (DEVICE_API_IS(i2c, d)) {
			while (controller_idx < ARRAY_SIZE(i2c_controllers) &&
			       i2c_controllers[controller_idx] != NULL) {
				controller_idx++;
			}
			if (controller_idx < ARRAY_SIZE(i2c_controllers)) {
				i2c_controllers[controller_idx++] = d;
			}
		}
	}
}

ZTESTABLE_STATIC const struct device *i2c_portmap(int remote_port)
{
	if (remote_port >= 0 && remote_port < ARRAY_SIZE(i2c_controllers)) {
		const struct device *i2c_dev = i2c_controllers[remote_port];

		if (i2c_dev != NULL && device_is_ready(i2c_dev)) {
			return i2c_dev;
		}
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

	switch (port_cfg->allow_type) {
	case ALLOW_IF_UNPROTECTED_BY_AP:
		if (port_cfg->data->port_protected)
			return 0;
		break;
#if HAS_VIRTUAL_BATTERY
	case VIRTUAL_BATTERY: {
		struct i2c_battery_parser_state parser_state =
			i2c_battery_parser_state_create();
		struct msg_queue_t msg_queue;

		for (msg_queue = msg_queue_create(params);
		     msg_queue.msg < msg_queue.msg_end;) {
			const struct msg_value_t val =
				msg_queue_front(&msg_queue);

			if (battery_permission_handler(
				    &parser_state, msg_queue.in_len,
				    val.xferflags, val.read_len, val.write_len,
				    msg_queue.out))
				return 0;

			msg_queue_pop_front(&msg_queue, &val);
		}
		break;
	}
#endif
	default:
		return 0;
	}

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

static enum ec_host_cmd_status
check_i2c_params(const struct ec_host_cmd_handler_args *args)
{
	const struct ec_params_i2c_passthru *params = args->input_buf;
	struct msg_queue_t msg_queue;
	unsigned int size;

	if (args->input_buf_size < sizeof(*params)) {
		LOG_DBG("no params, params_size=%d, need at least %d",
			args->input_buf_size, sizeof(*params));
		return EC_HOST_CMD_REQUEST_TRUNCATED;
	}
	size = sizeof(*params) + params->num_msgs * sizeof(*params->msg);
	if (args->input_buf_size < size) {
		LOG_DBG("params_size=%d, need at least %d",
			args->input_buf_size, size);
		return EC_HOST_CMD_INVALID_PARAM;
	}

	/* Loop and process messages */
	for (msg_queue = msg_queue_create(params);
	     msg_queue.msg < msg_queue.msg_end;) {
		const struct msg_value_t val = msg_queue_front(&msg_queue);
		msg_queue_pop_front(&msg_queue, &val);
	}

	/* Check there is room for the data */
	if (args->output_buf_max <
	    sizeof(struct ec_response_i2c_passthru) + msg_queue.in_len) {
		LOG_DBG("overflow1");
		return EC_HOST_CMD_INVALID_PARAM;
	}

	/* Must have bytes to write */
	if (args->input_buf_size < size + msg_queue.out_len) {
		LOG_DBG("overflow2");
		return EC_HOST_CMD_INVALID_PARAM;
	}

	return EC_HOST_CMD_SUCCESS;
}

static enum ec_host_cmd_status
i2c_command_passthru(struct ec_host_cmd_handler_args *args)
{
	const struct ec_params_i2c_passthru *params =
		(const struct ec_params_i2c_passthru *)args->input_buf;
	const uint8_t remote_port = params->port;
	const struct device *dev = I2C_PASSTHRU_DEV;
	const struct i2c_passthru_port_config *port_cfg =
		find_i2c_passthru_port_config(dev, remote_port);
	const struct device *i2c_dev = NULL;
	struct ec_response_i2c_passthru *resp = args->output_buf;
	struct msg_queue_t msg_queue;
	int ret;

	if (port_cfg != NULL) {
		i2c_dev = port_cfg->i2c_dev;
	} else if (!system_is_locked()) {
		i2c_dev = i2c_portmap(remote_port);
		if (i2c_dev == NULL) {
			return EC_HOST_CMD_INVALID_PARAM;
		}
	} else {
		return EC_HOST_CMD_ACCESS_DENIED;
	}

#ifdef CONFIG_BATTERY_CUT_OFF
	/* Some batteries would wake up after cut-off if we talk to it. */
	if (battery_is_cut_off())
		return EC_HOST_CMD_ACCESS_DENIED;
#endif

	enum ec_host_cmd_status status = check_i2c_params(args);
	if (status != EC_HOST_CMD_SUCCESS)
		return status;

	if (!i2c_allow_passthru(dev, port_cfg, params))
		return EC_HOST_CMD_ACCESS_DENIED;

	/* Loop and process messages */
	resp->i2c_status = 0;
	resp->num_msgs = 0;

	msg_queue = msg_queue_create(params);

	if (HAS_VIRTUAL_BATTERY && port_cfg &&
	    port_cfg->allow_type == VIRTUAL_BATTERY) {
		struct i2c_battery_parser_state parser_state =
			i2c_battery_parser_state_create();

		for (; msg_queue.msg < msg_queue.msg_end; resp->num_msgs++) {
			const struct msg_value_t val =
				msg_queue_front(&msg_queue);
			ret = 1;
			if (virtual_battery_handler(
				    &parser_state, resp, msg_queue.in_len, &ret,
				    val.xferflags, val.read_len, val.write_len,
				    msg_queue.out) ||
			    ret) {
				msg_queue.in_len = 0;
				resp->i2c_status = EC_I2C_STATUS_NAK;
				break;
			}

			msg_queue_pop_front(&msg_queue, &val);
		}
	} else {
		struct i2c_msg msgs[CONFIG_I2C_PASSTHRU_MAX_MSGS];

		ret = zephyr_i2c_xfer_i2c_message_queue(i2c_dev, &msg_queue,
							msgs, ARRAY_SIZE(msgs),
							resp->data);
		if (ret == EC_SUCCESS) {
			resp->num_msgs = params->num_msgs;
		} else {
			msg_queue.in_len = 0;
			resp->i2c_status = EC_I2C_STATUS_NAK;
		}
	}

	args->output_buf_size = sizeof(*resp) + msg_queue.in_len;
	/*
	 * Return success even if transfer failed so response is sent.  Host
	 * will check message status to determine the transfer result.
	 */
	return EC_HOST_CMD_SUCCESS;
}
EC_HOST_CMD_HANDLER(EC_CMD_I2C_PASSTHRU, i2c_command_passthru, EC_VER_MASK(0),
		    struct ec_params_i2c_passthru,
		    struct ec_response_i2c_passthru);

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

static enum ec_host_cmd_status
i2c_passthru_protect_port(const struct device *dev, int remote_port)
{
	const struct i2c_passthru_port_config *port_cfg =
		find_i2c_passthru_port_config(dev, remote_port);

	if (port_cfg != NULL) {
		port_cfg->data->port_protected = true;
		return EC_HOST_CMD_SUCCESS;
	} else {
		LOG_DBG("Invalid I2C port %d to be protected", remote_port);
		return EC_HOST_CMD_INVALID_PARAM;
	}
}

static enum ec_host_cmd_status
i2c_command_passthru_protect(struct ec_host_cmd_handler_args *args)
{
	const struct ec_params_i2c_passthru_protect *params =
		(const struct ec_params_i2c_passthru_protect *)args->input_buf;
	const uint8_t remote_port = params->port;
	struct ec_response_i2c_passthru_protect *resp = args->output_buf;
	const struct device *dev = I2C_PASSTHRU_DEV;

	if (args->input_buf_size < sizeof(*params)) {
		LOG_DBG("protect no params, params_size=%d",
			args->input_buf_size);
		return EC_HOST_CMD_REQUEST_TRUNCATED;
	}

	switch (params->subcmd) {
	case EC_CMD_I2C_PASSTHRU_PROTECT_ENABLE_ALL:
		i2c_passthru_protect_all_ports(dev);
		return EC_HOST_CMD_SUCCESS;

	case EC_CMD_I2C_PASSTHRU_PROTECT_ENABLE:
		return i2c_passthru_protect_port(dev, remote_port);

	case EC_CMD_I2C_PASSTHRU_PROTECT_STATUS: {
		if (args->output_buf_max < sizeof(*resp)) {
			LOG_DBG("protect no response, "
				"response_max=%d, need at least %d",
				args->output_buf_max, sizeof(*resp));
			return EC_HOST_CMD_RESPONSE_TOO_BIG;
		}

		const struct i2c_passthru_port_config *port_cfg =
			find_i2c_passthru_port_config(dev, remote_port);
		if (port_cfg == NULL) {
			LOG_DBG("remote port invalid %d", remote_port);
			return EC_HOST_CMD_INVALID_PARAM;
		}

		resp->status = port_cfg->data->port_protected;
		args->output_buf_size = sizeof(*resp);
		return EC_HOST_CMD_SUCCESS;
	}

	default:
		return EC_HOST_CMD_INVALID_COMMAND;
	}
}
EC_HOST_CMD_HANDLER(EC_CMD_I2C_PASSTHRU_PROTECT, i2c_command_passthru_protect,
		    EC_VER_MASK(0), struct ec_params_i2c_passthru_protect,
		    struct ec_response_i2c_passthru_protect);

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
		enum ec_host_cmd_status ret;
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

#ifdef CONFIG_CMD_I2C_DTS_PORTMAP
static int command_i2c_portmap(int argc, const char **argv)
{
	if (argc > 1) {
		return EC_ERROR_PARAM_COUNT;
	}

	for (size_t i = 0; i < ARRAY_SIZE(i2c_controllers); i++) {
		const struct device *i2c_dev = i2c_controllers[i];

		if (i2c_dev != NULL) {
			ccprintf("Remote Port %d: %s\n", (int)i, i2c_dev->name);
		}
	}

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(i2c_portmap, command_i2c_portmap, NULL,
			"Show I2C port mapping");
#endif /* CONFIG_CMD_I2C_DTS_PORTMAP */

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
		.allow_type = COND_CODE_1(                                     \
			DT_NODE_HAS_PROP(node_id, allow_type),                 \
			(DT_STRING_UPPER_TOKEN(node_id, allow_type)),          \
			(ALLOW_IF_UNPROTECTED_BY_AP)),                         \
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
		init_i2c_controllers();                                     \
		return 0;                                                   \
	}                                                                   \
	DEVICE_DT_INST_DEFINE(inst, i2c_dts_passthru_init, NULL, NULL,      \
			      &i2c_passthru_cfg_##inst, POST_KERNEL,        \
			      CONFIG_KERNEL_INIT_PRIORITY_DEVICE, NULL);

DT_INST_FOREACH_STATUS_OKAY(I2C_DTS_PASSTHRU_INST_DEFINE)

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
