/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "builtin/assert.h"
#include "common.h"
#include "panic.h"
#include "panic_utils.h"
#include "system.h"
#include "task.h"


#include <zephyr/arch/cpu.h>
#include <zephyr/cache.h>
#include <zephyr/fatal.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>

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
#define PANIC_REG_EXCEPTION(pdata) pdata->cm.regs[1]
#define PANIC_REG_REASON(pdata) pdata->cm.regs[3]
#define PANIC_REG_INFO(pdata) pdata->cm.regs[4]
#elif defined(CONFIG_RISCV) && !defined(CONFIG_64BIT)
#define PANIC_ARCH PANIC_ARCH_RISCV_RV32I
/*
 * Not all registers are passed in the context from Zephyr
 * (see include/zephyr/arch/riscv/exp.h), in particular
 * the mcause register is not saved (mstatus is saved instead).
 * The assignments must match include/panic_defs.h
 */
#define PANIC_REG_LIST(M, M_GPR)  \
	M(ra, riscv.regs[29], ra) \
	M(a0, riscv.regs[26], a0) \
	M(a1, riscv.regs[25], a1) \
	M(a2, riscv.regs[24], a2) \
	M(a3, riscv.regs[23], a3) \
	M(a4, riscv.regs[22], a4) \
	M(a5, riscv.regs[21], a5) \
	M(a6, riscv.regs[20], a6) \
	M(a7, riscv.regs[19], a7) \
	M(t0, riscv.regs[18], t0) \
	M(t1, riscv.regs[17], t1) \
	M(t2, riscv.regs[16], t2) \
	M(t3, riscv.regs[15], t3) \
	M(t4, riscv.regs[14], t4) \
	M(t5, riscv.regs[13], t5) \
	M(t6, riscv.regs[12], t6) \
	M(mepc, riscv.mepc, mepc) \
	M(mstatus, riscv.mcause, mstatus)
#define PANIC_REG_EXCEPTION(pdata) (pdata->riscv.mcause)
#define PANIC_REG_REASON(pdata) (pdata->riscv.regs[11])
#define PANIC_REG_INFO(pdata) (pdata->riscv.regs[10])
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
#define PANIC_REG_EXCEPTION(pdata) (pdata->x86.eflags)
#define PANIC_REG_REASON(pdata) (pdata->x86.vector)
#define PANIC_REG_INFO(pdata) (pdata->x86.error_code)
#else
/* Not implemented for this arch */
#define PANIC_ARCH PANIC_ARCH_UNSUPPORTED
#define PANIC_REG_LIST(M, M_GPR)
static uint8_t placeholder_exception_reg;
static uint32_t placeholder_reason_reg;
static uint32_t placeholder_info_reg;
#define PANIC_REG_EXCEPTION(unused) placeholder_exception_reg
#define PANIC_REG_REASON(unused) placeholder_reason_reg
#define PANIC_REG_INFO(unused) placeholder_info_reg
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
	if (pdata->arch == PANIC_ARCH_WATCHDOG) {
		panic_printf("=== WATCHDOG ====== struct version: %d ===\n", pdata->wd.struct_version);
		panic_printf("  flags:          0x%08X\n", pdata->wd.flags);
		panic_printf("  thread_id:      0x%08X\n", pdata->wd.thread_id);
		panic_printf("  pc:             0x%08X\n", pdata->wd.pc);
		panic_printf("  lr:             0x%08X\n", pdata->wd.lr);
		panic_printf("  warning_rtc:    %u\n", pdata->wd.warning_rtc);
		panic_printf("  warning_uptime: %u ms\n", pdata->wd.warning_uptime);
		panic_printf("  reset_uptime:   %u ms\n", pdata->wd.watchdog_reset_uptime_ms);
		panic_printf("  version:        %.16s\n", pdata->wd.version);
		panic_printf("  power_state:    0x%02X\n", pdata->wd.power_state);
		panic_printf("  power_signals:  0x%08X\n", pdata->wd.power_signals);
	} else {
		PANIC_REG_LIST(PANIC_PRINT_REGS, PANIC_PRINT_REGS);
#if defined(CONFIG_RISCV) && !defined(CONFIG_64BIT)
		PANIC_PRINT_REGS(NULL, riscv.regs[10], S1);
		PANIC_PRINT_REGS(NULL, riscv.regs[11], S0);
#endif
	}
}


