/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_TEST_TEST_UTILS_HOST_COMMAND_TEST_UTILS_H_
#define PLATFORM_EC_ZEPHYR_TEST_TEST_UTILS_HOST_COMMAND_TEST_UTILS_H_

#include "common.h"
#include "ec_commands.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CROS_EC_COMMAND_INFO struct host_cmd_handler_args

uint16_t host_command_process(CROS_EC_COMMAND_INFO *args);

static inline void stub_send_response_callback(CROS_EC_COMMAND_INFO *args)
{
	ARG_UNUSED(args);
}

#define BUILD_HOST_COMMAND(CMD, VERSION, RESPONSE, PARAMS)              \
	{                                                               \
		.reserved = stub_send_response_callback,                \
		.command = (CMD),                                       \
		.version = (VERSION),                                   \
		COND_CODE_0(IS_EMPTY(PARAMS),                           \
			    (.input_buf = &(PARAMS),                    \
			     .input_buf_size = sizeof(PARAMS)),         \
			    (.input_buf = NULL, .input_buf_size = 0)),  \
		COND_CODE_0(IS_EMPTY(RESPONSE),                         \
			    (.output_buf = &(RESPONSE),                 \
			     .output_buf_max = sizeof(RESPONSE)),       \
			    (.output_buf = NULL, .output_buf_max = 0)), \
		.output_buf_size = 0,                                   \
	}

#define BUILD_HOST_COMMAND_RESPONSE(CMD, VERSION, RESPONSE) \
	BUILD_HOST_COMMAND(CMD, VERSION, RESPONSE, EMPTY)

#define BUILD_HOST_COMMAND_PARAMS(CMD, VERSION, PARAMS) \
	BUILD_HOST_COMMAND(CMD, VERSION, EMPTY, PARAMS)

#define BUILD_HOST_COMMAND_SIMPLE(CMD, VERSION) \
	BUILD_HOST_COMMAND(CMD, VERSION, EMPTY, EMPTY)

static inline int CROS_EC_COMMAND(CROS_EC_COMMAND_INFO *handle,
				  uint16_t command, uint8_t version,
				  const void *params, uint16_t params_size,
				  void *response, uint16_t response_size)
{
	CROS_EC_COMMAND_INFO args;
	int rv;

	if (handle == NULL)
		handle = &args;

	handle->reserved = (void *)stub_send_response_callback;
	handle->command = command;
	handle->version = version;
	handle->input_buf = params;
	handle->input_buf_size = params_size;
	handle->output_buf = response;
	handle->output_buf_max = response_size;
	handle->output_buf_size = 0;
#ifndef CONFIG_EC_HOST_CMD
	handle->result = 0;
#endif

	rv = host_command_process(handle);
#ifndef CONFIG_EC_HOST_CMD
	if (handle->result != EC_RES_SUCCESS)
		return handle->result;
#endif

	return rv;
}

#include "ec_cmd_api.h"

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_EC_ZEPHYR_TEST_TEST_UTILS_HOST_COMMAND_TEST_UTILS_H_ */
