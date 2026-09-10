/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "drivers/pdc.h"
#include "drivers/ucsi_v3.h"
#include "emul/emul_ite_it52xx.h"
#include "emul/emul_pdc.h"

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/ztest.h>

#define IT52XX_NODE DT_NODELABEL(pdc_emul1)
#define IT52XX_NODE2 DT_NODELABEL(pdc_emul2)

static const struct emul *emul = EMUL_DT_GET(IT52XX_NODE);
static const struct emul *emul2 = EMUL_DT_GET(IT52XX_NODE2);
static const struct device *dev = DEVICE_DT_GET(IT52XX_NODE);
static const struct device *dev2 = DEVICE_DT_GET(IT52XX_NODE2);

#define PDC_COMMAND_COMPLETE BIT(0)
#define PDC_OPERATION_TIMEOUT K_SECONDS(2)

static K_EVENT_DEFINE(pdc_event);
static atomic_t waiting_for_command_response;
static const struct device *callback_dev;
static union cci_event_t callback_cci;

/* Unit-test callback for CC events from the driver */
static void pdc_cc_handler_cb(const struct device *pdc,
			      const struct pdc_callback *callback,
			      union cci_event_t cci_event)
{
	ARG_UNUSED(callback);

	if (!cci_event.command_completed ||
	    !atomic_cas(&waiting_for_command_response, true, false)) {
		return;
	}

	callback_dev = pdc;
	callback_cci = cci_event;
	k_event_post(&pdc_event, PDC_COMMAND_COMPLETE);
}

static struct pdc_callback cc_callback = {
	.handler = pdc_cc_handler_cb,
};

static void prepare_for_command(void)
{
	k_event_set(&pdc_event, 0);
	atomic_set(&waiting_for_command_response, true);
}

static void wait_for_command_callback(const struct device *pdc,
				      uint8_t data_len, bool error,
				      bool not_supported)
{
	uint32_t events = k_event_wait_safe(&pdc_event, PDC_COMMAND_COMPLETE,
					    false, PDC_OPERATION_TIMEOUT);

	zassert_equal(events, PDC_COMMAND_COMPLETE);
	zassert_equal(callback_dev, pdc);
	zassert_true(callback_cci.command_completed);
	zassert_equal(callback_cci.error, error);
	zassert_equal(callback_cci.not_supported, not_supported);
	zassert_equal(callback_cci.data_len, data_len);
}

static void wait_for_command_result(const struct emul *target,
				    const struct device *pdc, uint8_t data_len,
				    bool error, bool not_supported)
{
	wait_for_command_callback(pdc, data_len, error, not_supported);
	zassert_ok(emul_pdc_idle_wait(target));
}

static void wait_for_command(const struct emul *target,
			     const struct device *pdc, uint8_t data_len)
{
	wait_for_command_result(target, pdc, data_len, false, false);
}

static void check_initialization(const struct emul *target,
				 const struct device *pdc)
{
	union notification_enable_t expected = { 0 };
	union notification_enable_t actual;
	struct pdc_info_t info;
	bool initialized = false;

	for (int i = 0; i < 200; ++i) {
		if (pdc_is_init_done(pdc) &&
		    emul_it52xx_init_complete(target)) {
			initialized = true;
			break;
		}
		k_msleep(10);
	}

	zassert_true(initialized);
	zassert_ok(emul_pdc_idle_wait(target));
	zassert_true(pdc_is_init_done(pdc));
	zassert_true(emul_it52xx_init_complete(target));

	/* Verify get notification fields */
	expected.command_completed = 1;
	expected.external_supply_change = 1;
	expected.power_operation_mode_change = 1;
	expected.attention = 1;
	expected.provider_capability_change_supported = 1;
	expected.negotiated_power_level_change = 1;
	expected.pd_reset_complete = 1;
	expected.support_cam_change = 1;
	expected.battery_charging_status_change = 1;
	expected.connector_partner_change = 1;
	expected.power_direction_change = 1;
	expected.connect_change = 1;
	expected.error = 1;
	expected.sink_path_status_change = 1;

	zassert_ok(emul_it52xx_get_notification_enable(target, &actual));
	zassert_equal(actual.raw_value, expected.raw_value);

	/* Verify get PDC info fields */
	zassert_ok(pdc_get_info(pdc, &info, false));
	zassert_equal(info.fw_version, 0x010200);
	zassert_equal(info.pd_revision, 0x0302);
	zassert_equal(info.pd_version, 0x0101);
	zassert_equal(info.vid, 0x048d);
	zassert_equal(info.pid, 0x5271);
	zassert_equal(info.is_running_flash_code, 1);
	zassert_equal(info.running_in_flash_bank, 1);
	zassert_equal(strcmp(info.project_name, "GOOGLE"), 0);
}

static void *it52xx_setup(void)
{
	zassert_true(device_is_ready(dev));
	zassert_true(device_is_ready(dev2));

	pdc_start_thread(dev);
	pdc_start_thread(dev2);

	check_initialization(emul, dev);
	check_initialization(emul2, dev2);

	pdc_set_cc_callback(dev, &cc_callback);
	pdc_set_cc_callback(dev2, &cc_callback);

	return NULL;
}

ZTEST(it52xx, test_driver_initialization)
{
	union notification_enable_t notifications;
	uint8_t command_specific[3];

	check_initialization(emul, dev);
	check_initialization(emul2, dev2);

	/* Verify notifications can be configured again after initialization. */
	zassert_ok(emul_it52xx_get_notification_enable(emul, &notifications));
	command_specific[0] = notifications.raw_value;
	command_specific[1] = notifications.raw_value >> 8;
	command_specific[2] = notifications.raw_value >> 16;
	prepare_for_command();
	zassert_ok(pdc_execute_ucsi_cmd(dev, UCSI_SET_NOTIFICATION_ENABLE,
					sizeof(command_specific),
					command_specific, NULL, &cc_callback));
	wait_for_command(emul, dev, 0);
}

ZTEST_SUITE(it52xx, NULL, it52xx_setup, NULL, NULL, NULL);
