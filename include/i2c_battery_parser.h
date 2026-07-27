// Copyright 2026 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef __CROS_EC_I2C_BATTERY_PARSER_H
#define __CROS_EC_I2C_BATTERY_PARSER_H

#include "common.h"
#include "ec_commands.h"
#include "i2c.h"

#ifdef CONFIG_ZEPHYR
#include "i2c/i2c.h"
#endif

#include <stdbool.h>
#include <stdint.h>

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

static inline struct msg_queue_t
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

static inline struct msg_value_t msg_queue_front(const struct msg_queue_t *q)
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

static inline void msg_queue_pop_front(struct msg_queue_t *q,
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

/*
 * The state machine used to parse smart battery command
 * to support virtual battery.
 */
enum batt_cmd_parse_state {
	IDLE = 0, /* initial state */
	START = 1, /* received the register address (command code) */
	WRITE_VB, /* writing data bytes to the peripheral */
	READ_VB, /* reading data bytes to the peripheral */
};

struct i2c_battery_parser_state {
	const uint8_t *batt_cmd_head;
	enum batt_cmd_parse_state sb_cmd_state;
	int acc_write_len;
	uint8_t cache_hit;
	uint8_t initialized;
};

int i2c_battery_parser(struct i2c_battery_parser_state *state,
		       struct ec_response_i2c_passthru *resp, int in_len,
		       int *err_code, int xferflags, int read_len,
		       int write_len, const uint8_t *out, bool permission_check,
		       int (*operation)(const uint8_t *, uint8_t *, int, int));

/**
 * Check if a battery i2c passthru read/write is allowed.
 *
 * @param in_len	Accumulative number of bytes read
 * @param xferflags	Flags
 * @param read_len	Number of bytes to read
 * @param write_len	Number of bytes to write
 * @param out		Data to send
 * @return EC_SUCCESS if successful, non-zero if error or not allowed.
 */
int battery_permission_handler(struct i2c_battery_parser_state *state,
			       int in_len, int xferflags, int read_len,
			       int write_len, const uint8_t *out);

/**
 * Create a parser state.
 */
struct i2c_battery_parser_state i2c_battery_parser_state_create(void);

#ifdef CONFIG_ZEPHYR
/**
 * Check if a battery I2C passthru command is allowed for Zephyr EC.
 *
 * @param params I2C passthru parameters.
 * @return 1 if allowed, 0 if denied.
 */
int battery_allow_passthru(int port,
			   const struct ec_params_i2c_passthru *params);
#endif

#endif // __CROS_EC_I2C_BATTERY_PARSER_H
