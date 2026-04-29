/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cros_board_info.h"
#include "cros_cbi.h"
#include "hooks.h"
#include "keyboard_mkbp.h"
#include "keyboard_protocol.h"
#include "keyboard_scan.h"
#include "util.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <zephyr/input/input.h>
#include <zephyr/input/input_kbd_matrix.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(mica_keyboard, LOG_LEVEL_INF);

#define CROS_EC_KEYBOARD_NODE DT_CHOSEN(cros_ec_keyboard)

static const struct device *const kbd_dev =
	DEVICE_DT_GET(CROS_EC_KEYBOARD_NODE);

static const uint8_t kb_default_mask[] = {
    0x08, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xa4, 0xff, 0xf7, 0x55, 0xff,
    0xff, 0x40, 0x0a, 0x40, 0xff, 0xff,
};

static void input_kbd_actual_key_mask_replace(void)
{
	int row;
	int col;
	bool enabled;
	int ret;

	for (col = 0; col < 18; col++) {
		for (row = 0; row < 8; row++) {
			enabled = !!(kb_default_mask[col] & BIT(row));

			ret = input_kbd_matrix_actual_key_mask_set(
				kbd_dev,
				row,
				col,
				enabled);

			if (ret) {
				LOG_INF("Set fail r=%d c=%d ret=%d",
				       row, col, ret);
			}
		}
	}

	LOG_INF("Keyboard actual_key_mask replace done");
}

static int kb_init(void)
{
	uint32_t board_id = 0;
	int rv;
	const struct input_kbd_matrix_common_config *cfg;

    rv = cbi_get_board_version(&board_id);
    if (rv != EC_SUCCESS) {
		LOG_INF("Error CBI board version, Fallback to Keyboard proto");
        return 0;
	}

	cfg = kbd_dev->config;

	LOG_INF("board_id is : %2x", board_id);
 	if (board_id > 0) {
		LOG_INF("Use keyboard default");
		input_kbd_actual_key_mask_replace();
	} else {
		LOG_INF("Use keyboard proto");
	}

	return 0;
}

SYS_INIT(kb_init, APPLICATION, 99);
