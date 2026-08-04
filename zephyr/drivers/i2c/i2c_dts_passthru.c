/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* DTS-configured I2C passthru driver for Zephyr EC */

#include "battery.h"
#include "battery_smart.h"
#include "console.h"
#include "host_command.h"
#include "i2c.h"
#include "i2c/i2c.h"
#include "i2c_battery_parser.h"
#include "i2c_msg_queue.h"
#include "system.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"
#include "util.h"
#include "virtual_battery.h"

#include <zephyr/devicetree.h>

#define CPUTS(outstr) cputs(CC_I2C, outstr)
#define CPRINTS(format, args...) cprints(CC_I2C, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_I2C, format, ##args)

#ifdef CONFIG_I2C_DEBUG_PASSTHRU
#define PTHRUPRINTS(format, args...) CPRINTS("I2C_PTHRU " format, ##args)
#define PTHRUPRINTF(format, args...) CPRINTF(format, ##args)
#else
#define PTHRUPRINTS(format, args...)
#define PTHRUPRINTF(format, args...)
#endif

enum i2c_passthru_allow_type {
	ALLOW_IF_UNPROTECTED_BY_AP,
};

struct i2c_passthru_t {
	int remote_port;
	int port;
	uint8_t *protected;
	enum i2c_passthru_allow_type allow_type;
};

#if defined(CONFIG_I2C_VIRTUAL_BATTERY) || defined(CONFIG_BATTERY)
static inline bool is_same_i2c_port(int port, int other_port)
{
	/* For Zephyr compare the actual device, which will be used in
	 * i2c_transfer function.
	 */
	return (i2c_get_device_for_port(port) ==
		i2c_get_device_for_port(other_port));
}

static inline bool is_i2c_battery(int port, uint16_t address, bool virtual_only)
{
	struct i2c_signature {
		int port;
		uint16_t address;
		bool is_virtual;
	};

	static const struct i2c_signature battery_signatures[] = {
#ifdef CONFIG_I2C_VIRTUAL_BATTERY
		{ .port = I2C_PORT_VIRTUAL_BATTERY,
		  .address = VIRTUAL_BATTERY_ADDR_FLAGS,
		  .is_virtual = true },
#endif
#ifdef BATTERY_ADDR_FLAGS
		{ .port = I2C_PORT_BATTERY,
		  .address = BATTERY_ADDR_FLAGS,
		  .is_virtual = false },
#endif
	};

	for (size_t index = 0; index < ARRAY_SIZE(battery_signatures);
	     ++index) {
		if (virtual_only && !battery_signatures[index].is_virtual)
			continue;
		if (is_same_i2c_port(port, battery_signatures[index].port) &&
		    address == battery_signatures[index].address) {
			return true;
		}
	}
	return false;
}
#endif

static uint8_t port_protected[I2C_PORT_COUNT];

#define INIT_FW_CONFIG_KEY_NODE(id)                                          \
	{ .remote_port = DT_PROP(id, reg) >= 0 ?                             \
				 DT_PROP(id, reg) :                          \
				 I2C_PORT_BUS(DT_PHANDLE(id, i2c_port)),     \
	  .port = I2C_PORT_BUS(DT_PHANDLE(id, i2c_port)),                    \
	  .protected =                                                       \
		  &port_protected[I2C_PORT_BUS(DT_PHANDLE(id, i2c_port))],   \
	  .allow_type = COND_CODE_1(DT_NODE_HAS_PROP(id, allow_type),        \
				    (DT_STRING_UPPER_TOKEN(id, allow_type)), \
				    (ALLOW_IF_UNPROTECTED_BY_AP)) },

static const struct i2c_passthru_t i2c_passthru_config[] = {
#if DT_HAS_COMPAT_STATUS_OKAY(cros_ec_i2c_passthru)
	DT_FOREACH_CHILD_STATUS_OKAY(
		DT_COMPAT_GET_ANY_STATUS_OKAY(cros_ec_i2c_passthru),
		INIT_FW_CONFIG_KEY_NODE)
#endif
};

static const struct i2c_passthru_t *find_i2c_passthru_config(int remote_port)
{
	for (size_t index = 0; index < ARRAY_SIZE(i2c_passthru_config);
	     index++) {
		if (i2c_passthru_config[index].remote_port == remote_port) {
			return i2c_passthru_config + index;
		}
	}
	return NULL;
}

static int
allow_if_unprotected_by_ap(const struct i2c_passthru_t *config,
			   const struct ec_params_i2c_passthru *params,
			   bool protect_by_ap)
{
	return !protect_by_ap;
}

static int i2c_allow_passthru(const struct i2c_passthru_t *config,
			      const struct ec_params_i2c_passthru *params)
{
	const bool protect_by_ap = *config->protected;

	if (!system_is_locked())
		return 1;

	switch (config->allow_type) {
	case ALLOW_IF_UNPROTECTED_BY_AP:
		return allow_if_unprotected_by_ap(config, params,
						  protect_by_ap);

	default:
		return 0;
	}
}

