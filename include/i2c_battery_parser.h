// Copyright 2026 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef __CROS_EC_I2C_BATTERY_PARSER_H
#define __CROS_EC_I2C_BATTERY_PARSER_H

#include "i2c.h"

#include <stdbool.h>
#include <stdint.h>

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

#endif // __CROS_EC_I2C_BATTERY_PARSER_H
