/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "builtin/assert.h"
#include "common.h"
#include "hooks.h"
#include "host_command.h"
#include "panic.h"
#include "panic_utils.h"
#include "system.h"
#include "task.h"

#include <zephyr/arch/cpu.h>
#include <zephyr/cache.h>
#include <zephyr/fatal.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>

LOG_MODULE_REGISTER(panic, LOG_LEVEL_INF);

/*
 * Arch-specific configuration
 *
 * For each architecture, define:
 * - PANIC_ARCH, which should be the corresponding arch field of the
 *   panic_data struct.
 * - PANIC_REG_LIST, which is a macro that takes parameters M and M_GPR, and
 *   applies M/M_GPR to 3-tuples of:
 *   - zephyr esf field name
 *   - panic_data struct field name
 *   - human readable name
 *   M_GPR is used for General Purpose Registers
 */

#if defined(CONFIG_ARM)
#define PANIC_ARCH PANIC_ARCH_CORTEX_M
#if defined(CONFIG_EXTRA_EXCEPTION_INFO)
#define EXTRA_PANIC_REG_LIST(M, M_GPR)                                       \
	M_GPR(extra_info.callee->v1, cm.regs[CORTEX_PANIC_REGISTER_R4], v1)  \
	M_GPR(extra_info.callee->v2, cm.regs[CORTEX_PANIC_REGISTER_R5], v2)  \
	M_GPR(extra_info.callee->v3, cm.regs[CORTEX_PANIC_REGISTER_R6], v3)  \
	M_GPR(extra_info.callee->v4, cm.regs[CORTEX_PANIC_REGISTER_R7], v4)  \
	M_GPR(extra_info.callee->v5, cm.regs[CORTEX_PANIC_REGISTER_R8], v5)  \
	M_GPR(extra_info.callee->v6, cm.regs[CORTEX_PANIC_REGISTER_R9], v6)  \
	M_GPR(extra_info.callee->v7, cm.regs[CORTEX_PANIC_REGISTER_R10], v7) \
	M_GPR(extra_info.callee->v8, cm.regs[CORTEX_PANIC_REGISTER_R11], v8) \
	M(extra_info.callee->psp, cm.regs[CORTEX_PANIC_REGISTER_PSP], psp)   \
	M(basic.xpsr, cm.regs[CORTEX_PANIC_REGISTER_IPSR], ipsr)             \
	M(extra_info.exc_return, cm.regs[CORTEX_PANIC_REGISTER_LR], exc_rtn) \
	M(extra_info.msp, cm.regs[CORTEX_PANIC_REGISTER_MSP], msp)
/*
 * IPSR is a subset of xPSR, which is already captured in PANIC_REG_LIST, but
 * print it anyway because it may contain panic exception.
 */
#else
#define EXTRA_PANIC_REG_LIST(M, M_GPR)
#endif
/* TODO(b/245423691): Copy other status registers (e.g. CFSR) when available. */
#define PANIC_REG_LIST(M, M_GPR)          \
	M_GPR(basic.r0, cm.frame[0], a1)  \
	M_GPR(basic.r1, cm.frame[1], a2)  \
	M_GPR(basic.r2, cm.frame[2], a3)  \
	M_GPR(basic.r3, cm.frame[3], a4)  \
	M_GPR(basic.r12, cm.frame[4], ip) \
	M(basic.lr, cm.frame[5], lr)      \
	M(basic.pc, cm.frame[6], pc)      \
	M(basic.xpsr, cm.frame[7], xpsr)  \
	EXTRA_PANIC_REG_LIST(M, M_GPR)
#define PANIC_REG_EXCEPTION(pdata) ((pdata)->cm.regs[1])
#define PANIC_REG_REASON(pdata) ((pdata)->cm.regs[3])
#define PANIC_REG_INFO(pdata) ((pdata)->cm.regs[4])
#elif defined(CONFIG_RISCV) && !defined(CONFIG_64BIT)
/*
 * Not all registers are passed in the context from Zephyr
 * (see include/zephyr/arch/riscv/exp.h), in particular
 * the mcause register is not saved (mstatus is saved instead).
 * The assignments must match include/panic_defs.h
 */
