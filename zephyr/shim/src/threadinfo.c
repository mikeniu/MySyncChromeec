/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ec_commands.h"
#include "host_command.h"
#include "panic_utils.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/kernel_structs.h>

/* Helper to get program counter for a thread. */
static uintptr_t get_thread_pc(const struct k_thread *thread)
{
	if (thread == k_current_get() && !k_is_in_isr())
		return 0;

#if defined(CONFIG_ARM)
	uint32_t *psp = (uint32_t *)get_stack_ptr(thread);
	if (psp != NULL) {
		return psp[6];
	}
#elif defined(CONFIG_RISCV)
	return thread->callee_saved.ra;
#endif

	return 0;
}

/* Helper to get link register (return address) for a thread. */
static uintptr_t get_thread_lr(const struct k_thread *thread)
{
	if (thread == k_current_get() && !k_is_in_isr())
		return 0;

#if defined(CONFIG_ARM)
	uint32_t *psp = (uint32_t *)get_stack_ptr(thread);
	if (psp != NULL) {
		return psp[5];
	}
#endif

	return 0;
}

#ifdef CONFIG_PLATFORM_EC_HOSTCMD_THREAD_INFO

struct list_data {
	struct ec_response_thread_info_list *resp;
	int count;
};

static void match_thread_list(const struct k_thread *thread, void *user_data)
{
	struct list_data *d = user_data;
	if (d->count < 32) {
		d->resp->thread_ids[d->count++] = (uint32_t)(uintptr_t)thread;
	}
}

static enum ec_status hc_thread_info_list(struct host_cmd_handler_args *args)
{
	struct ec_response_thread_info_list *r = args->response;
	struct list_data d = { .resp = r, .count = 0 };

	k_thread_foreach_unlocked(match_thread_list, &d);
	r->thread_count = d.count;
	args->response_size = sizeof(uint32_t) + d.count * sizeof(uint32_t);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_THREAD_INFO_LIST, hc_thread_info_list,
		     EC_VER_MASK(0));

extern struct k_thread z_idle_threads[];

/* Checks if a thread is the idle thread. */
static bool thread_is_idle(const struct k_thread *thread)
{
#if defined(CONFIG_SMP)
	return thread->base.is_idle;
#else
	return thread == &z_idle_threads[0];
#endif
}

struct detail_data {
	uintptr_t target_id;
	const struct k_thread *found;
};

static void match_thread_detail(const struct k_thread *thread, void *user_data)
{
	struct detail_data *d = user_data;
	if ((uintptr_t)thread == d->target_id) {
		d->found = thread;
	}
}

static enum ec_status hc_thread_info_detail(struct host_cmd_handler_args *args)
{
	const struct ec_params_thread_info_detail *p = args->params;
	struct ec_response_thread_info_detail *r = args->response;

	struct detail_data d = { .target_id = p->thread_id, .found = NULL };
	k_thread_foreach_unlocked(match_thread_detail, &d);

	if (!d.found) {
		return EC_RES_INVALID_PARAM;
	}

	const struct k_thread *thread = d.found;
	const k_tid_t current = k_current_get();

	memset(r, 0, sizeof(*r));

	r->timestamp_us = k_uptime_get() * 1000;
	r->valid_flags = 0;

#ifdef CONFIG_THREAD_STACK_INFO
	r->valid_flags |= EC_THREAD_INFO_STACK_VALID;
	r->stack_size = thread->stack_info.size;

	uintptr_t cur_sp;
	if (thread == current) {
		cur_sp = (uintptr_t)&cur_sp;
	} else {
		cur_sp = get_stack_ptr(thread);
	}
	r->stack_cur = (cur_sp >= (uintptr_t)thread->stack_info.start &&
			cur_sp <= (uintptr_t)thread->stack_info.start +
					  thread->stack_info.size) ?
			       ((uintptr_t)thread->stack_info.start +
				thread->stack_info.size - cur_sp) :
			       0;

	size_t unused_stack;
	if (k_thread_stack_space_get(thread, &unused_stack) == 0) {
		r->stack_max = thread->stack_info.size - unused_stack;
	}
#endif

#ifdef CONFIG_SCHED_THREAD_USAGE
	r->valid_flags |= EC_THREAD_INFO_RUNTIME_USAGE_VALID;
	k_thread_runtime_stats_t thread_stats;
	if (k_thread_runtime_stats_get((k_tid_t)thread, &thread_stats) == 0) {
		r->execution_time_us =
			k_cyc_to_us_near64(thread_stats.execution_cycles);
	}
#endif

#ifdef CONFIG_SCHED_THREAD_USAGE_ANALYSIS
	r->valid_flags |= EC_THREAD_INFO_USAGE_ANALYSIS_VALID;
	r->window_peak_us = k_cyc_to_us_near64(thread_stats.peak_cycles);
	r->window_avg_us = k_cyc_to_us_near64(thread_stats.average_cycles);
#endif

#ifdef CONFIG_THREAD_NAME
	r->valid_flags |= EC_THREAD_INFO_NAME_VALID;
	const char *name = k_thread_name_get((struct k_thread *)thread);
	if (name) {
		strncpy(r->name, name, sizeof(r->name) - 1);
	}
#endif

	r->entry_point = (uint32_t)(uintptr_t)thread->entry.pEntry;

	k_ticks_t ticks = k_thread_timeout_remaining_ticks(thread);
	if (ticks > 0 && ticks != K_TICKS_FOREVER) {
		r->timeout_us = k_ticks_to_us_near64(ticks);
	} else if (ticks == K_TICKS_FOREVER) {
		r->timeout_us = 0xffffffff;
	}

	r->prio = (int8_t)thread->base.prio;
	r->thread_state = thread->base.thread_state;
	r->is_current = (thread == current);

	if (thread_is_idle(thread)) {
		r->is_idle = 1;
	}

	r->wait_object = (uint32_t)(uintptr_t)thread->base.pended_on;

	/* Fill Arch specific fields */
	r->valid_flags |= EC_THREAD_INFO_CPU_REGS_VALID;

	r->pc = (uint32_t)get_thread_pc(thread);
	r->lr = (uint32_t)get_thread_lr(thread);
	if (thread == current) {
		r->sp = (uint32_t)&current; /* Approximate current SP */
	} else {
		r->sp = (uint32_t)get_stack_ptr(thread);
	}

	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_THREAD_INFO_DETAIL, hc_thread_info_detail,
		     EC_VER_MASK(0));

#endif