static int check_i2c_params(const struct host_cmd_handler_args *args)
{
	const struct ec_params_i2c_passthru *params = args->params;
	struct msg_queue_t msg_queue;
	unsigned int size;

	if (args->params_size < sizeof(*params)) {
		PTHRUPRINTS("no params, params_size=%d, need at least %d",
			    args->params_size, sizeof(*params));
		return EC_RES_INVALID_PARAM;
	}
	size = sizeof(*params) + params->num_msgs * sizeof(*params->msg);
	if (args->params_size < size) {
		PTHRUPRINTS("params_size=%d, need at least %d",
			    args->params_size, size);
		return EC_RES_INVALID_PARAM;
	}

	/* Loop and process messages */;
	for (msg_queue = msg_queue_create(params);
	     msg_queue.msg < msg_queue.msg_end;) {
		const struct msg_value_t val = msg_queue_front(&msg_queue);
		msg_queue_pop_front(&msg_queue, &val);
	}

	/* Check there is room for the data */
	if (args->response_max <
	    sizeof(struct ec_response_i2c_passthru) + msg_queue.in_len) {
		PTHRUPRINTS("overflow1");
		return EC_RES_INVALID_PARAM;
	}

	/* Must have bytes to write */
	if (args->params_size < size + msg_queue.out_len) {
		PTHRUPRINTS("overflow2");
		return EC_RES_INVALID_PARAM;
	}

	return EC_RES_SUCCESS;
}

static enum ec_status i2c_command_passthru(struct host_cmd_handler_args *args)
{
	/* Force casting (const void *) to (struct ec_params_i2c_passthru *) */
	const struct ec_params_i2c_passthru *params =
		(struct ec_params_i2c_passthru *)args->params;
	const uint8_t remote_port = params->port;
	const struct i2c_passthru_t *config =
		find_i2c_passthru_config(remote_port);
#ifdef CONFIG_I2C_VIRTUAL_BATTERY
	struct i2c_battery_parser_state parser_state = { .initialized = 0 };
#endif
	struct ec_response_i2c_passthru *resp = args->response;
	struct msg_queue_t msg_queue;
	int ret, i;
	int port_is_locked = 0;
	struct i2c_passthru_t passthru_config;

	/* Follow logic in i2c_get_port_from_remote_port, which maps port to
	 * remote_port when no config found. This is required to pass the
	 * test_read_without_write unit test. */
	if (config == NULL) {
		passthru_config = (struct i2c_passthru_t){
			.port = remote_port,
			.remote_port = remote_port,
			.protected = &port_protected[remote_port],
			.allow_type = ALLOW_IF_UNPROTECTED_BY_AP,
		};
		config = &passthru_config;
	}

#ifdef CONFIG_BATTERY_CUT_OFF
	/*
	 * Some batteries would wake up after cut-off if we talk to it.
	 */
	if (battery_is_cut_off())
		return EC_RES_ACCESS_DENIED;
#endif

	if (!get_i2c_port(config->port))
		return EC_RES_INVALID_PARAM;

	ret = check_i2c_params(args);
	if (ret)
		return ret;

	if (!i2c_allow_passthru(config, params))
		return EC_RES_ACCESS_DENIED;

	/* Loop and process messages */
	resp->i2c_status = 0;
	resp->num_msgs = 0;

	for (msg_queue = msg_queue_create(params);
	     msg_queue.msg < msg_queue.msg_end; resp->num_msgs++) {
		int rv = 1;
		const struct msg_value_t val = msg_queue_front(&msg_queue);

#ifdef CONFIG_I2C_VIRTUAL_BATTERY
		if (is_i2c_battery(config->port, val.addr_flags, true)) {
			/* Lazy initialization. */
			if (parser_state.initialized == 0)
				parser_state =
					i2c_battery_parser_state_create();

			if (virtual_battery_handler(
				    &parser_state, resp, msg_queue.in_len, &rv,
				    val.xferflags, val.read_len, val.write_len,
				    msg_queue.out))
				break;
		}
#endif
		/* Transfer next message */
		PTHRUPRINTS("xfer port=%x addr=0x%x rlen=%d flags=0x%x",
			    config->port, val.addr_flags, val.read_len,
			    val.xferflags);
		if (val.write_len) {
			PTHRUPRINTF("  out:");
			for (i = 0; i < val.write_len; i++)
				PTHRUPRINTF(" 0x%02x", msg_queue.out[i]);
			PTHRUPRINTF("\n");
		}
		if (rv) {
			if (!port_is_locked)
				i2c_lock(config->port, (port_is_locked = 1));
			rv = i2c_xfer_unlocked(config->port, val.addr_flags,
					       msg_queue.out, val.write_len,
					       &resp->data[msg_queue.in_len],
					       val.read_len, val.xferflags);
		}

		if (rv) {
			/* Driver will have sent a stop bit here */
			if (rv == EC_ERROR_TIMEOUT)
				resp->i2c_status = EC_I2C_STATUS_TIMEOUT;
			else
				resp->i2c_status = EC_I2C_STATUS_NAK;
			break;
		}

		msg_queue_pop_front(&msg_queue, &val);
	}
	args->response_size = sizeof(*resp) + msg_queue.in_len;

