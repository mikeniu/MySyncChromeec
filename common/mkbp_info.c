/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* MKBP info host command */

#include "common.h"
#include "console.h"
#include "ec_commands.h"
#include "host_command.h"
#include "keyboard_config.h"
#include "keyboard_mkbp.h"
#include "keyboard_scan.h"
#include "mkbp_info.h"
#include "mkbp_input_devices.h"
#include "util.h"

__overridable int mkbp_support_volume_buttons(void)
{
#ifdef CONFIG_VOLUME_BUTTONS
	return 1;
#else
	return 0;
#endif
}

test_export_static uint32_t get_supported_buttons(void)
{
	uint32_t val = 0;

	if (mkbp_support_volume_buttons()) {
		val |= BIT(EC_MKBP_VOL_UP) | BIT(EC_MKBP_VOL_DOWN);
	}

#ifdef CONFIG_DEDICATED_RECOVERY_BUTTON
	val |= BIT(EC_MKBP_RECOVERY);
#endif /* defined(CONFIG_DEDICATED_RECOVERY_BUTTON) */

#ifdef CONFIG_POWER_BUTTON
	val |= BIT(EC_MKBP_POWER_BUTTON);
#endif /* defined(CONFIG_POWER_BUTTON) */

	return val;
}

test_export_static uint32_t get_supported_switches(void)
{
	uint32_t val = 0;

#ifdef CONFIG_LID_SWITCH
	val |= BIT(EC_MKBP_LID_OPEN);
#endif
#ifdef CONFIG_TABLET_MODE_SWITCH
	val |= BIT(EC_MKBP_TABLET_MODE);
#endif
#ifdef CONFIG_BASE_ATTACHED_SWITCH
	val |= BIT(EC_MKBP_BASE_ATTACHED);
#endif
#ifdef CONFIG_FRONT_PROXIMITY_SWITCH
	val |= BIT(EC_MKBP_FRONT_PROXIMITY);
#endif
	return val;
}

static enum ec_host_cmd_status
mkbp_get_info(struct ec_host_cmd_handler_args *args)
{
	const struct ec_params_mkbp_info *p = args->input_buf;

	if (args->input_buf_size == 0 || p->info_type == EC_MKBP_INFO_KBD) {
		struct ec_response_mkbp_info *r = args->output_buf;

		if (args->output_buf_max < sizeof(struct ec_response_mkbp_info))
			return EC_HOST_CMD_RESPONSE_TOO_BIG;

#ifdef CONFIG_KEYBOARD_PROTOCOL_MKBP
		/* Version 0 just returns info about the keyboard. */
		r->rows = KEYBOARD_ROWS;
		r->cols = keyboard_cols;
#else
		r->rows = 0;
		r->cols = 0;
#endif /* CONFIG_KEYBOARD_PROTOCOL_MKBP */

		/* This used to be "switches" which was previously 0. */
		r->reserved = 0;

		args->output_buf_size = sizeof(struct ec_response_mkbp_info);
	} else {
		union ec_response_get_next_data *r = args->output_buf;

		if (args->input_buf_size < sizeof(*p))
			return EC_HOST_CMD_INVALID_PARAM;

		/* Version 1 (other than EC_MKBP_INFO_KBD) */
		switch (p->info_type) {
		case EC_MKBP_INFO_SUPPORTED:
			switch (p->event_type) {
			case EC_MKBP_EVENT_BUTTON:
				if (args->output_buf_max < sizeof(r->buttons))
					return EC_HOST_CMD_RESPONSE_TOO_BIG;
				r->buttons = get_supported_buttons();
				args->output_buf_size = sizeof(r->buttons);
				break;

			case EC_MKBP_EVENT_SWITCH:
				if (args->output_buf_max < sizeof(r->switches))
					return EC_HOST_CMD_RESPONSE_TOO_BIG;
				r->switches = get_supported_switches();
				args->output_buf_size = sizeof(r->switches);
				break;

			default:
				/* Don't care for now for other types. */
				return EC_HOST_CMD_INVALID_PARAM;
			}
			break;

		case EC_MKBP_INFO_CURRENT:
			switch (p->event_type) {
#ifdef HAS_TASK_KEYSCAN
			case EC_MKBP_EVENT_KEY_MATRIX:
				if (args->output_buf_max <
				    sizeof(r->key_matrix))
					return EC_HOST_CMD_RESPONSE_TOO_BIG;
				memcpy(r->key_matrix, keyboard_scan_get_state(),
				       sizeof(r->key_matrix));
				args->output_buf_size = sizeof(r->key_matrix);
				break;
#endif
			case EC_MKBP_EVENT_HOST_EVENT:
				if (args->output_buf_max <
				    sizeof(r->host_event))
					return EC_HOST_CMD_RESPONSE_TOO_BIG;
				r->host_event = (uint32_t)host_get_events();
				args->output_buf_size = sizeof(r->host_event);
				break;

			case EC_MKBP_EVENT_HOST_EVENT64:
				if (args->output_buf_max <
				    sizeof(r->host_event64))
					return EC_HOST_CMD_RESPONSE_TOO_BIG;
				r->host_event64 = host_get_events();
				args->output_buf_size = sizeof(r->host_event64);
				break;

#ifdef CONFIG_MKBP_INPUT_DEVICES
			case EC_MKBP_EVENT_BUTTON:
				if (args->output_buf_max < sizeof(r->buttons))
					return EC_HOST_CMD_RESPONSE_TOO_BIG;
				r->buttons = mkbp_get_button_state();
				args->output_buf_size = sizeof(r->buttons);
				break;

			case EC_MKBP_EVENT_SWITCH:
				if (args->output_buf_max < sizeof(r->switches))
					return EC_HOST_CMD_RESPONSE_TOO_BIG;
				r->switches = mkbp_get_switch_state();
				args->output_buf_size = sizeof(r->switches);
				break;
#endif /* CONFIG_MKBP_INPUT_DEVICES */

			default:
				/* Doesn't make sense for other event types. */
				return EC_HOST_CMD_INVALID_PARAM;
			}
			break;

		default:
			/* Unsupported query. */
			return EC_HOST_CMD_ERROR;
		}
	}
	return EC_HOST_CMD_SUCCESS;
}
/*
 * Version 0 has 0 request bytes and returns 9 bytes.
 * Version 1 has 2 request bytes and returns 4-13 bytes depending on subcommand.
 * We use RESP_ONLY so .min_rqt_size is 0 (required to allow v0's 0-byte
 * request). We specify uint32_t (4 bytes) as response type to set .min_rsp_size
 * to 4, which is the minimum response size returned by any version/subcommand.
 */
EC_HOST_CMD_HANDLER_RESP_ONLY(EC_CMD_MKBP_INFO, mkbp_get_info,
			      EC_VER_MASK(0) | EC_VER_MASK(1), uint32_t);