#define PANIC_ARCH PANIC_ARCH_RISCV_RV32I
#define PANIC_REG_LIST(M, M_GPR)      \
	M(ra, riscv.regs[29], ra)     \
	M_GPR(a0, riscv.regs[26], a0) \
	M_GPR(a1, riscv.regs[25], a1) \
	M_GPR(a2, riscv.regs[24], a2) \
	M_GPR(a3, riscv.regs[23], a3) \
	M_GPR(a4, riscv.regs[22], a4) \
	M_GPR(a5, riscv.regs[21], a5) \
	M_GPR(a6, riscv.regs[20], a6) \
	M_GPR(a7, riscv.regs[19], a7) \
	M_GPR(t0, riscv.regs[18], t0) \
	M_GPR(t1, riscv.regs[17], t1) \
	M_GPR(t2, riscv.regs[16], t2) \
	M_GPR(t3, riscv.regs[15], t3) \
	M_GPR(t4, riscv.regs[14], t4) \
	M_GPR(t5, riscv.regs[13], t5) \
	M_GPR(t6, riscv.regs[12], t6) \
	M(mepc, riscv.mepc, mepc)     \
	M(mstatus, riscv.mcause, mstatus)
#define PANIC_REG_EXCEPTION(pdata) ((pdata)->riscv.mcause)
#define PANIC_REG_REASON(pdata) ((pdata)->riscv.regs[11])
#define PANIC_REG_INFO(pdata) ((pdata)->riscv.regs[10])
#elif defined(CONFIG_X86)
#define PANIC_ARCH PANIC_ARCH_X86
#define PANIC_REG_LIST(M, M_GPR) \
	M(eax, x86.eax, eax)     \
	M(ebx, x86.ebx, ebx)     \
	M(ecx, x86.ecx, ecx)     \
	M(edx, x86.edx, edx)     \
	M(esi, x86.esi, esi)     \
	M(edi, x86.edi, edi)     \
	M(cs, x86.cs, cs)        \
	M(eip, x86.eip, eip)
#define PANIC_REG_EXCEPTION(pdata) ((pdata)->x86.eflags)
#define PANIC_REG_REASON(pdata) ((pdata)->x86.vector)
#define PANIC_REG_INFO(pdata) ((pdata)->x86.error_code)
#elif defined(CONFIG_ARCH_POSIX)
#define PANIC_ARCH PANIC_ARCH_POSIX
#define PANIC_REG_LIST(M, M_GPR) \
	M(dummy, posix.esf_placeholder, placeholder) /* nocheck */
#define PANIC_REG_EXCEPTION(pdata) ((pdata)->posix.exception)
#define PANIC_REG_REASON(pdata) ((pdata)->posix.reason)
#define PANIC_REG_INFO(pdata) ((pdata)->posix.info)
#else
/* Not implemented for this arch */
#error "Unsupported architecture for PANIC_ARCH"
#endif

/* Macros to be applied to PANIC_REG_LIST as M */
#define PANIC_COPY_REGS(esf_field, pdata_field, human_name) \
	pdata->pdata_field = esf->esf_field;
#define PANIC_COPY_REGS_GPR(esf_field, pdata_field, human_name)              \
	pdata->pdata_field = COND_CODE_1(CONFIG_PLATFORM_EC_PANIC_STRIP_GPR, \
					 (0), (esf->esf_field));