	/* Unlock port */
	if (port_is_locked)
		i2c_lock(config->port, 0);

	/*
	 * Return success even if transfer failed so response is sent.  Host
	 * will check message status to determine the transfer result.
	 */
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_I2C_PASSTHRU, i2c_command_passthru, EC_VER_MASK(0));

__test_only void i2c_passthru_protect_reset(void)
{
	memset(port_protected, 0, sizeof(port_protected));
}

static void i2c_passthru_protect_port(uint8_t remote_port)
{
	const struct i2c_passthru_t *config =
		find_i2c_passthru_config(remote_port);
	if (config != NULL)
		*config->protected = 1;
	else
		PTHRUPRINTS("Invalid I2C port %d to be protected\n",
			    remote_port);
}

static void i2c_passthru_protect_tcpc_ports(void)
{
#ifdef CONFIG_USB_PD_PORT_MAX_COUNT
	int i;

	/*
	 * If WP is not enabled i.e. system is not locked leave the tunnels open
	 * so that factory line can do updates without a new RO BIOS.
	 */
	if (!system_is_locked()) {
		CPRINTS("System unlocked, TCPC I2C tunnels may be unprotected");
		return;
	}

	for (i = 0; i < board_get_usb_pd_port_count(); i++) {
#ifdef CONFIG_USB_PD_CONTROLLER
		/* TODO:b/294550823 - Create an allow list for I2C passthru
		 * commands */
#else
		/* TCPC tunnel not configured. No need to protect anything */
		if (!I2C_STRIP_FLAGS(tcpc_config[i].i2c_info.addr_flags))
			continue;
		/* TODO:b/ - Map this to remote port. */
		i2c_passthru_protect_port(tcpc_config[i].i2c_info.port);
#endif
	}
#endif
}

static enum ec_status
i2c_command_passthru_protect(struct host_cmd_handler_args *args)
{
	/* Force casting (const void *) to (struct
	 * ec_params_i2c_passthru_protect *) */
	const struct ec_params_i2c_passthru_protect *params =
		(struct ec_params_i2c_passthru_protect *)args->params;
	const uint8_t remote_port = params->port;
	struct ec_response_i2c_passthru_protect *resp = args->response;

	if (args->params_size < sizeof(*params)) {
		PTHRUPRINTS("protect no params, params_size=%d, ",
			    args->params_size);
		return EC_RES_INVALID_PARAM;
	}

	/*
	 * When calling the subcmd to protect all tcpcs, the i2c port isn't
	 * expected to be set in the args. So, putting a check here to avoid
	 * the get_i2c_port return error.
	 */
	if (params->subcmd == EC_CMD_I2C_PASSTHRU_PROTECT_ENABLE_TCPCS) {
		if (IS_ENABLED(CONFIG_USB_POWER_DELIVERY) &&
		    !IS_ENABLED(CONFIG_USB_PD_TCPM_STUB))
			i2c_passthru_protect_tcpc_ports();
		return EC_RES_SUCCESS;
	}

	if (params->subcmd == EC_CMD_I2C_PASSTHRU_PROTECT_STATUS) {
		const struct i2c_passthru_t *config;

		if (args->response_max < sizeof(*resp)) {
			PTHRUPRINTS("protect no response, "
				    "response_max=%d, need at least %d",
				    args->response_max, sizeof(*resp));
			return EC_RES_INVALID_PARAM;
		}

		config = find_i2c_passthru_config(remote_port);
		if (config == NULL) {
			PTHRUPRINTS("remote port invalid %d", remote_port);
			return EC_RES_INVALID_PARAM;
		}

		resp->status = *config->protected;
		args->response_size = sizeof(*resp);
	} else if (params->subcmd == EC_CMD_I2C_PASSTHRU_PROTECT_ENABLE) {
		i2c_passthru_protect_port(remote_port);
	} else {
		return EC_RES_INVALID_COMMAND;
	}

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_I2C_PASSTHRU_PROTECT, i2c_command_passthru_protect,
		     EC_VER_MASK(0));

#ifdef CONFIG_CMD_I2C_PROTECT
static int command_i2cprotect(int argc, const char **argv)
{
	const struct i2c_passthru_t *config;

	if (argc == 1) {
		for (size_t index = 0; index < ARRAY_SIZE(port_protected);
		     index++) {
			config = &i2c_passthru_config[index];
			ccprintf("Remote Port %d Port %d: %sprotected\n",
				 config->remote_port, config->port,
				 *config->protected ? "" : "un");
		}
	} else if (argc == 2) {
		int remote_port;
		char *e;

		remote_port = strtoi(argv[1], &e, 0);
		if (*e)
			return EC_ERROR_PARAM2;

		config = find_i2c_passthru_config(remote_port);
		if (config == NULL || !get_i2c_port(config->port)) {
			ccprintf(
				"i2c passthru protect invalid remote_port %d\n",
				remote_port);
			return EC_RES_INVALID_PARAM;
		}

		*config->sprotected = 1;
	} else {
		return EC_ERROR_PARAM_COUNT;
	}

	return EC_RES_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(i2cprotect, command_i2cprotect, "[remote_port]",
			"Protect I2C bus");
#endif
