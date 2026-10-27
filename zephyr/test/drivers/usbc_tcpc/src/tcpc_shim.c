/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "gpio_signal.h"
#include "hooks.h"
#include "test/drivers/test_state.h"
#include "timer.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"
#include "usbc/tcpci.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

LOG_MODULE_REGISTER(test_drivers_tcpc, LOG_LEVEL_DBG);

#define TCPC_CHIP_STUB(node_id) {},

static struct tcpc_config_t tcpc_config_saved[] = { DT_FOREACH_STATUS_OKAY(
	named_usbc_port, TCPC_CHIP_STUB) };

ZTEST(tcpc_shim, test_tcpc_alts_exist)
{
	/* Verify all TCPC types are able to create an alternate TCPC entry */
	zassert_not_null(&TCPC_ALT_FROM_NODELABEL(tcpc_anx7447_alt));
	zassert_not_null(&TCPC_ALT_FROM_NODELABEL(tcpc_ccgxxf_alt));
	zassert_not_null(&TCPC_ALT_FROM_NODELABEL(tcpc_fusb302_alt));
	zassert_not_null(&TCPC_ALT_FROM_NODELABEL(tcpc_ps8xxx_alt));
	zassert_not_null(&TCPC_ALT_FROM_NODELABEL(tcpc_raa489000_alt));
	zassert_not_null(&TCPC_ALT_FROM_NODELABEL(tcpc_nct38xx_alt));
	zassert_not_null(&TCPC_ALT_FROM_NODELABEL(tcpc_rt1715_alt));
	zassert_not_null(&TCPC_ALT_FROM_NODELABEL(tcpc_rt1718s_alt));
	zassert_not_null(&TCPC_ALT_FROM_NODELABEL(tcpc_alt));
}

ZTEST(tcpc_shim, test_tcpc_alt_enable)
{
	/* Enable an alternate TCPC on each USB-C port */
	TCPC_ENABLE_ALTERNATE_BY_NODELABEL(0, tcpc_ps8xxx_alt);
	TCPC_ENABLE_ALTERNATE_BY_NODELABEL(1, tcpc_rt1715_alt);

	zassert_mem_equal(&tcpc_config[0],
			  &TCPC_ALT_FROM_NODELABEL(tcpc_ps8xxx_alt),
			  sizeof(struct tcpc_config_t));
	zassert_mem_equal(&tcpc_config[1],
			  &TCPC_ALT_FROM_NODELABEL(tcpc_rt1715_alt),
			  sizeof(struct tcpc_config_t));

	TCPC_ENABLE_ALTERNATE_BY_NODELABEL(0, tcpc_anx7447_alt);
	zassert_mem_equal(&tcpc_config[0],
			  &TCPC_ALT_FROM_NODELABEL(tcpc_anx7447_alt),
			  sizeof(struct tcpc_config_t));
}

ZTEST(tcpc_shim, test_audio_accessory_cc_states)
{
	/* Verify Audio Accessory CC voltage profile detection (Ra on CC1 & CC2)
	 */
	zassert_true(cc_is_audio_acc(TYPEC_CC_VOLT_RA, TYPEC_CC_VOLT_RA));
	zassert_false(cc_is_audio_acc(TYPEC_CC_VOLT_RD, TYPEC_CC_VOLT_RD));
	zassert_false(cc_is_audio_acc(TYPEC_CC_VOLT_RA, TYPEC_CC_VOLT_RD));
}

ZTEST(tcpc_shim, test_debug_accessory_cc_states)
{
	/* Verify Source Debug Accessory CC voltage profile detection (Rp on CC1
	 * & CC2)
	 */
	zassert_true(
		cc_is_src_dbg_acc(TYPEC_CC_VOLT_RP_DEF, TYPEC_CC_VOLT_RP_1_5));
	zassert_true(
		cc_is_src_dbg_acc(TYPEC_CC_VOLT_RP_3_0, TYPEC_CC_VOLT_RP_3_0));
	zassert_false(cc_is_src_dbg_acc(TYPEC_CC_VOLT_RD, TYPEC_CC_VOLT_RD));

	/* Verify Sink Debug Accessory CC voltage profile detection (Rd on CC1 &
	 * CC2)
	 */
	zassert_true(cc_is_snk_dbg_acc(TYPEC_CC_VOLT_RD, TYPEC_CC_VOLT_RD));
	zassert_false(
		cc_is_snk_dbg_acc(TYPEC_CC_VOLT_RP_DEF, TYPEC_CC_VOLT_RP_DEF));
	zassert_false(cc_is_snk_dbg_acc(TYPEC_CC_VOLT_RD, TYPEC_CC_VOLT_RA));
}

#if defined(CONFIG_ZTEST) || defined(TEST_BUILD)
#define ALERT_BURST_YIELD_THRESHOLD 10
extern struct storm_tracker_t {
	int count;
	timestamp_t time;
} storm_tracker[];
void service_one_port(int port);

#ifdef CONFIG_USB_PD_TCPM_TCPCI
extern const struct deferred_data resume_pd_port_data;
#endif

static int mock_alert_count;
static void test_mock_alert(int port)
{
	mock_alert_count++;
}

static const struct tcpm_drv test_mock_drv = {
	.tcpc_alert = test_mock_alert,
};

static const struct tcpm_drv *saved_drv;

