/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ap_power/ap_power.h"
#include "ap_power/ap_power_events.h"
#include "chipset.h"
#include "cros_board_info.h"
#include "cros_cbi.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "power.h"
#include "rauru_dp.h"
#include "test_state.h"
#include "typec_control.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usb_pd_dp_hpd_gpio.h"

#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/fff.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

/* Forward declarations */
void hdmi_hpd_interrupt(enum gpio_signal signal);

/* FFF fakes */
FAKE_VALUE_FUNC(int, cros_cbi_get_fw_config, enum cbi_fw_config_field_id,
		uint32_t *);
FAKE_VOID_FUNC(pd_notify_dp_alt_mode_entry, int);

/* Test exports from hdmi.c */
extern bool hdmi_init;
extern bool rauru_has_hdmi;
extern void hdmi_hpd_low_deferred(void);
extern const struct deferred_data hdmi_hpd_low_deferred_data;

static int cbi_not_present(enum cbi_fw_config_field_id id, uint32_t *val)
{
	*val = FW_HDMI_NOT_PRESENT;
	return 0;
}

static int cbi_present(enum cbi_fw_config_field_id id, uint32_t *val)
{
	*val = FW_HDMI_PRESENT;
	return 0;
}

static int cbi_unknown(enum cbi_fw_config_field_id id, uint32_t *val)
{
	*val = 0xFF;
	return 0;
}

static void test_dp_reset(void *fixture)
{
	ARG_UNUSED(fixture);

	RESET_FAKE(cros_cbi_get_fw_config);
	RESET_FAKE(pd_notify_dp_alt_mode_entry);
	FFF_RESET_HISTORY();

	/* Set deterministic fake so rauru_has_hdmi_port() does not read
	 * uninitialized memory */
	cros_cbi_get_fw_config_fake.custom_fake = cbi_not_present;

	gpio_emul_input_set(GPIO_DT_FROM_NODELABEL(gpio_hdmi_ec_hpd)->port,
			    GPIO_DT_FROM_NODELABEL(gpio_hdmi_ec_hpd)->pin, 0);

	/* Reset power state to S0 */
	power_set_state(POWER_S0);

	/* Reset ports */
	for (int i = 0; i < board_get_usb_pd_port_count(); i++) {
		dp_flags[i] = 0;
		dp_status[i] = 0;
		svdm_exit_dp_mode(i);
	}
	rauru_set_dp_path(DP_PORT_NONE);

	/* Reset HDMI cache after exit loop so subsequent tests start
	 * uninitialized */
	hdmi_init = false;
	rauru_has_hdmi = false;

	/* Default HPD GPIO output to 1 (inactive, active low) */
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_ap_dp_hpd_l), 1);
}

/* ========================================================================= */
/* hdmi.c Unit Tests                                                         */
/* ========================================================================= */

ZTEST(dp_alt_mode_test, test_hdmi_fw_config_branches)
{
	/* 1. CBI read error branch */
	cros_cbi_get_fw_config_fake.custom_fake = NULL;
	cros_cbi_get_fw_config_fake.return_val = -1;
	hdmi_init = false;
	rauru_has_hdmi = false;
	zassert_false(rauru_has_hdmi_port());
	zassert_true(hdmi_init);

	/* 2. Cached branch */
	rauru_has_hdmi = true;
	zassert_true(rauru_has_hdmi_port());

	/* 3. HDMI not present branch */
	hdmi_init = false;
	rauru_has_hdmi = false;
	cros_cbi_get_fw_config_fake.return_val = 0;
	cros_cbi_get_fw_config_fake.custom_fake = cbi_not_present;
	zassert_false(rauru_has_hdmi_port());

	/* 4. HDMI present branch */
	hdmi_init = false;
	cros_cbi_get_fw_config_fake.custom_fake = cbi_present;
	zassert_true(rauru_has_hdmi_port());

	/* 5. HDMI unknown branch */
	hdmi_init = false;
	rauru_has_hdmi = false;
	cros_cbi_get_fw_config_fake.custom_fake = cbi_unknown;
	zassert_false(rauru_has_hdmi_port());
}

