/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "hooks.h"
#include "host_command.h"
#include "strings.h"
#include "task.h"
#include "timer.h"
#include "watchdog.h"

/* Variables for tracking watchdog stats */
static timestamp_t watchdog_reload_period_max_ts;
static int32_t watchdog_reload_period_max_ms;
static timestamp_t watchdog_reload_last_ts;
static uint32_t watchdog_reload_count;
static timestamp_t watchdog_stats_reset_ts;

void watchdog_reload(void)
{
	if (IS_ENABLED(CONFIG_HOSTCMD_WATCHDOG_INFO) ||
	    IS_ENABLED(CONFIG_CONSOLE_CMD_WATCHDOG_INFO)) {
		uint32_t key = irq_lock();
		timestamp_t now_ts = get_time();
		int32_t elapsed_ms =
			(now_ts.val - watchdog_reload_last_ts.val) / MSEC;

		watchdog_reload_count++;
		if (elapsed_ms > watchdog_reload_period_max_ms) {
			watchdog_reload_period_max_ms = elapsed_ms;
			watchdog_reload_period_max_ts = watchdog_reload_last_ts;
		}
		watchdog_reload_last_ts = now_ts;
		irq_unlock(key);
	}

	chip_watchdog_reload();
}
DECLARE_HOOK(HOOK_TICK, watchdog_reload, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_SYSJUMP, watchdog_reload, HOOK_PRIO_LAST);

__maybe_unused static void reset_watchdog_stats(void)
{
	uint32_t key = irq_lock();
	watchdog_stats_reset_ts = watchdog_reload_last_ts;
	watchdog_reload_period_max_ts.val = 0;
	watchdog_reload_period_max_ms = 0;
	watchdog_reload_count = 0;
	irq_unlock(key);
}

#if defined(CONFIG_HOSTCMD_WATCHDOG_INFO)

static enum ec_status hostcmd_watchdog_info(struct host_cmd_handler_args *args)
{
	const struct ec_params_hostcmd_watchdog_info *p = args->params;
	struct ec_response_hostcmd_watchdog_info *r = args->response;

	if (args->params_size < sizeof(*p))
		return EC_RES_INVALID_PARAM;

	r->watchdog_period_ms = CONFIG_WATCHDOG_PERIOD_MS;
	r->watchdog_warning_period_ms = CONFIG_WATCHDOG_PERIOD_MS -
					CONFIG_WATCHDOG_WARNING_LEADING_TIME_MS;
	r->watchdog_reload_period_nominal_ms = HOOK_TICK_INTERVAL / MSEC;

	{
		uint32_t key = irq_lock();
		r->watchdog_reload_period_max_ms =
			watchdog_reload_period_max_ms;
		r->watchdog_reload_period_max_ts_ms =
			watchdog_reload_period_max_ts.val / MSEC;
		r->watchdog_reload_count = watchdog_reload_count;
		r->watchdog_stats_elapsed_ms =
			time_since32(watchdog_stats_reset_ts) / MSEC;
		irq_unlock(key);
	}

	if (p->reset_stats) {
		reset_watchdog_stats();
	}

	args->response_size = sizeof(*r);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_HOSTCMD_WATCHDOG_INFO, hostcmd_watchdog_info,
		     EC_VER_MASK(0));

#endif /* CONFIG_HOSTCMD_WATCHDOG_INFO */

#if defined(CONFIG_CONSOLE_CMD_WATCHDOG_INFO)

static void print_watchdog_info(void)
{
	uint64_t elapsed;
	uint32_t count;
	int32_t max_ms;
	uint64_t max_ts_ms;

	ccprintf("Watchdog Info:\n");
	ccprintf("Period: %d ms\n", CONFIG_WATCHDOG_PERIOD_MS);
	ccprintf("Warning Period: %d ms\n",
		 CONFIG_WATCHDOG_PERIOD_MS -
			 CONFIG_WATCHDOG_WARNING_LEADING_TIME_MS);
	ccprintf("Reload Period Nominal: %d ms\n", HOOK_TICK_INTERVAL / MSEC);

	uint32_t key = irq_lock();
	elapsed = time_since32(watchdog_stats_reset_ts) / MSEC;
	count = watchdog_reload_count;
	max_ms = watchdog_reload_period_max_ms;
	max_ts_ms = watchdog_reload_period_max_ts.val / MSEC;
	irq_unlock(key);

	ccprintf("Stats Elapsed Time: %llu.%03llu s\n",
		 (unsigned long long)(elapsed / 1000),
		 (unsigned long long)(elapsed % 1000));
	ccprintf("Reload Count: %u\n", count);
	ccprintf("Reload Period Max: %d ms @ %llu.%03llu s\n", max_ms,
		 (unsigned long long)(max_ts_ms / 1000),
		 (unsigned long long)(max_ts_ms % 1000));
	ccprintf("Reload Period Avg: %llu ms\n",
		 (unsigned long long)(count > 0 ? elapsed / count : 0));
}

test_mockable_static int command_watchdoginfo(int argc, const char **argv)
{
	bool reset_stats = false;

	if (argc == 2) {
		if (strcasecmp(argv[1], "reset_stats") == 0) {
			reset_stats = true;
		} else {
			return EC_ERROR_PARAM1;
		}
	}

	print_watchdog_info();

	if (reset_stats) {
		reset_watchdog_stats();
		ccprintf("Watchdog stats reset.\n");
	}

	return EC_SUCCESS;
}

#ifndef TEST_BUILD
DECLARE_CONSOLE_COMMAND(watchdoginfo, command_watchdoginfo, "[reset_stats]",
			"Watchdog Info");
#endif /* TEST_BUILD */

#endif /* CONFIG_CONSOLE_CMD_WATCHDOG_INFO */
