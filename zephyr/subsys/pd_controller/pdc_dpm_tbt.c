/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "usb_pd.h"
#include "usbc/pdc_dpm.h"
#include "usbc/pdc_power_mgmt.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>

LOG_MODULE_REGISTER(pdc_dpm_tbt);

#define PDC_TBT_RESET_ONGOING_DELAY_MS 5000

static atomic_t tbt_usb4_reset_pending;
static atomic_t tbt_usb4_reset_ongoing;

static void dpm_tbt_usb4_clear_work_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(tbt_usb4_clear_work,
			       dpm_tbt_usb4_clear_work_handler);

void pdc_dpm_tbt_eval_sink_pdo(int port, uint32_t vsafe5v_pdo)
{
	if (atomic_test_bit(&tbt_usb4_reset_ongoing, port))
		return;

	/* Check if partner is Sink Only (non-DRP) */
	if (!(vsafe5v_pdo & PDO_FIXED_GET_DRP))
		atomic_set_bit(&tbt_usb4_reset_pending, port);
}

void pdc_dpm_tbt_check_reset(int port)
{
	if (atomic_test_bit(&tbt_usb4_reset_ongoing, port))
		return;

	if (atomic_test_bit(&tbt_usb4_reset_pending, port)) {
		LOG_INF("DPM C%d: Hard Reset after Src Cap update", port);
		pdc_power_mgmt_request_tbt_reset(port);
	}
}

void pdc_dpm_tbt_set_reset_ongoing(int port)
{
	atomic_set_bit(&tbt_usb4_reset_ongoing, port);
	atomic_clear_bit(&tbt_usb4_reset_pending, port);

	/* After setting the reset ongoing flag, unconditionally wait for
	 * PDC_TBT_RESET_ONGOING_DELAY_MS before clearing to prevent looping.
	 */
	k_work_schedule(&tbt_usb4_clear_work,
			K_MSEC(PDC_TBT_RESET_ONGOING_DELAY_MS));
}

void pdc_dpm_tbt_clear_port(int port)
{
	atomic_clear_bit(&tbt_usb4_reset_pending, port);
}

static void dpm_tbt_usb4_clear_work_handler(struct k_work *work)
{
	int port;

	/* Clear bits for all ports. This allows the DPM to use a single work
	 * handler to manage TBT reset instead of one for each port. It is
	 * possible for this to clear a pending reset after another port issues
	 * a hard reset, but that will only impact systems with 2+ 3A ports.
	 */
	for (port = 0; port < pdc_power_mgmt_get_usb_pd_port_count(); port++) {
		atomic_clear_bit(&tbt_usb4_reset_pending, port);
		atomic_clear_bit(&tbt_usb4_reset_ongoing, port);
	}
}
