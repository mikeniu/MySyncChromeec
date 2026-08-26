/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "host_command.h"
#include "task.h"

#include <zephyr/kernel.h>
#include <zephyr/mgmt/ec_host_cmd/ec_host_cmd.h>
#include <zephyr/sys/iterable_sections.h>

#ifndef CONFIG_ZTEST
#ifndef CONFIG_TASK_HOSTCMD_THREAD_MAIN
#error "The legacy Host Command subsystem is supported only with reusing " \
	"the main thread. Please enable CONFIG_TASK_HOSTCMD_THREAD_MAIN."
#endif
#endif /* !CONFIG_ZTEST */

struct host_command *zephyr_find_host_command(int command)
{
	STRUCT_SECTION_FOREACH(host_command, cmd)
	{
		if (cmd->command == command)
			return cmd;
	}

	return NULL;
}

static enum ec_host_cmd_status prepare_response(uint8_t *tx_buf, size_t tx_max,
						enum ec_status result,
						uint16_t data_len,
						size_t *tx_len)
{
	struct ec_host_response *r = (struct ec_host_response *)tx_buf;
	size_t total_len = sizeof(*r) + data_len;
	uint8_t sum = 0;

	if (result != EC_RES_SUCCESS) {
		data_len = 0;
		total_len = sizeof(*r);
	} else if (total_len > tx_max) {
		result = EC_RES_RESPONSE_TOO_BIG;
		data_len = 0;
		total_len = sizeof(*r);
	}

	r->struct_version = EC_HOST_RESPONSE_VERSION;
	r->checksum = 0;
	r->result = result;
	r->data_len = data_len;
	r->reserved = 0;

	for (size_t i = 0; i < total_len; i++) {
		sum += tx_buf[i];
	}
	r->checksum = (uint8_t)(-sum);

	if (tx_len) {
		*tx_len = total_len;
	}

	return (enum ec_host_cmd_status)result;
}

enum ec_host_cmd_status
ec_host_cmd_process_packet(const uint8_t *rx_buf, size_t rx_len,
			   uint8_t *tx_buf, size_t tx_max, size_t *tx_len)
{
	if (!rx_buf || !tx_buf || tx_max < sizeof(struct ec_host_response)) {
		if (tx_len) {
			*tx_len = 0;
		}
		return EC_HOST_CMD_INVALID_PARAM;
	}

	if (rx_len < sizeof(struct ec_host_request)) {
		return prepare_response(tx_buf, tx_max,
					EC_RES_REQUEST_TRUNCATED, 0, tx_len);
	}

	const struct ec_host_request *r =
		(const struct ec_host_request *)rx_buf;

	if (r->struct_version != EC_HOST_REQUEST_VERSION) {
		return prepare_response(tx_buf, tx_max, EC_RES_INVALID_HEADER,
					0, tx_len);
	}

	if (rx_len < sizeof(*r) + r->data_len) {
		return prepare_response(tx_buf, tx_max,
					EC_RES_REQUEST_TRUNCATED, 0, tx_len);
	}

	uint8_t csum = 0;
	for (size_t i = 0; i < sizeof(*r) + r->data_len; i++) {
		csum += rx_buf[i];
	}
	if (csum != 0) {
		return prepare_response(tx_buf, tx_max, EC_RES_INVALID_CHECKSUM,
					0, tx_len);
	}

	struct host_cmd_handler_args args = {
		.command = r->command,
		.version = r->command_version,
		.params = rx_buf + sizeof(*r),
		.params_size = r->data_len,
		.response = tx_buf + sizeof(struct ec_host_response),
		.response_max = tx_max - sizeof(struct ec_host_response),
		.response_size = 0,
	};

	enum ec_status rv = host_command_process(&args);

	return prepare_response(tx_buf, tx_max, rv, args.response_size, tx_len);
}

void host_command_main(void)
{
	k_thread_priority_set(get_main_thread(),
			      EC_TASK_PRIORITY(EC_TASK_HOSTCMD_PRIO));
	k_thread_name_set(get_main_thread(), "HOSTCMD");
	host_command_task(NULL);
}
