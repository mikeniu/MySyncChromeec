/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ec_commands.h"
#include "host_command.h"
#include "panic_utils.h"
#include "threadinfo.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/kernel_structs.h>
#include <zephyr/shell/shell.h>

#define THREADINFO_MAX_COL_WIDTH 16

/* Width of the Name column. */
#ifdef CONFIG_THREAD_NAME
#define COL_WIDTH_NAME                                           \
	(CONFIG_THREAD_MAX_NAME_LEN < THREADINFO_MAX_COL_WIDTH ? \
		 CONFIG_THREAD_MAX_NAME_LEN :                    \
		 THREADINFO_MAX_COL_WIDTH)
#else
#define COL_WIDTH_NAME 0
#endif

#define THREADINFO_COL_SEP_PRE "| "
#define THREADINFO_COL_SEP_POST " "

/* Helper to get program counter for a thread.
 * Note: For ARM, this reads from the stacked registers assuming standard
 * exception frame layout. For RISC-V, it uses callee-saved registers.
 */
#if defined(CONFIG_ARCH_STACKWALK)
struct pc_lr_data {
	uintptr_t pc;
	uintptr_t lr;
	int frame_idx;
};

static bool get_pc_lr_callback(void *arg, unsigned long pc)
{
	struct pc_lr_data *data = arg;
	if (data->frame_idx == 0) {
		data->pc = pc;
	} else if (data->frame_idx == 1) {
		data->lr = pc;
		return false; /* Stop walking */
	}
	data->frame_idx++;
	return true;
}
#endif

static void get_thread_registers(const struct k_thread *thread, uint32_t *pc,
				 uint32_t *lr)
{
	*pc = 0;
	*lr = 0;

	if (thread == k_current_get() && !k_is_in_isr())
		return;

#if defined(CONFIG_ARCH_STACKWALK)
	struct pc_lr_data data = { .pc = 0, .lr = 0, .frame_idx = 0 };
	arch_stack_walk(get_pc_lr_callback, &data, thread, NULL);
	*pc = (uint32_t)data.pc;
	*lr = (uint32_t)data.lr;
#elif defined(CONFIG_ARM) || defined(CONFIG_RISCV)
	uint32_t sp = get_stack_ptr(thread);
	bool sp_valid = false;

	/* Verify stack pointer is within valid bounds before dereferencing. */
#if defined(CONFIG_THREAD_STACK_INFO)
	uintptr_t stack_start = (uintptr_t)thread->stack_info.start;
	uintptr_t stack_end = stack_start + thread->stack_info.size;
	sp_valid = (sp >= stack_start && sp <= stack_end);
#else
	sp_valid = (sp != 0) && ((sp & 0x3) == 0) && (sp >= CONFIG_RAM_BASE) &&
		   (sp < (CONFIG_RAM_BASE + CONFIG_RAM_SIZE));
#endif

	if (sp_valid) {
		struct arch_esf *esf = (struct arch_esf *)sp;
#if defined(CONFIG_ARM)
		*pc = esf->basic.pc;
		*lr = esf->basic.lr;
#elif defined(CONFIG_RISCV)
		*pc = esf->mepc;
		*lr = esf->ra;
#endif
	}
#endif
}

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

static void get_thread_info_detail(const struct k_thread *thread,
				   struct ec_response_thread_info_detail *r)
{
	const k_tid_t current = k_current_get();

	memset(r, 0, sizeof(*r));

	r->timestamp_us = k_uptime_get() * 1000;
	r->valid_flags = 0;

#ifdef CONFIG_THREAD_STACK_INFO
	r->valid_flags |= EC_THREAD_INFO_DETAIL_STACK_VALID;
	r->stack_size = thread->stack_info.size;

	uintptr_t cur_sp = get_stack_ptr(thread);
	uintptr_t stack_start = (uintptr_t)thread->stack_info.start;
	uintptr_t stack_end = stack_start + thread->stack_info.size;

