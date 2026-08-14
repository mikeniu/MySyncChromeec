/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "charge_state.h"
#include "extpower.h"
#include "hooks.h"
#include "usb_pd.h"
#include "usbc/pdc_dpm.h"
#include "usbc/pdc_power_mgmt.h"

#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>

#include <drivers/pdc.h>
#include <usbc/utils.h>

LOG_MODULE_REGISTER(pdc_dpm);

/*
 * Source-out policy variables and APIs
 *
 * Priority for the available 3.0 A ports is given in the following order:
 * - sink partners which report requiring > 1.5 A in their Sink_Capabilities
 * - source partners with FRS that request 3.0A as a sink
 * - non-pd sink partners
 */

/*
 * Bitmasks of port numbers in each following category
 *
 * Note: request bitmasks should be accessed atomically as other ports may alter
 * them
 */
static uint32_t max_current_claimed;

/* Ports with PD sink needing > 1.5 A */
static atomic_t sink_max_pdo_requested;
/* Ports with FRS source needing > 1.5 A */
static atomic_t source_frs_max_requested;
/* Ports with non-PD sinks, so current requirements are unknown */
static atomic_t non_pd_sink_max_requested;

int pdc_dpm_get_source_current(const int port);

static void pdc_dpm_balance_source_ports(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(dpm_work, pdc_dpm_balance_source_ports);

static K_MUTEX_DEFINE(max_current_claimed_mtx);

#define LOWEST_PORT(p) __builtin_ctz(p) /* Undefined behavior if p == 0 */

#ifdef CONFIG_PDC_POWER_MGMT_SRC_THROTTLING
#define OVERLOAD_DEBOUNCE_SECONDS 3
#define OVERLOAD_CURRENT_THRESHOLD_MA 3000
#define BATTERY_IS_GOOD 0
#define BATTERY_IS_OVERLOADED 1
static atomic_t overload_status = ATOMIC_INIT(BATTERY_IS_GOOD);
static atomic_t overload_counter = ATOMIC_INIT(0);
#endif

static int count_port_bits(uint32_t bitmask)
{
	int i, total = 0;

	for (i = 0; i < pdc_power_mgmt_get_usb_pd_port_count(); i++) {
		if (bitmask & BIT(i))
			total++;
	}

	return total;
}

/**
 * @brief Adjust source current allocations for usbc ports
 *
 * This function is called when new port partners are either added or removed
 * that could affect how source current limits per port are allocated. The
 * number of ports capable of sourcing 3.0A current will be defined by
 * CONFIG_PLATFORM_EC_USB_PD_3A_PORTS.
 *
 * Note that this function is called both from a PDC thread when new ports or
 * added/removed and from the system workqueue when the current
 * limit for a port is being reduced.
 */
static void pdc_dpm_balance_source_ports(struct k_work *work)
{
	uint32_t removed_ports;
	uint32_t new_ports;
	enum usb_typec_current_t rp;
	int rv;

	rv = k_work_busy_get(&dpm_work.work);
	/* check if work is delayed work is pending */
	if (rv && (rv & K_WORK_DELAYED)) {
		return;
	}

	k_mutex_lock(&max_current_claimed_mtx, K_FOREVER);

	/* Remove any ports which no longer require 3.0 A */
	removed_ports = max_current_claimed &
			~(sink_max_pdo_requested | source_frs_max_requested |
			  non_pd_sink_max_requested);
	max_current_claimed &= ~removed_ports;

	/* Update current limit for removed ports */
	while (removed_ports) {
		int removed_port = LOWEST_PORT(removed_ports);
		rp = pdc_power_mgmt_get_default_current_limit(removed_port);
		pdc_power_mgmt_set_current_limit(removed_port, rp);
		removed_ports &= ~BIT(removed_port);
	}

#ifdef CONFIG_PDC_POWER_MGMT_SRC_THROTTLING
	if (atomic_get(&overload_status) == BATTERY_IS_OVERLOADED) {
		goto unlock;
	}
#endif

	/* Allocate 3.0 A to new PD sink ports that need it */
	new_ports = sink_max_pdo_requested & ~max_current_claimed;

	while (new_ports) {
		int new_max_port = LOWEST_PORT(new_ports);

		if (count_port_bits(max_current_claimed) <
		    pd_get_usb_pd_3a_ports()) {
			max_current_claimed |= BIT(new_max_port);
			pdc_power_mgmt_set_current_limit(new_max_port,
							 TC_CURRENT_3_0A);
			if (IS_ENABLED(CONFIG_USBC_PDC_TBT_SUPPORTED))
				pdc_dpm_tbt_check_reset(new_max_port);

		} else if (non_pd_sink_max_requested & max_current_claimed) {
			/* Always downgrade non-PD ports first */
			int rem_non_pd = LOWEST_PORT(non_pd_sink_max_requested &
						     max_current_claimed);

			rp = pdc_power_mgmt_get_default_current_limit(
				rem_non_pd);
			pdc_power_mgmt_set_current_limit(rem_non_pd, rp);
			max_current_claimed &= ~BIT(rem_non_pd);

			/* Wait tSinkAdj before using current */
			k_work_reschedule(&dpm_work, K_MSEC(75));
			goto unlock;
		} else if (source_frs_max_requested & max_current_claimed) {
			/* Downgrade lowest FRS port from 3.0 A slot */
			int rem_frs = LOWEST_PORT(source_frs_max_requested &
						  max_current_claimed);

			rp = pdc_power_mgmt_get_default_current_limit(rem_frs);
			pdc_power_mgmt_set_current_limit(rem_frs, rp);
			max_current_claimed &= ~BIT(rem_frs);

			/* Give 50 ms for the PD task to process DPM flag */
			k_work_reschedule(&dpm_work, K_MSEC(50));
			goto unlock;
		} else {
			/* No lower priority ports to downgrade */
			goto unlock;
		}
		new_ports &= ~BIT(new_max_port);
	}

	/* Allocate 3.0 A to any new FRS ports that need it */
	new_ports = source_frs_max_requested & ~max_current_claimed;
	while (new_ports) {
		int new_frs_port = LOWEST_PORT(new_ports);

		if (count_port_bits(max_current_claimed) <
		    pd_get_usb_pd_3a_ports()) {
			max_current_claimed |= BIT(new_frs_port);
			/* Allocate 3A for this port */
			pdc_power_mgmt_set_current_limit(new_frs_port,
							 TC_CURRENT_3_0A);
		} else if (non_pd_sink_max_requested & max_current_claimed) {
			int rem_non_pd = LOWEST_PORT(non_pd_sink_max_requested &
						     max_current_claimed);

			rp = pdc_power_mgmt_get_default_current_limit(
				rem_non_pd);
			pdc_power_mgmt_set_current_limit(rem_non_pd, rp);
			max_current_claimed &= ~BIT(rem_non_pd);

			/* Wait tSinkAdj before using current */
			k_work_reschedule(&dpm_work, K_MSEC(75));
			goto unlock;
		} else {
			/* No lower priority ports to downgrade */
			goto unlock;
		}
		new_ports &= ~BIT(new_frs_port);
	}

	/* Allocate 3.0 A to any non-PD ports which could need it */
	new_ports = non_pd_sink_max_requested & ~max_current_claimed;
	while (new_ports) {
		int new_max_port = LOWEST_PORT(new_ports);

		if (count_port_bits(max_current_claimed) <
		    pd_get_usb_pd_3a_ports()) {
			max_current_claimed |= BIT(new_max_port);
			pdc_power_mgmt_set_current_limit(new_max_port,
							 TC_CURRENT_3_0A);
		} else {
			/* No lower priority ports to downgrade */
			goto unlock;
		}
		new_ports &= ~BIT(new_max_port);
	}
unlock:
	k_mutex_unlock(&max_current_claimed_mtx);
}

/* Process port's first Sink_Capabilities PDO for port current consideration */
void pdc_dpm_eval_sink_fixed_pdo(int port, uint32_t vsafe5v_pdo)
{
	/* Verify partner supplied valid vSafe5V fixed object first */
	if ((vsafe5v_pdo & PDO_TYPE_MASK) != PDO_TYPE_FIXED)
		return;

	if (PDO_FIXED_VOLTAGE(vsafe5v_pdo) != 5000)
		return;

	if (pdc_power_mgmt_get_power_role(port) == PD_ROLE_SOURCE) {
		if (pd_get_usb_pd_3a_ports() == 0)
			return;

		/* Valid PDO to process, so evaluate whether >1.5A is needed */
		if (PDO_FIXED_CURRENT(vsafe5v_pdo) <= 1500)
			return;

		atomic_set_bit(&sink_max_pdo_requested, port);
		if (IS_ENABLED(CONFIG_USBC_PDC_TBT_SUPPORTED))
			pdc_dpm_tbt_eval_sink_pdo(port, vsafe5v_pdo);
	} else {
		int frs_current = vsafe5v_pdo & PDO_FIXED_FRS_CURR_MASK;

		if (!pdc_power_mgmt_get_frs_hw_supported(port))
			return;

		/* If FRS is supported, the power manager will request 3A when
		 * a PD Source is connected to enable FRS until Sink Caps are
		 * evaluated. Clear 3A request if it is not necessary and
		 * disable FRS if it is not supported.
		 */

		/* FRS is only supported in PD 3.0 and higher */
		if (pdc_power_mgmt_get_rev(port, TCPCI_MSG_SOP) == PD_REV20) {
			atomic_clear_bit(&source_frs_max_requested, port);
			goto balance;
		}

		if ((vsafe5v_pdo & PDO_FIXED_DUAL_ROLE) && frs_current) {
			/* Clear FRS request when 3.0 A is not needed */
			if (frs_current == PDO_FIXED_FRS_CURR_DFLT_USB_POWER ||
			    frs_current == PDO_FIXED_FRS_CURR_1A5_AT_5V) {
				atomic_clear_bit(&source_frs_max_requested,
						 port);
				goto balance;
			}

			if (pd_get_usb_pd_3a_ports() == 0)
				return;

			atomic_set_bit(&source_frs_max_requested, port);
		} else {
			atomic_clear_bit(&source_frs_max_requested, port);
		}
	}

balance:
	pdc_dpm_balance_source_ports(&dpm_work.work);
}

void pdc_dpm_add_non_pd_sink(int port)
{
	if (pd_get_usb_pd_3a_ports() == 0)
		return;

	atomic_set_bit(&non_pd_sink_max_requested, port);
	pdc_dpm_balance_source_ports(&dpm_work.work);
}

#ifdef CONFIG_PDC_POWER_MGMT_SRC_THROTTLING

/*
 * Latching behavior: Once an overload is detected, Type-C source limits are
 * throttled to 1.5A and latched. To prevent ping-pong oscillation, the 3A
 * capacity is ONLY restored upon physical sink removal or connection of AC
 * power.
 */

static void pdc_dpm_restore_work_handler(struct k_work *work);
static K_WORK_DEFINE(restore_work, pdc_dpm_restore_work_handler);

static void pdc_dpm_throttle_work_handler(struct k_work *work);
static K_WORK_DEFINE(throttle_work, pdc_dpm_throttle_work_handler);

static void pdc_dpm_restore_work_handler(struct k_work *work)
{
	int i;

	pdc_dpm_balance_source_ports(&dpm_work.work);
	for (i = 0; i < pdc_power_mgmt_get_usb_pd_port_count(); i++) {
		pdc_power_mgmt_set_new_power_request(i);
	}
}

static void pdc_dpm_throttle_work_handler(struct k_work *work)
{
	int i;

	k_mutex_lock(&max_current_claimed_mtx, K_FOREVER);
	max_current_claimed = 0;
	k_mutex_unlock(&max_current_claimed_mtx);
	for (i = 0; i < pdc_power_mgmt_get_usb_pd_port_count(); i++) {
		pdc_power_mgmt_set_current_limit(i, TC_CURRENT_1_5A);
		pdc_power_mgmt_set_new_power_request(i);
	}
}

static void pdc_dpm_clear_battery_overload(int port)
{
	if (atomic_cas(&overload_status, BATTERY_IS_OVERLOADED,
		       BATTERY_IS_GOOD)) {
		atomic_set(&overload_counter, 0);
		LOG_INF("Battery overload resolved: Port %d removed, restoring 3A capacity.",
			port);
		k_work_submit(&restore_work);
	}
}
#endif

void pdc_dpm_remove_sink(int port)
{
	enum usb_typec_current_t rp;

#ifdef CONFIG_PDC_POWER_MGMT_SRC_THROTTLING
	pdc_dpm_clear_battery_overload(port);
#endif

	if (pd_get_usb_pd_3a_ports() == 0)
		return;

	if (!atomic_test_bit(&sink_max_pdo_requested, port) &&
	    !atomic_test_bit(&non_pd_sink_max_requested, port))
		return;

	atomic_clear_bit(&sink_max_pdo_requested, port);
	atomic_clear_bit(&non_pd_sink_max_requested, port);
	if (IS_ENABLED(CONFIG_USBC_PDC_TBT_SUPPORTED))
		pdc_dpm_tbt_clear_port(port);

	/* Restore selected default Rp on the port */
	rp = pdc_power_mgmt_get_default_current_limit(port);
	pdc_power_mgmt_set_current_limit(port, rp);
	pdc_dpm_balance_source_ports(&dpm_work.work);
}

void pdc_dpm_remove_source(int port)
{
	enum usb_typec_current_t rp;

	if (pd_get_usb_pd_3a_ports() == 0)
		return;

	if (!pdc_power_mgmt_get_frs_hw_supported(port))
		return;

	if (!(BIT(port) & (uint32_t)source_frs_max_requested))
		return;

	atomic_clear_bit(&source_frs_max_requested, port);

	/* Restore selected default Rp on the port */
	rp = pdc_power_mgmt_get_default_current_limit(port);
	pdc_power_mgmt_set_current_limit(port, rp);
	pdc_dpm_balance_source_ports(&dpm_work.work);
}

int pdc_dpm_get_source_current(const int port)
{
	if (pd_get_power_role(port) == PD_ROLE_SINK) {
		return 0;
	}

	if (pd_get_usb_pd_3a_ports() == 0) {
		return 1500;
	}

	if (max_current_claimed & BIT(port)) {
		return 3000;
	}

	/* PDC implementations default to sourcing 1.5A unless this
	 * module allocates 3A to the port via max_current_claimed.
	 */
	return 1500;
}

#ifdef CONFIG_PDC_POWER_MGMT_SRC_THROTTLING

BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(usbc_port_policy) == 1,
	     "Exactly one instance of usbc-port-policy should be defined");

