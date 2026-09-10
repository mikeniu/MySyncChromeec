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
#include <zephyr/ztest.h>

#define IT52XX_NODE DT_NODELABEL(pdc_emul1)
#define IT52XX_NODE2 DT_NODELABEL(pdc_emul2)

static const struct emul *emul = EMUL_DT_GET(IT52XX_NODE);
static const struct emul *emul2 = EMUL_DT_GET(IT52XX_NODE2);
static const struct device *dev = DEVICE_DT_GET(IT52XX_NODE);
static const struct device *dev2 = DEVICE_DT_GET(IT52XX_NODE2);

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

ZTEST(it52xx, test_driver_initialization)
{
	zassert_true(device_is_ready(dev));
	zassert_true(device_is_ready(dev2));

	pdc_start_thread(dev);
	pdc_start_thread(dev2);

	check_initialization(emul, dev);
	check_initialization(emul2, dev2);
}

ZTEST_SUITE(it52xx, NULL, NULL, NULL, NULL, NULL);
