/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "task.h"
#include "usb_sm.h"

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#define SEQUENCE_SIZE 55
#define PORT0 0

enum state_id {
	ENTER_A1 = 1,
	RUN_A1,
	EXIT_A1,
	ENTER_A2,
	RUN_A2,
	EXIT_A2,
	ENTER_A3,
	RUN_A3,
	EXIT_A3,
	ENTER_A4,
	RUN_A4,
	EXIT_A4,
	ENTER_A5,
	RUN_A5,
	EXIT_A5,
	ENTER_A6,
	RUN_A6,
	EXIT_A6,
	ENTER_A7,
	RUN_A7,
	EXIT_A7,
	ENTER_B1,
	RUN_B1,
	EXIT_B1,
	ENTER_B2,
	RUN_B2,
	EXIT_B2,
	ENTER_B3,
	RUN_B3,
	EXIT_B3,
	ENTER_B4,
	RUN_B4,
	EXIT_B4,
	ENTER_B5,
	RUN_B5,
	EXIT_B5,
	ENTER_B6,
	RUN_B6,
	EXIT_B6,
	ENTER_C,
	RUN_C,
	EXIT_C,
};

struct sm_rec {
	struct sm_ctx ctx;
	int sv_tmp;
	int idx;
	int seq[SEQUENCE_SIZE];
};

static struct sm_rec sm[1];

enum state {
	SM_TEST_SUPER_A1,
	SM_TEST_SUPER_A2,
	SM_TEST_SUPER_A3,
	SM_TEST_SUPER_B1,
	SM_TEST_SUPER_B2,
	SM_TEST_SUPER_B3,
	SM_TEST_A4,
	SM_TEST_A5,
	SM_TEST_A6,
	SM_TEST_A7,
	SM_TEST_B4,
	SM_TEST_B5,
	SM_TEST_B6,
	SM_TEST_C,
};

static const struct usb_state states[];

static struct control {
	usb_state_ptr a3_entry_to;
	usb_state_ptr b3_run_to;
	usb_state_ptr b6_entry_to;
	usb_state_ptr c_entry_to;
	usb_state_ptr c_exit_to;
} test_control;

static void set_state_sm(const int port, const enum state new_state)
{
	set_state(port, &sm[port].ctx, &states[new_state]);
}

static void sm_test_super_A1_entry(const int port)
{
	sm[port].seq[sm[port].idx++] = ENTER_A1;
}

static void sm_test_super_A1_run(const int port)
{
	sm[port].seq[sm[port].idx++] = RUN_A1;
}

static void sm_test_super_A1_exit(const int port)
{
	sm[port].seq[sm[port].idx++] = EXIT_A1;
}

static void sm_test_super_B1_entry(const int port)
{
	sm[port].seq[sm[port].idx++] = ENTER_B1;
}

static void sm_test_super_B1_run(const int port)
{
	sm[port].seq[sm[port].idx++] = RUN_B1;
}

static void sm_test_super_B1_exit(const int port)
{
	sm[port].seq[sm[port].idx++] = EXIT_B1;
}

static void sm_test_super_A2_entry(const int port)
{
	sm[port].seq[sm[port].idx++] = ENTER_A2;
}

static void sm_test_super_A2_run(const int port)
{
	sm[port].seq[sm[port].idx++] = RUN_A2;
}

static void sm_test_super_A2_exit(const int port)
{
	sm[port].seq[sm[port].idx++] = EXIT_A2;
}

static void sm_test_super_B2_entry(const int port)
{
	sm[port].seq[sm[port].idx++] = ENTER_B2;
}

static void sm_test_super_B2_run(const int port)
{
	sm[port].seq[sm[port].idx++] = RUN_B2;
}

static void sm_test_super_B2_exit(const int port)
{
	sm[port].seq[sm[port].idx++] = EXIT_B2;
}

static void sm_test_super_A3_entry(const int port)
{
	sm[port].seq[sm[port].idx++] = ENTER_A3;
	if (test_control.a3_entry_to)
		set_state(port, &sm[port].ctx, test_control.a3_entry_to);
}

static void sm_test_super_A3_run(const int port)
{
	sm[port].seq[sm[port].idx++] = RUN_A3;
}

static void sm_test_super_A3_exit(const int port)
{
	sm[port].seq[sm[port].idx++] = EXIT_A3;
}

