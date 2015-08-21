/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Pure GPIO-based external power detection, buffered to PCH.
 * Drive high in S5-S0 when AC_PRESENT is high, otherwise drive low.
 */

#include "chipset.h"
#include "common.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "system.h"
#include "util.h"

#define EXTPOWER_DEBOUNCE_US  (30 * MSEC)

static int debounced_extpower_presence;

int extpower_is_present(void)
{
	return gpio_get_level(GPIO_AC_PRESENT);
}

/**
 * Deferred function to handle external power change
 */
static void extpower_deferred(void)
{
        int extpower_presence = gpio_get_level(GPIO_AC_PRESENT);

        if (extpower_presence == debounced_extpower_presence)
                return;

        debounced_extpower_presence = extpower_presence;
        hook_notify(HOOK_AC_CHANGE);

        /* Forward notification to host */
        if (extpower_presence)
                host_set_single_event(EC_HOST_EVENT_AC_CONNECTED);
        else
                host_set_single_event(EC_HOST_EVENT_AC_DISCONNECTED);
}
DECLARE_DEFERRED(extpower_deferred);

static void extpower_buffer_to_soc(void)
{
	/* Drive high when AP is off */
	gpio_set_level(GPIO_LEVEL_SHIFT_EN_L,
		       chipset_in_state(CHIPSET_STATE_HARD_OFF) ? 1 : 0);
}
DECLARE_HOOK(HOOK_CHIPSET_PRE_INIT, extpower_buffer_to_soc, HOOK_PRIO_DEFAULT);

static void extpower_shutdown(void)
{
	/* Disable level shift to SoC when shutting down */
	gpio_set_level(GPIO_LEVEL_SHIFT_EN_L, 1);
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, extpower_shutdown, HOOK_PRIO_DEFAULT);

void extpower_interrupt(enum gpio_signal signal)
{
	/* Trigger notification of external power change */
	extpower_buffer_to_soc();
	/* Trigger deferred notification of external power change */
	hook_call_deferred(extpower_deferred, EXTPOWER_DEBOUNCE_US);
}

static void extpower_init(void)
{
	uint8_t *memmap_batt_flags = host_get_memmap(EC_MEMMAP_BATT_FLAG);

	debounced_extpower_presence = gpio_get_level(GPIO_AC_PRESENT);

	/* Initialize the memory-mapped AC_PRESENT flag */
	if (debounced_extpower_presence)
		*memmap_batt_flags |= EC_BATT_FLAG_AC_PRESENT;
	else
		*memmap_batt_flags &= ~EC_BATT_FLAG_AC_PRESENT;

	extpower_buffer_to_soc();

	/* Enable interrupts, now that we've initialized */
	gpio_enable_interrupt(GPIO_AC_PRESENT);
}
DECLARE_HOOK(HOOK_INIT, extpower_init, HOOK_PRIO_DEFAULT);