#if !defined(CONFIG_ZTEST_FATAL_HOOK)
static void copy_esf_to_panic_data(const struct arch_esf *esf,
				   struct panic_data *pdata)
{
	memset(pdata, 0, CONFIG_PANIC_DATA_SIZE);
	pdata->arch = PANIC_ARCH;
	pdata->struct_version = 2;
	pdata->flags = IS_ENABLED(SECTION_IS_RW) ? PANIC_DATA_FLAG_RW_IMAGE :
						   PANIC_DATA_FLAG_RO_IMAGE;
	pdata->flags |= (PANIC_ARCH == PANIC_ARCH_CORTEX_M) ?
				PANIC_DATA_FLAG_FRAME_VALID :
				0;
	pdata->reserved = 0;
	pdata->struct_size = sizeof(*pdata);
	pdata->magic = PANIC_DATA_MAGIC;

	PANIC_REG_LIST(PANIC_COPY_REGS, PANIC_COPY_REGS_GPR);

	/* Flush the panic data to RAM before coming reboot. */
	sys_cache_data_flush_range(pdata, sizeof(*pdata));
}

void k_sys_fatal_error_handler(unsigned int reason, const struct arch_esf *esf)
{
	struct panic_data *pdata = get_panic_data_write();

	/*
	 * If CONFIG_LOG is on, the exception details
	 * have already been logged to the console.
	 */
	if (!IS_ENABLED(CONFIG_LOG)) {
		panic_printf("Fatal error: %u\n", reason);
	}

	if ((PANIC_ARCH != PANIC_ARCH_UNSUPPORTED) && esf) {
		copy_esf_to_panic_data(esf, pdata);
		if (!IS_ENABLED(CONFIG_LOG)) {
			panic_data_print(panic_get_data());
		}
	} else {
		/* If a esf structure is empty, store just the reason provided
		 * by Zephyr. It can be caused e.g. by a spurious interrupt.
		 */
		uint8_t flags = pdata->flags;
		panic_set_reason(PANIC_ZEPHYR_FATAL_ERROR, (uint32_t)reason,
				 task_get_current());
		/* Keep panic flags */
		pdata->flags = flags;
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
	__ASSERT_UNREACHABLE;
}
#endif /* CONFIG_ZTEST_FATAL_HOOK */

#ifdef CONFIG_PLATFORM_EC_DEBUG_ASSERT
#ifdef CONFIG_ASSERT_NO_FILE_INFO
__override void assert_post_action(void)
{
	panic_set_reason(PANIC_SW_ASSERT, -1, task_get_current());

	if (IS_ENABLED(CONFIG_PLATFORM_EC_CONSOLE_CMD_CRASH_NESTED))
		command_crash_nested_handler();

	panic_reboot();
	__ASSERT_UNREACHABLE;
}
#else
__override void assert_post_action(const char *path, unsigned int line)
{
	const k_tid_t thread = k_current_get();

	/* Extract filename from path */
	const char *last_slash = strrchr(path, '/');
	const char *filename = last_slash ? last_slash + 1 : path;
	/* Top two bytes of info register is first two characters of filename.
	 * Bottom two bytes of info register is line number.
	 */
	panic_set_reason(PANIC_SW_ASSERT,
			 (filename[0] << 24) | (filename[1] << 16) |
				 (line & 0xffff),
			 task_get_current());

	if (IS_ENABLED(CONFIG_PLATFORM_EC_CONSOLE_CMD_CRASH_NESTED))
		command_crash_nested_handler();

	if (IS_ENABLED(CONFIG_PLATFORM_EC_PANIC_PRINT_STACK_ON_ASSERT)) {
		print_stack_trace(thread);
	}

	panic_reboot();
	__ASSERT_UNREACHABLE;
}
#endif /* CONFIG_ASSERT_NO_FILE_INFO */
#endif /* CONFIG_PLATFORM_EC_DEBUG_ASSERT */

void panic_set_reason(uint32_t reason, uint32_t info, uint8_t exception)
{
	struct panic_data *const pdata = get_panic_data_write();

	/* Setup panic data structure */
	memset(pdata, 0, CONFIG_PANIC_DATA_SIZE);
	pdata->magic = PANIC_DATA_MAGIC;
	pdata->struct_size = CONFIG_PANIC_DATA_SIZE;
	pdata->struct_version = 2;
	pdata->arch = PANIC_ARCH;
	pdata->flags = IS_ENABLED(SECTION_IS_RW) ? PANIC_DATA_FLAG_RW_IMAGE :
						   PANIC_DATA_FLAG_RO_IMAGE;

	/* Log panic cause */
	PANIC_REG_EXCEPTION(pdata) = exception;
	PANIC_REG_REASON(pdata) = reason;
	PANIC_REG_INFO(pdata) = info;

	/* Flush the panic data to RAM before potential reboot. */
	sys_cache_data_flush_range(pdata, sizeof(*pdata));

}

void panic_get_reason(uint32_t *reason, uint32_t *info, uint8_t *exception)
{
	struct panic_data *const pdata = panic_get_data();

	if (pdata && pdata->struct_version == 2) {
               if (pdata->arch == PANIC_ARCH_WATCHDOG) {
                       *reason = pdata->wd.reason_cm;
                       *info = pdata->wd.pc;
                       *exception = (uint8_t)pdata->wd.thread_id;
               } else {
                       *exception = PANIC_REG_EXCEPTION(pdata);
                       *reason = PANIC_REG_REASON(pdata);
                       *info = PANIC_REG_INFO(pdata);
               }
	} else {
		*exception = *reason = *info = 0;
	}
}

void panic_record_hard_watchdog(void)
{
       struct panic_data *const pdata = get_panic_data_write();
       uint64_t uptime = k_uptime_get();
       uint32_t rtc_sec = IS_ENABLED(CONFIG_PLATFORM_EC_RTC) ?
                          system_get_rtc_sec() : 0;

       memset(pdata, 0, sizeof(*pdata));
       pdata->magic = PANIC_DATA_MAGIC;
       pdata->struct_size = sizeof(*pdata);
       pdata->struct_version = 2;
       pdata->arch = PANIC_ARCH_WATCHDOG;
       pdata->flags = IS_ENABLED(SECTION_IS_RW) ? PANIC_DATA_FLAG_RW_IMAGE :
                                                  PANIC_DATA_FLAG_RO_IMAGE;

       pdata->wd.struct_version = 1;
       pdata->wd.thread_id = (uint32_t)task_get_current();
       pdata->wd.pc = 0;
       pdata->wd.lr = 0;

       pdata->wd.flags = 0;
       pdata->wd.warning_rtc = rtc_sec;
       pdata->wd.warning_uptime = (uint32_t)uptime;

       /* Populate all reason fields to satisfy older RO on any architecture */
       pdata->wd.reason_x86 = PANIC_SW_WATCHDOG;
       pdata->wd.reason_cm = PANIC_SW_WATCHDOG;
       pdata->wd.reason_riscv = PANIC_SW_WATCHDOG;

       sys_cache_data_flush_range(pdata, sizeof(*pdata));
}

void panic_record_preliminary_watchdog(uint32_t pc, uint32_t lr)
{
       struct panic_data *const pdata = get_panic_data_write();
       uint64_t uptime = k_uptime_get();
       uint32_t rtc_sec = IS_ENABLED(CONFIG_PLATFORM_EC_RTC) ?
                          system_get_rtc_sec() : 0;

       memset(pdata, 0, sizeof(*pdata));
       pdata->magic = PANIC_DATA_MAGIC;
       pdata->struct_size = sizeof(*pdata);
       pdata->struct_version = 2;
       pdata->arch = PANIC_ARCH_WATCHDOG;
       pdata->flags = IS_ENABLED(SECTION_IS_RW) ? PANIC_DATA_FLAG_RW_IMAGE :
                                                  PANIC_DATA_FLAG_RO_IMAGE;

       pdata->wd.struct_version = 1;
       pdata->wd.thread_id = (uint32_t)task_get_current();
       pdata->wd.pc = pc;
       pdata->wd.lr = lr;

       pdata->wd.flags = WATCHDOG_PANIC_FLAG_PRELIMINARY;
       pdata->wd.warning_rtc = rtc_sec;
       pdata->wd.warning_uptime = (uint32_t)uptime;

       /* Populate all reason fields to satisfy older RO on any architecture */
       pdata->wd.reason_x86 = PANIC_SW_WATCHDOG;
       pdata->wd.reason_cm = PANIC_SW_WATCHDOG;
       pdata->wd.reason_riscv = PANIC_SW_WATCHDOG;

       sys_cache_data_flush_range(pdata, sizeof(*pdata));
}

void panic_finalize_preliminary_watchdog(void)
{
       struct panic_data *const pdata = panic_get_data();

       assert(pdata);
       assert(pdata->arch == PANIC_ARCH_WATCHDOG);
       assert(pdata->wd.flags & WATCHDOG_PANIC_FLAG_PRELIMINARY);
       assert(pdata->wd.struct_version == 1);

       pdata->wd.flags &= ~WATCHDOG_PANIC_FLAG_PRELIMINARY;
       pdata->wd.watchdog_reset_uptime_ms = (uint32_t)k_uptime_get();

       sys_cache_data_flush_range(pdata, sizeof(*pdata));
}

uint32_t panic_get_legacy_reason(void)
{
       const struct panic_data *const pdata = panic_get_data();

       if (!pdata || pdata->arch != PANIC_ARCH) {
               return 0;
       }
       return PANIC_REG_REASON(pdata);
}

void panic_update_legacy_reason(uint32_t reason)
{
       struct panic_data *const pdata = panic_get_data();

       if (!pdata || pdata->arch != PANIC_ARCH)
               return;

       PANIC_REG_REASON(pdata) = reason;

       sys_cache_data_flush_range(pdata, sizeof(*pdata));
}
