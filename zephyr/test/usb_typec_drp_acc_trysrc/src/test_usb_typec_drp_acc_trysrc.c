/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "usb_typec_drp_acc_trysrc_shim.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/fff.h>
#include <zephyr/ztest.h>

FAKE_VALUE_FUNC(int, tcpm_get_cc, int, int *, int *);
FAKE_VALUE_FUNC(bool, cc_is_snk_dbg_acc, int, int);
FAKE_VALUE_FUNC(bool, cc_is_at_least_one_rd, int, int);
FAKE_VALUE_FUNC(bool, board_is_dts_port, int);
FAKE_VOID_FUNC(set_state_tc, int, enum usb_tc_state);
FAKE_VALUE_FUNC(bool, pd_check_vbus_level, int, enum vbus_level);
FAKE_VALUE_FUNC(bool, pd_timer_is_expired, int, enum pd_task_timer);

#define TEST_PORT 0
static enum pd_drp_state test_drp_state[1];

enum pd_drp_state tc_get_drp_state(int port)
{
	return test_drp_state[port];
}

static void tc_aws_before(void *f)
{
	ARG_UNUSED(f);

	RESET_FAKE(tcpm_get_cc);
	RESET_FAKE(cc_is_snk_dbg_acc);
	RESET_FAKE(cc_is_at_least_one_rd);
	RESET_FAKE(board_is_dts_port);
	RESET_FAKE(set_state_tc);
	RESET_FAKE(pd_check_vbus_level);
	RESET_FAKE(pd_timer_is_expired);

	memset(test_drp_state, 0, sizeof(test_drp_state));
	test_drp_state[TEST_PORT] = PD_DRP_TOGGLE_ON;
	pd_check_vbus_level_fake.return_val = true;
	pd_timer_is_expired_fake.return_val = true;
}

ZTEST_SUITE(usb_typec_drp_acc_trysrc, NULL, NULL, tc_aws_before, NULL, NULL);

ZTEST(usb_typec_drp_acc_trysrc, test_dbg_acc_dts_supported)
{
	cc_is_snk_dbg_acc_fake.return_val = true;
	board_is_dts_port_fake.return_val = true;

	tc_attach_wait_src_run(TEST_PORT);

	zassert_equal(set_state_tc_fake.call_count, 1,
		      "Expected set_state_tc called once, got %d",
		      set_state_tc_fake.call_count);
	zassert_equal(set_state_tc_fake.arg1_val, TC_ATTACHED_SRC,
		      "Expected TC_ATTACHED_SRC, got %d",
		      set_state_tc_fake.arg1_val);
}

ZTEST(usb_typec_drp_acc_trysrc, test_dbg_acc_no_dts_force_sink)
{
	cc_is_snk_dbg_acc_fake.return_val = true;
	board_is_dts_port_fake.return_val = false;
	test_drp_state[TEST_PORT] = PD_DRP_FORCE_SINK;

	tc_attach_wait_src_run(TEST_PORT);

	zassert_equal(set_state_tc_fake.call_count, 1,
		      "Expected set_state_tc called once, got %d",
		      set_state_tc_fake.call_count);
	zassert_equal(set_state_tc_fake.arg1_val, TC_UNATTACHED_SNK,
		      "Expected TC_UNATTACHED_SNK, got %d",
		      set_state_tc_fake.arg1_val);
}

ZTEST(usb_typec_drp_acc_trysrc, test_dbg_acc_no_dts_remain_wait)
{
	cc_is_snk_dbg_acc_fake.return_val = true;
	board_is_dts_port_fake.return_val = false;
	test_drp_state[TEST_PORT] = PD_DRP_TOGGLE_ON;

	tc_attach_wait_src_run(TEST_PORT);

	zassert_equal(set_state_tc_fake.call_count, 0,
		      "set_state_tc must NOT be called, got %d",
		      set_state_tc_fake.call_count);
	zassert_equal(cc_is_at_least_one_rd_fake.call_count, 0,
		      "cc_is_at_least_one_rd must NOT be reached, got %d",
		      cc_is_at_least_one_rd_fake.call_count);
}

ZTEST(usb_typec_drp_acc_trysrc, test_ufp_attached)
{
	cc_is_snk_dbg_acc_fake.return_val = false;
	cc_is_at_least_one_rd_fake.return_val = true;

	tc_attach_wait_src_run(TEST_PORT);

	zassert_equal(set_state_tc_fake.call_count, 1,
		      "Expected set_state_tc called once, got %d",
		      set_state_tc_fake.call_count);
	zassert_equal(set_state_tc_fake.arg1_val, TC_ATTACHED_SRC,
		      "Expected TC_ATTACHED_SRC, got %d",
		      set_state_tc_fake.arg1_val);
	zassert_equal(board_is_dts_port_fake.call_count, 0,
		      "board_is_dts_port must NOT be called, got %d",
		      board_is_dts_port_fake.call_count);
}
