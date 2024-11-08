/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Set up the N8 core
 */

#include "cpu.h"
#include "registers.h"
#include "console.h"
#include "system.h"
#include "timer.h"
#include "util.h"
#include "gpio.h"

void cpu_init(void)
{
	/* DLM initialization is done in init.S */
	/* Global interrupt enable */
	asm volatile("setgie.e");
}

/* This function must be a sub-routine in cpu_console_cmd(), or branch instruction can't across 4KB gap, so do not optimize. */
static void __attribute__((optimize("O0"))) execute_nop_1024_cnt(void)
{
	/* Loop to execute 4 bytes nop instruction for 1024 counts */
	asm volatile(".rept 0x400\n\t"
		     "nop\n\t"
		     ".endr\n\t");
}

static int cpu_console_cmd(int argc, const char **argv)
{
	//int time_delta;
	//uint64_t time_start, time_stop;

	if (argc > 1) {
		if ((argv[1][0] == '.') && (strlen(argv[1]) == 2)) {
			switch (argv[1][1]) {
			case 'n':
				/* disable all interrupts */
				interrupt_disable();

				//time_start = get_time().val;
				gpio_set_level(GPIO_TEST_A0, 1);

				/* equivalent Nds32 fetch 4KB(1024*4) instruction from flash and execute */
				execute_nop_1024_cnt();

				//time_stop = get_time().val;
				//time_delta = time_stop - time_start;
				gpio_set_level(GPIO_TEST_A0, 0);

				/* enable all interrupts */
				interrupt_enable();

				//ccprintf("time_delta %d(us)\n", time_delta);
				cflush();
				return EC_SUCCESS;
			default:
				return EC_ERROR_PARAM1;
			}
		}
	}

	return EC_ERROR_PARAM1;
}

DECLARE_CONSOLE_COMMAND(
	ex, cpu_console_cmd, "[.n]",
	"execute 4bytes nop instruction for 1024 counts");
