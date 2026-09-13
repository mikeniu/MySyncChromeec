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

static void set_state_sm(const enum state new_state)
{
	set_state(PORT0, &sm[PORT0].ctx, &states[new_state]);
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
		set_state_sm(SM_TEST_B4);
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
		set_state_sm(SM_TEST_A4);
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
		set_state_sm(SM_TEST_A5);
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
		set_state_sm(SM_TEST_A6);
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
		set_state_sm(SM_TEST_B5);
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
		set_state_sm(SM_TEST_B6);
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
		set_state_sm(SM_TEST_C);
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
		set_state_sm(SM_TEST_A7);
	}
}

static void sm_test_C_exit(const int port)
{
	sm[port].seq[sm[port].idx++] = EXIT_C;
	if (test_control.c_exit_to)
		set_state(port, &sm[port].ctx, test_control.c_exit_to);
}

/*
 * State Hierarchy under test:
 *
 *     SUPER_A1
 *     ├── SUPER_A2
 *     │   ├── SUPER_A3
 *     │   │   ├── A4 (leaf)
 *     │   │   └── A5 (leaf)
 *     │   └── A6 (leaf)
 *     └── A7 (leaf)
 *
 *     SUPER_B1
 *     ├── SUPER_B2
 *     │   ├── SUPER_B3
 *     │   │   └── B4 (leaf)
 *     │   └── B5 (leaf)
 *     └── B6 (leaf)
 *
 *     C (independent leaf state, no parent)
 */
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

/**
 * Asserts that the sequence of state events recorded in @p rec matches
 * the expected list of events in exact order and count, then resets the
 * event buffer and index for the next phase.
 */
#define ASSERT_SEQ(rec, ...)                                                  \
	do {                                                                  \
		const int _exp[] = { __VA_ARGS__ };                           \
		struct sm_rec *_r = (rec);                                    \
		zassert_equal(_r->idx, (int)ARRAY_SIZE(_exp),                 \
			      "Event count mismatch: expected %zu, got %d",   \
			      ARRAY_SIZE(_exp), _r->idx);                     \
		for (size_t _k = 0; _k < ARRAY_SIZE(_exp); _k++) {            \
			zassert_equal(                                        \
				_r->seq[_k], _exp[_k],                        \
				"Mismatch at index %zu: expected %d, got %d", \
				_k, _exp[_k], _r->seq[_k]);                   \
		}                                                             \
		_r->idx = 0;                                                  \
		memset(_r->seq, 0, sizeof(_r->seq));                          \
	} while (0)

/**
 * Test hierarchical state machine execution order, parent/super state
 * entry/run/exit bubbling, and transitions between sibling, cousin, and
 * cross-tree states.
 */