static const int sys_max_i =
	DT_PROP_OR(DT_COMPAT_GET_ANY_STATUS_OKAY(usbc_port_policy),
		   max_discharge_current_ma, 0);

static const int sys_max_p =
	DT_PROP_OR(DT_COMPAT_GET_ANY_STATUS_OKAY(usbc_port_policy),
		   max_discharge_power_mw, 0);

static void pdc_dpm_unattached_cb(int port)
{
	pdc_dpm_clear_battery_overload(port);
}

static int pdc_battery_overload_init(void)
{
	/*
	 * Register UNATTACH callback to handle devices that fail PD
	 * negotiation. Without this, failed-PD detachments bypass the DPM's
	 * remove_sink/source paths, causing the port to remain locked at 1.5A.
	 */
	pdc_power_mgmt_register_board_callback(
		PDC_BOARD_CB_UNATTACH, (const void *)pdc_dpm_unattached_cb);
	return 0;
}
SYS_INIT(pdc_battery_overload_init, APPLICATION,
	 CONFIG_APPLICATION_INIT_PRIORITY);

static void pdc_monitor_battery_overload(void)
{
	const struct batt_params *batt;
	int discharging_current_ma = 0;
	int discharging_power_mw = 0;

	if (sys_max_i == 0 && sys_max_p == 0) {
		return;
	}

	if (extpower_is_present()) {
		atomic_set(&overload_counter, 0);
		if (atomic_cas(&overload_status, BATTERY_IS_OVERLOADED,
			       BATTERY_IS_GOOD)) {
			LOG_INF("AC power connected, restoring 3A capacity.");
			k_work_submit(&restore_work);
		}
		return;
	}

	batt = charger_current_battery_params();
	if (batt->flags & (BATT_FLAG_BAD_CURRENT | BATT_FLAG_BAD_VOLTAGE)) {
		return;
	}

	discharging_current_ma = batt->current < 0 ? -batt->current :
						     batt->current;
	discharging_power_mw =
		(int)(((int64_t)discharging_current_ma * batt->voltage) / 1000);

	if ((sys_max_i > 0 && discharging_current_ma > sys_max_i) ||
	    (sys_max_p > 0 && discharging_power_mw > sys_max_p)) {
		if (atomic_get(&overload_counter) < OVERLOAD_DEBOUNCE_SECONDS) {
			atomic_inc(&overload_counter);
		}
		if (atomic_get(&overload_counter) >=
		    OVERLOAD_DEBOUNCE_SECONDS) {
			if (atomic_cas(&overload_status, BATTERY_IS_GOOD,
				       BATTERY_IS_OVERLOADED)) {
				LOG_WRN("Battery overload detected! Throttling source capacity.");
				k_work_submit(&throttle_work);
			}
		}
	} else {
		atomic_set(&overload_counter, 0);
	}
}
DECLARE_HOOK(HOOK_SECOND, pdc_monitor_battery_overload, HOOK_PRIO_DEFAULT);

#endif /* CONFIG_PDC_POWER_MGMT_SRC_THROTTLING */
