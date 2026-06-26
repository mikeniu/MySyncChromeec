/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "host_command.h"
#include "printf.h"
#include "system.h"

#include <zephyr/drivers/entropy.h>
#include <zephyr/kernel.h>

#define rng DEVICE_DT_GET(DT_CHOSEN(zephyr_entropy))

void trng_rand_bytes(void *buffer, size_t len)
{
	/*
	 * In EC, we use size_t to represent buffer size, but Zephyr uses
	 * uint16_t. Crash the system if user requested more than UINT16_MAX
	 * bytes.
	 */
	if (!device_is_ready(rng) || len > UINT16_MAX)
		k_oops();

	if (entropy_get_entropy(rng, (uint8_t *)buffer, (uint16_t)len))
		k_oops();
}

#if defined(CONFIG_PLATFORM_EC_CONSOLE_CMD_RAND)
static int command_rand(const struct shell *shell, int argc, const char **argv)
{
	uint8_t data[32];
	char str_buf[hex_str_buf_size(sizeof(data))];

	trng_rand_bytes(data, sizeof(data));

	snprintf_hex_buffer(str_buf, sizeof(str_buf),
			    HEX_BUF(data, sizeof(data)));
	shell_fprintf(shell, SHELL_NORMAL, "rand %s\n", str_buf);

	return EC_SUCCESS;
}
SHELL_CMD_REGISTER(rand, NULL, "Output random bytes to console.", command_rand);
#endif

#if defined(CONFIG_PLATFORM_EC_HOSTCMD_RAND)
static enum ec_host_cmd_status
host_command_rand(struct ec_host_cmd_handler_args *args)
{
	const struct ec_params_rand_num *p = args->input_buf;
	struct ec_response_rand_num *r = args->output_buf;
	uint16_t num_rand_bytes = p->num_rand_bytes;

	if (system_is_locked())
		return EC_HOST_CMD_ACCESS_DENIED;

	if (num_rand_bytes > args->output_buf_max)
		return EC_HOST_CMD_OVERFLOW;

	trng_rand_bytes(r->rand, num_rand_bytes);

	args->output_buf_size = num_rand_bytes;

	return EC_HOST_CMD_SUCCESS;
}

EC_HOST_CMD_HANDLER_REQ_ONLY(EC_CMD_RAND_NUM, host_command_rand,
			     EC_VER_MASK(EC_VER_RAND_NUM),
			     struct ec_params_rand_num);
#endif
