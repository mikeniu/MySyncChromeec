/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Implement the stub host_command_process function for tests with the upstream
 * Host Command subsystem.
 */

#include "host_command.h"

#include <zephyr/mgmt/ec_host_cmd/ec_host_cmd.h>

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
	host_command_process(args);
}