	if (cur_sp >= stack_start && cur_sp <= stack_end) {
		r->stack_cur = stack_end - cur_sp;
	} else {
		r->stack_cur = 0;
	}

#ifdef CONFIG_INIT_STACKS
	size_t unused_stack;
	if (k_thread_stack_space_get(thread, &unused_stack) == 0) {
		r->stack_max = thread->stack_info.size - unused_stack;
	}
#endif /* CONFIG_INIT_STACKS */
#endif /* CONFIG_THREAD_STACK_INFO */

#ifdef CONFIG_SCHED_THREAD_USAGE
	k_thread_runtime_stats_t thread_stats;
	if (k_thread_runtime_stats_get((k_tid_t)thread, &thread_stats) == 0) {
		r->valid_flags |= EC_THREAD_INFO_DETAIL_RUNTIME_USAGE_VALID;
		r->execution_time_us =
			k_cyc_to_us_near64(thread_stats.execution_cycles);

#ifdef CONFIG_SCHED_THREAD_USAGE_ANALYSIS
		r->valid_flags |= EC_THREAD_INFO_DETAIL_USAGE_ANALYSIS_VALID;
		r->window_peak_us =
			k_cyc_to_us_near64(thread_stats.peak_cycles);
		r->window_avg_us =
			k_cyc_to_us_near64(thread_stats.average_cycles);
#endif /* CONFIG_SCHED_THREAD_USAGE_ANALYSIS */
	}
#endif /* CONFIG_SCHED_THREAD_USAGE */

#ifdef CONFIG_THREAD_NAME
	r->valid_flags |= EC_THREAD_INFO_DETAIL_NAME_VALID;
	const char *name = k_thread_name_get((struct k_thread *)thread);
	if (name) {
		strncpy(r->name, name, sizeof(r->name) - 1);
	}
#endif /* CONFIG_THREAD_NAME */

	r->entry_point = (uint32_t)(uintptr_t)thread->entry.pEntry;

	k_ticks_t ticks = k_thread_timeout_remaining_ticks(thread);
	if (ticks > 0 && ticks != K_TICKS_FOREVER) {
		r->timeout_us = k_ticks_to_us_near64(ticks);
	} else if (ticks == K_TICKS_FOREVER) {
		r->timeout_us = 0xffffffff;
	}

	r->user_options = thread->base.user_options;
	r->prio = (int8_t)thread->base.prio;
	r->thread_state = thread->base.thread_state;
	r->is_current = (thread == current);

	if (thread_is_idle(thread)) {
		r->is_idle = 1;
	}

	r->pending_on = (uint32_t)(uintptr_t)thread->base.pended_on;

	/* Fill Arch specific fields. Registers are considered invalid for the
	 * current thread as they cannot be read reliably.
	 */
	if (thread != current) {
		uint32_t pc;
		uint32_t lr;
		get_thread_registers(thread, &pc, &lr);
		uint32_t sp = (uint32_t)get_stack_ptr(thread);

		if (pc != 0) {
			r->pc = pc;
			r->valid_flags |= EC_THREAD_INFO_DETAIL_PC_VALID;
		}
		if (lr != 0) {
			r->lr = lr;
			r->valid_flags |= EC_THREAD_INFO_DETAIL_LR_VALID;
		}
		if (sp != 0) {
			r->sp = sp;
			r->valid_flags |= EC_THREAD_INFO_DETAIL_SP_VALID;
		}
	}
}

/*
 * Formats a time in microseconds to a string with exactly 3 significant digits,
 * right-aligned within 7 characters.
 */
static void format_time_3_sig_digits(char *buf, size_t size, uint64_t us)
{
	const char *unit;
	uint32_t val;

	if (us >= 3600000000ULL) {
		unit = "h ";
		val = (uint32_t)(us / 36000000ULL);
	} else if (us >= 60000000ULL) {
		unit = "m ";
		val = (uint32_t)(us / 600000ULL);
	} else if (us >= 1000000ULL) {
		unit = "s ";
		val = (uint32_t)(us / 10000ULL);
	} else if (us >= 1000ULL) {
		unit = "ms";
		val = (uint32_t)(us / 10ULL);
	} else {
		unit = "us";
		val = (uint32_t)us * 100;
	}

	uint32_t whole = val / 100;
	uint32_t frac = val % 100;

	if (whole >= 100) {
		snprintf(buf, size, " %u %s", (unsigned int)whole, unit);
	} else if (whole >= 10) {
		snprintf(buf, size, "%u.%u %s", (unsigned int)whole,
			 (unsigned int)(frac / 10), unit);
	} else {
		snprintf(buf, size, "%u.%02u %s", (unsigned int)whole,
			 (unsigned int)frac, unit);
	}
}

static void print_col(const struct shell *sh, const char *str, size_t width)
{
	if (sh != NULL) {
		shell_fprintf(sh, SHELL_NORMAL,
			      THREADINFO_COL_SEP_PRE
			      "%-*.*s" THREADINFO_COL_SEP_POST,
			      (int)width, (int)width, str);
	} else {
		printk(THREADINFO_COL_SEP_PRE "%-*.*s" THREADINFO_COL_SEP_POST,
		       (int)width, (int)width, str);
	}
}

