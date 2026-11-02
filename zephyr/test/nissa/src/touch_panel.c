/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Unit tests for program/nissa/src/touch_panel.c.
 */

#include "cros_cbi.h"
#include "gpio/gpio_int.h"
#include "gpio_signal.h"
#include "hooks.h"

#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/fff.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include <ap_power/ap_power.h>
#include <ap_power/ap_power_events.h>

FAKE_VOID_FUNC(usb_interrupt_c1, enum gpio_signal);

#define TEST_DELAY_MS 1

#if DT_NODE_EXISTS(DT_NODELABEL(gpio_ec_touch_en))

#define TOUCH_ENABLE_DELAY_MS (500 + TEST_DELAY_MS)
#define TOUCH_DISABLE_DELAY_MS (0 + TEST_DELAY_MS)

void soc_edp_bl_interrupt(enum gpio_signal signal);

#if DT_NODE_EXISTS(DT_PATH(nissa_fw_config, touch_en))
FAKE_VALUE_FUNC(int, cros_cbi_get_fw_config, enum cbi_fw_config_field_id,
		uint32_t *);

static bool cbi_touch_en;
static bool cbi_read_fail;

static int cbi_get_touch_en_config(enum cbi_fw_config_field_id field,
				   uint32_t *value)
{
	if (field != FW_TOUCH_EN)
		return -EINVAL;

	if (cbi_read_fail)
		return -1;

	*value = cbi_touch_en ? FW_TOUCH_EN_ENABLE : FW_TOUCH_EN_DISABLE;
	return 0;
}
#endif /* DT_NODE_EXISTS(DT_PATH(nissa_fw_config, touch_en)) */

static void touch_panel_before(void *fixture)
{
	const struct gpio_dt_spec *bl_en =
		GPIO_DT_FROM_NODELABEL(gpio_soc_edp_bl_en);
	const struct gpio_dt_spec *touch_en =
		GPIO_DT_FROM_NODELABEL(gpio_ec_touch_en);

	ARG_UNUSED(fixture);

	gpio_disable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_soc_edp_bl_en));
	gpio_emul_input_set(bl_en->port, bl_en->pin, 0);
	gpio_pin_set_dt(touch_en, 0);

#if DT_NODE_EXISTS(DT_PATH(nissa_fw_config, touch_en))
	RESET_FAKE(cros_cbi_get_fw_config);
	cbi_touch_en = true;
	cbi_read_fail = false;
	cros_cbi_get_fw_config_fake.custom_fake = cbi_get_touch_en_config;
#endif
}

ZTEST_SUITE(nissa_touch_panel, NULL, NULL, touch_panel_before, NULL, NULL);

ZTEST(nissa_touch_panel, test_touch_enable)
{
	const struct gpio_dt_spec *bl_en =
		GPIO_DT_FROM_NODELABEL(gpio_soc_edp_bl_en);
	const struct gpio_dt_spec *touch_en =
		GPIO_DT_FROM_NODELABEL(gpio_ec_touch_en);

	hook_notify(HOOK_INIT);

	/* Unrelated interrupt signal is ignored */
	soc_edp_bl_interrupt(GPIO_SIGNAL(DT_NODELABEL(gpio_ec_touch_en)));
	zassert_equal(gpio_emul_output_get(touch_en->port, touch_en->pin), 0);

	/* touch_en becomes high after TOUCH_ENABLE_DELAY_MS delay */
	zassert_ok(gpio_emul_input_set(bl_en->port, bl_en->pin, 1));
	zassert_equal(gpio_emul_output_get(touch_en->port, touch_en->pin), 0);

	k_sleep(K_MSEC(TOUCH_ENABLE_DELAY_MS));
	zassert_equal(gpio_emul_output_get(touch_en->port, touch_en->pin), 1);

	/* touch_en becomes low after TOUCH_DISABLE_DELAY_MS delay */
	zassert_ok(gpio_emul_input_set(bl_en->port, bl_en->pin, 0));
	k_sleep(K_MSEC(TOUCH_DISABLE_DELAY_MS));
	zassert_equal(gpio_emul_output_get(touch_en->port, touch_en->pin), 0);

#if DT_NODE_EXISTS(DT_PATH(nissa_fw_config, touch_en))
	/* touch_en stays low if fw_config is not enabled */
	cbi_touch_en = false;
	gpio_disable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_soc_edp_bl_en));
	hook_notify(HOOK_INIT);

	zassert_ok(gpio_emul_input_set(bl_en->port, bl_en->pin, 1));
	zassert_equal(gpio_emul_output_get(touch_en->port, touch_en->pin), 0);

	k_sleep(K_MSEC(TOUCH_ENABLE_DELAY_MS));
	zassert_equal(gpio_emul_output_get(touch_en->port, touch_en->pin), 0);

	/* touch_en stays low if fw_config read fails */
	cbi_read_fail = true;
	gpio_disable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_soc_edp_bl_en));
	hook_notify(HOOK_INIT);

	zassert_ok(gpio_emul_input_set(bl_en->port, bl_en->pin, 0));
	zassert_equal(gpio_emul_output_get(touch_en->port, touch_en->pin), 0);

	zassert_ok(gpio_emul_input_set(bl_en->port, bl_en->pin, 1));
	zassert_equal(gpio_emul_output_get(touch_en->port, touch_en->pin), 0);

	k_sleep(K_MSEC(TOUCH_ENABLE_DELAY_MS));
	zassert_equal(gpio_emul_output_get(touch_en->port, touch_en->pin), 0);
