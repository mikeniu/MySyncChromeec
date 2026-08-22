/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "multistep_test.h"
#include "panic.h"

#include <stdlib.h>

#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

LOG_MODULE_REGISTER(panic_hw_test, LOG_LEVEL_INF);

struct reg_vals {
	int index;
	uint32_t val;
	bool is_gpr;
};

static const struct reg_vals expected_regs[] = {
#if defined(CONFIG_ARM)
	{ .index = CORTEX_PANIC_REGISTER_R4, .val = 0xecec0004, .is_gpr = true },
	{ .index = CORTEX_PANIC_REGISTER_R5, .val = 0xecec0005, .is_gpr = true },
	{ .index = CORTEX_PANIC_REGISTER_R6, .val = 0xecec0006, .is_gpr = true },
	{ .index = CORTEX_PANIC_REGISTER_R7, .val = 0xecec0007, .is_gpr = true },
	{ .index = CORTEX_PANIC_REGISTER_R8, .val = 0xecec0008, .is_gpr = true },
	{ .index = CORTEX_PANIC_REGISTER_R9, .val = 0xecec0009, .is_gpr = true },
	{ .index = CORTEX_PANIC_REGISTER_R10,
	  .val = 0xecec000a,
	  .is_gpr = true },
	{ .index = CORTEX_PANIC_REGISTER_R11,
	  .val = 0xecec000b,
	  .is_gpr = true },
#elif defined(CONFIG_RISCV)
	{ .index = 26, .val = 0xecec0001, .is_gpr = true }, /* a0 */
	{ .index = 25, .val = 0xecec0002, .is_gpr = true }, /* a1 */
	{ .index = 24, .val = 0xecec0003, .is_gpr = true }, /* a2 */
	{ .index = 23, .val = 0xecec0004, .is_gpr = true }, /* a3 */
	{ .index = 22, .val = 0xecec0005, .is_gpr = true }, /* a4 */
	{ .index = 21, .val = 0xecec0006, .is_gpr = true }, /* a5 */
	{ .index = 20, .val = 0xecec0007, .is_gpr = true }, /* a6 */
	{ .index = 19, .val = 0xecec0008, .is_gpr = true }, /* a7 */
	{ .index = 18, .val = 0xecec0009, .is_gpr = true }, /* t0 */
	{ .index = 17, .val = 0xecec000a, .is_gpr = true }, /* t1 */
	{ .index = 16, .val = 0xecec000b, .is_gpr = true }, /* t2 */
	{ .index = 15, .val = 0xecec000c, .is_gpr = true }, /* t3 */
	{ .index = 14, .val = 0xecec000d, .is_gpr = true }, /* t4 */
	{ .index = 13, .val = 0xecec000e, .is_gpr = true }, /* t5 */
	{ .index = 12, .val = 0xecec000f, .is_gpr = true }, /* t6 */
	{ .index = 29, .val = 0xecec0010, .is_gpr = false }, /* ra */
#else
#error "Unsupported architecture."
#endif
};

static void test_panic(void)
{
	LOG_INF("Step 1: Panic");

	if (IS_ENABLED(CONFIG_CPU_CORTEX_M0)) {
		__asm__ volatile("ldr r1, =0xecec0001\n"
				 "ldr r2, =0xecec0002\n"
				 "ldr r3, =0xecec0003\n"
				 "ldr r4, =0xecec0004\n"
				 "ldr r5, =0xecec0005\n"
				 "ldr r6, =0xecec0006\n"
				 "ldr r7, =0xecec0007\n"
				 "ldr r0, =0xecec0008\n"
				 "mov r8, r0\n"
				 "ldr r0, =0xecec0009\n"
				 "mov r9, r0\n"
				 "ldr r0, =0xecec000a\n"
				 "mov r10, r0\n"
				 "ldr r0, =0xecec000b\n"
				 "mov r11, r0\n"
				 "ldr r0, =0xecec000e\n"
				 "mov r14, r0\n"
				 /* Undefined instruction. */
				 "udf #0\n");
	} else if (IS_ENABLED(CONFIG_CPU_CORTEX_M)) {
		__asm__ volatile("ldr r0, =0xecec0000\n"
				 "ldr r1, =0xecec0001\n"
				 "ldr r2, =0xecec0002\n"
				 "ldr r3, =0xecec0003\n"
				 "ldr r4, =0xecec0004\n"
				 "ldr r5, =0xecec0005\n"
				 "ldr r6, =0xecec0006\n"
				 "ldr r7, =0xecec0007\n"
				 "ldr r8, =0xecec0008\n"
				 "ldr r9, =0xecec0009\n"
				 "ldr r10, =0xecec000a\n"
				 "ldr r11, =0xecec000b\n"
				 "ldr r14, =0xecec000e\n"
				 /* Undefined instruction. */
				 "udf #0\n");
	} else if (IS_ENABLED(CONFIG_RISCV)) {
		__asm__ volatile("li a0, 0xecec0001\n"
				 "li a1, 0xecec0002\n"
				 "li a2, 0xecec0003\n"
				 "li a3, 0xecec0004\n"
				 "li a4, 0xecec0005\n"
				 "li a5, 0xecec0006\n"
				 "li a6, 0xecec0007\n"
				 "li a7, 0xecec0008\n"
				 "li t0, 0xecec0009\n"
				 "li t1, 0xecec000a\n"
				 "li t2, 0xecec000b\n"
				 "li t3, 0xecec000c\n"
				 "li t4, 0xecec000d\n"
				 "li t5, 0xecec000e\n"
				 "li t6, 0xecec000f\n"
				 "li ra, 0xecec0010\n"
				 /* Illegal instruction */
				 "unimp\n");
	}
	zassert_unreachable();
}

static void test_panic_data(void)
{
	struct panic_data *const pdata = panic_get_data();
	int i;

	LOG_INF("Step 2: Read panic data");
	for (i = 0; i < ARRAY_SIZE(expected_regs); i++) {
		uint32_t expected = expected_regs[i].val;

		if (IS_ENABLED(CONFIG_PLATFORM_EC_PANIC_STRIP_GPR) &&
		    expected_regs[i].is_gpr) {
			expected = 0;
		}

		if (IS_ENABLED(CONFIG_ARM)) {
			zassert_equal(expected,
				      pdata->cm.regs[expected_regs[i].index]);
		} else if (IS_ENABLED(CONFIG_RISCV)) {
			zassert_equal(
				expected,
				pdata->riscv.regs[expected_regs[i].index]);
		}
	}
}

static void (*test_steps[])(void) = { test_panic, test_panic_data };

MULTISTEP_TEST(panic, test_steps)
