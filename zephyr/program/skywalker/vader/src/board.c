// Copyright 2026 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "charge_state.h"
#include "common.h"
#include "gpio.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "util.h"
#include "watchdog.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/timing/timing.h>

LOG_MODULE_REGISTER(board_init, LOG_LEVEL_INF);

static int command_perf_template(int argc, const char **argv, const char *name,
				 int (*func)(void))
{
	char *end;
	int repeat, ret;
	int cnt[2] = { 0 };
	uint64_t total_ns = 0;

	if (argc != 2) {
		return EC_ERROR_PARAM_COUNT;
	}

	repeat = strtoi(argv[1], &end, 0);
	if (*end || repeat <= 0) {
		return EC_ERROR_PARAM1;
	}

	timing_init();
	timing_start();

	for (int i = 0; i < repeat; ++i) {
		watchdog_reload();
		timing_t start = timing_counter_get();
		ret = func();
		timing_t end = timing_counter_get();
		uint64_t cycles = timing_cycles_get(&start, &end);
		uint64_t ns = timing_cycles_to_ns(cycles);
		total_ns += ns;
		cnt[ret != 0]++;
	}

	timing_stop();

	LOG_INF("%s cnt[0]=%8d cnt[1]=%8d total_ns=%llu", name, cnt[0], cnt[1],
		(unsigned long long)total_ns / repeat);
	return EC_SUCCESS;
}

int sbbp(void)
{
	int manufacture_date = 0;
	int ret = sb_read(SB_MANUFACTURE_DATE, &manufacture_date);
	if (ret != 0) {
		k_busy_wait(1 * USEC_PER_MSEC);
		ret = sb_read(SB_MANUFACTURE_DATE, &manufacture_date);
	}
	return ret;
}

static int command_perfsbbp(int argc, const char **argv)
{
	return command_perf_template(argc, argv, __func__, sbbp);
}
DECLARE_CONSOLE_COMMAND(perfsbbp, command_perfsbbp, "iterations", NULL);

int gpiobp(void)
{
	return gpio_get_level(GPIO_BATT_PRES_ODL);
}

static int command_perfgpiobp(int argc, const char **argv)
{
	return command_perf_template(argc, argv, __func__, gpiobp);
}
DECLARE_CONSOLE_COMMAND(perfgpiobp, command_perfgpiobp, "iterations", NULL);
