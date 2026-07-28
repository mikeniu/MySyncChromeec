/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_INCLUDE_I2C_MSG_QUEUE_H_
#define PLATFORM_EC_INCLUDE_I2C_MSG_QUEUE_H_

#include "ec_commands.h"

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

struct msg_queue_t
msg_queue_create(const struct ec_params_i2c_passthru *params);

struct msg_value_t msg_queue_front(const struct msg_queue_t *q);

void msg_queue_pop_front(struct msg_queue_t *q, const struct msg_value_t *val);

#endif /* PLATFORM_EC_INCLUDE_I2C_MSG_QUEUE_H_ */
