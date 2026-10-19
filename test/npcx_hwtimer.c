/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "../chip/npcx/hwtimer_chip.h"
#include "common.h"
#include "test_util.h"

static uint16_t mock_val16;
static uint32_t mock_val32;
static int mock_reads16;
static int mock_reads32;
static int mock_glitch_count16;
static int mock_glitch_count32;

static uint16_t mock_read16(void)
{
	mock_reads16++;
	if (mock_reads16 <= mock_glitch_count16)
		mock_val16++;
	return mock_val16;
}

static uint32_t mock_read32(void)
{
	mock_reads32++;
	if (mock_reads32 <= mock_glitch_count32)
		mock_val32++;
	return mock_val32;
}

test_static int test_npcx_itim_read_stable16(void)
{
	/* Stable read on first attempt completes in 2 reads */
	mock_val16 = 0x1234;
	mock_reads16 = 0;
	mock_glitch_count16 = 0;
	TEST_EQ(npcx_itim_read_stable16(mock_read16), 0x1234, "0x%x");
	TEST_EQ(mock_reads16, 2, "%d");

	/* Transient 2-read glitch recovers once two consecutive reads match */
	mock_val16 = 0x1000;
	mock_reads16 = 0;
	mock_glitch_count16 = 3;
	TEST_EQ(npcx_itim_read_stable16(mock_read16), 0x1003, "0x%x");
	TEST_EQ(mock_reads16, 4, "%d");

	/* Persistent metastability stops at exact NPCX_HWTIMER_MAX_RETRIES
	 * bound */
	mock_val16 = 0x2000;
	mock_reads16 = 0;
	mock_glitch_count16 = 100;
	TEST_EQ(npcx_itim_read_stable16(mock_read16),
		0x2000 + 1 + NPCX_HWTIMER_MAX_RETRIES, "0x%x");
	TEST_EQ(mock_reads16, 1 + NPCX_HWTIMER_MAX_RETRIES, "%d");

	return EC_SUCCESS;
}

test_static int test_npcx_itim_read_stable32(void)
{
	/* Stable read on first attempt completes in 2 reads */
	mock_val32 = 0x89abcdef;
	mock_reads32 = 0;
	mock_glitch_count32 = 0;
	TEST_EQ(npcx_itim_read_stable32(mock_read32), 0x89abcdefU, "0x%x");
	TEST_EQ(mock_reads32, 2, "%d");

	/* Transient glitch recovers once two consecutive reads match */
	mock_val32 = 0x3000;
	mock_reads32 = 0;
	mock_glitch_count32 = 2;
	TEST_EQ(npcx_itim_read_stable32(mock_read32), 0x3002U, "0x%x");
	TEST_EQ(mock_reads32, 3, "%d");

	/* Persistent metastability stops at exact NPCX_HWTIMER_MAX_RETRIES
	 * bound */
	mock_val32 = 0x4000;
	mock_reads32 = 0;
	mock_glitch_count32 = 100;
	TEST_EQ(npcx_itim_read_stable32(mock_read32),
		(uint32_t)(0x4000 + 1 + NPCX_HWTIMER_MAX_RETRIES), "0x%x");
	TEST_EQ(mock_reads32, 1 + NPCX_HWTIMER_MAX_RETRIES, "%d");

	return EC_SUCCESS;
}

void run_test(int argc, const char **argv)
{
	test_reset();
	RUN_TEST(test_npcx_itim_read_stable16);
	RUN_TEST(test_npcx_itim_read_stable32);
	test_print_result();
}
