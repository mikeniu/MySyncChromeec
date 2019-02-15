/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Kukui SCP configuration */

#include "common.h"
#include "console.h"
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

void disable_dram(void);

static int command_ramtest(int argc, char **argv)
{
	uint32_t *direct = (void *)0x30000000;
	int i, j, k;
	const int it = 10;
	const int len = 0x100;
	timestamp_t start;
	uint64_t val;

	for (j = 0; j < len; j++)
		direct[j] = j;

	for (k = 0; k < 10; k++) {
		if ((k & 1) == 0) {
			ccprintf("Disable!\n");
			cflush();
			disable_dram();
			msleep(100);
		}
		start = get_time();
		val = 0;
		for (i = 0; i < it; i++) {
			for (j = 0; j < len; j++)
				val += direct[j];
		}
		ccprintf("direct: %d us (val: %lx)\n", time_since32(start), val);
		cflush();
	}

	return EC_SUCCESS;
}
DECLARE_SAFE_CONSOLE_COMMAND(ramtest, command_ramtest,
			     NULL,
			     "Do RAM performance test");
