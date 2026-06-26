/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* I2C passthru support for Chrome EC */

#include "battery.h"
#include "battery_smart.h"
#include "console.h"
#include "host_command.h"
#include "i2c.h"
#include "i2c_battery_parser.h"
#include "system.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"
#include "util.h"
#include "virtual_battery.h"

#ifdef CONFIG_ZEPHYR
#include "i2c/i2c.h"
#endif

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

static uint8_t port_protected[I2C_PORT_COUNT];

struct msg_queue_t {
	const struct ec_params_i2c_passthru_msg *msg_start;
	const struct ec_params_i2c_passthru_msg *msg_end;
	const struct ec_params_i2c_passthru_msg *msg;
	const uint8_t *out;
	int in_len;
	int out_len;
};

struct msg_value_t {
	int xferflags;
	int read_len;
	int write_len;
	uint16_t addr_flags;
	bool is_read;
};

static struct msg_queue_t
msg_queue_create(const struct ec_params_i2c_passthru *params)
{
	return (struct msg_queue_t){
		.msg_start = params->msg,
		.msg_end = params->msg + params->num_msgs,
		.msg = params->msg,
		.out = (const uint8_t *)params + sizeof(*params) +
		       params->num_msgs * sizeof(*params->msg),
		.in_len = 0,
		.out_len = 0,
	};
}

static struct msg_value_t msg_queue_front(const struct msg_queue_t *q)
{
	const struct ec_params_i2c_passthru_msg *msg = q->msg;
	struct msg_value_t val = {
		.xferflags = I2C_XFER_START,
		.read_len = 0,
		.write_len = 0,
		/* Have to remove the EC flags from the address flags */
		.addr_flags = msg->addr_flags & EC_I2C_ADDR_MASK,
		.is_read = msg->addr_flags & EC_I2C_FLAG_READ,
	};
	if (val.is_read)
		val.read_len = msg->len;
	else
		val.write_len = msg->len;

	/* Set stop bit for last message */
	if (msg == q->msg_end - 1)
		val.xferflags |= I2C_XFER_STOP;

	/* More than one transactions, do a restart */
	if (msg > q->msg_start)
		val.xferflags |= I2C_XFER_RESTART;
	return val;
}

static void msg_queue_pop_front(struct msg_queue_t *q,
				const struct msg_value_t *val)
{
	++q->msg;
	q->in_len += val->read_len;
	q->out += val->write_len;
	q->out_len += val->write_len;
}

#if defined(CONFIG_I2C_VIRTUAL_BATTERY) || \
	(defined(CONFIG_I2C_PASSTHRU_RESTRICTED) && defined(CONFIG_BATTERY))
