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
	rv = system_set_bbram(SYSTEM_BBRAM_IDX_CHG_LIMIT_FLAGS, flags);
	if (rv)
		return rv;

	return EC_SUCCESS;
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

	/*
	 * Validate BBRAM values:
	 * If disabled (lower == -1 && upper == -1), return disabled.
	 * If uninitialized / zeroed (e.g. lower == 0 && upper == 0) or
	 * invalid ranges (lower < 0, upper > 100, or lower > upper),
	 * treat as uninitialized and return disabled (-1, -1, 0).
	 */
	if (l == -1 && u == -1) {
		*lower = -1;
		*upper = -1;
		*flags = 0;
		return EC_SUCCESS;
	}

	if (0 <= l && l <= u && u <= 100 && !(l == 0 && u == 0)) {
		*lower = l;
		*upper = u;
		*flags = raw_flags;
		return EC_SUCCESS;
	}

	/* Invalid or uninitialized BBRAM data -> default to disabled */
	*lower = -1;
	*upper = -1;
	*flags = 0;
	return EC_SUCCESS;
}

#if !defined(CONFIG_CHARGER) && !defined(CONFIG_PLATFORM_EC_CHARGER)
static enum ec_host_cmd_status
charge_control_offload_cmd(struct ec_host_cmd_handler_args *args)
{
	const struct ec_params_charge_control *p = args->input_buf;
	struct ec_response_charge_control *r = args->output_buf;
	int8_t lower, upper;
	uint8_t flags;
	int rv;

	if (p->cmd == EC_CHARGE_CONTROL_CMD_SET) {
		if (p->mode == CHARGE_CONTROL_NORMAL) {
			lower = p->sustain_soc.lower;
			upper = p->sustain_soc.upper;
			if (lower == -1 || upper == -1) {
				lower = -1;
				upper = -1;
				flags = 0;
			} else if (0 <= lower && lower <= upper &&
				   upper <= 100) {
				if (args->version == 2) {
					flags = (lower < upper) ?
							EC_CHARGE_CONTROL_FLAG_NO_IDLE :
							0;
				} else {
					flags = p->flags;
				}
			} else {
				return EC_HOST_CMD_INVALID_PARAM;
			}
		} else {
			lower = -1;
			upper = -1;
			flags = 0;
		}

#if defined(CONFIG_CHARGE_CONTROL_PERSIST_TO_BBRAM)
		rv = charge_control_save_to_bbram(lower, upper, flags);
		if (rv != EC_SUCCESS)
			return EC_HOST_CMD_ERROR;
#endif
		return EC_HOST_CMD_SUCCESS;
	} else if (p->cmd == EC_CHARGE_CONTROL_CMD_GET) {
		if (args->output_buf_max < sizeof(*r))
			return EC_HOST_CMD_RESPONSE_TOO_BIG;

		lower = -1;
		upper = -1;
		flags = 0;

#if defined(CONFIG_CHARGE_CONTROL_PERSIST_TO_BBRAM)
		charge_control_load_from_bbram(&lower, &upper, &flags);
#endif

		r->mode = CHARGE_CONTROL_NORMAL;
		r->sustain_soc.lower = lower;
		r->sustain_soc.upper = upper;
		if (args->version > 2)
			r->flags = flags;
		args->output_buf_size = sizeof(*r);
		return EC_HOST_CMD_SUCCESS;
	}

	return EC_HOST_CMD_INVALID_PARAM;
}
EC_HOST_CMD_HANDLER_REQ_ONLY(EC_CMD_CHARGE_CONTROL, charge_control_offload_cmd,
			     EC_VER_MASK(2) | EC_VER_MASK(3),
			     struct ec_params_charge_control);
#endif /* !CONFIG_CHARGER && !CONFIG_PLATFORM_EC_CHARGER */
