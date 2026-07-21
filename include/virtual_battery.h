/* Copyright 2016 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_VIRTUAL_BATTERY_H
#define __CROS_EC_VIRTUAL_BATTERY_H

#if defined(CONFIG_I2C_VIRTUAL_BATTERY) && defined(CONFIG_BATTERY_SMART) && \
	!defined(VIRTUAL_BATTERY_ADDR_FLAGS)
#define VIRTUAL_BATTERY_ADDR_FLAGS BATTERY_ADDR_FLAGS
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

/**
 * Read/write value of battery parameter from charge state.
 *
 * @param batt_cmd_head	The beginning of the smart battery command
 * @param dest		Destination buffer for data
 * @param read_len	Number of bytes to read to the buffer
 * @param write_len	Number of bytes to write
 * @return EC_SUCCESS if successful, non-zero if error.
 *
 */
int virtual_battery_operation(const uint8_t *batt_cmd_head, uint8_t *dest,
			      int read_len, int write_len);

/**
 * Parse a command for virtual battery function.
 *
 * @param resp		Pointer to the data structure to store the i2c messages
 * @param in_len	Accumulative number of bytes read
 * @param err_code	Pointer to the return value of i2c_xfer() or
 *			virtual_battery_operation()
 * @param xferflags	Flags
 * @param read_len	Number of bytes to read
 * @param write_len	Number of bytes to write
 * @param out		Data to send
 * @return EC_SUCCESS if successful, non-zero if error.
 */
int virtual_battery_handler(struct i2c_battery_parser_state *state,
			    struct ec_response_i2c_passthru *resp, int in_len,
			    int *err_code, int xferflags, int read_len,
			    int write_len, const uint8_t *out);

/**
 * Create a parser state.
 */
struct i2c_battery_parser_state i2c_battery_parser_state_create(void);

#endif /* __CROS_EC_VIRTUAL_BATTERY_H */
