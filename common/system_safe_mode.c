/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "cpu.h"
#include "ec_commands.h"
#include "hooks.h"
#include "host_command.h"
#include "panic.h"
#include "stddef.h"
#include "system.h"
#include "system_safe_mode.h"
#include "task.h"
#include "timer.h"
#include "watchdog.h"

static bool in_safe_mode;

static const int safe_mode_allowed_hostcmds[] = {
	EC_CMD_SYSINFO,		EC_CMD_GET_PROTOCOL_INFO,
	EC_CMD_GET_VERSION,	EC_CMD_CONSOLE_SNAPSHOT,
	EC_CMD_CONSOLE_READ,	EC_CMD_GET_NEXT_EVENT,
	EC_CMD_GET_UPTIME_INFO, EC_CMD_GET_PANIC_INFO
};

bool is_task_safe_mode_critical(task_id_t task_id)
{
	const task_id_t safe_mode_critical_tasks[] = {
#ifdef HAS_TASK_HOOKS
		TASK_ID_HOOKS,
#endif
		TASK_ID_IDLE,
#ifdef HAS_TASK_HOSTCMD
		TASK_ID_HOSTCMD,
#endif
#ifdef HAS_TASK_MAIN
		TASK_ID_MAIN,
#endif
#ifdef HAS_TASK_SYSWORKQ
		TASK_ID_SYSWORKQ,
#endif
	};
	for (int i = 0; i < ARRAY_SIZE(safe_mode_critical_tasks); i++)
		if (safe_mode_critical_tasks[i] == task_id)
			return true;
	return false;
}

bool is_current_task_safe_mode_critical(void)
{
	return is_task_safe_mode_critical(task_get_current());
}

#ifndef CONFIG_ZEPHYR

int disable_non_safe_mode_critical_tasks(void)
{
	for (task_id_t task_id = 0; task_id < TASK_ID_COUNT; task_id++) {
		if (!is_task_safe_mode_critical(task_id)) {
			task_disable_task(task_id);
		}
	}
	return EC_SUCCESS;
}

#endif /* CONFIG_ZEPHYR */

void handle_system_safe_mode_timeout(void)
{
	panic_printf("Safe mode timeout after %d msec\n",
		     CONFIG_SYSTEM_SAFE_MODE_TIMEOUT_MSEC);
	panic_reboot();
}
DECLARE_DEFERRED(handle_system_safe_mode_timeout);

__overridable int schedule_system_safe_mode_timeout(void)
{
	hook_call_deferred(&handle_system_safe_mode_timeout_data,
			   CONFIG_SYSTEM_SAFE_MODE_TIMEOUT_MSEC * MSEC);
	return EC_SUCCESS;
}

bool system_is_in_safe_mode(void)
{
	return !!in_safe_mode;
}

/* TODO: Remove when coredumps are supported.
 *
 * This is a temporary workaround for getting the stack into the console
 * buffer. When coredumps are supported this workaround can be removed.
 */

#define STACK_DUMP_SIZE_WORDS 32

/*
 * Returns non-zero if the exception frame was created on the main stack, or
 * zero if it's on the process stack.
 *
 * See B1.5.8 "Exception return behavior" of ARM DDI 0403D for details.
 */
static int32_t is_frame_in_handler_stack(const uint32_t exc_return)
{
	return (exc_return & 0xf) == 1 || (exc_return & 0xf) == 9;
}

/*
 * Returns the size of the exception frame.
 *
 * See B1.5.7 "Stack alignment on exception entry" of ARM DDI 0403D for details.
 * In short, the exception frame size can be either 0x20, 0x24, 0x68, or 0x6c
 * depending on FPU context and padding for 8-byte alignment.
 */
