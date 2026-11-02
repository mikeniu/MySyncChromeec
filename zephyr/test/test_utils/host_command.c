/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Implement the stub host_command_process function for tests with the upstream
 * Host Command subsystem.
 */

#include "host_command.h"

#include <string.h>

#include <zephyr/mgmt/ec_host_cmd/ec_host_cmd.h>

static uint8_t cal_checksum(const uint8_t *const buffer, const size_t size)
{
	uint8_t checksum = 0;

	for (size_t i = 0; i < size; ++i) {
		checksum += buffer[i];
	}
	return (uint8_t)(-checksum);
}

uint16_t host_command_process(struct host_cmd_handler_args *args)
{
	const struct ec_host_cmd *hc = ec_host_cmd_get_hc();

	if (args != NULL && hc != NULL && hc->user_cb != NULL) {
		struct ec_host_cmd_request_header rx_header = {
			.cmd_id = args->command,
		};
		struct ec_host_cmd_rx_ctx rx_ctx = {
			.buf = (uint8_t *)&rx_header,
			.len = sizeof(rx_header),
		};

		hc->user_cb(&rx_ctx, hc->user_data);
	}

	return ec_host_cmd_process((struct ec_host_cmd_handler_args *)args);
}

void host_command_received(struct host_cmd_handler_args *args)
{
	struct ec_host_cmd *hc = (struct ec_host_cmd *)ec_host_cmd_get_hc();
	struct ec_host_cmd_rx_ctx *rx;
	struct ec_host_cmd_request_header *rx_header;
	size_t rx_size;

	if (args == NULL || hc == NULL || hc->rx_ctx.buf == NULL) {
		return;
	}

	rx = &hc->rx_ctx;
	rx_size = sizeof(*rx_header) + args->input_buf_size;
	if (rx_size > rx->len_max) {
		return;
	}

	rx_header = (struct ec_host_cmd_request_header *)rx->buf;
	*rx_header = (struct ec_host_cmd_request_header){
		.prtcl_ver = 3,
		.checksum = 0,
		.cmd_id = args->command,
		.cmd_ver = args->version,
		.reserved = 0,
		.data_len = args->input_buf_size,
	};

	if (args->input_buf != NULL && args->input_buf_size > 0) {
		memcpy(rx->buf + sizeof(*rx_header), args->input_buf,
		       args->input_buf_size);
	}
	rx_header->checksum = cal_checksum(rx->buf, rx_size);
	rx->len = rx_size;

	ec_host_cmd_rx_notify();
}