static void sm_test_super_B3_entry(const int port)
{
	sm[port].seq[sm[port].idx++] = ENTER_B3;
}

static void sm_test_super_B3_run(const int port)
{
	sm[port].seq[sm[port].idx++] = RUN_B3;
	if (test_control.b3_run_to)
		set_state(port, &sm[port].ctx, test_control.b3_run_to);
}

static void sm_test_super_B3_exit(const int port)
{
	sm[port].seq[sm[port].idx++] = EXIT_B3;
}

static void sm_test_A4_entry(const int port)
{
	sm[port].sv_tmp = 0;
	sm[port].seq[sm[port].idx++] = ENTER_A4;
}

static void sm_test_A4_run(const int port)
{
	if (sm[port].sv_tmp == 0) {
		sm[port].sv_tmp = 1;
		sm[port].seq[sm[port].idx++] = RUN_A4;
	} else {
		set_state_sm(port, SM_TEST_B4);
	}
}

static void sm_test_A4_exit(const int port)
{
	sm[port].seq[sm[port].idx++] = EXIT_A4;
}

static void sm_test_A5_entry(const int port)
{
	sm[port].sv_tmp = 0;
	sm[port].seq[sm[port].idx++] = ENTER_A5;
}

static void sm_test_A5_run(const int port)
{
	if (sm[port].sv_tmp == 0) {
		sm[port].sv_tmp = 1;
		sm[port].seq[sm[port].idx++] = RUN_A5;
	} else {
		set_state_sm(port, SM_TEST_A4);
	}
}

static void sm_test_A5_exit(const int port)
{
	sm[port].seq[sm[port].idx++] = EXIT_A5;
}

static void sm_test_A6_entry(const int port)
{
	sm[port].sv_tmp = 0;
	sm[port].seq[sm[port].idx++] = ENTER_A6;
}

static void sm_test_A6_run(const int port)
{
	if (sm[port].sv_tmp == 0) {
		sm[port].sv_tmp = 1;
		sm[port].seq[sm[port].idx++] = RUN_A6;
	} else {
		set_state_sm(port, SM_TEST_A5);
	}
}

static void sm_test_A6_exit(const int port)
{
	sm[port].seq[sm[port].idx++] = EXIT_A6;
}

static void sm_test_A7_entry(const int port)
{
	sm[port].sv_tmp = 0;
	sm[port].seq[sm[port].idx++] = ENTER_A7;
}

static void sm_test_A7_run(const int port)
{
	if (sm[port].sv_tmp == 0) {
		sm[port].sv_tmp = 1;
		sm[port].seq[sm[port].idx++] = RUN_A7;
	} else {
		set_state_sm(port, SM_TEST_A6);
	}
}

static void sm_test_A7_exit(const int port)
{
	sm[port].seq[sm[port].idx++] = EXIT_A7;
}

static void sm_test_B4_entry(const int port)
{
	sm[port].sv_tmp = 0;
	sm[port].seq[sm[port].idx++] = ENTER_B4;
}

static void sm_test_B4_run(const int port)
{
	if (sm[port].sv_tmp == 0) {
		sm[port].seq[sm[port].idx++] = RUN_B4;
		sm[port].sv_tmp = 1;
	} else {
		set_state_sm(port, SM_TEST_B5);
	}
}

static void sm_test_B4_exit(const int port)
{
	sm[port].seq[sm[port].idx++] = EXIT_B4;
}

static void sm_test_B5_entry(const int port)
{
	sm[port].sv_tmp = 0;
	sm[port].seq[sm[port].idx++] = ENTER_B5;
}

static void sm_test_B5_run(const int port)
{
	if (sm[port].sv_tmp == 0) {
		sm[port].sv_tmp = 1;
		sm[port].seq[sm[port].idx++] = RUN_B5;
	} else {
		set_state_sm(port, SM_TEST_B6);
	}
}

static void sm_test_B5_exit(const int port)
{
	sm[port].seq[sm[port].idx++] = EXIT_B5;
}

static void sm_test_B6_entry(const int port)
{
	sm[port].sv_tmp = 0;
	sm[port].seq[sm[port].idx++] = ENTER_B6;
	if (test_control.b6_entry_to)
		set_state(port, &sm[port].ctx, test_control.b6_entry_to);
}

