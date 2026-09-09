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

/* If BUILD_ASSERT isn't already defined, make it a no-op */
#ifndef BUILD_ASSERT
#define BUILD_ASSERT(_cond)
#endif /* !BUILD_ASSERT */

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

/* Flags for watchdog_panic_data.flags */
/*
 * This flag indicates that watchdog_panic_data was populated during a watchdog
 * warning and has not yet been finalized following a watchdog reset.
 */
#define WATCHDOG_PANIC_FLAG_WARNING BIT(0)

#define WATCHDOG_PANIC_DATA_MAJOR_VERSION 1
#define WATCHDOG_PANIC_DATA_MINOR_VERSION 0

/*
 * Architecture-agnostic watchdog panic data (PANIC_ARCH_WATCHDOG).
 *
 * Backward Compatibility with Deployed RO Images:
 * Legacy RO/RW firmware inspects panic_data without checking for
 * PANIC_ARCH_WATCHDOG, reading the panic reason from its native architecture's
 * register offset:
 *   - x86 / NDS32: offset 0  (x86.vector / nds_n8.itype) -> reason_x86
 *   - Cortex-M:    offset 12 (cm.regs[3] / R4)           -> reason_cm
 *   - RISC-V:      offset 44 (riscv.regs[11] / S0)       -> reason_riscv
 * All three reason_* fields MUST remain at these exact byte offsets and be
 * populated with PANIC_SW_WATCHDOG so older RO images recognize the watchdog
 * panic and do not overwrite panic_data.
 *
 * Versioning & Append-Only Evolution:
 * - struct_major_version (offset 4) and struct_minor_version (offset 5) use
 *   semantic versioning:
 *   1. Append-Only Additions (increment struct_minor_version, keep
 *      struct_major_version):
 *      All existing fields in v1.0 (offsets 0..59, 60 bytes total) are frozen
 *      and enforced via struct watchdog_panic_data_v1_0 and per-field
 *      offsetof() BUILD_ASSERTs below.
 *      To add new fields in v1.(N+1):
 *        a) Append the new fields to the anonymous struct inside
 *           struct watchdog_panic_data (after the v1.N fields).
 *        b) Define struct watchdog_panic_data_v1_<N+1> (embedding v1_<N> as its
 *           first member) and add it to the union in struct watchdog_panic_data
 *           along with a sizeof() BUILD_ASSERT.
 *        c) Increment WATCHDOG_PANIC_DATA_MINOR_VERSION.
 *      Because updates are strictly append-only, any v1.x reader (checking
 *      struct_major_version == 1) can always safely decode all fields up to
 *      MIN(reader_minor, pdata->wd.struct_minor_version).
 *   2. Breaking Layout Changes (increment struct_major_version, reset
 *      struct_minor_version):
 *      If fields must ever be removed or reordered, increment
 *      WATCHDOG_PANIC_DATA_MAJOR_VERSION (e.g. to 2) and add a v2_0 struct to
 *      the union while keeping reason_x86 (0), struct_major_version (4),
 *      struct_minor_version (5), reason_cm (12), and reason_riscv (44) at their
 *      fixed offsets. Readers checking struct_major_version == 1 will cleanly
 *      reject struct_major_version != 1 without misinterpreting fields.
 *   3. Union Size Ceiling (104 bytes):
 *      panic_get_data() requires pdata->struct_size == sizeof(struct
 * panic_data) (CONFIG_PANIC_DATA_SIZE). The union size in struct panic_data is
 * bounded by cortex_panic_data (104 bytes when
 *      CONFIG_DO_NOT_INCLUDE_RV32I_PANIC_DATA is set) or rv32i_panic_data
 *      (132 bytes otherwise). Since v1.0 is 60 bytes, up to 44 bytes of new
 *      fields can be appended (up to 104 bytes total) without changing
 *      sizeof(struct panic_data) on any board or breaking RO/RW size
 *      compatibility.
 */
