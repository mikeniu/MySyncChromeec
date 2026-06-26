/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "config.h"
#include "ec_commands.h"
#include "host_command.h"
#include "uart.h"

static enum ec_host_cmd_status
host_command_console_snapshot(struct ec_host_cmd_handler_args *args)
{
	return uart_console_read_buffer_init() == EC_RES_SUCCESS ?
		       EC_HOST_CMD_SUCCESS :
		       EC_HOST_CMD_ERROR;
}
EC_HOST_CMD_HANDLER_UNBOUND(EC_CMD_CONSOLE_SNAPSHOT,
			    host_command_console_snapshot, EC_VER_MASK(0));

static enum ec_host_cmd_status
host_command_console_read(struct ec_host_cmd_handler_args *args)
{
	enum ec_status res;

	if (args->version == 0) {
		/*
		 * Prior versions of this command only support reading from
		 * an entire snapshot, not just the output since the last
		 * snapshot.
		 */
		res = uart_console_read_buffer(CONSOLE_READ_NEXT,
					       (char *)args->output_buf,
					       args->output_buf_max,
					       &args->output_buf_size);
	} else if (IS_ENABLED(CONFIG_CONSOLE_ENABLE_READ_V1) &&
		   args->version == 1) {
		const struct ec_params_console_read_v1 *p;

		if (args->input_buf_size <
		    sizeof(struct ec_params_console_read_v1))
			return EC_HOST_CMD_REQUEST_TRUNCATED;

		/* Check the params to figure out where to start reading. */
		p = args->input_buf;
		res = uart_console_read_buffer(p->subcmd,
					       (char *)args->output_buf,
					       args->output_buf_max,
					       &args->output_buf_size);
	} else {
		return EC_HOST_CMD_INVALID_PARAM;
	}

	return res == EC_RES_SUCCESS ? EC_HOST_CMD_SUCCESS : EC_HOST_CMD_ERROR;
}

#ifdef CONFIG_CONSOLE_ENABLE_READ_V1
#define READ_V1_MASK EC_VER_MASK(1)
#else
#define READ_V1_MASK 0
#endif

EC_HOST_CMD_HANDLER_UNBOUND(EC_CMD_CONSOLE_READ, host_command_console_read,
			    EC_VER_MASK(0) | READ_V1_MASK);
