/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Implement the stub host_command_process function for tests with the upstream
 * Host Command to pass all needed parameters to the backend simulator.
 */

#include "ec_commands.h"
#include "host_command.h"

#include <zephyr/logging/log.h>
#include <zephyr/mgmt/ec_host_cmd/simulator.h>
#include <zephyr/ztest.h>

LOG_MODULE_REGISTER(test_host_command, LOG_LEVEL_INF);

#define RX_HEADER_SIZE sizeof(struct ec_host_response)
#define TX_HEADER_SIZE sizeof(struct ec_host_request)
K_SEM_DEFINE(send_called, 0, 1);
static struct ec_host_cmd_tx_buf *tx_buf;

static int host_send(const struct ec_host_cmd_backend *backend)
{
	k_sem_give(&send_called);

	return 0;
}

static uint8_t cal_checksum(const uint8_t *const buffer, const uint16_t size)
{
	uint8_t checksum = 0;

	for (size_t i = 0; i < size; ++i) {
		checksum += buffer[i];
	}
	return (uint8_t)(-checksum);
}

static uint16_t pass_args_to_sim(struct host_cmd_handler_args *args)
{
	uint8_t rx_buf[args->input_buf_size + RX_HEADER_SIZE];
	struct ec_host_request *rx_header = (struct ec_host_request *)rx_buf;
	struct ec_host_response *tx_header;
	int rv;

	k_sem_reset(&send_called);

	rx_header->struct_version = 3;
	rx_header->checksum = 0;
	rx_header->command = args->command;
	rx_header->command_version = args->version;
	rx_header->data_len = args->input_buf_size;
	rx_header->reserved = 0;

	memcpy(rx_buf + RX_HEADER_SIZE, args->input_buf, args->input_buf_size);
	rx_header->checksum = cal_checksum(rx_buf, sizeof(rx_buf));

	ec_host_cmd_backend_sim_install_send_cb(host_send, &tx_buf);
	static uint16_t original_len_max;
	if (original_len_max == 0) {
		original_len_max = tx_buf->len_max;
	}
	uint16_t requested_len = args->output_buf_max + TX_HEADER_SIZE;
	if (requested_len > original_len_max) {
		LOG_WRN("requested response size %d exceeds Host Command TX "
			"buffer size %d. Truncating.",
			requested_len, original_len_max);
		args->output_buf_max = original_len_max - TX_HEADER_SIZE;
		tx_buf->len_max = original_len_max;
	} else {
		tx_buf->len_max = requested_len;
	}

	/* Pass RX buffer to the backend simulator */
	ec_host_cmd_backend_sim_data_received(rx_buf, sizeof(rx_buf));

	/* Ensure send was called so we can verify outputs */
	rv = k_sem_take(&send_called,
			K_MSEC(CONFIG_TEST_UTILS_HOST_CMD_RESPONSE_TIMEOUT_MS));
	zassert_equal(rv, 0, "Send was not called after %dms",
		      CONFIG_TEST_UTILS_HOST_CMD_RESPONSE_TIMEOUT_MS);

	args->output_buf_size = tx_buf->len - TX_HEADER_SIZE;
	memcpy(args->output_buf, (uint8_t *)tx_buf->buf + TX_HEADER_SIZE,
	       args->output_buf_size);
	tx_header = tx_buf->buf;

	return tx_header->result;
}

uint16_t host_command_process(struct host_cmd_handler_args *args)
{
	return pass_args_to_sim(args);
}

void host_command_received(struct host_cmd_handler_args *args)
{
	pass_args_to_sim(args);
}
