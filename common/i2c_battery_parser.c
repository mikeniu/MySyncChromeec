// Copyright 2026 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "battery_smart.h"
#include "i2c_battery_parser.h"

struct i2c_battery_parser_state i2c_battery_parser_state_create(void)
{
	return (struct i2c_battery_parser_state){
		.batt_cmd_head = NULL,
		.sb_cmd_state = IDLE,
		.acc_write_len = 0,
		.cache_hit = 0,
		.initialized = 1,
	};
}

/*
 * Parse the i2c battery command and pass the command to the operation function.
 */
int i2c_battery_parser(struct i2c_battery_parser_state *state,
		       struct ec_response_i2c_passthru *resp, int in_len,
		       int *err_code, int xferflags, int read_len,
		       int write_len, const uint8_t *out, bool permission_check,
		       int (*operation)(const uint8_t *, uint8_t *, int, int))
{
	int ret;

	switch (state->sb_cmd_state) {
	case IDLE:
		/*
		 * A legal battery command must start
		 * with a i2c write for reg index.
		 */
		if (write_len == 0) {
			resp->i2c_status = EC_I2C_STATUS_NAK;
			return EC_ERROR_INVAL;
		}
		/* Record the head of battery command. */
		state->batt_cmd_head = out;
		state->sb_cmd_state = START;
		*err_code = 0;
		break;
	case START:
		if (write_len > 0) {
			state->sb_cmd_state = WRITE_VB;
			*err_code = 0;
		} else {
			state->sb_cmd_state = READ_VB;
			*err_code = operation(state->batt_cmd_head, NULL, 0, 0);
			/*
			 * If the reg is not handled by virtual battery, we
			 * do not support it.
			 */
			if (*err_code)
				return EC_ERROR_INVAL;
			state->cache_hit = 1;
		}
		break;
	case WRITE_VB:
		if (write_len == 0) {
			resp->i2c_status = EC_I2C_STATUS_NAK;
			return EC_ERROR_INVAL;
		}
		*err_code = 0;
		break;
	case READ_VB:
		if (read_len == 0) {
			resp->i2c_status = EC_I2C_STATUS_NAK;
			return EC_ERROR_INVAL;
		}
		/*
		 * Do not send the command to battery
		 * if the reg is cached.
		 */
		if (state->cache_hit)
			*err_code = 0;
		break;
	}

	state->acc_write_len += write_len;

	/* the last message */
	if (xferflags & I2C_XFER_STOP) {
		switch (state->sb_cmd_state) {
		/* write to virtual battery */
		case START:
		case WRITE_VB:
			ret = operation(state->batt_cmd_head, NULL, 0,
					state->acc_write_len);
			if (permission_check)
				*err_code = ret;
			break;
		/* read from virtual battery */
		case READ_VB:
			if (state->cache_hit) {
				read_len += in_len;
				if (!permission_check)
					memset(&resp->data[0], 0, read_len);
				ret = operation(state->batt_cmd_head,
						&resp->data[0], read_len, 0);
				if (permission_check)
					*err_code = ret;
			}
			break;
		/* LCOV_EXCL_START - Unreachable in IDLE state and remaining
		 * states covered above.
		 */
		default:
			return EC_ERROR_INVAL;
		}
		/* LCOV_EXCL_STOP */
	}
	return EC_RES_SUCCESS;
}

static int battery_allow_i2c_passthru(const uint8_t *batt_cmd_head,
				      uint8_t *dest, int read_len,
				      int write_len)
{
	return *batt_cmd_head == SB_MANUFACTURER_ACCESS ? EC_RES_ACCESS_DENIED :
							  EC_RES_SUCCESS;
}

int battery_permission_handler(struct i2c_battery_parser_state *state,
			       int in_len, int xferflags, int read_len,
			       int write_len, const uint8_t *out)
{
	struct ec_response_i2c_passthru resp;
	int rv = EC_RES_SUCCESS;
	int rv2 = i2c_battery_parser(state, &resp, in_len, &rv, xferflags,
				     read_len, write_len, out, true,
				     battery_allow_i2c_passthru);
	return rv == EC_RES_SUCCESS ? rv2 : rv;
}