#define WATCHDOG_PANIC_DATA_V1_0_FIELDS                                       \
	/* --- Header & Architecture Alignment --- */                         \
	uint32_t reason_x86; /* offset 0. Matches x86.vector / nds_n8.itype   \
			      */                                              \
	uint8_t struct_major_version; /* offset 4:                            \
					 WATCHDOG_PANIC_DATA_MAJOR_VERSION */ \
	uint8_t struct_minor_version; /* offset 5:                            \
					 WATCHDOG_PANIC_DATA_MINOR_VERSION */ \
	uint16_t struct_flags; /* offset 6: WATCHDOG_PANIC_FLAG_* */          \
	uint32_t thread_id; /* offset 8: Interrupted k_tid_t pointer */       \
	uint32_t reason_cm; /* offset 12. Matches cm.regs[3] / R4 */          \
                                                                              \
	/* --- Execution Registers --- */                                     \
	uint32_t pc; /* offset 16 */                                          \
	uint32_t lr; /* offset 20 */                                          \
	uint32_t sp; /* offset 24 */                                          \
                                                                              \
	/* --- Firmware Build --- */                                          \
	char version[16]; /* offset 28-43: Tail 16 bytes of EC version */     \
	uint32_t reason_riscv; /* offset 44. Matches riscv.regs[11] */        \
                                                                              \
	/* --- Timestamps & System Telemetry --- */                           \
	uint32_t warning_uptime; /* offset 48 */                              \
	uint32_t reset_flags; /* offset 52 */                                 \
	uint16_t power_signals; /* offset 56 */                               \
	uint8_t power_state; /* offset 58 */                                  \
	uint8_t battery_level /* offset 59 */

/* Frozen v1.0 layout (60 bytes) */
struct watchdog_panic_data_v1_0 {
	WATCHDOG_PANIC_DATA_V1_0_FIELDS;
};

struct watchdog_panic_data {
	union {
		struct {
			WATCHDOG_PANIC_DATA_V1_0_FIELDS;
			/*
			 * Append v1.1+ fields here and add a corresponding
			 * struct watchdog_panic_data_v1_<N> to this union.
			 */
		};
		struct watchdog_panic_data_v1_0 v1_0;
	};
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
		struct watchdog_panic_data wd; /* Common Watchdog Panic struct
						*/
	};

	/*
	 * These fields go at the END of the struct so we can find it at the
	 * end of memory.
	 */
	uint32_t struct_size; /* Size of this struct */
	uint32_t magic; /* PANIC_SAVE_MAGIC if valid */
};

/*
 * Compile-time assertions to guarantee exact byte alignment with legacy unions,
 * freeze v1.0 field offsets, and ensure watchdog_panic_data does not grow
 * sizeof(struct panic_data).
 */
BUILD_ASSERT(offsetof(struct panic_data, wd) == 4);
BUILD_ASSERT(sizeof(struct watchdog_panic_data_v1_0) == 60);
BUILD_ASSERT(sizeof(struct watchdog_panic_data) >=
	     sizeof(struct watchdog_panic_data_v1_0));
BUILD_ASSERT(sizeof(struct watchdog_panic_data) <=
	     sizeof(struct cortex_panic_data));
BUILD_ASSERT(offsetof(struct watchdog_panic_data, reason_x86) == 0);
BUILD_ASSERT(offsetof(struct watchdog_panic_data, struct_major_version) == 4);
BUILD_ASSERT(offsetof(struct watchdog_panic_data, struct_minor_version) == 5);
BUILD_ASSERT(offsetof(struct watchdog_panic_data, struct_flags) == 6);
BUILD_ASSERT(offsetof(struct watchdog_panic_data, thread_id) == 8);
BUILD_ASSERT(offsetof(struct watchdog_panic_data, reason_cm) == 12);
BUILD_ASSERT(offsetof(struct watchdog_panic_data, pc) == 16);
BUILD_ASSERT(offsetof(struct watchdog_panic_data, lr) == 20);
BUILD_ASSERT(offsetof(struct watchdog_panic_data, sp) == 24);
BUILD_ASSERT(offsetof(struct watchdog_panic_data, version) == 28);
BUILD_ASSERT(offsetof(struct watchdog_panic_data, reason_riscv) == 44);
BUILD_ASSERT(offsetof(struct watchdog_panic_data, warning_uptime) == 48);
BUILD_ASSERT(offsetof(struct watchdog_panic_data, reset_flags) == 52);
BUILD_ASSERT(offsetof(struct watchdog_panic_data, power_signals) == 56);
BUILD_ASSERT(offsetof(struct watchdog_panic_data, power_state) == 58);
BUILD_ASSERT(offsetof(struct watchdog_panic_data, battery_level) == 59);
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
	PANIC_ARCH_WATCHDOG = 6, /* Common Watchdog Panic struct */
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