static void sm_test_B6_run(const int port)
{
	if (sm[port].sv_tmp == 0) {
		sm[port].sv_tmp = 1;
		sm[port].seq[sm[port].idx++] = RUN_B6;
	} else {
		set_state_sm(port, SM_TEST_C);
	}
}

static void sm_test_B6_exit(const int port)
{
	sm[port].seq[sm[port].idx++] = EXIT_B6;
}

static void sm_test_C_entry(const int port)
{
	sm[port].sv_tmp = 0;
	sm[port].seq[sm[port].idx++] = ENTER_C;
	if (test_control.c_entry_to)
		set_state(port, &sm[port].ctx, test_control.c_entry_to);
}

static void sm_test_C_run(const int port)
{
	if (sm[port].sv_tmp == 0) {
		sm[port].seq[sm[port].idx++] = RUN_C;
		sm[port].sv_tmp = 1;
	} else {
		set_state_sm(port, SM_TEST_A7);
	}
}

static void sm_test_C_exit(const int port)
{
	sm[port].seq[sm[port].idx++] = EXIT_C;
	if (test_control.c_exit_to)
		set_state(port, &sm[port].ctx, test_control.c_exit_to);
}

static const struct usb_state states[] = {
	[SM_TEST_SUPER_A1] = {
		.entry  = sm_test_super_A1_entry,
		.run    = sm_test_super_A1_run,
		.exit   = sm_test_super_A1_exit,
	},
	[SM_TEST_SUPER_A2] = {
		.entry  = sm_test_super_A2_entry,
		.run    = sm_test_super_A2_run,
		.exit   = sm_test_super_A2_exit,
		.parent = &states[SM_TEST_SUPER_A1],
	},
	[SM_TEST_SUPER_A3] = {
		.entry  = sm_test_super_A3_entry,
		.run    = sm_test_super_A3_run,
		.exit   = sm_test_super_A3_exit,
		.parent = &states[SM_TEST_SUPER_A2],
	},
	[SM_TEST_SUPER_B1] = {
		.entry  = sm_test_super_B1_entry,
		.run    = sm_test_super_B1_run,
		.exit   = sm_test_super_B1_exit,
	},
	[SM_TEST_SUPER_B2] = {
		.entry  = sm_test_super_B2_entry,
		.run    = sm_test_super_B2_run,
		.exit   = sm_test_super_B2_exit,
		.parent = &states[SM_TEST_SUPER_B1],
	},
	[SM_TEST_SUPER_B3] = {
		.entry  = sm_test_super_B3_entry,
		.run    = sm_test_super_B3_run,
		.exit   = sm_test_super_B3_exit,
		.parent = &states[SM_TEST_SUPER_B2],
	},
	[SM_TEST_A4] = {
		.entry  = sm_test_A4_entry,
		.run    = sm_test_A4_run,
		.exit   = sm_test_A4_exit,
		.parent = &states[SM_TEST_SUPER_A3],
	},
	[SM_TEST_A5] = {
		.entry  = sm_test_A5_entry,
		.run    = sm_test_A5_run,
		.exit   = sm_test_A5_exit,
		.parent = &states[SM_TEST_SUPER_A3],
	},
	[SM_TEST_A6] = {
		.entry  = sm_test_A6_entry,
		.run    = sm_test_A6_run,
		.exit   = sm_test_A6_exit,
		.parent = &states[SM_TEST_SUPER_A2],
	},
	[SM_TEST_A7] = {
		.entry  = sm_test_A7_entry,
		.run    = sm_test_A7_run,
		.exit   = sm_test_A7_exit,
		.parent = &states[SM_TEST_SUPER_A1],
	},
	[SM_TEST_B4] = {
		.entry  = sm_test_B4_entry,
		.run    = sm_test_B4_run,
		.exit   = sm_test_B4_exit,
		.parent = &states[SM_TEST_SUPER_B3],
	},
	[SM_TEST_B5] = {
		.entry  = sm_test_B5_entry,
		.run    = sm_test_B5_run,
		.exit   = sm_test_B5_exit,
		.parent = &states[SM_TEST_SUPER_B2],
	},
	[SM_TEST_B6] = {
		.entry  = sm_test_B6_entry,
		.run    = sm_test_B6_run,
		.exit   = sm_test_B6_exit,
		.parent = &states[SM_TEST_SUPER_B1],
	},
	[SM_TEST_C] = {
		.entry  = sm_test_C_entry,
		.run    = sm_test_C_run,
		.exit   = sm_test_C_exit,
	},
};

