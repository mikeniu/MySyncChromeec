/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Kukui SCP configuration */

#include "common.h"
#include "console.h"
#include "cpu.h"
#include "ec_version.h"
#include "gpio.h"
#include "hooks.h"
#include "registers.h"
#include "task.h"
#include "timer.h"
#include "util.h"

/* Build GPIO tables */
void eint_event(enum gpio_signal signal);

#include "gpio_list.h"


void eint_event(enum gpio_signal signal)
{
	ccprintf("EINT event: %d\n", signal);
}

/* Initialize board.  */
static void board_init(void)
{
	gpio_enable_interrupt(GPIO_EINT5_TP);
	gpio_enable_interrupt(GPIO_EINT6_TP);
	gpio_enable_interrupt(GPIO_EINT7_TP);
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

static uint32_t *cached = (void *)0x10000000;
static uint32_t *direct = (void *)0x30000000;
static const int len = 0x40;

static int command_filltest(int argc, char **argv)
{
	int j;
	int c = get_time().le.lo;

	for (j = 0; j < len; j++)
		cached[j] = c << 8 | j;

	return EC_SUCCESS;
}

DECLARE_SAFE_CONSOLE_COMMAND(filltest, command_filltest,
			     NULL,
			     "Do D-cache fill test");

static int command_flushtest(int argc, char **argv)
{
	int j;
	uintptr_t addr;
	char *e;

	if (argc >= 3) {
		addr = (uintptr_t)strtoi(argv[1], &e, 0);
		if (*e)
			return EC_ERROR_PARAM1;

		if (argv[2][0] == 'c') {
			ccprintf("Clean\n");
			if (addr == 0)
				cpu_clean_invalidate_dcache();
			else
				cpu_clean_invalidate_dcache_address(addr);
		} else if (argv[2][0] == 'i') {
			ccprintf("Inval %08x\n", addr);
			if (addr == 0)
				cpu_invalidate_dcache();
			else
				cpu_invalidate_dcache_address(addr);
		}
	}

	ccprintf("cached:");
	for (j = 0; j < len; j++) {
		if ((j % 8) == 0) {
			ccprintf("\n%08x: ", &cached[j]);
			cflush();
		}
		ccprintf("%08x ", cached[j]);
	}
	ccprintf("\n");

	ccprintf("direct:");
	for (j = 0; j < len; j++) {
		if ((j % 8) == 0) {
			ccprintf("\n%08x: ", &direct[j]);
			cflush();
		}
		ccprintf("%08x ", direct[j]);
	}
	ccprintf("\n");

	return EC_SUCCESS;
}
DECLARE_SAFE_CONSOLE_COMMAND(flushtest, command_flushtest,
			     NULL,
			     "Do D-cache flush test");
