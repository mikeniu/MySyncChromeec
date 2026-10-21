#include <zephyr/ztest.h>

struct itim64_reg {
	volatile uint32_t ITCNT64L;
};

/* Mock struct with ITCNT64H as a macro calling a function */
struct itim64_reg mock_sys_tmr;
struct itim64_reg *const sys_tmr = &mock_sys_tmr;

int mock_reads = 0;
uint32_t mock_read_cnt64h(void)
{
	mock_reads++;
	return mock_reads; /* Never matches itself on two consecutive reads */
}

/* Redefine sys_tmr->ITCNT64H to call our function */
#define ITCNT64H_MACRO mock_read_cnt64h()

/* The code under test (Original Version without retry limit) */
static inline uint64_t npcx_itim_get_sys_cyc64_original(void)
{
	uint32_t cnt64h, cnt64h_check, cnt64l;

	/* Read 64-bit counter value from two 32-bit registers */
	do {
		cnt64h_check = mock_read_cnt64h();
		cnt64l = sys_tmr->ITCNT64L;
		cnt64h = mock_read_cnt64h();
	} while (cnt64h != cnt64h_check);

	cnt64h = 0xFFFFFFFF - cnt64h;
	cnt64l = 0xFFFFFFFF - cnt64l + 1;

	return (((uint64_t)cnt64h) << 32) | cnt64l;
}

static inline uint64_t npcx_itim_get_sys_cyc64_fixed(void)
{
	uint32_t cnt64h, cnt64h_check, cnt64l;
	int retries = 0;

	/* Read 64-bit counter value from two 32-bit registers */
	do {
		cnt64h_check = mock_read_cnt64h();
		cnt64l = sys_tmr->ITCNT64L;
		cnt64h = mock_read_cnt64h();
		retries++;
	} while ((cnt64h != cnt64h_check) && (retries < 3));

	cnt64h = 0xFFFFFFFF - cnt64h;
	cnt64l = 0xFFFFFFFF - cnt64l + 1;

	return (((uint64_t)cnt64h) << 32) | cnt64l;
}

ZTEST(npcx_itim, test_spin_indefinitely)
{
	mock_reads = 0;
	/* Verify the fixed version escapes the spin! */
	uint64_t val = npcx_itim_get_sys_cyc64_fixed();

	/* It should break out after 3 retries.
	 * Each retry does 2 reads, so 3 retries = 6 reads. */
	zassert_equal(mock_reads, 6, "Should stop after 3 retries");
}

ZTEST_SUITE(npcx_itim, NULL, NULL, NULL, NULL, NULL);