static void print_end_row(const struct shell *sh)
{
	if (sh != NULL) {
		shell_fprintf(sh, SHELL_NORMAL, "\n");
	} else {
		printk("\n");
	}
}

static void print_hex(const struct shell *sh, uint32_t val, bool valid)
{
	char buf[9];

	if (valid) {
		snprintf(buf, sizeof(buf), "%08x", val);
	}
	print_col(sh, valid ? buf : "", 8);
}

static void print_u32(const struct shell *sh, uint32_t val, bool valid)
{
	char buf[16];

	if (valid) {
		snprintf(buf, sizeof(buf), "%6u", (unsigned int)val);
	}
	print_col(sh, valid ? buf : "", 6);
}

static void print_time(const struct shell *sh, uint64_t us, bool valid)
{
	char buf[16];

	if (valid) {
		format_time_3_sig_digits(buf, sizeof(buf), us);
	}
	print_col(sh, valid ? buf : "", 7);
}

static void print_header_or_divider(const struct shell *sh, bool full,
				    bool is_divider)
{
	const char *dashes = "----------------";

#if CONFIG_THREAD_MAX_NAME_LEN > 0
	print_col(sh, is_divider ? dashes : "Name", COL_WIDTH_NAME);
#endif
	if (full) {
		print_col(sh, is_divider ? dashes : "tid", 8);
	}
	if (full || !IS_ENABLED(CONFIG_THREAD_NAME)) {
		print_col(sh, is_divider ? dashes : "Entry", 8);
	}
	if (full) {
		print_col(sh, is_divider ? dashes : "Prio", 4);
	}
	print_col(sh, is_divider ? dashes : "State", 7);
	print_col(sh, is_divider ? dashes : "WaitObj", 8);
	print_col(sh, is_divider ? dashes : "Timeout", 7);
#ifdef CONFIG_SCHED_THREAD_USAGE
	print_col(sh, is_divider ? dashes : "Time", 7);
#endif
#ifdef CONFIG_SCHED_THREAD_USAGE_ANALYSIS
	print_col(sh, is_divider ? dashes : "Peak", 7);
	print_col(sh, is_divider ? dashes : "Avg", 7);
#endif
#ifdef CONFIG_THREAD_STACK_INFO
	print_col(sh, is_divider ? dashes : "StkCur", 6);
#ifdef CONFIG_INIT_STACKS
	print_col(sh, is_divider ? dashes : "StkMax", 6);
#endif
	if (full) {
		print_col(sh, is_divider ? dashes : "StkSize", 6);
	}
#endif
	print_col(sh, is_divider ? dashes : "PC", 8);
#ifdef CONFIG_ARM
	print_col(sh, is_divider ? dashes : "LR", 8);
#endif
	if (full) {
		print_col(sh, is_divider ? dashes : "SP", 8);
	}
	print_end_row(sh);
}

static void print_header(const struct shell *sh, bool full)
{
	print_header_or_divider(sh, full, false);
	print_header_or_divider(sh, full, true);
}

