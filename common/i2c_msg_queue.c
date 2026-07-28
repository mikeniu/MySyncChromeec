/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ec_commands.h"
#include "i2c.h"
#include "i2c_msg_queue.h"

#include <stdbool.h>
#include <stdint.h>

struct msg_queue_t msg_queue_create(const struct ec_params_i2c_passthru *params)
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

struct msg_value_t msg_queue_front(const struct msg_queue_t *q)
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

void msg_queue_pop_front(struct msg_queue_t *q, const struct msg_value_t *val)
{
	++q->msg;
	q->in_len += val->read_len;
	q->out += val->write_len;
	q->out_len += val->write_len;
}