static inline bool is_same_i2c_port(int port, int other_port)
{
#ifdef CONFIG_ZEPHYR
	/* For Zephyr compare the actual device, which will be used in
	 * i2c_transfer function.
	 */
	return (i2c_get_device_for_port(port) ==
		i2c_get_device_for_port(other_port));
#else
	return (port == other_port);
#endif
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

/**
 * Perform the voluminous checking required for this message
 *
 * @param port	I2C port number
 * @param args	Arguments
 * @return 0 if OK, EC_RES_INVALID_PARAM on error
 */
static enum ec_host_cmd_status
check_i2c_params(const uint8_t port,
		 const struct ec_host_cmd_handler_args *args)
{
	const struct ec_params_i2c_passthru *params = args->input_buf;
	struct msg_queue_t msg_queue;
	unsigned int size;
#if defined(CONFIG_I2C_PASSTHRU_RESTRICTED) && defined(CONFIG_BATTERY)
	struct i2c_battery_parser_state parser_state = { .initialized = 0 };
#endif

	if (args->input_buf_size < sizeof(*params)) {
		PTHRUPRINTS("no params, params_size=%d, need at least %d",
			    args->input_buf_size, sizeof(*params));
		return EC_HOST_CMD_REQUEST_TRUNCATED;
	}
	size = sizeof(*params) + params->num_msgs * sizeof(*params->msg);
	if (args->input_buf_size < size) {
		PTHRUPRINTS("params_size=%d, need at least %d",
			    args->input_buf_size, size);
		return EC_HOST_CMD_INVALID_PARAM;
	}

	/* Loop and process messages */;
	for (msg_queue = msg_queue_create(params);
	     msg_queue.msg < msg_queue.msg_end;) {
		const struct msg_value_t val = msg_queue_front(&msg_queue);

		PTHRUPRINTS("port=%d, %s, addr=0x%x(7-bit), len=%d", port,
			    val.is_read ? "read" : "write", val.addr_flags,
			    msg_queue.msg->len);

#ifdef CONFIG_I2C_PASSTHRU_RESTRICTED
		if (system_is_locked()) {
			const struct i2c_cmd_desc_t cmd_desc = {
				.port = port,
				.addr_flags = msg_queue.msg->addr_flags,
				.cmd = val.is_read ? 0xff : *msg_queue.out,
			};
			if (!board_allow_i2c_passthru(&cmd_desc))
				return EC_HOST_CMD_ACCESS_DENIED;

#ifdef CONFIG_BATTERY
			if (is_i2c_battery(port, val.addr_flags, false)) {
				if (parser_state.initialized == 0)
					parser_state =
						i2c_battery_parser_state_create();

				if (battery_permission_handler(
					    &parser_state, msg_queue.in_len,
					    val.xferflags, val.read_len,
					    val.write_len, msg_queue.out))
					return EC_HOST_CMD_ACCESS_DENIED;
			}
#endif
		}
#endif
		msg_queue_pop_front(&msg_queue, &val);
	}

	/* Check there is room for the data */
	if (args->output_buf_max <
	    sizeof(struct ec_response_i2c_passthru) + msg_queue.in_len) {
		PTHRUPRINTS("overflow1");
		return EC_HOST_CMD_INVALID_PARAM;
	}

	/* Must have bytes to write */
	if (args->input_buf_size < size + msg_queue.out_len) {
		PTHRUPRINTS("overflow2");
		return EC_HOST_CMD_INVALID_PARAM;
	}

	return EC_HOST_CMD_SUCCESS;
}

static enum ec_host_cmd_status
i2c_command_passthru(struct ec_host_cmd_handler_args *args)
{
	/* Force casting (const void *) to (struct ec_params_i2c_passthru *) */
	const struct ec_params_i2c_passthru *params =
		(struct ec_params_i2c_passthru *)args->input_buf;
	uint8_t port = params->port;
#ifdef CONFIG_ZEPHYR
	/* For Zephyr, convert the received remote port number to a port number
	 * used in EC.
	 */
	port = i2c_get_port_from_remote_port(params->port);
#endif
#ifdef CONFIG_I2C_VIRTUAL_BATTERY
	struct i2c_battery_parser_state parser_state = { .initialized = 0 };
#endif
	struct ec_response_i2c_passthru *resp = args->output_buf;
	const struct i2c_port_t *i2c_port;
	struct msg_queue_t msg_queue;
	int i;
	int port_is_locked = 0;

#ifdef CONFIG_BATTERY_CUT_OFF
	/*
	 * Some batteries would wake up after cut-off if we talk to it.
	 */
	if (battery_is_cut_off())
		return EC_HOST_CMD_ACCESS_DENIED;
#endif

	i2c_port = get_i2c_port(port);
	if (!i2c_port)
		return EC_HOST_CMD_INVALID_PARAM;

	enum ec_host_cmd_status status = check_i2c_params(port, args);
	if (status != EC_HOST_CMD_SUCCESS)
		return status;

	if (port_protected[port]) {
		if (!i2c_port->passthru_allowed)
			return EC_HOST_CMD_ACCESS_DENIED;

		for (i = 0; i < params->num_msgs; i++) {
			if (!i2c_port->passthru_allowed(
				    i2c_port, params->msg[i].addr_flags))
				return EC_HOST_CMD_ACCESS_DENIED;
		}
	}

	/* Loop and process messages */
	resp->i2c_status = 0;
	resp->num_msgs = 0;

	for (msg_queue = msg_queue_create(params);
	     msg_queue.msg < msg_queue.msg_end; resp->num_msgs++) {
		int rv = 1;
		const struct msg_value_t val = msg_queue_front(&msg_queue);

#ifdef CONFIG_I2C_VIRTUAL_BATTERY
		if (is_i2c_battery(port, val.addr_flags, true)) {
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
		PTHRUPRINTS("xfer port=%x addr=0x%x rlen=%d flags=0x%x", port,
			    val.addr_flags, val.read_len, val.xferflags);
		if (val.write_len) {
			PTHRUPRINTF("  out:");
			for (i = 0; i < val.write_len; i++)
				PTHRUPRINTF(" 0x%02x", msg_queue.out[i]);
			PTHRUPRINTF("\n");
		}
		if (rv) {
			if (!port_is_locked)
				i2c_lock(port, (port_is_locked = 1));
			rv = i2c_xfer_unlocked(port, val.addr_flags,
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
	args->output_buf_size = sizeof(*resp) + msg_queue.in_len;

	/* Unlock port */
	if (port_is_locked)
		i2c_lock(port, 0);

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
	memset(port_protected, 0, sizeof(port_protected));
}

static void i2c_passthru_protect_port(uint32_t port)
{
	if (port < ARRAY_SIZE(port_protected))
		port_protected[port] = 1;
	else
		PTHRUPRINTS("Invalid I2C port %d to be protected\n", port);
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
		i2c_passthru_protect_port(tcpc_config[i].i2c_info.port);
#endif
	}
#endif
}

static enum ec_host_cmd_status
i2c_command_passthru_protect(struct ec_host_cmd_handler_args *args)
{
	/* Force casting (const void *) to (struct
	 * ec_params_i2c_passthru_protect *) */
	const struct ec_params_i2c_passthru_protect *params =
		(struct ec_params_i2c_passthru_protect *)args->input_buf;
	uint8_t port = params->port;
#ifdef CONFIG_ZEPHYR
	/* For Zephyr, convert the received remote port number to a port number
	 * used in EC.
	 */
	port = i2c_get_port_from_remote_port(params->port);
#endif
	struct ec_response_i2c_passthru_protect *resp = args->output_buf;

	if (args->input_buf_size < sizeof(*params)) {
		PTHRUPRINTS("protect no params, params_size=%d, ",
			    args->input_buf_size);
		return EC_HOST_CMD_REQUEST_TRUNCATED;
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
		return EC_HOST_CMD_SUCCESS;
	}

	if (!get_i2c_port(port)) {
		PTHRUPRINTS("protect invalid port %d", port);
		return EC_HOST_CMD_INVALID_PARAM;
	}

	if (params->subcmd == EC_CMD_I2C_PASSTHRU_PROTECT_STATUS) {
		if (args->output_buf_max < sizeof(*resp)) {
			PTHRUPRINTS("protect no response, "
				    "response_max=%d, need at least %d",
				    args->output_buf_max, sizeof(*resp));
			return EC_HOST_CMD_RESPONSE_TOO_BIG;
		}

		resp->status = port_protected[port];
		args->output_buf_size = sizeof(*resp);
	} else if (params->subcmd == EC_CMD_I2C_PASSTHRU_PROTECT_ENABLE) {
		i2c_passthru_protect_port(port);
	} else {
		return EC_HOST_CMD_INVALID_COMMAND;
	}

	return EC_HOST_CMD_SUCCESS;
}
EC_HOST_CMD_HANDLER(EC_CMD_I2C_PASSTHRU_PROTECT, i2c_command_passthru_protect,
		    EC_VER_MASK(0), struct ec_params_i2c_passthru_protect,
		    struct ec_response_i2c_passthru_protect);

/*****************************************************************************/
/* Console commands */

#ifdef CONFIG_CMD_I2C_PROTECT
static int command_i2cprotect(int argc, const char **argv)
{
	if (argc == 1) {
		int i, port;

		for (i = 0; i < i2c_ports_used; i++) {
			port = i2c_ports[i].port;
			ccprintf("Port %d: %s\n", port,
				 port_protected[port] ? "Protected" :
							"Unprotected");
		}
	} else if (argc == 2) {
		int port;
		char *e;

		port = strtoi(argv[1], &e, 0);
		if (*e)
			return EC_ERROR_PARAM2;

		if (!get_i2c_port(port)) {
			ccprintf("i2c passthru protect invalid port %d\n",
				 port);
			return EC_RES_INVALID_PARAM;
		}

		port_protected[port] = 1;
	} else {
		return EC_ERROR_PARAM_COUNT;
	}

	return EC_RES_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(i2cprotect, command_i2cprotect, "[port]",
			"Protect I2C bus");
#endif