#endif /* DT_NODE_EXISTS(DT_PATH(nissa_fw_config, touch_en)) */
}

#elif DT_NODE_EXISTS(DT_NODELABEL(gpio_tp_vcc_od))

#define TP_ENABLE_DELAY_MS (30 + TEST_DELAY_MS)
#define BL_DISABLE_DELAY_MS (102 + TEST_DELAY_MS)

#if DT_NODE_EXISTS(DT_NODELABEL(gpio_tchscr_report_en))
#define TP_EN_GPIO GPIO_DT_FROM_NODELABEL(gpio_tchscr_report_en)
#else
#define TP_EN_GPIO GPIO_DT_FROM_NODELABEL(gpio_tp_en_od)
#endif

static void touch_panel_before(void *fixture)
{
	const struct gpio_dt_spec *ap_bl_en =
		GPIO_DT_FROM_NODELABEL(gpio_ap_bl_en_odl);
	const struct gpio_dt_spec *ec_bl_en =
		GPIO_DT_FROM_NODELABEL(gpio_ec_edp_bl_en_od);
	const struct gpio_dt_spec *tp_en = TP_EN_GPIO;
	const struct gpio_dt_spec *tp_vcc =
		GPIO_DT_FROM_NODELABEL(gpio_tp_vcc_od);

	ARG_UNUSED(fixture);

	gpio_disable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_ap_bl_en));
	gpio_emul_input_set(ap_bl_en->port, ap_bl_en->pin, 0);
	gpio_pin_set_dt(ec_bl_en, 0);
	gpio_pin_set_dt(tp_en, 0);
	gpio_pin_set_dt(tp_vcc, 0);
}

ZTEST_SUITE(nissa_touch_panel, NULL, NULL, touch_panel_before, NULL, NULL);

ZTEST(nissa_touch_panel, test_ap_bl_en_interrupt)
{
	const struct gpio_dt_spec *ap_bl_en =
		GPIO_DT_FROM_NODELABEL(gpio_ap_bl_en_odl);
	const struct gpio_dt_spec *ec_bl_en =
		GPIO_DT_FROM_NODELABEL(gpio_ec_edp_bl_en_od);
	const struct gpio_dt_spec *tp_en = TP_EN_GPIO;

	hook_notify(HOOK_INIT);

	/* When AP backlight enable goes high:
	 * - EC backlight enable goes high immediately (0 ms deferred)
	 * - Touch panel enable goes high after 30 ms
	 */
	zassert_ok(gpio_emul_input_set(ap_bl_en->port, ap_bl_en->pin, 1));
	k_sleep(K_MSEC(TEST_DELAY_MS));
	zassert_equal(gpio_emul_output_get(ec_bl_en->port, ec_bl_en->pin), 1);
	zassert_equal(gpio_emul_output_get(tp_en->port, tp_en->pin), 0);

	k_sleep(K_MSEC(TP_ENABLE_DELAY_MS));
	zassert_equal(gpio_emul_output_get(tp_en->port, tp_en->pin), 1);

	/* When AP backlight enable goes low:
	 * - Touch panel enable goes low immediately (0 ms deferred)
	 * - EC backlight enable goes low after 102 ms
	 */
	zassert_ok(gpio_emul_input_set(ap_bl_en->port, ap_bl_en->pin, 0));
	k_sleep(K_MSEC(TEST_DELAY_MS));
	zassert_equal(gpio_emul_output_get(tp_en->port, tp_en->pin), 0);
	zassert_equal(gpio_emul_output_get(ec_bl_en->port, ec_bl_en->pin), 1);

	k_sleep(K_MSEC(BL_DISABLE_DELAY_MS));
	zassert_equal(gpio_emul_output_get(ec_bl_en->port, ec_bl_en->pin), 0);
}

ZTEST(nissa_touch_panel, test_board_backlight_handler)
{
	const struct gpio_dt_spec *tp_vcc =
		GPIO_DT_FROM_NODELABEL(gpio_tp_vcc_od);

	/* Resume sets TP VCC high */
	ap_power_ev_send_callbacks(AP_POWER_RESUME);
	zassert_equal(gpio_emul_output_get(tp_vcc->port, tp_vcc->pin), 1);

	/* Unhandled event value leaves TP VCC unchanged */
	ap_power_ev_send_callbacks(AP_POWER_RESUME | AP_POWER_PRE_INIT);
	zassert_equal(gpio_emul_output_get(tp_vcc->port, tp_vcc->pin), 1);

	/* Suspend sets TP VCC low */
	ap_power_ev_send_callbacks(AP_POWER_SUSPEND);
	zassert_equal(gpio_emul_output_get(tp_vcc->port, tp_vcc->pin), 0);

	/* Resume again, then Shutdown sets TP VCC low */
	ap_power_ev_send_callbacks(AP_POWER_RESUME);
	zassert_equal(gpio_emul_output_get(tp_vcc->port, tp_vcc->pin), 1);

	ap_power_ev_send_callbacks(AP_POWER_SHUTDOWN);
	zassert_equal(gpio_emul_output_get(tp_vcc->port, tp_vcc->pin), 0);
}

#endif /* DT_NODE_EXISTS(DT_NODELABEL(gpio_tp_vcc_od)) */
