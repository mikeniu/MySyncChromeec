/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Pure GPIO-based external power detection */

#include "extpower.h"
#include "gpio/gpio_int.h"

#include <zephyr/init.h>

static int debounced_extpower_presence;

static struct k_work_delayable board_extpower_handle;

test_mockable int extpower_is_present(void)
{
	return debounced_extpower_presence;
}

static void board_extpower_handler(struct k_work *extpower_work)
{
	int extpower_presence =
		gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(bc_acok));

	if (extpower_presence == debounced_extpower_presence)
		return;

	debounced_extpower_presence = extpower_presence;
	extpower_handle_update(extpower_presence);
}

void extpower_interrupt(enum gpio_signal signal)
{
	/* Trigger deferred notification of external power change */
	k_work_schedule(&board_extpower_handle,
			K_MSEC(CONFIG_EXTPOWER_DEBOUNCE_MS));
}

static int board_extpower_init(void)
{
	debounced_extpower_presence =
		gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(bc_acok));

	k_work_init_delayable(&board_extpower_handle, board_extpower_handler);

	/* Handler not deffered during Board charge initialization */
	board_extpower_handler(NULL);
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_ac_present));

	return 0;
}
SYS_INIT(board_extpower_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
