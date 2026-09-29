/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "cros/reg_audit.h"
#include "hooks.h"

#include <zephyr/ztest.h>

struct mock_periph_regs {
	uint8_t ctrl8;
	uint8_t status8;
	uint16_t cfg16;
	uint32_t data32;
	uint32_t fifo_se;
	uint32_t cmd_wo;
	uint32_t arr32[16];
};

static struct mock_periph_regs mock_mod0_regs = {
	.ctrl8 = 0x12,
	.status8 = 0x34,
	.cfg16 = 0x5678,
	.data32 = 0x9abcdef0,
	.fifo_se = 0x11111111,
	.cmd_wo = 0x22222222,
	.arr32 = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 },
};

static struct mock_periph_regs mock_mod1_regs = {
	.ctrl8 = 0xa5,
	.status8 = 0x5a,
	.cfg16 = 0x1234,
	.data32 = REG_AUDIT_VAL_FAULT,
};

static int mod0_clocked = 1;
static int mod1_clocked;
static int mod1_enter_rc;
static int core_dump_calls;
static int exit_calls;

static const struct reg_field_desc mock_fields[] = {
	REG_FIELD(struct mock_periph_regs, ctrl8, 0),
	REG_FIELD(struct mock_periph_regs, status8, REG_FLAG_VOLATILE),
	REG_FIELD(struct mock_periph_regs, cfg16, 0),
	REG_FIELD(struct mock_periph_regs, data32, 0),
	REG_FIELD(struct mock_periph_regs, fifo_se, REG_FLAG_SIDE_EFFECT),
	REG_FIELD(struct mock_periph_regs, cmd_wo, REG_FLAG_WRITE_ONLY),
	/* Invalid size (3) exercises the default case in reg_audit_safe_read */
	REG_RAW("BAD_SZ", 0, 3, 0),
	REG_FIELD_ARR(struct mock_periph_regs, arr32, 0),
};
static const struct reg_block_type mock_block =
	REG_BLOCK_TYPE("MOCK_BLK", mock_fields);

static const struct reg_module_inst mock_modules[] = {
	{ "MOD0", (uintptr_t)&mock_mod0_regs, &mock_block, 0 },
	{ "MOD1", (uintptr_t)&mock_mod1_regs, &mock_block, 1 },
	{ "MOD2", (uintptr_t)&mock_mod1_regs, &mock_block, REG_CLK_NONE },
	{ "MOD3", (uintptr_t)&mock_mod1_regs, &mock_block, REG_CLK_NONE },
	{ "MOD4", (uintptr_t)&mock_mod1_regs, &mock_block, REG_CLK_NONE },
	{ "MOD5", (uintptr_t)&mock_mod1_regs, &mock_block, REG_CLK_NONE },
	{ "MOD6", (uintptr_t)&mock_mod1_regs, &mock_block, REG_CLK_NONE },
	{ "MOD7", (uintptr_t)&mock_mod1_regs, &mock_block, REG_CLK_NONE },
	{ "MOD8", (uintptr_t)&mock_mod1_regs, &mock_block, REG_CLK_NONE },
	{ "MOD9", (uintptr_t)&mock_mod1_regs, &mock_block, REG_CLK_NONE },
	{ "MOD10", (uintptr_t)&mock_mod1_regs, &mock_block, REG_CLK_NONE },
	{ "MOD11", (uintptr_t)&mock_mod1_regs, &mock_block, REG_CLK_NONE },
	{ "MOD12", (uintptr_t)&mock_mod1_regs, &mock_block, REG_CLK_NONE },
	{ "MOD13", (uintptr_t)&mock_mod1_regs, &mock_block, REG_CLK_NONE },
	{ "MOD14", (uintptr_t)&mock_mod1_regs, &mock_block, REG_CLK_NONE },
	{ "MOD15", (uintptr_t)&mock_mod1_regs, &mock_block, REG_CLK_NONE },
};

const struct reg_module_inst *reg_audit_chip_get_modules(size_t *num_modules)
{
	*num_modules = ARRAY_SIZE(mock_modules);
	return mock_modules;
}

int reg_audit_chip_module_enter(const struct reg_module_inst *mod,
				int *was_clocked, uint32_t *saved_state)
{
	if (mod->clk_id == 0) {
		*was_clocked = mod0_clocked;
		*saved_state = 0xaa;
		return 0;
	}
	if (mod->clk_id == 1) {
		*was_clocked = mod1_clocked;
		*saved_state = 0xbb;
		return mod1_enter_rc;
	}
	*was_clocked = 1;
	*saved_state = 0;
	return 0;
}

void reg_audit_chip_module_exit(const struct reg_module_inst *mod,
				uint32_t saved_state)
{
	ARG_UNUSED(mod);
	ARG_UNUSED(saved_state);
	exit_calls++;
}

void reg_audit_chip_dump_core_regs(const char *stage_tag)
{
	ARG_UNUSED(stage_tag);
	core_dump_calls++;
}

ZTEST_SUITE(reg_audit, NULL, NULL, NULL, NULL, NULL);

ZTEST(reg_audit, test_snapshots_and_commands)
{
	/*
	 * PRE_KERNEL_1 already captured snap_pre_init.
	 * Mutate mock registers and clock state before triggering HOOK_INIT
	 * so snap_post_init differs from snap_pre_init across scalars,
	 * array elements (>= 16 diffs), and clock state.
	 */
	mock_mod0_regs.ctrl8 = 0x99;
	mock_mod0_regs.cfg16 = 0xabcd;
	mock_mod0_regs.data32 = REG_AUDIT_VAL_FAULT;
	for (int i = 0; i < 16; i++) {
		mock_mod0_regs.arr32[i] = 0x100 + i;
	}
	mod0_clocked = 0;
	mod1_clocked = 1;
	mod1_enter_rc = -EIO;

	hook_notify(HOOK_INIT);

	/* Default (post) dump */
	zassert_equal(shell_execute_cmd(get_ec_shell(), "regdump"), EC_SUCCESS);

	/* Pre-init dump (full and filtered) */
	zassert_equal(shell_execute_cmd(get_ec_shell(), "regdump pre"),
		      EC_SUCCESS);
	zassert_equal(shell_execute_cmd(get_ec_shell(), "regdump pre MOD0"),
		      EC_SUCCESS);

	/* Post-init dump (explicit) */
	zassert_equal(shell_execute_cmd(get_ec_shell(), "regdump post"),
		      EC_SUCCESS);

	/* Live dump (unfiltered calls reg_audit_chip_dump_core_regs) */
	mod1_enter_rc = 0;
	int prev_core_calls = core_dump_calls;

	zassert_equal(shell_execute_cmd(get_ec_shell(), "regdump live"),
		      EC_SUCCESS);
	zassert_equal(core_dump_calls, prev_core_calls + 1);

	/* Filtered live dump */
	zassert_equal(shell_execute_cmd(get_ec_shell(), "regdump live MOD0"),
		      EC_SUCCESS);

	/* Diff (unfiltered and filtered) */
	zassert_equal(shell_execute_cmd(get_ec_shell(), "regdump diff"),
		      EC_SUCCESS);
	zassert_equal(shell_execute_cmd(get_ec_shell(), "regdump diff MOD0"),
		      EC_SUCCESS);

	/* Info summary */
	zassert_equal(shell_execute_cmd(get_ec_shell(), "regdump info"),
		      EC_SUCCESS);

	/* Invalid subcommand */
	zassert_equal(shell_execute_cmd(get_ec_shell(), "regdump badarg"),
		      EC_ERROR_PARAM1);
}
