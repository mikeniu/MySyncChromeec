/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_REG_AUDIT_H
#define __CROS_EC_REG_AUDIT_H

#include <stddef.h>
#include <stdint.h>

#include <zephyr/sys/util.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Register field descriptor flags.
 */
/* Reading this register has side effects (FIFO pop, clear-on-read, etc.) */
#define REG_FLAG_SIDE_EFFECT BIT(0)
/* Register is write-only; reading is invalid or undefined */
#define REG_FLAG_WRITE_ONLY BIT(1)
/* Register value changes dynamically (counters, pin inputs, ADC data) */
#define REG_FLAG_VOLATILE BIT(2)

/* Sentinel value recorded when a register read is skipped or faults */
#define REG_AUDIT_VAL_SKIPPED 0xFFFFFFFFU
#define REG_AUDIT_VAL_FAULT 0xDEADFA11U

/**
 * Describes a single register or contiguous array of registers within a
 * peripheral block type.
 */
struct reg_field_desc {
	const char *name;
	uint16_t offset;
	uint8_t size; /* Access width in bytes: 1, 2, or 4 */
	uint8_t flags; /* REG_FLAG_* bitmask */
	uint16_t count; /* 1 for scalar, >1 for register array */
	uint8_t stride; /* Byte stride between elements if count > 1 */
};

/**
 * Helper macros for building struct reg_field_desc tables from SoC structs.
 */
#define REG_FIELD(_struct, _member, _flags)                       \
	{                                                         \
		.name = #_member,                                 \
		.offset = (uint16_t)offsetof(_struct, _member),   \
		.size = (uint8_t)sizeof(((_struct *)0)->_member), \
		.flags = (_flags),                                \
		.count = 1,                                       \
		.stride = 0,                                      \
	}

#define REG_FIELD_ARR(_struct, _member, _flags)                         \
	{                                                               \
		.name = #_member,                                       \
		.offset = (uint16_t)offsetof(_struct, _member),         \
		.size = (uint8_t)sizeof(((_struct *)0)->_member[0]),    \
		.flags = (_flags),                                      \
		.count = (uint16_t)ARRAY_SIZE(((_struct *)0)->_member), \
		.stride = (uint8_t)sizeof(((_struct *)0)->_member[0]),  \
	}

#define REG_RAW(_name, _offset, _size, _flags) \
	{                                      \
		.name = (_name),               \
		.offset = (_offset),           \
		.size = (_size),               \
		.flags = (_flags),             \
		.count = 1,                    \
		.stride = 0,                   \
	}

#define REG_RAW_ARR(_name, _offset, _size, _count, _stride, _flags) \
	{                                                           \
		.name = (_name),                                    \
		.offset = (_offset),                                \
		.size = (_size),                                    \
		.flags = (_flags),                                  \
		.count = (_count),                                  \
		.stride = (_stride),                                \
	}

/**
 * Describes a peripheral block layout shared across one or more instances.
 */
struct reg_block_type {
	const char *type_name;
	const struct reg_field_desc *fields;
	uint16_t num_fields;
};

#define REG_BLOCK_TYPE(_name, _fields)             \
	{                                          \
		.type_name = (_name),              \
		.fields = (_fields),               \
		.num_fields = ARRAY_SIZE(_fields), \
	}

/* No clock gating associated with this module instance */
#define REG_CLK_NONE 0xFFFFU

/**
 * Describes an instantiated peripheral block on the SoC.
 */
struct reg_module_inst {
	const char *name;
	uintptr_t base;
	const struct reg_block_type *type;
	/* Chip-specific clock gate identifier, or REG_CLK_NONE */
	uint16_t clk_id;
};

/**
 * Chip-specific hooks implemented by each SoC family crawler.
 */
const struct reg_module_inst *reg_audit_chip_get_modules(size_t *num_modules);

/**
 * Prepare to read a module's registers.
 * If the module's clock is currently gated, ungate it temporarily so reads
 * succeed, and return 0 in *was_clocked (1 if already clocked).
 * Returns 0 on success, negative errno if the module cannot be read.
 */
int reg_audit_chip_module_enter(const struct reg_module_inst *mod,
				int *was_clocked, uint32_t *saved_state);

/**
 * Restore any clock gating state modified by reg_audit_chip_module_enter().
 */
void reg_audit_chip_module_exit(const struct reg_module_inst *mod,
				uint32_t saved_state);

/**
 * Optional chip hook to dump architecture/core CSRs (e.g. RISC-V CSRs).
 */
void reg_audit_chip_dump_core_regs(const char *stage_tag);

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_REG_AUDIT_H */