ZTEST(dp_alt_mode_test, test_hdmi_hpd_interrupt_flow)
{
	const struct gpio_dt_spec *hdmi_hpd =
		GPIO_DT_FROM_NODELABEL(gpio_hdmi_ec_hpd);
	const struct gpio_dt_spec *ec_ap_hpd_l =
		GPIO_DT_FROM_NODELABEL(gpio_ec_ap_dp_hpd_l);

	/* Set up HDMI present */
	hdmi_init = true;
	rauru_has_hdmi = true;

	/* Verify not muxable when another port (e.g. C0) is already active */
	rauru_set_dp_path(DP_PORT_C0);
	zassert_false(rauru_is_dp_muxable(DP_PORT_HDMI));
	gpio_emul_input_set(hdmi_hpd->port, hdmi_hpd->pin, 1);
	hdmi_hpd_interrupt(0);
	/* Path should still be C0 */
	zassert_equal(rauru_get_dp_path(), DP_PORT_C0);

	/* Reset to NONE so HDMI is muxable */
	rauru_set_dp_path(DP_PORT_NONE);
	zassert_true(rauru_is_dp_muxable(DP_PORT_HDMI));

	/* HPD High flow */
	gpio_emul_input_set(hdmi_hpd->port, hdmi_hpd->pin, 1);
	hdmi_hpd_interrupt(0);
	zassert_equal(rauru_get_dp_path(), DP_PORT_HDMI);
	/* ec_ap_dp_hpd_l is low active, so hpd=1 means level 0 on output */
	zassert_equal(gpio_emul_output_get(ec_ap_hpd_l->port, ec_ap_hpd_l->pin),
		      0);

	/* HPD Low flow */
	gpio_emul_input_set(hdmi_hpd->port, hdmi_hpd->pin, 0);
	hdmi_hpd_interrupt(0);
	/* Output pin should become high (inactive) */
	zassert_equal(gpio_emul_output_get(ec_ap_hpd_l->port, ec_ap_hpd_l->pin),
		      1);

	/* Deferred low handler runs when HPD is still low -> detaches */
	hdmi_hpd_low_deferred();
	zassert_equal(rauru_get_dp_path(), DP_PORT_NONE);

	/* Deferred low handler runs when HPD is high -> does NOT detach */
	rauru_set_dp_path(DP_PORT_HDMI);
	gpio_emul_input_set(hdmi_hpd->port, hdmi_hpd->pin, 1);
	hdmi_hpd_low_deferred();
	zassert_equal(rauru_get_dp_path(), DP_PORT_HDMI);

	/* Cancel any dangling deferred timer scheduled by HPD low */
	hook_call_deferred(&hdmi_hpd_low_deferred_data, -1);
}

ZTEST(dp_alt_mode_test, test_board_hdmi_suspend_resume)
{
	const struct gpio_dt_spec *hdmi_pwr =
		GPIO_DT_FROM_ALIAS(gpio_en_hdmi_pwr);

	/* 1. When HDMI is not supported, callbacks do nothing */
	hdmi_init = true;
	rauru_has_hdmi = false;
	gpio_pin_set_dt(hdmi_pwr, 0);
	ap_power_ev_send_callbacks(AP_POWER_RESUME);
	zassert_equal(gpio_emul_output_get(hdmi_pwr->port, hdmi_pwr->pin), 0);

	/* 2. When HDMI is supported */
	rauru_has_hdmi = true;

	/* Resume: sets en_hdmi_pwr to 1 and enables interrupt */
	ap_power_ev_send_callbacks(AP_POWER_RESUME);
	zassert_equal(gpio_emul_output_get(hdmi_pwr->port, hdmi_pwr->pin), 1);

	/* Suspend: sets en_hdmi_pwr to 0 and disables interrupt */
	ap_power_ev_send_callbacks(AP_POWER_SUSPEND);
	zassert_equal(gpio_emul_output_get(hdmi_pwr->port, hdmi_pwr->pin), 0);

	/* Non-handled event (e.g. AP_POWER_RESET) should do nothing */
	ap_power_ev_send_callbacks(AP_POWER_RESET);
}

/* ========================================================================= */
/* dp_alt_mode.c Unit Tests                                                  */
/* ========================================================================= */

