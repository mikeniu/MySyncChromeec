/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charge_state.h"
#include "common.h"
#include "ec_commands.h"
#include "host_command.h"
#include "system.h"

int charge_control_save_to_bbram(int8_t lower, int8_t upper, uint8_t flags)
{
	int rv;

	rv = system_set_bbram(SYSTEM_BBRAM_IDX_CHG_LIMIT_LOWER, (uint8_t)lower);
	if (rv)
		return rv;
	rv = system_set_bbram(SYSTEM_BBRAM_IDX_CHG_LIMIT_UPPER, (uint8_t)upper);
	if (rv)
		return rv;
	return system_set_bbram(SYSTEM_BBRAM_IDX_CHG_LIMIT_FLAGS, flags);
}

int charge_control_load_from_bbram(int8_t *lower, int8_t *upper, uint8_t *flags)
{
	uint8_t raw_lower, raw_upper, raw_flags;
	int8_t l, u;
	int rv;

	rv = system_get_bbram(SYSTEM_BBRAM_IDX_CHG_LIMIT_LOWER, &raw_lower);
	if (rv)
		return rv;
	rv = system_get_bbram(SYSTEM_BBRAM_IDX_CHG_LIMIT_UPPER, &raw_upper);
	if (rv)
		return rv;
	rv = system_get_bbram(SYSTEM_BBRAM_IDX_CHG_LIMIT_FLAGS, &raw_flags);
	if (rv)
		return rv;

	l = (int8_t)raw_lower;
	u = (int8_t)raw_upper;

	/* 1. Explicit disabled state (-1, -1) */
	if (l == CHARGE_CONTROL_SUSTAINER_DISABLED &&
	    u == CHARGE_CONTROL_SUSTAINER_DISABLED) {
		*lower = CHARGE_CONTROL_SUSTAINER_DISABLED;
		*upper = CHARGE_CONTROL_SUSTAINER_DISABLED;
		*flags = 0;
		return EC_SUCCESS;
	}

	/* 2. Uninitialized / zeroed BBRAM on CRC mismatch or power loss (0, 0)
	 */
	if (l == 0 && u == 0) {
		*lower = CHARGE_CONTROL_SUSTAINER_DISABLED;
		*upper = CHARGE_CONTROL_SUSTAINER_DISABLED;
		*flags = 0;
		return EC_SUCCESS;
	}

	/* 3. Invalid SoC range (out of 0-100 bounds or lower > upper) */
	if (l < 0 || l > u || u > 100) {
		*lower = CHARGE_CONTROL_SUSTAINER_DISABLED;
		*upper = CHARGE_CONTROL_SUSTAINER_DISABLED;
		*flags = 0;
		return EC_SUCCESS;
	}

	/* 4. Valid sustainer settings */
	*lower = l;
	*upper = u;
	*flags = raw_flags;
	return EC_SUCCESS;
}

static enum ec_host_cmd_status
charge_command_charge_control(struct ec_host_cmd_handler_args *args)
{
	const struct ec_params_charge_control *p = args->input_buf;
	struct ec_response_charge_control *r = args->output_buf;
	int8_t lower = CHARGE_CONTROL_SUSTAINER_DISABLED;
	int8_t upper = CHARGE_CONTROL_SUSTAINER_DISABLED;
	uint8_t flags = 0;
	int rv;

	if (p->cmd == EC_CHARGE_CONTROL_CMD_SET) {
#if defined(CONFIG_CHARGER) || defined(CONFIG_PLATFORM_EC_CHARGER)
		if (p->mode >= CHARGE_CONTROL_COUNT)
			return EC_HOST_CMD_INVALID_PARAM;
#else
		if (p->mode != CHARGE_CONTROL_NORMAL)
			return EC_HOST_CMD_INVALID_PARAM;
#endif

		if (p->mode == CHARGE_CONTROL_NORMAL) {
			lower = p->sustain_soc.lower;
			upper = p->sustain_soc.upper;
			if (lower == CHARGE_CONTROL_SUSTAINER_DISABLED ||
			    upper == CHARGE_CONTROL_SUSTAINER_DISABLED) {
				lower = CHARGE_CONTROL_SUSTAINER_DISABLED;
				upper = CHARGE_CONTROL_SUSTAINER_DISABLED;
				flags = 0;
			} else if (0 <= lower && lower <= upper &&
				   upper <= 100) {
				if (args->version == 2) {
					/*
					 * V2 uses lower == upper to indicate
					 * NO_IDLE.
					 * TODO: Remove this if-branch once all
					 * OS-side components are updated to v3.
					 */
					flags = (lower < upper) ?
							EC_CHARGE_CONTROL_FLAG_NO_IDLE :
							0;
				} else {
					flags = p->flags;
				}
			} else {
				return EC_HOST_CMD_INVALID_PARAM;
			}
		}

#if defined(CONFIG_CHARGER) || defined(CONFIG_PLATFORM_EC_CHARGER)
		if (lower != CHARGE_CONTROL_SUSTAINER_DISABLED) {
			rv = battery_sustainer_set(lower, upper, flags);
			if (rv == EC_RES_UNAVAILABLE)
				return EC_HOST_CMD_UNAVAILABLE;
			if (rv)
				return EC_HOST_CMD_INVALID_PARAM;
		} else {
			battery_sustainer_disable();
		}

		rv = set_chg_ctrl_mode(p->mode);
		if (rv != EC_SUCCESS)
			return EC_HOST_CMD_ERROR;
#endif

#if defined(CONFIG_CHARGE_CONTROL_PERSIST_TO_BBRAM)
		rv = charge_control_save_to_bbram(lower, upper, flags);
		if (rv != EC_SUCCESS)
			return EC_HOST_CMD_ERROR;
#endif
		return EC_HOST_CMD_SUCCESS;
	} else if (p->cmd == EC_CHARGE_CONTROL_CMD_GET) {
		if (args->output_buf_max < sizeof(*r))
			return EC_HOST_CMD_RESPONSE_TOO_BIG;

#if defined(CONFIG_CHARGER) || defined(CONFIG_PLATFORM_EC_CHARGER)
		r->mode = get_chg_ctrl_mode();
		battery_sustainer_get(&lower, &upper, &flags);
#elif defined(CONFIG_CHARGE_CONTROL_PERSIST_TO_BBRAM)
		r->mode = CHARGE_CONTROL_NORMAL;
		charge_control_load_from_bbram(&lower, &upper, &flags);
#else
		r->mode = CHARGE_CONTROL_NORMAL;
		lower = CHARGE_CONTROL_SUSTAINER_DISABLED;
		upper = CHARGE_CONTROL_SUSTAINER_DISABLED;
		flags = 0;
#endif

		r->sustain_soc.lower = lower;
		r->sustain_soc.upper = upper;
		if (args->version > 2)
			r->flags = flags;
		args->output_buf_size = sizeof(*r);
		return EC_HOST_CMD_SUCCESS;
	}

	return EC_HOST_CMD_INVALID_PARAM;
}
EC_HOST_CMD_HANDLER_REQ_ONLY(EC_CMD_CHARGE_CONTROL,
			     charge_command_charge_control,
			     EC_VER_MASK(2) | EC_VER_MASK(3),
			     struct ec_params_charge_control);
