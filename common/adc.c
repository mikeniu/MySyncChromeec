/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* ADC module for Chrome EC */

#include "adc.h"
#include "common.h"
#include "console.h"
#include "ec_commands.h"
#include "host_command.h"
#include "util.h"

/* 'adc' console command is not supported in continuous mode */
#ifndef CONFIG_ADC_PROFILE_FAST_CONTINUOUS

static int print_one_adc(int channel)
{
	int v;

	v = adc_read_channel(channel);
	if (v == ADC_READ_ERROR)
		return EC_ERROR_UNKNOWN;
	ccprintf("[%d] %s = %d mV\n", channel, adc_channels[channel].name, v);
	return EC_SUCCESS;
}

static int command_adc(int argc, const char **argv)
{
	int i, ret;

	/* If a channel is specified, read only that one */
	if (argc == 2) {
		char *end;

		i = strtoi(argv[1], &end, 0);
		if (*end || i < 0 || i >= ADC_CH_COUNT)
			return EC_ERROR_PARAM1;
		return print_one_adc(i);
	} else {
		/* Otherwise print them all */
		for (i = 0; i < ADC_CH_COUNT; ++i) {
			ret = print_one_adc(i);
			if (ret)
				return ret;
		}
		return EC_SUCCESS;
	}
}
DECLARE_CONSOLE_COMMAND(adc, command_adc, "[id]", NULL);

static enum ec_host_cmd_status
hc_adc_read(struct ec_host_cmd_handler_args *args)
{
	const struct ec_params_adc_read *params = args->input_buf;
	struct ec_response_adc_read *resp = args->output_buf;
	enum adc_channel ch = (enum adc_channel)params->adc_channel;
	int32_t adc_value;

	if (ch >= ADC_CH_COUNT)
		return EC_HOST_CMD_INVALID_PARAM;

	adc_value = adc_read_channel(ch);
	if (adc_value == ADC_READ_ERROR)
		return EC_HOST_CMD_ERROR;

	resp->adc_value = adc_value;
	args->output_buf_size = sizeof(*resp);
	return EC_HOST_CMD_SUCCESS;
}
EC_HOST_CMD_HANDLER(EC_CMD_ADC_READ, hc_adc_read, EC_VER_MASK(0),
		    struct ec_params_adc_read, struct ec_response_adc_read);
#endif /* CONFIG_ADC_PROFILE_FAST_CONTINUOUS */
