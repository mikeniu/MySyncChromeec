/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ec_commands.h"
#include "host_command.h"
#include "panic_utils.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/init.h>
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

/*
 * Defines the columns displayed by the threadinfo command.
 * This uses the X-macro pattern.
 */
#define THREADINFO_COLUMNS_FOREACH(X)                                        \
	X(NAME, COL_WIDTH_NAME, "Name", fill_name,                           \
	  IS_ENABLED(CONFIG_THREAD_NAME))                                    \
	X(ID, 8, "tid", fill_id, true)                                       \
	X(ENTRY, 8, "Entry", fill_entry, true)                               \
	X(PRIO, 4, "Prio", fill_prio, true)                                  \
	X(STATE, 5, "State", fill_state, true)                               \
	X(WAITOBJ, 8, "WaitObj", fill_waitobj, true)                         \
	X(TIMEOUT, 7, "Timeout", fill_timeout, true)                         \
	X(TIME, 7, "Time", fill_time, IS_ENABLED(CONFIG_SCHED_THREAD_USAGE)) \
	X(PEAK, 7, "Peak", fill_peak,                                        \
	  IS_ENABLED(CONFIG_SCHED_THREAD_USAGE_ANALYSIS))                    \
	X(AVG, 7, "Avg", fill_avg,                                           \
	  IS_ENABLED(CONFIG_SCHED_THREAD_USAGE_ANALYSIS))                    \
	X(STKCUR, 6, "StkCur", fill_stkcur,                                  \
	  IS_ENABLED(CONFIG_THREAD_STACK_INFO))                              \
	X(STKMAX, 6, "StkMax", fill_stkmax,                                  \
	  IS_ENABLED(CONFIG_THREAD_STACK_INFO) &&                            \
		  IS_ENABLED(CONFIG_INIT_STACKS))                            \
	X(STKSIZE, 6, "StkSize", fill_stksize,                               \
	  IS_ENABLED(CONFIG_THREAD_STACK_INFO))                              \
	X(PC, 8, "PC", fill_pc, true)                                        \
	X(LR, 8, "LR", fill_lr, IS_ENABLED(CONFIG_ARM))                      \
	X(SP, 8, "SP", fill_sp, true)

#define THREADINFO_ENUM_VAL(col_id, width, header, func, cond) \
	THREADINFO_COL_##col_id,
enum { THREADINFO_COLUMNS_FOREACH(THREADINFO_ENUM_VAL) };
#undef THREADINFO_ENUM_VAL

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

/* Fills the buffer with the thread name. */
static void fill_name(const struct k_thread *thread, char *buf, size_t size)
{
	snprintf(buf, size, "%s", k_thread_name_get((struct k_thread *)thread));
}

/* Fills the buffer with the thread ID (address). */
static void fill_id(const struct k_thread *thread, char *buf, size_t size)
{
	snprintf(buf, size, "%08x", (uint32_t)(uintptr_t)thread);
}

/* Fills the buffer with the thread entry point address. */
static void fill_entry(const struct k_thread *thread, char *buf, size_t size)
{
	snprintf(buf, size, "%08x", (uint32_t)(uintptr_t)thread->entry.pEntry);
}

/* Fills the buffer with the thread state string, overriding to 'current' for
 * the running thread. */
static void fill_state(const struct k_thread *thread, char *buf, size_t size)
{
	if (thread == k_current_get()) {
		snprintf(buf, size, "current");
	} else {
		k_thread_state_str((k_tid_t)thread, buf, size);
	}
}

/* Fills the buffer with the accumulated execution time. */
static void fill_time(const struct k_thread *thread, char *buf, size_t size)
{
#ifdef CONFIG_SCHED_THREAD_USAGE
	k_thread_runtime_stats_t thread_stats;
	if (k_thread_runtime_stats_get((k_tid_t)thread, &thread_stats) == 0) {
		uint64_t us = k_cyc_to_us_near64(thread_stats.execution_cycles);
		format_time_3_sig_digits(buf, size, us);
	}
#endif
}

/* Fills the buffer with the peak execution time. */
static void fill_peak(const struct k_thread *thread, char *buf, size_t size)
{
#ifdef CONFIG_SCHED_THREAD_USAGE_ANALYSIS
	k_thread_runtime_stats_t thread_stats;
	if (k_thread_runtime_stats_get((k_tid_t)thread, &thread_stats) == 0) {
		uint64_t us = k_cyc_to_us_near64(thread_stats.peak_cycles);
		format_time_3_sig_digits(buf, size, us);
	}
#endif
}

/* Fills the buffer with the average execution time. */
static void fill_avg(const struct k_thread *thread, char *buf, size_t size)
{
#ifdef CONFIG_SCHED_THREAD_USAGE_ANALYSIS
	k_thread_runtime_stats_t thread_stats;
	if (k_thread_runtime_stats_get((k_tid_t)thread, &thread_stats) == 0) {
		uint64_t us = k_cyc_to_us_near64(thread_stats.average_cycles);
		format_time_3_sig_digits(buf, size, us);
	}
#endif
}

/* Fills the buffer with the thread priority. */
static void fill_prio(const struct k_thread *thread, char *buf, size_t size)
{
	snprintf(buf, size, "%d", (int)thread->base.prio);
}