ZTEST(dp_alt_mode_test, test_dp_path_and_gpio)
{
	const struct gpio_dt_spec *c1_en =
		GPIO_DT_FROM_NODELABEL(gpio_dp_path_usb_c1_en);
	const struct gpio_dt_spec *hdmi_en =
		GPIO_DT_FROM_NODELABEL(gpio_dp_path_hdmi_en);
	const struct gpio_dt_spec *c0_hpd =
		GPIO_DT_FROM_NODELABEL(gpio_usb_c0_dp_in_hpd);
	const struct gpio_dt_spec *c1_hpd =
		GPIO_DT_FROM_NODELABEL(gpio_usb_c1_dp_in_hpd);
	const struct gpio_dt_spec *ap_hpd_l =
		GPIO_DT_FROM_NODELABEL(gpio_ec_ap_dp_hpd_l);

	/* Initially NONE */
	rauru_set_dp_path(DP_PORT_NONE);
	zassert_equal(rauru_get_dp_path(), DP_PORT_NONE);
	zassert_equal(gpio_emul_output_get(c1_en->port, c1_en->pin), 0);
	zassert_equal(gpio_emul_output_get(hdmi_en->port, hdmi_en->pin), 0);
	zassert_equal(gpio_emul_output_get(c0_hpd->port, c0_hpd->pin), 0);
	zassert_equal(gpio_emul_output_get(c1_hpd->port, c1_hpd->pin), 0);

	/* Setting same path is a no-op */
	rauru_set_dp_path(DP_PORT_NONE);

	/* Path C1 */
	rauru_set_dp_path(DP_PORT_C1);
	zassert_equal(rauru_get_dp_path(), DP_PORT_C1);
	zassert_equal(gpio_emul_output_get(c1_en->port, c1_en->pin), 1);
	zassert_equal(gpio_emul_output_get(hdmi_en->port, hdmi_en->pin), 0);
	zassert_equal(gpio_emul_output_get(c0_hpd->port, c0_hpd->pin), 0);
	zassert_equal(gpio_emul_output_get(c1_hpd->port, c1_hpd->pin), 1);

	/* Path HDMI */
	rauru_set_dp_path(DP_PORT_HDMI);
	zassert_equal(rauru_get_dp_path(), DP_PORT_HDMI);
	zassert_equal(gpio_emul_output_get(c1_en->port, c1_en->pin), 0);
	zassert_equal(gpio_emul_output_get(hdmi_en->port, hdmi_en->pin), 1);
	zassert_equal(gpio_emul_output_get(c0_hpd->port, c0_hpd->pin), 0);
	zassert_equal(gpio_emul_output_get(c1_hpd->port, c1_hpd->pin), 0);

	/* Path C0 */
	rauru_set_dp_path(DP_PORT_C0);
	zassert_equal(rauru_get_dp_path(), DP_PORT_C0);
	zassert_equal(gpio_emul_output_get(c1_en->port, c1_en->pin), 0);
	zassert_equal(gpio_emul_output_get(hdmi_en->port, hdmi_en->pin), 0);
	zassert_equal(gpio_emul_output_get(c0_hpd->port, c0_hpd->pin), 1);
	zassert_equal(gpio_emul_output_get(c1_hpd->port, c1_hpd->pin), 0);

	/* svdm_set_hpd_gpio for inactive port does nothing */
	svdm_set_hpd_gpio(DP_PORT_C1, 1);
	zassert_equal(svdm_get_hpd_gpio(DP_PORT_C0), 0);

	/* svdm_set_hpd_gpio for active port */
	svdm_set_hpd_gpio(DP_PORT_C0, 1);
	zassert_equal(gpio_emul_output_get(ap_hpd_l->port, ap_hpd_l->pin), 0);
	zassert_equal(svdm_get_hpd_gpio(DP_PORT_C0), 1);

	svdm_set_hpd_gpio(DP_PORT_C0, 0);
	zassert_equal(gpio_emul_output_get(ap_hpd_l->port, ap_hpd_l->pin), 1);
	zassert_equal(svdm_get_hpd_gpio(DP_PORT_C0), 0);
}

ZTEST(dp_alt_mode_test, test_rauru_is_hpd_high)
{
	const struct gpio_dt_spec *hdmi_hpd =
		GPIO_DT_FROM_NODELABEL(gpio_hdmi_ec_hpd);

	/* HDMI port check */
	gpio_emul_input_set(hdmi_hpd->port, hdmi_hpd->pin, 1);
	zassert_true(rauru_is_hpd_high(DP_PORT_HDMI));

	gpio_emul_input_set(hdmi_hpd->port, hdmi_hpd->pin, 0);
	zassert_false(rauru_is_hpd_high(DP_PORT_HDMI));

	/* Type-C port checks via dp_status */
	dp_status[0] = 0;
	zassert_false(rauru_is_hpd_high(DP_PORT_C0));

	dp_status[0] = BIT(7); /* HPD_LVL bit */
	zassert_true(rauru_is_hpd_high(DP_PORT_C0));
}

