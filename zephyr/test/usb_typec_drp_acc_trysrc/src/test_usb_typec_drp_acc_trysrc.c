/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "usb_pd.h"
#include "usb_pd_tcpm.h"
#include "usb_pd_timer.h"
#include "usb_tc_sm.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/fff.h>
#include <zephyr/ztest.h>

FAKE_VALUE_FUNC(int, tcpm_get_cc, int, enum tcpc_cc_voltage_status *,
		enum tcpc_cc_voltage_status *);

FAKE_VALUE_FUNC(bool, board_is_dts_port, int);
FAKE_VOID_FUNC(set_state_tc, int, enum usb_tc_state);

FAKE_VALUE_FUNC(bool, pd_check_vbus_level, int, enum vbus_level);

FAKE_VALUE_FUNC(bool, pd_timer_is_expired, int, enum pd_task_timer);
FAKE_VOID_FUNC(pd_timer_enable, int, enum pd_task_timer, uint32_t);
FAKE_VOID_FUNC(pd_timer_disable, int, enum pd_task_timer);

FAKE_VOID_FUNC(tc_set_data_role, int, enum pd_data_role);

#define TEST_PORT 0
#define CONFIG_USB_PD_PORT_MAX_COUNT 1

static enum pd_drp_state test_drp_state[CONFIG_USB_PD_PORT_MAX_COUNT];

enum pd_drp_state tc_get_drp_state(int port)
{
	return test_drp_state[port];
}

static int fake_tcpm_get_cc_rd_rd(int port, enum tcpc_cc_voltage_status *cc1,
				  enum tcpc_cc_voltage_status *cc2)
{
	*cc1 = TYPEC_CC_VOLT_RD;
	*cc2 = TYPEC_CC_VOLT_RD;
	return 0;
}

static int fake_tcpm_get_cc_rd_open(int port, enum tcpc_cc_voltage_status *cc1,
				    enum tcpc_cc_voltage_status *cc2)
{
	*cc1 = TYPEC_CC_VOLT_RD;
	*cc2 = TYPEC_CC_VOLT_OPEN;
	return 0;
}

#include "usbc/usb_tc_drp_acc_trysrc_sm.c"

static void tc_aws_before(void *f)
{
	ARG_UNUSED(f);

	RESET_FAKE(tcpm_get_cc);
	RESET_FAKE(board_is_dts_port);
	RESET_FAKE(set_state_tc);
	RESET_FAKE(pd_check_vbus_level);
	RESET_FAKE(pd_timer_is_expired);
	RESET_FAKE(pd_timer_enable);
	RESET_FAKE(pd_timer_disable);
	RESET_FAKE(tc_set_data_role);

	memset(test_drp_state, 0, sizeof(test_drp_state));
	test_drp_state[TEST_PORT] = PD_DRP_TOGGLE_ON;
	pd_check_vbus_level_fake.return_val = true;
	pd_timer_is_expired_fake.return_val = true;
}

ZTEST_SUITE(usb_typec_drp_acc_trysrc, NULL, NULL, tc_aws_before, NULL, NULL);

ZTEST(usb_typec_drp_acc_trysrc, test_dbg_acc_dts_supported)
{
	tcpm_get_cc_fake.custom_fake = fake_tcpm_get_cc_rd_rd;
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
	tcpm_get_cc_fake.custom_fake = fake_tcpm_get_cc_rd_rd;
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
	tcpm_get_cc_fake.custom_fake = fake_tcpm_get_cc_rd_rd;
	board_is_dts_port_fake.return_val = false;
	test_drp_state[TEST_PORT] = PD_DRP_TOGGLE_ON;

	tc_attach_wait_src_run(TEST_PORT);

	zassert_equal(set_state_tc_fake.call_count, 0,
		      "set_state_tc must NOT be called, got %d",
		      set_state_tc_fake.call_count);
}

ZTEST(usb_typec_drp_acc_trysrc, test_ufp_attached)
{
	tcpm_get_cc_fake.custom_fake = fake_tcpm_get_cc_rd_open;
	board_is_dts_port_fake.return_val = false;

	tc_attach_wait_src_run(TEST_PORT);

	zassert_equal(set_state_tc_fake.call_count, 1,
		      "Expected set_state_tc called once, got %d",
		      set_state_tc_fake.call_count);
	zassert_equal(set_state_tc_fake.arg1_val, TC_ATTACHED_SRC,
		      "Expected TC_ATTACHED_SRC, got %d",
		      set_state_tc_fake.arg1_val);
	zassert_equal(board_is_dts_port_fake.call_count, 0,
		      "board_is_dts_port must NOT be called for normal UFP, "
		      "got %d",
		      board_is_dts_port_fake.call_count);
}
