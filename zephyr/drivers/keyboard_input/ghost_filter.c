/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/device.h>
#include <zephyr/input/input_kbd_matrix.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(ghost_filter, CONFIG_INPUT_LOG_LEVEL);

static const struct filter_mask {
	kbd_row_t row_mask;
	uint8_t col1;
	uint8_t col2;
	kbd_row_t clear_mask;
	uint8_t clear_col;
} filter_masks[] = {
	{ BIT(5) | BIT(7), 6, 7, BIT(7), 7 }, /* LShift W Space [RShift] */
	{ BIT(5) | BIT(7), 5, 6, BIT(5), 5 }, /* W Space Q [C] */
	{ BIT(5) | BIT(7), 6, 8, BIT(5), 8 }, /* W Space E [X] */
};

static void input_kbd_matrix_ghost_filter(const struct device *dev)
{
	const struct input_kbd_matrix_common_config *cfg = dev->config;
	kbd_row_t *state = cfg->matrix_new_state;

	for (uint8_t i = 0; i < ARRAY_SIZE(filter_masks); i++) {
		const struct filter_mask *mask = &filter_masks[i];

		if ((state[mask->col1] & mask->row_mask) == mask->row_mask &&
		    (state[mask->col2] & mask->row_mask) == mask->row_mask) {
			LOG_DBG("filter mask: %d", i);
			state[mask->clear_col] &= ~mask->clear_mask;
		}
	}
}

#ifndef CONFIG_CROS_EC_COL_GPIO_DRIVE
/* Hook into the upstream keyboard driver directly. */
void input_kbd_matrix_drive_column_hook(const struct device *dev, int col)
#else
/* Define this as an internal function and let the hook from col_gpio_drive
 * call this.
 */
void input_kbd_matrix_ghost_filter_hook(const struct device *dev, int col)
#endif
{
	/* This hook happens to be called with the DRIVE_NONE argument just
	 * before input_kbd_matrix_ghosting()
	 */
	if (col == INPUT_KBD_MATRIX_COLUMN_DRIVE_NONE) {
		input_kbd_matrix_ghost_filter(dev);
	}
}
