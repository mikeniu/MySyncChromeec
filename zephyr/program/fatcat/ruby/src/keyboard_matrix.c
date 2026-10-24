/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cros_cbi.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/init.h>
#include <zephyr/input/input_kbd_matrix.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <drivers/keyboard_alt.h>
#include <drivers/scancode_set2.h>
#include <drivers/vivaldi_kbd.h>

LOG_MODULE_REGISTER(ruby_kb_matrix, LOG_LEVEL_INF);

#define KBD_NODE DT_CHOSEN(cros_ec_keyboard)

static const uint8_t kb_alt_key_mask[] = {
	0x53, 0x67, 0x79, 0x2d, 0xfb, 0xad, 0xac, 0xf9, 0xed,
	0xdd, 0xe5, 0xf5, 0x4f, 0xe3, 0x8d, 0xb9, 0xbd, 0xc2,
};
BUILD_ASSERT(ARRAY_SIZE(kb_alt_key_mask) == DT_PROP(KBD_NODE, col_size));

static bool kb_alt;

bool board_ghost_filter_enabled(void)
{
	return !kb_alt;
}

int8_t board_vivaldi_keybd_idx(void)
{
	return kb_alt ? VIVALDI_CFG_IDX(kbd_config_alt) :
			VIVALDI_CFG_IDX(kbd_config_0);
}

static int kb_matrix_init(void)
{
	const struct device *dev = DEVICE_DT_GET(KBD_NODE);

	if (!cros_cbi_ufsc_check_match(
		    CBI_UFSC_VALUE_ID(DT_NODELABEL(ufsc_kb_matrix_alt)))) {
		return 0;
	}

	kb_alt = true;

	set_scancode_set2_table(SCANCODE_SET2_IDX(scancodes_alt));

	for (size_t col = 0; col < ARRAY_SIZE(kb_alt_key_mask); col++) {
		for (size_t row = 0; row < DT_PROP(KBD_NODE, row_size); row++) {
			input_kbd_matrix_actual_key_mask_set(
				dev, row, col, kb_alt_key_mask[col] & BIT(row));
		}
	}

	boot_keys_use_alternate();
	runtime_keys_use_alternate();
	fn_keys_use_alternate();

	LOG_INF("alternate keyboard matrix");

	return 0;
}
SYS_INIT(kb_matrix_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