static void print_thread_row(const struct k_thread *thread, void *user_data)
{
	const struct shell *sh = (const struct shell *)user_data;
	bool full = (sh != NULL);
	struct ec_response_thread_info_detail r;

	get_thread_info_detail(thread, &r);

#if CONFIG_THREAD_MAX_NAME_LEN > 0
	print_col(sh, r.name, COL_WIDTH_NAME);
#endif
	if (full) {
		print_hex(sh, (uint32_t)(uintptr_t)thread, true);
	}
	if (full || !IS_ENABLED(CONFIG_THREAD_NAME)) {
		print_hex(sh, r.entry_point, true);
	}
	if (full) {
		char buf[8];

		snprintf(buf, sizeof(buf), "%4d", r.prio);
		print_col(sh, buf, 4);
	}

	if (r.is_current) {
		print_col(sh, "current", 7);
	} else {
		char buf[16];

		k_thread_state_str((k_tid_t)thread, buf, sizeof(buf));
		print_col(sh, buf, 7);
	}

	print_hex(sh, r.pending_on, r.pending_on != 0);
	print_time(sh, r.timeout_us,
		   r.timeout_us != 0 && r.timeout_us != 0xffffffff);

#ifdef CONFIG_SCHED_THREAD_USAGE
	print_time(sh, r.execution_time_us,
		   r.valid_flags & EC_THREAD_INFO_DETAIL_RUNTIME_USAGE_VALID);
#endif

#ifdef CONFIG_SCHED_THREAD_USAGE_ANALYSIS
	bool usage_valid =
		(r.valid_flags & EC_THREAD_INFO_DETAIL_USAGE_ANALYSIS_VALID);

	print_time(sh, r.window_peak_us, usage_valid);
	print_time(sh, r.window_avg_us, usage_valid);
#endif

#ifdef CONFIG_THREAD_STACK_INFO
	bool stack_valid = (r.valid_flags & EC_THREAD_INFO_DETAIL_STACK_VALID);

	print_u32(sh, r.stack_cur,
		  stack_valid && (!r.is_current || k_is_in_isr()));
#ifdef CONFIG_INIT_STACKS
	print_u32(sh, r.stack_max, stack_valid);
#endif
	if (full) {
		print_u32(sh, r.stack_size, stack_valid);
	}
#endif

	print_hex(sh, r.pc, r.valid_flags & EC_THREAD_INFO_DETAIL_PC_VALID);

#ifdef CONFIG_ARM
	print_hex(sh, r.lr, r.valid_flags & EC_THREAD_INFO_DETAIL_LR_VALID);
#endif

	if (full) {
		bool sp_valid =
			(r.valid_flags & EC_THREAD_INFO_DETAIL_SP_VALID);
		uint32_t sp = r.sp;

		if (!sp_valid && thread == k_current_get()) {
			sp = (uint32_t)get_stack_ptr(thread);
			sp_valid = true;
		}
		print_hex(sh, sp, sp_valid);
	}

	print_end_row(sh);
}

void threadinfo_dump(const struct shell *shell)
{
	bool full = (shell != NULL);

	print_header(shell, full);
	k_thread_foreach_unlocked(print_thread_row, (void *)shell);
}

#ifdef CONFIG_PLATFORM_EC_CONSOLE_CMD_THREADINFO
static int cmd_threadinfo(const struct shell *shell, size_t argc, char *argv[])
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	threadinfo_dump(shell);
	return 0;
}

SHELL_CMD_ARG_REGISTER(threadinfo, NULL, "Prints thread info", cmd_threadinfo,
		       1, 1);
#endif

#ifdef CONFIG_PLATFORM_EC_HOSTCMD_THREAD_INFO

struct list_data {
	struct ec_response_thread_info_list *resp;
	int count;
};

static void match_thread_list(const struct k_thread *thread, void *user_data)
{
	struct list_data *d = user_data;
	/* Limit to EC_THREAD_INFO_MAX_COUNT threads as per
	 * ec_response_thread_info_list fixed array size.
	 */
	if (d->count < EC_THREAD_INFO_MAX_COUNT) {
		d->resp->thread_ids[d->count++] = (uint32_t)(uintptr_t)thread;
	}
}

static enum ec_host_cmd_status
hc_thread_info_list(struct ec_host_cmd_handler_args *args)
{
	struct ec_response_thread_info_list *r = args->output_buf;
	struct list_data d = { .resp = r, .count = 0 };

	k_thread_foreach_unlocked(match_thread_list, &d);
	r->thread_count = d.count;
	args->output_buf_size = sizeof(uint32_t) + d.count * sizeof(uint32_t);

	return EC_HOST_CMD_SUCCESS;
}
EC_HOST_CMD_HANDLER_RESP_ONLY(EC_CMD_THREAD_INFO_LIST, hc_thread_info_list,
			      EC_VER_MASK(0),
			      struct ec_response_thread_info_list);

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

static enum ec_host_cmd_status
hc_thread_info_detail(struct ec_host_cmd_handler_args *args)
{
	const struct ec_params_thread_info_detail *p = args->input_buf;
	struct ec_response_thread_info_detail *r = args->output_buf;

	struct detail_data d = { .target_id = p->thread_id, .found = NULL };
	k_thread_foreach_unlocked(match_thread_detail, &d);

	if (!d.found) {
		return EC_HOST_CMD_INVALID_PARAM;
	}

	get_thread_info_detail(d.found, r);

	args->output_buf_size = sizeof(*r);

	return EC_HOST_CMD_SUCCESS;
}
EC_HOST_CMD_HANDLER(EC_CMD_THREAD_INFO_DETAIL, hc_thread_info_detail,
		    EC_VER_MASK(0), struct ec_params_thread_info_detail,
		    struct ec_response_thread_info_detail);

#endif