/* Fills the buffer with the current stack usage. */
static void fill_stkcur(const struct k_thread *thread, char *buf, size_t size)
{
#ifdef CONFIG_THREAD_STACK_INFO
	if (thread == k_current_get() && !k_is_in_isr()) {
		return;
	}
	uintptr_t cur_sp = get_stack_ptr(thread);
	size_t cur_used = 0;
	size_t stack_size = thread->stack_info.size;
	uintptr_t stack_top = (uintptr_t)thread->stack_info.start + stack_size;
	if (cur_sp >= (uintptr_t)thread->stack_info.start &&
	    cur_sp <= stack_top) {
		cur_used = stack_top - cur_sp;
	}
	snprintf(buf, size, "%u", (uint32_t)cur_used);
#endif
}

/* Fills the buffer with the maximum stack usage. */
static void fill_stkmax(const struct k_thread *thread, char *buf, size_t size)
{
#if defined(CONFIG_THREAD_STACK_INFO) && defined(CONFIG_INIT_STACKS)
	size_t unused_stack;
	if (k_thread_stack_space_get(thread, &unused_stack) == 0) {
		snprintf(buf, size, "%u",
			 (uint32_t)(thread->stack_info.size - unused_stack));
	}
#endif
}

/* Fills the buffer with the total stack size. */
static void fill_stksize(const struct k_thread *thread, char *buf, size_t size)
{
#ifdef CONFIG_THREAD_STACK_INFO
	snprintf(buf, size, "%zu", thread->stack_info.size);
#endif
}

/* Fills the buffer with the current stack pointer. */
static void fill_sp(const struct k_thread *thread, char *buf, size_t size)
{
	snprintf(buf, size, "%08x", (uint32_t)get_stack_ptr(thread));
}

/* Fills the buffer with the object address the thread is pending on. */
static void fill_waitobj(const struct k_thread *thread, char *buf, size_t size)
{
	if (thread->base.pended_on != NULL) {
		uintptr_t w_addr = (uintptr_t)thread->base.pended_on;
		snprintf(buf, size, "%08x", (uint32_t)w_addr);
	}
}

/* Fills the buffer with the remaining timeout in ticks. */
static void fill_timeout(const struct k_thread *thread, char *buf, size_t size)
{
	k_ticks_t ticks = k_thread_timeout_remaining_ticks(thread);
	if (ticks > 0 && ticks != K_TICKS_FOREVER) {
		uint64_t us = k_ticks_to_us_near64(ticks);
		format_time_3_sig_digits(buf, size, us);
	}
}

/* Fills the buffer with the current program counter. */
static void fill_pc(const struct k_thread *thread, char *buf, size_t size)
{
	uint32_t pc, lr;

	get_thread_registers(thread, &pc, &lr);
	if (pc != 0) {
		snprintf(buf, size, "%08x", pc);
	}
}

/* Fills the buffer with the current link register (return address). */
static void fill_lr(const struct k_thread *thread, char *buf, size_t size)
{
	uint32_t pc, lr;

	get_thread_registers(thread, &pc, &lr);
	if (lr != 0) {
		snprintf(buf, size, "%08x", lr);
	}
}

static void print_col(const struct shell *sh, const char *str, size_t width)
{
	if (sh != NULL) {
		shell_fprintf(sh, SHELL_NORMAL,
			      THREADINFO_COL_SEP_PRE
			      "%-*.*s" THREADINFO_COL_SEP_POST,
			      width, width, str);
	} else {
		printk(THREADINFO_COL_SEP_PRE "%-*.*s" THREADINFO_COL_SEP_POST,
		       width, width, str);
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

struct threadinfo_col {
	const char *header;
	uint8_t width;
	void (*fill)(const struct k_thread *thread, char *buf, size_t size);
	bool enabled;
};

static const struct threadinfo_col cols[] = {
#define COL_ENTRY(col_id, col_width, col_header, fill_func, cond) \
	{ .header = col_header,                                   \
	  .width = col_width,                                     \
	  .fill = fill_func,                                      \
	  .enabled = cond },
	THREADINFO_COLUMNS_FOREACH(COL_ENTRY)
#undef COL_ENTRY
};

struct thread_print_data {
	const struct shell *shell;
	uint32_t mask;
};

static void print_thread_row(const struct k_thread *thread, void *user_data)
{
	struct thread_print_data *d = (struct thread_print_data *)user_data;
	char buf[THREADINFO_MAX_COL_WIDTH];

	for (int i = 0; i < ARRAY_SIZE(cols); i++) {
		if (cols[i].enabled && (d->mask & BIT(i))) {
			buf[0] = '\0';
			cols[i].fill(thread, buf, sizeof(buf));
			print_col(d->shell, buf, cols[i].width);
		}
	}
	print_end_row(d->shell);
}

static void print_header(const struct shell *shell, uint32_t mask)
{
	for (int i = 0; i < ARRAY_SIZE(cols); i++) {
		if (cols[i].enabled && (mask & BIT(i))) {
			print_col(shell, cols[i].header, cols[i].width);
		}
	}
	print_end_row(shell);

	for (int i = 0; i < ARRAY_SIZE(cols); i++) {
		if (cols[i].enabled && (mask & BIT(i))) {
			print_col(shell, "--------", cols[i].width);
		}
	}
	print_end_row(shell);
}

void threadinfo_dump(uint32_t mask, const struct shell *shell)
{
	struct thread_print_data data = { .shell = shell, .mask = mask };
	print_header(data.shell, data.mask);
	k_thread_foreach_unlocked(print_thread_row, &data);
}

#ifdef CONFIG_PLATFORM_EC_CONSOLE_CMD_THREADINFO
static int cmd_threadinfo(const struct shell *shell, size_t argc, char *argv[])
{
	threadinfo_dump(-1, shell);
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

	args->response_size = sizeof(*r);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_THREAD_INFO_DETAIL, hc_thread_info_detail,
		     EC_VER_MASK(0));
#endif
