/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "task.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"
#include "timer.h"

#include <zephyr/ztest.h>

struct tasks_fixture {
	timestamp_t fake_time;
};

static void *setup(void)
{
	static struct tasks_fixture fixture;

	return &fixture;
}

static void before(void *f)
{
	struct tasks_fixture *fixture = f;

	fixture->fake_time.val = 0;
}

static void after(void *f)
{
	ARG_UNUSED(f);

	get_time_mock = NULL;
}

ZTEST_SUITE(tasks, drivers_predicate_post_main, setup, before, after, NULL);

ZTEST(tasks, test_enable_irq)
{
	arch_irq_disable(0);
	task_enable_irq(0);
	zassert_true(arch_irq_is_enabled(0));
}

ZTEST(tasks, test_interrupt_context)
{
	zassert_false(in_interrupt_context());
}

ZTEST(tasks, test_get_event_bitmap_invalid_tid)
{
	zassert_is_null(
		task_get_event_bitmap(TASK_ID_COUNT + EXTRA_TASK_COUNT));
}
