/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Implement the stub host_command_process function for tests with the upstream
 * Host Command subsystem.
 */

#include "ec_commands.h"
#include "host_command.h"

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/mgmt/ec_host_cmd/ec_host_cmd.h>
#include <zephyr/ztest.h>

LOG_MODULE_REGISTER(test_host_command, LOG_LEVEL_INF);

K_SEM_DEFINE(send_called, 0, 1);

static int host_send(const struct ec_host_cmd_backend *backend)
{
	ARG_UNUSED(backend);
	k_sem_give(&send_called);

	return 0;
}

static const struct ec_host_cmd_backend_api test_hc_api = {
	.init = NULL,
	.send = host_send,
};

static struct ec_host_cmd_backend test_hc_backend = {
	.api = &test_hc_api,
	.ctx = NULL,
};

static uint8_t cal_checksum(const uint8_t *const buffer, const uint16_t size)
{
	uint8_t checksum = 0;

	for (size_t i = 0; i < size; ++i) {
		checksum += buffer[i];
	}
	return (uint8_t)(-checksum);
}

uint16_t host_command_process(struct host_cmd_handler_args *args)
{
	struct ec_host_cmd *hc = (struct ec_host_cmd *)ec_host_cmd_get_hc();
	struct ec_host_cmd_backend *orig_backend = hc->backend;
	struct ec_host_request *rx_header =
		(struct ec_host_request *)hc->rx_ctx.buf;
	struct ec_host_response *tx_header =
		(struct ec_host_response *)hc->tx.buf;
	const size_t rx_size = args->input_buf_size + sizeof(*rx_header);
	const size_t orig_tx_len_max = hc->tx.len_max;
	const size_t requested_len = args->output_buf_max + sizeof(*tx_header);
	int rv;

	zassert_not_null(hc->rx_ctx.buf, "Host Command RX buffer is NULL");
	zassert_not_null(hc->tx.buf, "Host Command TX buffer is NULL");
	zassert_true(orig_tx_len_max >= sizeof(*tx_header),
		     "Host Command TX buffer size %zu smaller than header %zu",
		     orig_tx_len_max, sizeof(*tx_header));
	zassert_true(rx_size <= hc->rx_ctx.len_max,
		     "Request size %zu exceeds Host Command RX buffer size %zu",
		     rx_size, hc->rx_ctx.len_max);

	k_sem_reset(&send_called);

	rx_header->struct_version = 3;
	rx_header->checksum = 0;
	rx_header->command = args->command;
	rx_header->command_version = args->version;
	rx_header->data_len = args->input_buf_size;
	rx_header->reserved = 0;

	if (args->input_buf_size > 0) {
		zassert_not_null(args->input_buf,
				 "input_buf is NULL with size %u",
				 args->input_buf_size);
		memcpy(hc->rx_ctx.buf + sizeof(*rx_header), args->input_buf,
		       args->input_buf_size);
	}
	rx_header->checksum = cal_checksum(hc->rx_ctx.buf, rx_size);
	hc->rx_ctx.len = rx_size;

	if (requested_len > orig_tx_len_max) {
		LOG_WRN("requested response size %zu exceeds Host Command TX "
			"buffer size %zu. Truncating.",
			requested_len, orig_tx_len_max);
		args->output_buf_max = orig_tx_len_max - sizeof(*tx_header);
	} else {
		hc->tx.len_max = requested_len;
	}

	hc->backend = &test_hc_backend;
	ec_host_cmd_rx_notify();

	/* Ensure send was called so we can verify outputs */
	rv = k_sem_take(&send_called,
			K_MSEC(CONFIG_TEST_UTILS_HOST_CMD_RESPONSE_TIMEOUT_MS));
	hc->backend = orig_backend;
	hc->tx.len_max = orig_tx_len_max;

	zassert_equal(rv, 0, "Send was not called after %dms",
		      CONFIG_TEST_UTILS_HOST_CMD_RESPONSE_TIMEOUT_MS);

	zassert_true(hc->tx.len >= sizeof(*tx_header),
		     "Response length %zu smaller than header %zu", hc->tx.len,
		     sizeof(*tx_header));
	args->output_buf_size = hc->tx.len - sizeof(*tx_header);
	zassert_true(args->output_buf_size <= args->output_buf_max,
		     "Response data size %u exceeds output_buf_max %u",
		     args->output_buf_size, args->output_buf_max);
	if (args->output_buf_size > 0) {
		zassert_not_null(args->output_buf,
				 "output_buf is NULL with response size %u",
				 args->output_buf_size);
		memcpy(args->output_buf,
		       (uint8_t *)hc->tx.buf + sizeof(*tx_header),
		       args->output_buf_size);
	}

	return tx_header->result;
}

void host_command_received(struct host_cmd_handler_args *args)
{
	host_command_process(args);
}