static void usbc_intr_storm_before(void *data)
{
	saved_drv = tcpc_config[0].drv;
	tcpc_config[0].drv = &test_mock_drv;
	mock_alert_count = 0;
	pd_set_suspend(0, 0);
}

static void usbc_intr_storm_after(void *data)
{
	get_time_mock = NULL;
	tcpc_config[0].drv = saved_drv;
	pd_set_suspend(0, 0);
#ifdef CONFIG_USB_PD_TCPM_TCPCI
	hook_call_deferred(&resume_pd_port_data, -1);
#endif
}

ZTEST(usbc_intr_storm, test_usbc_intr_storm_decay_within_interval)
{
	timestamp_t mock_time;

	/* Current simulated time: 10.1 seconds */
	mock_time.val = 10 * USEC_PER_SEC + 100 * USEC_PER_MSEC;
	get_time_mock = &mock_time;

	/* Expired 100ms ago (window was 10.0s, now is 10.1s -> within 1s
	 * interval) */
	storm_tracker[0].count = 100;
	storm_tracker[0].time.val = 10 * USEC_PER_SEC;

	service_one_port(0);

	get_time_mock = NULL;

	/* Should decay to (100 / 2) + 1 = 51 */
	zassert_equal(storm_tracker[0].count, 51, "Expected count 51, got %d",
		      storm_tracker[0].count);
	zassert_equal(mock_alert_count, 1, "Alert handler should be called");
}

ZTEST(usbc_intr_storm, test_usbc_intr_storm_decay_after_interval)
{
	timestamp_t mock_time;

	/* Current simulated time: 10 seconds */
	mock_time.val = 10 * USEC_PER_SEC;
	get_time_mock = &mock_time;

	/* Expired > 1 interval in silence (window was 5.0s, now is 10.0s -> 5s
	 * silence) */
	storm_tracker[0].count = 100;
	storm_tracker[0].time.val = 5 * USEC_PER_SEC;

	service_one_port(0);

	get_time_mock = NULL;

	/* Should reset to 1 */
	zassert_equal(storm_tracker[0].count, 1, "Expected count 1, got %d",
		      storm_tracker[0].count);
	zassert_equal(mock_alert_count, 1, "Alert handler should be called");
}

ZTEST(usbc_intr_storm, test_usbc_intr_storm_triggers_suspend)
{
	timestamp_t mock_time;

	/* Current simulated time: 10 seconds */
	mock_time.val = 10 * USEC_PER_SEC;
	get_time_mock = &mock_time;

	/* Count at MAX, unexpired (window ends at 10.5s) */
	storm_tracker[0].count = CONFIG_USB_PD_INT_STORM_MAX;
	storm_tracker[0].time.val = 10 * USEC_PER_SEC + 500 * USEC_PER_MSEC;

	service_one_port(0);

	get_time_mock = NULL;

	/* Should increment past MAX */
	zassert_equal(storm_tracker[0].count, CONFIG_USB_PD_INT_STORM_MAX + 1,
		      "Expected count %d, got %d",
		      CONFIG_USB_PD_INT_STORM_MAX + 1, storm_tracker[0].count);
	zassert_equal(mock_alert_count, 1, "Alert handler should be called");
}

ZTEST(usbc_intr_storm, test_usbc_intr_storm_burst_yield)
{
	timestamp_t mock_time;

	/* Current simulated time: 10 seconds */
	mock_time.val = 10 * USEC_PER_SEC;
	get_time_mock = &mock_time;

	/* Window valid until 10.5 seconds, count at 9 (one below threshold of
	 * 10) */
	storm_tracker[0].count = ALERT_BURST_YIELD_THRESHOLD - 1;
	storm_tracker[0].time.val = 10 * USEC_PER_SEC + 500 * USEC_PER_MSEC;

	/* Next alert increments count to 10 (multiple of
	 * ALERT_BURST_YIELD_THRESHOLD -> yields) */
	service_one_port(0);
	zassert_equal(storm_tracker[0].count, ALERT_BURST_YIELD_THRESHOLD,
		      "Expected count %d, got %d", ALERT_BURST_YIELD_THRESHOLD,
		      storm_tracker[0].count);

	/* Subsequent alert increments count to 11 (non-multiple -> no yield) */
	service_one_port(0);
	zassert_equal(storm_tracker[0].count, ALERT_BURST_YIELD_THRESHOLD + 1,
		      "Expected count %d, got %d",
		      ALERT_BURST_YIELD_THRESHOLD + 1, storm_tracker[0].count);

	get_time_mock = NULL;
	zassert_equal(mock_alert_count, 2,
		      "Alert handler should be called twice");
}

ZTEST_SUITE(usbc_intr_storm, drivers_predicate_pre_main, NULL,
	    usbc_intr_storm_before, usbc_intr_storm_after, NULL);
#endif /* TEST_BUILD */

void tcpc_shim_before_test(void *data)
{
	memcpy(tcpc_config_saved, tcpc_config,
	       sizeof(struct tcpc_config_t) * ARRAY_SIZE(tcpc_config_saved));
}

void tcpc_shim_after_test(void *data)
{
	memcpy(tcpc_config, tcpc_config_saved,
	       sizeof(struct tcpc_config_t) * ARRAY_SIZE(tcpc_config_saved));
}

ZTEST_SUITE(tcpc_shim, NULL, NULL, tcpc_shim_before_test, tcpc_shim_after_test,
	    NULL);
