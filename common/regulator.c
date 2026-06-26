/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Regulator control module for Chrome EC */

#include "common.h"
#include "console.h"
#include "ec_commands.h"
#include "host_command.h"
#include "regulator.h"

static enum ec_host_cmd_status
hc_regulator_get_info(struct ec_host_cmd_handler_args *args)
{
	const struct ec_params_regulator_get_info *p = args->input_buf;
	struct ec_response_regulator_get_info *r = args->output_buf;
	void *voltages_mv = r->voltages_mv;
	uint16_t num_voltages;
	int rv;

	if (args->output_buf_max < sizeof(*r))
		return EC_HOST_CMD_INVALID_RESPONSE;

	rv = board_regulator_get_info(p->index, r->name, &num_voltages,
				      voltages_mv);

	if (rv)
		return EC_HOST_CMD_ERROR;

	r->num_voltages = num_voltages;
	args->output_buf_size = sizeof(*r);
	return EC_HOST_CMD_SUCCESS;
}
EC_HOST_CMD_HANDLER(EC_CMD_REGULATOR_GET_INFO, hc_regulator_get_info,
		    EC_VER_MASK(0), struct ec_params_regulator_get_info,
		    struct ec_response_regulator_get_info);

static enum ec_host_cmd_status
hc_regulator_enable(struct ec_host_cmd_handler_args *args)
{
	const struct ec_params_regulator_enable *p = args->input_buf;
	int rv;

	rv = board_regulator_enable(p->index, p->enable);

	if (rv)
		return EC_HOST_CMD_ERROR;

	return EC_HOST_CMD_SUCCESS;
}
EC_HOST_CMD_HANDLER_REQ_ONLY(EC_CMD_REGULATOR_ENABLE, hc_regulator_enable,
			     EC_VER_MASK(0), struct ec_params_regulator_enable);

static enum ec_host_cmd_status
hc_regulator_is_enabled(struct ec_host_cmd_handler_args *args)
{
	const struct ec_params_regulator_is_enabled *p = args->input_buf;
	struct ec_response_regulator_is_enabled *r = args->output_buf;
	int rv;

	rv = board_regulator_is_enabled(p->index, &r->enabled);

	if (rv)
		return EC_HOST_CMD_ERROR;

	args->output_buf_size = sizeof(*r);
	return EC_HOST_CMD_SUCCESS;
}
EC_HOST_CMD_HANDLER(EC_CMD_REGULATOR_IS_ENABLED, hc_regulator_is_enabled,
		    EC_VER_MASK(0), struct ec_params_regulator_is_enabled,
		    struct ec_response_regulator_is_enabled);

static enum ec_host_cmd_status
hc_regulator_get_voltage(struct ec_host_cmd_handler_args *args)
{
	const struct ec_params_regulator_get_voltage *p = args->input_buf;
	struct ec_response_regulator_get_voltage *r = args->output_buf;
	uint32_t voltage_mv;
	int rv;

	rv = board_regulator_get_voltage(p->index, &voltage_mv);

	if (rv)
		return EC_HOST_CMD_ERROR;

	r->voltage_mv = voltage_mv;
	args->output_buf_size = sizeof(*r);
	return EC_HOST_CMD_SUCCESS;
}
EC_HOST_CMD_HANDLER(EC_CMD_REGULATOR_GET_VOLTAGE, hc_regulator_get_voltage,
		    EC_VER_MASK(0), struct ec_params_regulator_get_voltage,
		    struct ec_response_regulator_get_voltage);

static enum ec_host_cmd_status
hc_regulator_set_voltage(struct ec_host_cmd_handler_args *args)
{
	const struct ec_params_regulator_set_voltage *p = args->input_buf;
	int rv;

	rv = board_regulator_set_voltage(p->index, p->min_mv, p->max_mv);

	if (rv)
		return EC_HOST_CMD_ERROR;

	return EC_HOST_CMD_SUCCESS;
}
EC_HOST_CMD_HANDLER_REQ_ONLY(EC_CMD_REGULATOR_SET_VOLTAGE,
			     hc_regulator_set_voltage, EC_VER_MASK(0),
			     struct ec_params_regulator_set_voltage);
