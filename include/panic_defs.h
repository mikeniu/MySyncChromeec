/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Panic handling macros and structures.
 */

#ifndef PLATFORM_EC_INCLUDE_PANIC_DEFS_H_
#define PLATFORM_EC_INCLUDE_PANIC_DEFS_H_

#include <stddef.h>
#include <stdint.h>

#ifdef CHROMIUM_EC
#include "compile_time_macros.h"
#else
/* If BUILD_ASSERT isn't already defined, make it a no-op */
#ifndef BUILD_ASSERT
#define BUILD_ASSERT(_cond)
#endif /* !BUILD_ASSERT */
#endif /* CHROMIUM_EC */

#ifdef __cplusplus
extern "C" {
#endif

enum cortex_panic_frame_registers {
	CORTEX_PANIC_FRAME_REGISTER_R0 = 0,
	CORTEX_PANIC_FRAME_REGISTER_R1,
	CORTEX_PANIC_FRAME_REGISTER_R2,
	CORTEX_PANIC_FRAME_REGISTER_R3,
	CORTEX_PANIC_FRAME_REGISTER_R12,
	CORTEX_PANIC_FRAME_REGISTER_LR,
	CORTEX_PANIC_FRAME_REGISTER_PC,
	CORTEX_PANIC_FRAME_REGISTER_PSR,
	NUM_CORTEX_PANIC_FRAME_REGISTERS
};

enum cortex_panic_registers_v1 {
	CORTEX_PANIC_REGISTER_PSP_V1 = 0,
	CORTEX_PANIC_REGISTER_IPSR_V1,
	CORTEX_PANIC_REGISTER_LR_V1,
	CORTEX_PANIC_REGISTER_R4_V1,
	CORTEX_PANIC_REGISTER_R5_V1,
	CORTEX_PANIC_REGISTER_R6_V1,
	CORTEX_PANIC_REGISTER_R7_V1,
	CORTEX_PANIC_REGISTER_R8_V1,
	CORTEX_PANIC_REGISTER_R9_V1,
	CORTEX_PANIC_REGISTER_R10_V1,
	CORTEX_PANIC_REGISTER_R11_V1,
	NUM_CORTEX_PANIC_REGISTERS_V1
};

enum cortex_panic_registers {
	CORTEX_PANIC_REGISTER_PSP = 0,
	CORTEX_PANIC_REGISTER_IPSR,
	CORTEX_PANIC_REGISTER_MSP,
	CORTEX_PANIC_REGISTER_R4,
	CORTEX_PANIC_REGISTER_R5,
	CORTEX_PANIC_REGISTER_R6,
	CORTEX_PANIC_REGISTER_R7,
	CORTEX_PANIC_REGISTER_R8,
	CORTEX_PANIC_REGISTER_R9,
	CORTEX_PANIC_REGISTER_R10,
	CORTEX_PANIC_REGISTER_R11,
	CORTEX_PANIC_REGISTER_LR,
	NUM_CORTEX_PANIC_REGISTERS
};

/* Version 1 ARM Cortex-Mx registers saved on panic */
struct cortex_panic_data_v1 {
	/* See cortex_panic_registers_v1 enum for information about registers */
	uint32_t regs[NUM_CORTEX_PANIC_REGISTERS_V1];

	/* See cortex_panic_frame_registers enum for more information */
	uint32_t frame[NUM_CORTEX_PANIC_FRAME_REGISTERS];

	uint32_t cfsr;
	uint32_t bfar;
	uint32_t mfar;
	uint32_t shcsr;
	uint32_t hfsr;
	uint32_t dfsr;
};

/* ARM Cortex-Mx registers saved on panic */
struct cortex_panic_data {
	/* See cortex_panic_registers enum for information about registers */
	uint32_t regs[NUM_CORTEX_PANIC_REGISTERS];

	/* See cortex_panic_frame_registers enum for more information */
	uint32_t frame[NUM_CORTEX_PANIC_FRAME_REGISTERS];

	uint32_t cfsr;
	uint32_t bfar;
	uint32_t mfar;
	uint32_t shcsr;
	uint32_t hfsr;
	uint32_t dfsr;
};

/* NDS32 N8 registers saved on panic */
struct nds32_n8_panic_data {
	uint32_t itype;
	uint32_t regs[16]; /* r0-r10, r15, fp, gp, lp, sp */
	uint32_t ipc;
	uint32_t ipsw;
};

/* RISC-V RV32I registers saved on panic */
struct rv32i_panic_data {
	uint32_t regs[31]; /* sp, ra, gp, tp, a0-a7, t0-t6, s0-s11 */
	uint32_t mepc; /* mepc */
	uint32_t mcause; /* mcause */
};

/* x86 registers saved on panic */
struct x86_panic_data {
	uint32_t vector; /* Exception vector number */

	/* Data pushed when exception handler called */
	uint32_t error_code;
	uint32_t eip;
	uint32_t cs;
	uint32_t eflags;

	/* General purpose registers */
	uint32_t eax;
	uint32_t ebx;
	uint32_t ecx;
	uint32_t edx;
	uint32_t esi;
	uint32_t edi;

	/* Task id at time of panic */
	uint8_t task_id;
};

/* POSIX architecture panic data */
struct posix_panic_data {
	uint32_t esf_placeholder;
	uint32_t reason;
	uint32_t info;
	uint8_t exception;
	uint8_t reserved[3];
};

/* Flags for watchdog_panic_data.struct_flags */
/* Written by the watchdog warning; cleared by RW after a watchdog reset. */
#define WATCHDOG_PANIC_FLAG_WARNING BIT(0)

#define WATCHDOG_PANIC_DATA_MAJOR_VERSION 1
#define WATCHDOG_PANIC_DATA_MINOR_VERSION 0

/* watchdog_panic_data.battery_level when no battery level is available */
#define WATCHDOG_PANIC_BATTERY_UNKNOWN 0xFF

/*
 * AP power state stored in watchdog_panic_data.power_state. Values match
 * enum power_state with CONFIG_POWER_S0IX; other sequencers are mapped onto
 * them. Values are part of the panic data format and must not change.
 */
enum panic_power_state {
	PANIC_POWER_G3 = 0,
	PANIC_POWER_S5 = 1,
	PANIC_POWER_S4 = 2,
	PANIC_POWER_S3 = 3,
	PANIC_POWER_S0 = 4,
	PANIC_POWER_S0IX = 5,
	PANIC_POWER_G3S5 = 6,
	PANIC_POWER_S5S3 = 7,
	PANIC_POWER_S3S0 = 8,
	PANIC_POWER_S0S3 = 9,
	PANIC_POWER_S3S5 = 10,
	PANIC_POWER_S5G3 = 11,
	PANIC_POWER_S3S4 = 12,
	PANIC_POWER_S4S3 = 13,
	PANIC_POWER_S4S5 = 14,
	PANIC_POWER_S5S4 = 15,
	PANIC_POWER_S0IXS0 = 16,
	PANIC_POWER_S0S0IX = 17,
	PANIC_POWER_UNKNOWN = 0xFF,
};

/*
 * Architecture-agnostic watchdog warning data (PANIC_ARCH_WATCHDOG).
 *
 * Images that predate PANIC_ARCH_WATCHDOG ignore arch and read the software
 * panic reason from a fixed per-arch offset. reason_x86, reason_cm and
 * reason_riscv sit at those offsets and all hold the same reason, so any
 * image recognizes a watchdog panic and leaves the data intact.
 *
 * Layout rules (see docs/panic_data.md):
 * - Minor version: append fields or assign reserved bytes only. Reserved
 *   bytes are zero when written; readers ignore them unless
 *   struct_minor_version defines them.
 * - Major version: required for any removal/reorder. The reason_* fields and
 *   the version bytes must stay at their current offsets.
 * - Total size must not exceed sizeof(struct cortex_panic_data) (104 bytes) so
 *   sizeof(struct panic_data) is unchanged on every board.
 */
struct watchdog_panic_data {
	uint32_t reason_x86; /* Panic reason at the x86/NDS32 reason offset */
	uint8_t struct_major_version; /* WATCHDOG_PANIC_DATA_MAJOR_VERSION */
	uint8_t struct_minor_version; /* WATCHDOG_PANIC_DATA_MINOR_VERSION */
	uint16_t struct_flags; /* WATCHDOG_PANIC_FLAG_* */
	uint32_t thread_id; /* Thread running when the warning fired */
	uint32_t reason_cm; /* Panic reason at the Cortex-M reason offset */
	uint32_t pc; /* Thread PC when the warning fired (MEPC on RISC-V) */
	uint32_t lr; /* Thread LR when the warning fired (RA on RISC-V) */
	uint32_t sp; /* Thread SP at exception entry */
	char image_version[16]; /* Last 16 chars of the version, no NUL */
	uint32_t reason_riscv; /* Panic reason at the RISC-V reason offset */
	uint32_t uptime; /* ms since boot; wraps at ~49.7 days */
	uint32_t reset_flags; /* system_get_reset_flags() */
	uint32_t power_signals; /* AP power signal mask */
	uint8_t power_state; /* enum panic_power_state */
	uint8_t battery_level; /* %, or WATCHDOG_PANIC_BATTERY_UNKNOWN */
	/*
	 * Interrupt handlers active when the warning fired, excluding the
	 * warning itself; 0 = thread context. If non-zero, pc/lr/sp describe
	 * the thread as of the outermost interrupt.
	 */
	uint8_t isr_depth;
	uint8_t reserved[1]; /* Zero */
};

/* Data saved across reboots */
struct panic_data {
	uint8_t arch; /* Architecture (PANIC_ARCH_*) */
	uint8_t struct_version; /* Structure version (currently 2) */
	uint8_t flags; /* Flags (PANIC_DATA_FLAG_*) */
	uint8_t reserved; /* Reserved; set 0 */

	/* core specific panic data */
	union {
		struct cortex_panic_data_v1 cm_v1; /* V1 Cortex-Mx registers */
		struct cortex_panic_data cm; /* V2+ Cortex-Mx registers */
		struct nds32_n8_panic_data nds_n8; /* NDS32 N8 registers */
		struct x86_panic_data x86; /* Intel x86 */
#ifndef CONFIG_DO_NOT_INCLUDE_RV32I_PANIC_DATA
		struct rv32i_panic_data riscv; /* RISC-V RV32I */
#endif
		struct posix_panic_data posix; /* POSIX (native_sim) */
		struct watchdog_panic_data wd; /* Watchdog warning */
	};

	/*
	 * These fields go at the END of the struct so we can find it at the
	 * end of memory.
	 */
	uint32_t struct_size; /* Size of this struct */
	uint32_t magic; /* PANIC_SAVE_MAGIC if valid */
};

/*
 * Keep the reason_* fields at the legacy reason offsets. The size is frozen
 * for v1.0; the <= check is the ceiling for future minor versions.
 */
BUILD_ASSERT(sizeof(struct watchdog_panic_data) == 64);
BUILD_ASSERT(sizeof(struct watchdog_panic_data) <=
	     sizeof(struct cortex_panic_data));
BUILD_ASSERT(offsetof(struct panic_data, wd.reason_x86) ==
	     offsetof(struct panic_data, x86.vector));
BUILD_ASSERT(offsetof(struct panic_data, wd.reason_x86) ==
	     offsetof(struct panic_data, nds_n8.itype));
BUILD_ASSERT(offsetof(struct panic_data, wd.reason_cm) ==
	     offsetof(struct panic_data, cm.regs[CORTEX_PANIC_REGISTER_R4]));
#ifndef CONFIG_DO_NOT_INCLUDE_RV32I_PANIC_DATA
BUILD_ASSERT(offsetof(struct panic_data, wd.reason_riscv) ==
	     offsetof(struct panic_data, riscv.regs[11]));
#endif

#define PANIC_DATA_MAGIC 0x21636e50 /* "Pnc!" */
#define PANIC_DATA_VERSION 2
enum panic_arch {
	PANIC_ARCH_UNSUPPORTED = 0,
	PANIC_ARCH_CORTEX_M = 1, /* Cortex-M architecture */
	PANIC_ARCH_NDS32_N8 = 2, /* NDS32 N8 architecture */
	PANIC_ARCH_X86 = 3, /* Intel x86 */
#ifndef CONFIG_DO_NOT_INCLUDE_RV32I_PANIC_DATA
	PANIC_ARCH_RISCV_RV32I = 4, /* RISC-V RV32I */
#endif
	PANIC_ARCH_POSIX = 5, /* POSIX (native_sim) */
	PANIC_ARCH_WATCHDOG = 6, /* Arch-independent watchdog_panic_data */
};

#define PANIC_ZEPHYR_FATAL_ERROR 0xDEAD6800

/* Flags for panic_data.flags */
/* panic_data.frame is valid */
#define PANIC_DATA_FLAG_FRAME_VALID BIT(0)
/* Already printed at console */
#define PANIC_DATA_FLAG_OLD_CONSOLE BIT(1)
/* Already returned via host command */
#define PANIC_DATA_FLAG_OLD_HOSTCMD BIT(2)
/* Already reported via host event */
#define PANIC_DATA_FLAG_OLD_HOSTEVENT BIT(3)
/* The data was truncated to fit panic info host cmd */
/* Deprecated with EC_CMD_GET_PANIC_INFO >=v2 */
#define PANIC_DATA_FLAG_TRUNCATED BIT(4)
/* Panic occurred in RO image */
#define PANIC_DATA_FLAG_RO_IMAGE BIT(5)
/* Panic occurred in RW image */
#define PANIC_DATA_FLAG_RW_IMAGE BIT(6)

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_EC_INCLUDE_PANIC_DEFS_H_ */
