/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "cros_cbi.h"
#include "gpio.h"
#include "gpio/gpio_int.h"
#include "hooks.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/input/input.h>
#include <zephyr/input/input_kbd_matrix.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(pic_keyboard, LOG_LEVEL_INF);

#define CROS_EC_KEYBOARD_NODE DT_CHOSEN(cros_ec_keyboard)
#define KBD_COLS 18
#define KBD_ROWS 8

static const struct device *const kbd_dev =
	DEVICE_DT_GET(CROS_EC_KEYBOARD_NODE);

/*
 * Bits to OR onto the US dtsi mask. Only extra physical keys for that SKU.
 *
 * ISO: Europe2 at (r=2,c=7), plus C9/C10 international intersections.
 * JP:  spreadsheet extras (0,6) (1,12) (3,12) (2,17) (6,17), and C16 remainder.
 */
static const uint8_t kb_mask_uk_extra[KBD_COLS] = {
	[7] = 0x04,
	[9] = 0x05,
	[10] = 0x54,
};

static const uint8_t kb_mask_jp_extra[KBD_COLS] = {
	[6] = 0x01,
	[12] = 0x0a,
	[16] = 0xfe,
	[17] = 0x44,
};

static void kbd_mask_or_extra(const uint8_t *extra)
{
	int col;
	int row;
	int ret;

	for (col = 0; col < KBD_COLS; col++) {
		if (!extra[col])
			continue;

		for (row = 0; row < KBD_ROWS; row++) {
			if (!(extra[col] & BIT(row)))
				continue;

			ret = input_kbd_matrix_actual_key_mask_set(kbd_dev, row,
								   col, true);
			if (ret)
				LOG_ERR("mask set fail r=%d c=%d ret=%d", row,
					col, ret);
		}
	}
}

test_export_static void kb_layout_init(void)
{
	if (cros_cbi_ufsc_check_match(
		    CBI_UFSC_VALUE_ID(DT_NODELABEL(ufsc_kb_layout_uk)))) {
		kbd_mask_or_extra(kb_mask_uk_extra);
		LOG_INF("UFSC KB_LAYOUT=UK, extra key mask applied");
	} else if (cros_cbi_ufsc_check_match(CBI_UFSC_VALUE_ID(
			   DT_NODELABEL(ufsc_kb_layout_jp)))) {
		kbd_mask_or_extra(kb_mask_jp_extra);
		LOG_INF("UFSC KB_LAYOUT=JP, extra key mask applied");
	} else {
		LOG_INF("UFSC KB_LAYOUT=US, dtsi base mask");
	}
}
DECLARE_HOOK(HOOK_INIT, kb_layout_init, HOOK_PRIO_POST_FIRST);

static void pic_kbd_matrix_log_cb(struct input_event *evt, void *user_data)
{
	static int row;
	static int col;
	static bool pressed;
	static uint8_t col_byte[KBD_COLS];

	switch (evt->code) {
	case INPUT_ABS_X:
		col = evt->value;
		break;
	case INPUT_ABS_Y:
		row = evt->value;
		break;
	case INPUT_BTN_TOUCH:
		pressed = evt->value;
		break;
	}

	if (!evt->sync)
		return;

	if (col < 0 || col >= KBD_COLS || row < 0 || row >= KBD_ROWS) {
		LOG_ERR("kbd matrix invalid r=%d c=%d", row, col);
		return;
	}

	if (pressed)
		col_byte[col] |= BIT(row);
	else
		col_byte[col] &= ~BIT(row);

	LOG_DBG("kbd matrix r=%d c=%d pressed=%d bit=0x%02x C=0x%02x", row, col,
		pressed, (unsigned int)BIT(row), (unsigned int)col_byte[col]);
}
INPUT_CALLBACK_DEFINE(kbd_dev, pic_kbd_matrix_log_cb, NULL);

/* Trigger Zoss coverage check */