static void before_test(void *fixture)
{
	memset(&sm[PORT0], 0, sizeof(struct sm_rec));
	memset(&test_control, 0, sizeof(struct control));
}

ZTEST_SUITE(usbc_sm, NULL, NULL, before_test, NULL, NULL);

ZTEST(usbc_sm, test_hierarchy_super_states)
{
	int port = PORT0;
	int i = 0;

	set_state_sm(port, SM_TEST_A4);

	run_state(port, &sm[port].ctx);
	zassert_equal(sm[port].seq[i++], ENTER_A1);
	zassert_equal(sm[port].seq[i++], ENTER_A2);
	zassert_equal(sm[port].seq[i++], ENTER_A3);
	zassert_equal(sm[port].seq[i++], ENTER_A4);

	run_state(port, &sm[port].ctx);
	zassert_equal(sm[port].seq[i++], RUN_A4);
	zassert_equal(sm[port].seq[i++], RUN_A3);
	zassert_equal(sm[port].seq[i++], RUN_A2);
	zassert_equal(sm[port].seq[i++], RUN_A1);

	run_state(port, &sm[port].ctx);
	zassert_equal(sm[port].seq[i++], EXIT_A4);
	zassert_equal(sm[port].seq[i++], EXIT_A3);
	zassert_equal(sm[port].seq[i++], EXIT_A2);
	zassert_equal(sm[port].seq[i++], EXIT_A1);
	zassert_equal(sm[port].seq[i++], ENTER_B1);
	zassert_equal(sm[port].seq[i++], ENTER_B2);
	zassert_equal(sm[port].seq[i++], ENTER_B3);
	zassert_equal(sm[port].seq[i++], ENTER_B4);

	run_state(port, &sm[port].ctx);
	zassert_equal(sm[port].seq[i++], RUN_B4);
	zassert_equal(sm[port].seq[i++], RUN_B3);
	zassert_equal(sm[port].seq[i++], RUN_B2);
	zassert_equal(sm[port].seq[i++], RUN_B1);

	run_state(port, &sm[port].ctx);
	zassert_equal(sm[port].seq[i++], EXIT_B4);
	zassert_equal(sm[port].seq[i++], EXIT_B3);
	zassert_equal(sm[port].seq[i++], ENTER_B5);

	run_state(port, &sm[port].ctx);
	zassert_equal(sm[port].seq[i++], RUN_B5);
	zassert_equal(sm[port].seq[i++], RUN_B2);
	zassert_equal(sm[port].seq[i++], RUN_B1);

	run_state(port, &sm[port].ctx);
	zassert_equal(sm[port].seq[i++], EXIT_B5);
	zassert_equal(sm[port].seq[i++], EXIT_B2);
	zassert_equal(sm[port].seq[i++], ENTER_B6);

	run_state(port, &sm[port].ctx);
	zassert_equal(sm[port].seq[i++], RUN_B6);
	zassert_equal(sm[port].seq[i++], RUN_B1);

	run_state(port, &sm[port].ctx);
	zassert_equal(sm[port].seq[i++], EXIT_B6);
	zassert_equal(sm[port].seq[i++], EXIT_B1);
	zassert_equal(sm[port].seq[i++], ENTER_C);

	run_state(port, &sm[port].ctx);
	zassert_equal(sm[port].seq[i++], RUN_C);

	run_state(port, &sm[port].ctx);
	zassert_equal(sm[port].seq[i++], EXIT_C);
	zassert_equal(sm[port].seq[i++], ENTER_A1);
	zassert_equal(sm[port].seq[i++], ENTER_A7);

	run_state(port, &sm[port].ctx);
	zassert_equal(sm[port].seq[i++], RUN_A7);
	zassert_equal(sm[port].seq[i++], RUN_A1);

	run_state(port, &sm[port].ctx);
	zassert_equal(sm[port].seq[i++], EXIT_A7);
	zassert_equal(sm[port].seq[i++], ENTER_A2);
	zassert_equal(sm[port].seq[i++], ENTER_A6);

	run_state(port, &sm[port].ctx);
	zassert_equal(sm[port].seq[i++], RUN_A6);
	zassert_equal(sm[port].seq[i++], RUN_A2);
	zassert_equal(sm[port].seq[i++], RUN_A1);

	run_state(port, &sm[port].ctx);
	zassert_equal(sm[port].seq[i++], EXIT_A6);
	zassert_equal(sm[port].seq[i++], ENTER_A3);
	zassert_equal(sm[port].seq[i++], ENTER_A5);

	run_state(port, &sm[port].ctx);
	zassert_equal(sm[port].seq[i++], RUN_A5);
	zassert_equal(sm[port].seq[i++], RUN_A3);
	zassert_equal(sm[port].seq[i++], RUN_A2);
	zassert_equal(sm[port].seq[i++], RUN_A1);

	run_state(port, &sm[port].ctx);
	zassert_equal(sm[port].seq[i++], EXIT_A5);
	zassert_equal(sm[port].seq[i++], ENTER_A4);

	for (; i < SEQUENCE_SIZE; i++)
		zassert_equal(sm[port].seq[i], 0);
}