ZTEST(usbc_sm, test_hierarchy_super_states)
{
	int port = PORT0;

	/* Phase 1: Enter root-to-leaf path A1 -> A2 -> A3 -> A4. */
	set_state_sm(SM_TEST_A4);
	ASSERT_SEQ(&sm[port], ENTER_A1, ENTER_A2, ENTER_A3, ENTER_A4);

	/*
	 * Phase 2: Run leaf A4 and all super states bottom-up (A4 -> A3 -> A2
	 * -> A1). On the next run, A4 triggers a state transition to B4.
	 */
	run_state(port, &sm[port].ctx);
	ASSERT_SEQ(&sm[port], RUN_A4, RUN_A3, RUN_A2, RUN_A1);

	/*
	 * Phase 3: Transition across trees from A4 to B4.
	 * Exits A hierarchy bottom-up (A4 -> A3 -> A2 -> A1), then enters B
	 * hierarchy top-down (B1 -> B2 -> B3 -> B4).
	 */
	run_state(port, &sm[port].ctx);
	ASSERT_SEQ(&sm[port], EXIT_A4, EXIT_A3, EXIT_A2, EXIT_A1, ENTER_B1,
		   ENTER_B2, ENTER_B3, ENTER_B4);

	/*
	 * Phase 4: Run B hierarchy (B4 -> B3 -> B2 -> B1).
	 * On next run, B4 triggers transition to sibling-branch B5 (child of
	 * B2).
	 */
	run_state(port, &sm[port].ctx);
	ASSERT_SEQ(&sm[port], RUN_B4, RUN_B3, RUN_B2, RUN_B1);

	/*
	 * Phase 5: Transition B4 -> B5.
	 * Exits up to common ancestor B2 (exits B4 -> B3), enters B5.
	 */
	run_state(port, &sm[port].ctx);
	ASSERT_SEQ(&sm[port], EXIT_B4, EXIT_B3, ENTER_B5);

	/*
	 * Phase 6: Run B5 -> B2 -> B1.
	 * Next run triggers transition to cousin state B6 (child of B1).
	 */
	run_state(port, &sm[port].ctx);
	ASSERT_SEQ(&sm[port], RUN_B5, RUN_B2, RUN_B1);

	/*
	 * Phase 7: Transition B5 -> B6.
	 * Exits up to common ancestor B1 (exits B5 -> B2), enters B6.
	 */
	run_state(port, &sm[port].ctx);
	ASSERT_SEQ(&sm[port], EXIT_B5, EXIT_B2, ENTER_B6);

	/*
	 * Phase 8: Run B6 -> B1.
	 * Next run triggers transition to standalone state C.
	 */
	run_state(port, &sm[port].ctx);
	ASSERT_SEQ(&sm[port], RUN_B6, RUN_B1);

	/*
	 * Phase 9: Transition B6 -> C.
	 * Exits entire B hierarchy (B6 -> B1), enters C.
	 */
	run_state(port, &sm[port].ctx);
	ASSERT_SEQ(&sm[port], EXIT_B6, EXIT_B1, ENTER_C);

	/*
	 * Phase 10: Run standalone state C.
	 * Next run triggers transition to A7 (child of A1).
	 */
	run_state(port, &sm[port].ctx);
	ASSERT_SEQ(&sm[port], RUN_C);

	/*
	 * Phase 11: Transition C -> A7.
	 * Exits C, enters A1 -> A7.
	 */
	run_state(port, &sm[port].ctx);
	ASSERT_SEQ(&sm[port], EXIT_C, ENTER_A1, ENTER_A7);

	/*
	 * Phase 12: Run A7 -> A1.
	 * Next run triggers transition deeper into A tree: A6 (child of A2).
	 */
	run_state(port, &sm[port].ctx);
	ASSERT_SEQ(&sm[port], RUN_A7, RUN_A1);

	/*
	 * Phase 13: Transition A7 -> A6.
	 * Exits A7 (common ancestor A1), enters A2 -> A6.
	 */
	run_state(port, &sm[port].ctx);
	ASSERT_SEQ(&sm[port], EXIT_A7, ENTER_A2, ENTER_A6);

	/*
	 * Phase 14: Run A6 -> A2 -> A1.
	 * Next run triggers transition deeper: A5 (child of A3).
	 */
	run_state(port, &sm[port].ctx);
	ASSERT_SEQ(&sm[port], RUN_A6, RUN_A2, RUN_A1);

	/*
	 * Phase 15: Transition A6 -> A5.
	 * Exits A6 (common ancestor A2), enters A3 -> A5.
	 */
	run_state(port, &sm[port].ctx);
	ASSERT_SEQ(&sm[port], EXIT_A6, ENTER_A3, ENTER_A5);

	/*
	 * Phase 16: Run A5 -> A3 -> A2 -> A1.
	 * Next run triggers sibling transition to A4 (both children of A3).
	 */
	run_state(port, &sm[port].ctx);
	ASSERT_SEQ(&sm[port], RUN_A5, RUN_A3, RUN_A2, RUN_A1);

	/*
	 * Phase 17: Sibling transition A5 -> A4.
	 * Exits A5 (common ancestor A3 preserved), enters A4.
	 */
	run_state(port, &sm[port].ctx);
	ASSERT_SEQ(&sm[port], EXIT_A5, ENTER_A4);
}

/**
 * Test parent/super state entry overrides, run overrides, and exit handler
 * guards.
 */
ZTEST(usbc_sm, test_set_state_from_parents)
{
	int port = PORT0;

	/*
	 * Case 1: Override state entry from within a parent's entry callback.
	 * When entering A4 (path: A1 -> A2 -> A3 -> A4), A3's entry callback
	 * calls set_state to B4.
	 * The state machine must abort entering A4, immediately exit A3 -> A2
	 * -> A1, and enter B1 -> B2 -> B3 -> B4.
	 */
	test_control.a3_entry_to = &states[SM_TEST_B4];
	set_state_sm(SM_TEST_A4);
	ASSERT_SEQ(&sm[port], ENTER_A1, ENTER_A2, ENTER_A3, EXIT_A3, EXIT_A2,
		   EXIT_A1, ENTER_B1, ENTER_B2, ENTER_B3, ENTER_B4);

	/*
	 * Case 2: Override state transition during a parent's run callback.
	 * B3's run callback calls set_state to B5.
	 * Running B4 -> B3 will trigger exit of B4 -> B3 and enter B5 without
	 * running higher parents B2 or B1.
	 */
	test_control.b3_run_to = &states[SM_TEST_B5];
	run_state(port, &sm[port].ctx);
	ASSERT_SEQ(&sm[port], RUN_B4, RUN_B3, EXIT_B4, EXIT_B3, ENTER_B5);

	run_state(port, &sm[port].ctx);
	ASSERT_SEQ(&sm[port], RUN_B5, RUN_B2, RUN_B1);

	/*
	 * Case 3: Multiple nested entry overrides and verify that set_state
	 * calls within exit handlers are ignored.
	 */
	test_control.b6_entry_to = &states[SM_TEST_C];
	test_control.c_entry_to = &states[SM_TEST_A7];
	test_control.c_exit_to = &states[SM_TEST_A4];
	run_state(port, &sm[port].ctx);
	ASSERT_SEQ(&sm[port], EXIT_B5, EXIT_B2, ENTER_B6, EXIT_B6, EXIT_B1,
		   ENTER_C, EXIT_C, ENTER_A1, ENTER_A7);
}