static uint32_t get_exception_frame_size(const struct panic_data *pdata)
{
	uint32_t frame_size = 0;

	/* base exception frame */
	frame_size += 8 * sizeof(uint32_t);

	/* CPU uses xPSR[9] to indicate whether it padded the stack for
	 * alignment or not.
	 */
	if (pdata->cm.frame[CORTEX_PANIC_FRAME_REGISTER_PSR] & BIT(9))
		frame_size += sizeof(uint32_t);

	if (IS_ENABLED(CONFIG_FPU)) {
		/* CPU uses EXC_RETURN[4] to indicate whether it stored extended
		 * frame for FPU or not.
		 */
		if (!(pdata->cm.regs[CORTEX_PANIC_REGISTER_LR] & BIT(4)))
			frame_size += 18 * sizeof(uint32_t);
	}

	return frame_size;
}

/*
 * Returns the position of the process stack before the exception frame.
 * It computes the size of the exception frame and adds it to psp.
 * If the exception happened in the exception context, it returns psp as is.
 */
static uint32_t get_process_stack_position(const struct panic_data *pdata)
{
	uint32_t psp = pdata->cm.regs[CORTEX_PANIC_REGISTER_PSP];

	if (!is_frame_in_handler_stack(
		    pdata->cm.regs[CORTEX_PANIC_REGISTER_LR]))
		psp += get_exception_frame_size(pdata);

	return psp;
}

/*
 * Prints process stack contents stored above the exception frame.
 */
static void print_panic_task_stack(void)
{
	uint32_t psp;
	const struct panic_data *pdata = panic_get_data();

	if (!pdata || !(pdata->flags & PANIC_DATA_FLAG_FRAME_VALID)) {
		ccprintf("Invalid panic data. Cannot dump stack.\n");
		return;
	}
	ccprintf("=========== Process Stack Contents ===========");
	psp = get_process_stack_position(pdata);
	for (int i = 0; i < STACK_DUMP_SIZE_WORDS; i++) {
		if (psp + sizeof(uint32_t) > CONFIG_RAM_BASE + CONFIG_RAM_SIZE)
			break;
		if (i % 4 == 0)
			ccprintf("\n%08x:", psp);
		ccprintf(" %08x", *(uint32_t *)psp);
		psp += sizeof(uint32_t);
	}
	ccprintf("\n");
	/* Flush so dump isn't mixed with other output */
	cflush();
}

bool command_is_allowed_in_safe_mode(int command)
{
	for (int i = 0; i < ARRAY_SIZE(safe_mode_allowed_hostcmds); i++)
		if (command == safe_mode_allowed_hostcmds[i])
			return true;
	return false;
}

static void system_safe_mode_start(void)
{
	ccprintf("*** Post Panic System Safe Mode ***\n");
	if (IS_ENABLED(CONFIG_PLATFORM_EC_SYSTEM_SAFE_MODE_PRINT_STACK))
		print_panic_task_stack();
	if (IS_ENABLED(CONFIG_HOSTCMD_EVENTS))
		host_set_single_event(EC_HOST_EVENT_PANIC);
}
DECLARE_DEFERRED(system_safe_mode_start);

int start_system_safe_mode(void)
{
	if (!system_is_in_rw()) {
		panic_printf("Can only enter safe mode from RW image\n");
		return EC_ERROR_INVAL;
	}

	if (system_is_in_safe_mode()) {
		panic_printf("Already in system safe mode");
		return EC_ERROR_INVAL;
	}

	if (is_current_task_safe_mode_critical()) {
		/* TODO: Restart critical tasks */
		panic_printf(
			"Fault in critical task, cannot enter system safe mode\n");
		return EC_ERROR_INVAL;
	}

	disable_non_safe_mode_critical_tasks();

	schedule_system_safe_mode_timeout();

	/*
	 * Schedule a deferred function to run immediately
	 * after returning from fault handler. Defer operations that
	 * must not run in an ISR to this function.
	 */
	hook_call_deferred(&system_safe_mode_start_data, 0);

	in_safe_mode = true;

	panic_printf("\nStarting system safe mode\n");

	return EC_SUCCESS;
}

#ifdef TEST_BUILD
void set_system_safe_mode(bool mode)
{
	in_safe_mode = mode;
}
#endif