ZTEST(dp_alt_mode_test, test_rauru_detach_and_rotate)
{
	const struct gpio_dt_spec *hdmi_hpd =
		GPIO_DT_FROM_NODELABEL(gpio_hdmi_ec_hpd);

	hdmi_init = true;
	rauru_has_hdmi = true;

	/* Detach non-active port does nothing */
	rauru_set_dp_path(DP_PORT_C0);
	rauru_detach_dp_path(DP_PORT_C1);
	zassert_equal(rauru_get_dp_path(), DP_PORT_C0);

	/* Rotate to HDMI if HDMI HPD is high */
	gpio_emul_input_set(hdmi_hpd->port, hdmi_hpd->pin, 1);
	rauru_detach_dp_path(DP_PORT_C0);
	zassert_equal(rauru_get_dp_path(), DP_PORT_HDMI);

	/* Detach HDMI: if C1 has HPD high, rotates to C1 */
	gpio_emul_input_set(hdmi_hpd->port, hdmi_hpd->pin, 0);
	dp_status[1] = BIT(7); /* C1 HPD high */
	dp_status[0] = 0;
	rauru_detach_dp_path(DP_PORT_HDMI);
	zassert_equal(rauru_get_dp_path(), DP_PORT_C1);

	/* Detach C1: if no other port active, path becomes NONE */
	dp_status[1] = 0;
	rauru_detach_dp_path(DP_PORT_C1);
	zassert_equal(rauru_get_dp_path(), DP_PORT_NONE);
}

ZTEST(dp_alt_mode_test, test_svdm_dp_post_config)
{
	dp_status[0] = 0;
	dp_status[1] = 0;

	/* 1. When port == active_dp_port */
	rauru_set_dp_path(DP_PORT_C0);
	svdm_dp_post_config(0);
	zassert_true(dp_flags[0] & DP_FLAGS_DP_ON);

	/* 2. When port != active_dp_port */
	svdm_dp_post_config(1);
	zassert_true(dp_flags[1] & DP_FLAGS_DP_ON);
}

ZTEST(dp_alt_mode_test, test_svdm_dp_attention)
{
	uint32_t payload[2];

	/* 1. Attention when port is not muxable */
	rauru_set_dp_path(DP_PORT_C0);
	payload[0] = 0;
	payload[1] = BIT(7); /* HPD_LVL = 1 */
	zassert_equal(svdm_dp_attention(1, payload), 0);

	/* 2. Level high, IRQ 0 in S0 */
	power_set_state(POWER_S0);
	rauru_set_dp_path(DP_PORT_NONE);
	payload[1] = BIT(7); /* lvl=1, irq=0 */
	zassert_equal(svdm_dp_attention(0, payload), 1);
	zassert_equal(rauru_get_dp_path(), DP_PORT_C0);
	zassert_equal(pd_notify_dp_alt_mode_entry_fake.call_count, 0);

	/* 3. Level high, IRQ 1 in Suspend (wakes AP) */
	power_set_state(POWER_S3);
	payload[1] = BIT(7) | BIT(8); /* lvl=1, irq=1 */
	zassert_equal(svdm_dp_attention(0, payload), 1);
	zassert_equal(pd_notify_dp_alt_mode_entry_fake.call_count, 1);

	/* 4. Level low (lvl=0, irq=0) in S0 - rotates to C1 since C1 HPD was
	 * high */
	power_set_state(POWER_S0);
	payload[1] = 0;
	zassert_equal(svdm_dp_attention(0, payload), 1);
	zassert_equal(rauru_get_dp_path(), DP_PORT_C1);

	/* 5. Level low on C1 - now detaches to DP_PORT_NONE */
	zassert_equal(svdm_dp_attention(1, payload), 1);
	zassert_equal(rauru_get_dp_path(), DP_PORT_NONE);

	/* 6. Invalid dp_hpd_gpio_set (lvl=0, irq=1 fails with EC_ERROR_INVAL)
	 */
	rauru_set_dp_path(DP_PORT_C0);
	payload[1] = BIT(8); /* irq=1, lvl=0 */
	zassert_equal(svdm_dp_attention(0, payload), 0);
}

ZTEST(dp_alt_mode_test, test_svdm_exit_dp_mode)
{
	rauru_set_dp_path(DP_PORT_C0);
	dp_flags[0] = DP_FLAGS_DP_ON;
	dp_status[0] = BIT(7);

	svdm_exit_dp_mode(0);

	zassert_equal(dp_flags[0], 0);
	zassert_equal(dp_status[0], 0);
	zassert_equal(rauru_get_dp_path(), DP_PORT_NONE);
}

ZTEST_SUITE(dp_alt_mode_test, rauru_predicate_post_main, NULL, test_dp_reset,
	    test_dp_reset, NULL);