#define PANIC_PRINT_REGS(esf_field, pdata_field, human_name) \
	panic_printf("  %-8s = 0x%08X\n", #human_name, pdata->pdata_field);

void panic_data_print(const struct panic_data *pdata)
{
	PANIC_REG_LIST(PANIC_PRINT_REGS, PANIC_PRINT_REGS);
#if defined(CONFIG_RISCV) && !defined(CONFIG_64BIT)
	PANIC_PRINT_REGS(NULL, riscv.regs[10], S1);
	PANIC_PRINT_REGS(NULL, riscv.regs[11], S0);
#endif
}

/**
 * Reset/prepare a panic_data structure for writing.
 *
 * Sets struct size/version, architecture, and default image flags
 * (PANIC_DATA_FLAG_RW_IMAGE or PANIC_DATA_FLAG_RO_IMAGE).
 * Note: magic is NOT set here; call panic_data_finalize() when writing
 * completes.
 */
struct panic_data *panic_data_reset(struct panic_data *pdata)
{
	if (!pdata) {
		pdata = get_panic_data_write();
	}

	memset(pdata, 0, CONFIG_PANIC_DATA_SIZE);
	pdata->struct_size = CONFIG_PANIC_DATA_SIZE;
	pdata->struct_version = PANIC_DATA_VERSION;
	pdata->arch = PANIC_ARCH;
	pdata->flags = IS_ENABLED(CONFIG_CROS_EC_RW) ?
			       PANIC_DATA_FLAG_RW_IMAGE :
			       PANIC_DATA_FLAG_RO_IMAGE;
	/* Note: magic remains 0 (uncommitted) until panic_data_finalize() */

	return pdata;
}

/**
 * Finalize panic data by setting the valid magic number and flushing to RAM.
 */
void panic_data_finalize(struct panic_data *pdata)
{
	if (pdata) {
		pdata->magic = PANIC_DATA_MAGIC;
		sys_cache_data_flush_range((void *)pdata, pdata->struct_size);
	}
}

void panic_data_write_esf(const struct arch_esf *esf)
{
	struct panic_data *pdata = panic_data_reset(NULL);

	if (esf) {
		if (PANIC_ARCH == PANIC_ARCH_CORTEX_M) {
			pdata->flags |= PANIC_DATA_FLAG_FRAME_VALID;
		}

		PANIC_REG_LIST(PANIC_COPY_REGS, PANIC_COPY_REGS_GPR);
	}

	/* Finalize and flush the panic data to RAM before reboot. */
	panic_data_finalize(pdata);
}

void panic_data_write_assert(const char *path, unsigned int line)
{
	struct panic_data *const pdata = panic_data_reset(NULL);
	uint32_t info;

	if (path) {
		const char *last_slash = strrchr(path, '/');
		const char *filename = last_slash ? last_slash + 1 : path;

		/*
		 * Encode the first two characters of the filename and 16-bit
		 * line number into the 32-bit panic info register.
		 */
		info = (filename[0] << 24) | (filename[1] << 16) |
		       (line & 0xffff);
	} else {
		info = (uint32_t)-1;
	}

	panic_set_reason_reg(pdata, PANIC_SW_ASSERT);
	panic_set_info_reg(pdata, info);
	panic_set_exception_reg(pdata, (uint8_t)(uintptr_t)k_current_get());

	panic_data_finalize(pdata);
}

void panic_data_write_watchdog_warning(uintptr_t pc,
				       const struct k_thread *thread)
{
	struct panic_data *const pdata = panic_data_reset(NULL);

	panic_set_reason_reg(pdata, PANIC_SW_WATCHDOG_WARN);
	panic_set_info_reg(pdata, (uint32_t)pc);
	panic_set_exception_reg(pdata, (uint8_t)(uintptr_t)thread);

	panic_data_finalize(pdata);
}

void panic_data_write_fatal(unsigned int reason, const struct k_thread *thread)
{
	struct panic_data *const pdata = panic_data_reset(NULL);

	panic_set_reason_reg(pdata, PANIC_ZEPHYR_FATAL_ERROR);
	panic_set_info_reg(pdata, (uint32_t)reason);
	panic_set_exception_reg(pdata, (uint8_t)(uintptr_t)thread);

	panic_data_finalize(pdata);
}

void panic_data_write_sw(uint32_t reason, uint32_t info, uint8_t exception)
{
	struct panic_data *const pdata = panic_data_reset(NULL);

	panic_set_reason_reg(pdata, reason);
	panic_set_info_reg(pdata, info);
	panic_set_exception_reg(pdata, exception);

	panic_data_finalize(pdata);
}

#if !defined(CONFIG_ZTEST_FATAL_HOOK)
void k_sys_fatal_error_handler(unsigned int reason, const struct arch_esf *esf)
{
	/*
	 * If CONFIG_LOG is on, the exception details
	 * have already been logged to the console.
	 */
	if (!IS_ENABLED(CONFIG_LOG)) {
		panic_printf("Fatal error: %u\n", reason);
	}

	if ((PANIC_ARCH != PANIC_ARCH_UNSUPPORTED) && esf) {
		panic_data_write_esf(esf);
		if (!IS_ENABLED(CONFIG_LOG)) {
			panic_data_print(panic_get_data());
		}
	} else {
		/* If an esf structure is empty, store just the reason provided
		 * by Zephyr. It can be caused e.g. by a spurious interrupt.
		 */
		panic_data_write_fatal(reason, k_current_get());
	}

	LOG_PANIC();

	if (IS_ENABLED(CONFIG_PLATFORM_EC_CONSOLE_CMD_CRASH_NESTED))
		command_crash_nested_handler();

	sys_cache_data_flush_and_invd_all();

	/*
	 * Reboot immediately, don't wait for watchdog, otherwise
	 * the watchdog will overwrite this panic.
	 */
	panic_reboot();
}
#endif /* CONFIG_ZTEST_FATAL_HOOK */

#ifdef CONFIG_PLATFORM_EC_DEBUG_ASSERT
#ifndef CONFIG_ASSERT_TEST
FUNC_NORETURN
#endif
__override void zassert_post_action(const char *path, unsigned int line)
{
	panic_data_write_assert(path, line);

	if (IS_ENABLED(CONFIG_PLATFORM_EC_CONSOLE_CMD_CRASH_NESTED))
		command_crash_nested_handler();

	if (!IS_ENABLED(CONFIG_ASSERT_NO_FILE_INFO) &&
	    IS_ENABLED(CONFIG_PLATFORM_EC_PANIC_PRINT_STACK_ON_ASSERT)) {
		print_stack_trace(k_current_get());
	}

	panic_reboot();

#ifndef CONFIG_ASSERT_TEST
	CODE_UNREACHABLE;
#endif
}
#endif /* CONFIG_PLATFORM_EC_DEBUG_ASSERT */

uint32_t panic_get_reason_reg(const struct panic_data *pdata)
{
	return pdata ? PANIC_REG_REASON(pdata) : 0;
}

void panic_set_reason_reg(struct panic_data *pdata, uint32_t reason)
{
	if (pdata) {
		PANIC_REG_REASON(pdata) = reason;
	}
}

uint32_t panic_get_info_reg(const struct panic_data *pdata)
{
	return pdata ? PANIC_REG_INFO(pdata) : 0;
}

void panic_set_info_reg(struct panic_data *pdata, uint32_t info)
{
	if (pdata) {
		PANIC_REG_INFO(pdata) = info;
	}
}

uint8_t panic_get_exception_reg(const struct panic_data *pdata)
{
	return pdata ? (uint8_t)PANIC_REG_EXCEPTION(pdata) : 0;
}

void panic_set_exception_reg(struct panic_data *pdata, uint8_t exception)
{
	if (pdata) {
		PANIC_REG_EXCEPTION(pdata) = exception;
	}
}

test_export_static int panic_data_init(void)
{
	bool is_panic_new;
	bool is_watchdog_reset;
	struct panic_data *pdata = panic_get_data();
	uint32_t reason = panic_get_reason_reg(pdata);

	is_watchdog_reset =
		!!(system_get_reset_flags() & EC_RESET_FLAG_WATCHDOG);

	if (is_watchdog_reset) {
		LOG_WRN("Watchdog Reset Detected");
	}

	is_panic_new = panic_data_is_new();

	if (is_panic_new) {
		LOG_WRN("New Panic Detected: %s",
			panic_sw_reason_is_valid(reason) ?
				panic_sw_reasons[reason - PANIC_SW_BASE] :
				"");
	}

	/*
	 * Only update the panic reason in RW since RO may have an older panic
	 * data version and updating the panic reason will cause new fields to
	 * be overwritten.
	 */
	if (IS_ENABLED(CONFIG_CROS_EC_RO)) {
		return 0;
	}

	/*
	 * Log panic cause if watchdog caused reset and panic cause
	 * was not already logged. This must happen after parsing jump_data
	 * to ensure we have restored the reset flags passed from the previous
	 * image.
	 */
	if (is_watchdog_reset) {
		/* If the panic reason is a watchdog warning, then change
		 * the reason to a regular watchdog reason while preserving
		 * the info and exception from the watchdog warning.
		 */
		if (is_panic_new && reason == PANIC_SW_WATCHDOG_WARN) {
			LOG_INF("Promoting watchdog warning to watchdog panic");
			panic_set_reason_reg(pdata, PANIC_SW_WATCHDOG);
			panic_data_finalize(pdata);
		} else if ((reason != PANIC_SW_WATCHDOG &&
			    reason != PANIC_SW_WATCHDOG_HARD) ||
			   !is_panic_new) {
			/* The watchdog panic info may have already been
			 * initialized by the watchdog handler, so only set it
			 * here if the panic reason is not a watchdog or the
			 * panic info has already been read, i.e. an old
			 * watchdog panic. Both RO and RW flags are unset
			 * because source image is not known.
			 */
			LOG_INF("Setting hard watchdog panic");
			pdata = panic_data_reset(NULL);
			pdata->flags &= ~(PANIC_DATA_FLAG_RW_IMAGE |
					  PANIC_DATA_FLAG_RO_IMAGE);
			panic_set_reason_reg(pdata, PANIC_SW_WATCHDOG_HARD);
			panic_data_finalize(pdata);
		}
	}

	return 0;
}

/* Initialize panic data after reset flags and console are ready. */
SYS_INIT(panic_data_init, PRE_KERNEL_2, 0);

#if defined(CONFIG_PLATFORM_EC_PANIC_HOST_EVENT)
static void panic_host_event_init(void)
{
	struct panic_data *pdata = panic_get_data();

	if (pdata && !(pdata->flags & PANIC_DATA_FLAG_OLD_HOSTEVENT)) {
		host_set_single_event(EC_HOST_EVENT_PANIC);
		pdata->flags |= PANIC_DATA_FLAG_OLD_HOSTEVENT;
	}
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, panic_host_event_init, HOOK_PRIO_LAST);
#endif
