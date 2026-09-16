/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cros_board_info.h"
#include "cros_cbi.h"
#include "hooks.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/input/input.h>
#include <zephyr/input/input_kbd_matrix.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(pic_keyboard, LOG_LEVEL_INF);

#define CROS_EC_KEYBOARD_NODE DT_CHOSEN(cros_ec_keyboard)

static const struct device *const kbd_dev =
	DEVICE_DT_GET(CROS_EC_KEYBOARD_NODE);

/*
 * US keyboard mask (same as keyboard.dtsi actual-key-mask).
 * JP-only keys are masked out: (0,6) (1,12) (3,12) (2,17) (6,17).
 * US-only key (3,11) is enabled.
 */
static const uint8_t kb_mask_us[] = {
	0x08, /* C0  */
	0xff, /* C1  */
	0xff, /* C2  */
	0xff, /* C3  */
	0xff, /* C4  */
	0xff, /* C5  */
	0xfe, /* C6  */
	0xa0, /* C7  */
	0xff, /* C8  */
	0xf2, /* C9  */
	0x01, /* C10 */
	0xff, /* C11 */
	0xc0, /* C12 */
	0x40, /* C13 */
	0x0a, /* C14 */
	0x40, /* C15 */
	0x01, /* C16 */
	0x00, /* C17 */
};

/*
 * UK keyboard mask.
 * Compared to US: mask US-only key (3,11),
 * enable UK-only keys (6,17)(7,17).
 */
static const uint8_t kb_mask_uk[] = {
	0x08, /* C0  */
	0xff, /* C1  */
	0xff, /* C2  */
	0xff, /* C3  */
	0xff, /* C4  */
	0xff, /* C5  */
	0xfe, /* C6  */
	0xa0, /* C7  */
	0xff, /* C8  */
	0xf2, /* C9  */
	0x01, /* C10 */
	0xf7, /* C11 */
	0xc0, /* C12 */
	0x40, /* C13 */
	0x0a, /* C14 */
	0x40, /* C15 */
	0x01, /* C16 */
	0xc0, /* C17 */
};

/*
 * JP keyboard mask.
 * Compared to US: enable JP-only keys (0,6)(1,12)(3,12)(2,17)(6,17),
 * mask US-only key (3,11).
 */
static const uint8_t kb_mask_jp[] = {
	0x08, /* C0  */
	0xff, /* C1  */
	0xff, /* C2  */
	0xff, /* C3  */
	0xff, /* C4  */
	0xff, /* C5  */
	0xff, /* C6  */
	0xa0, /* C7  */
	0xff, /* C8  */
	0xf2, /* C9  */
	0x01, /* C10 */
	0xf7, /* C11 */
	0xca, /* C12 */
	0x40, /* C13 */
	0x0a, /* C14 */
	0x40, /* C15 */
	0x01, /* C16 */
	0x44, /* C17 */
};

static void apply_kb_mask(const uint8_t *mask)
{
	int row;
	int col;
	int ret;

	for (col = 0; col < 18; col++) {
		for (row = 0; row < 8; row++) {
			ret = input_kbd_matrix_actual_key_mask_set(
				kbd_dev, row, col, !!(mask[col] & BIT(row)));
			if (ret) {
				LOG_INF("Set fail r=%d c=%d ret=%d", row, col,
					ret);
			}
		}
	}
}

static int kb_init(void)
{
	uint32_t layout;
	int rv;

	rv = cros_cbi_get_fw_config(FW_KB_LAYOUT, &layout);
	if (rv != 0) {
		LOG_INF("CBI FW_KB_LAYOUT read fail, fallback to US");
		apply_kb_mask(kb_mask_us);
		return 0;
	}

	switch (layout) {
	case FW_KB_LAYOUT_JP:
		apply_kb_mask(kb_mask_jp);

		break;
	case FW_KB_LAYOUT_UK:
		apply_kb_mask(kb_mask_uk);

		break;
	case FW_KB_LAYOUT_US:
	default:
		apply_kb_mask(kb_mask_us);

		break;
	}

	(void)rv;

	return 0;
}

SYS_INIT(kb_init, APPLICATION, 99);