ZTEST(usbc_sm, test_set_state_from_parents)
{
	int port = PORT0;
	int i = 0;

	/* Start state machine with entry override from parent */
	test_control.a3_entry_to = &states[SM_TEST_B4];
	set_state_sm(port, SM_TEST_A4);

	zassert_equal(sm[port].seq[i++], ENTER_A1);
	zassert_equal(sm[port].seq[i++], ENTER_A2);
	zassert_equal(sm[port].seq[i++], ENTER_A3);
	/* Does not enter or exit A4; aborts child entries and transitions to B4
	 */
	zassert_equal(sm[port].seq[i++], EXIT_A3);
	zassert_equal(sm[port].seq[i++], EXIT_A2);
	zassert_equal(sm[port].seq[i++], EXIT_A1);
	zassert_equal(sm[port].seq[i++], ENTER_B1);
	zassert_equal(sm[port].seq[i++], ENTER_B2);
	zassert_equal(sm[port].seq[i++], ENTER_B3);
	zassert_equal(sm[port].seq[i++], ENTER_B4);
	zassert_equal(sm[port].seq[i], 0);

	test_control.b3_run_to = &states[SM_TEST_B5];
	run_state(port, &sm[port].ctx);
	zassert_equal(sm[port].seq[i++], RUN_B4);
	zassert_equal(sm[port].seq[i++], RUN_B3);
	/* Does not run b2 or b1 due to state transition */
	zassert_equal(sm[port].seq[i++], EXIT_B4);
	zassert_equal(sm[port].seq[i++], EXIT_B3);
	zassert_equal(sm[port].seq[i++], ENTER_B5);
	zassert_equal(sm[port].seq[i], 0);

	run_state(port, &sm[port].ctx);
	zassert_equal(sm[port].seq[i++], RUN_B5);
	zassert_equal(sm[port].seq[i++], RUN_B2);
	zassert_equal(sm[port].seq[i++], RUN_B1);
	zassert_equal(sm[port].seq[i], 0);

	/*
	 * Multiple chains of parent entry. Also verify that set_state calls
	 * within exit handlers are ignored.
	 */
	test_control.b6_entry_to = &states[SM_TEST_C];
	test_control.c_entry_to = &states[SM_TEST_A7];
	test_control.c_exit_to = &states[SM_TEST_A4];
	run_state(port, &sm[port].ctx);
	zassert_equal(sm[port].seq[i++], EXIT_B5);
	zassert_equal(sm[port].seq[i++], EXIT_B2);
	zassert_equal(sm[port].seq[i++], ENTER_B6);
	zassert_equal(sm[port].seq[i++], EXIT_B6);
	zassert_equal(sm[port].seq[i++], EXIT_B1);
	zassert_equal(sm[port].seq[i++], ENTER_C);
	zassert_equal(sm[port].seq[i++], EXIT_C);
	zassert_equal(sm[port].seq[i++], ENTER_A1);
	zassert_equal(sm[port].seq[i++], ENTER_A7);
	zassert_equal(sm[port].seq[i], 0);

	for (; i < SEQUENCE_SIZE; i++)
		zassert_equal(sm[port].seq[i], 0);
}
