/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "charge_state.h"
#include "extpower.h"
#include "hooks.h"
#include "usb_pd.h"
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

	/* Allocate 3.0 A to new PD sink ports that need it */
	new_ports = sink_max_pdo_requested & ~max_current_claimed;

	while (new_ports) {
		int new_max_port = LOWEST_PORT(new_ports);

		if (count_port_bits(max_current_claimed) <
		    pd_get_usb_pd_3a_ports()) {
			max_current_claimed |= BIT(new_max_port);
			pdc_power_mgmt_set_current_limit(new_max_port,
							 TC_CURRENT_3_0A);
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

			pdc_power_mgmt_frs_enable(rem_frs, false);
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
			/* Enable FRS for this port */
			pdc_power_mgmt_frs_enable(new_frs_port, true);
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
			pdc_power_mgmt_frs_enable(port, false);
			goto balance;
		}

		if ((vsafe5v_pdo & PDO_FIXED_DUAL_ROLE) && frs_current) {
			/* Always enable FRS when 3.0 A is not needed */
			if (frs_current == PDO_FIXED_FRS_CURR_DFLT_USB_POWER ||
			    frs_current == PDO_FIXED_FRS_CURR_1A5_AT_5V) {
				atomic_clear_bit(&source_frs_max_requested,
						 port);
				pdc_power_mgmt_frs_enable(port, true);
				goto balance;
			}

			if (pd_get_usb_pd_3a_ports() == 0)
				return;

			atomic_set_bit(&source_frs_max_requested, port);
		} else {
			atomic_clear_bit(&source_frs_max_requested, port);
			pdc_power_mgmt_frs_enable(port, false);
		}
	}

balance:
	pdc_dpm_balance_source_ports(&dpm_work.work);
}

void pdc_dpm_add_pd_source(int port)
{
	/* The PDC DPM will attempt to enable FRS early while evaluating Snk
	 * Caps. If 3A is not available, the DPM will not enable FRS until it
	 * can confirm 3A is not required. It can take longer to enable FRS on
	 * platforms which don't source 3A.
	 */
	if (pd_get_usb_pd_3a_ports() == 0)
		return;

	atomic_set_bit(&source_frs_max_requested, port);
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
static atomic_t is_battery_overloaded = ATOMIC_INIT(0);
static int overload_counter = 0;

static void pdc_dpm_clear_battery_overload(int port)
{
	int i;
	if (atomic_cas(&is_battery_overloaded, 1, 0)) {
		overload_counter = 0;
		LOG_INF("Battery overload resolved: Port %d removed, restoring 3A capacity.",
			port);

		for (i = 0; i < board_get_usb_pd_port_count(); i++) {
			if (i != port) {
				enum usb_typec_current_t target_limit =
					(pdc_dpm_get_source_current(i) ==
					 3000) ?
						TC_CURRENT_3_0A :
						TC_CURRENT_1_5A;

				pdc_power_mgmt_set_current_limit(i,
								 target_limit);
				pdc_power_mgmt_set_new_power_request(i);
			}
		}
	}
}
#else
static inline void pdc_dpm_clear_battery_overload(int port)
{
}
#endif

void pdc_dpm_remove_sink(int port)
{
	enum usb_typec_current_t rp;

	pdc_dpm_clear_battery_overload(port);

	if (pd_get_usb_pd_3a_ports() == 0)
		return;

	if (!atomic_test_bit(&sink_max_pdo_requested, port) &&
	    !atomic_test_bit(&non_pd_sink_max_requested, port))
		return;

	atomic_clear_bit(&sink_max_pdo_requested, port);
	atomic_clear_bit(&non_pd_sink_max_requested, port);

	/* Restore selected default Rp on the port */
	rp = pdc_power_mgmt_get_default_current_limit(port);
	pdc_power_mgmt_set_current_limit(port, rp);
	pdc_dpm_balance_source_ports(&dpm_work.work);
}

void pdc_dpm_remove_source(int port)
{
	enum usb_typec_current_t rp;

	pdc_dpm_clear_battery_overload(port);

	if (pd_get_usb_pd_3a_ports() == 0)
		return;

	if (!pdc_power_mgmt_get_frs_hw_supported(port))
		return;

	if (!(BIT(port) & (uint32_t)source_frs_max_requested))
		return;

	atomic_clear_bit(&source_frs_max_requested, port);

	/* Disable FRS */
	pdc_power_mgmt_frs_enable(port, false);

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

#define USBC_POLICY_NODE \
	DT_COMPAT_GET_ANY_STATUS_OKAY(chromeos_usbc_port_policy)

__override int pd_get_usb_pd_3a_ports(void)
{
	if (atomic_get(&is_battery_overloaded) != 0) {
		return 0;
	}

	return CONFIG_USB_PD_3A_PORTS;
}

#define PDC_UPDATE_MAX_I(node_id)                                           \
	{                                                                   \
		int val = DT_PROP_OR(node_id, max_discharge_current_ma, 0); \
		if (val > sys_max_i) {                                      \
			sys_max_i = val;                                    \
		}                                                           \
	}

#define PDC_UPDATE_MAX_P(node_id)                                         \
	{                                                                 \
		int val = DT_PROP_OR(node_id, max_discharge_power_mw, 0); \
		if (val > sys_max_p) {                                    \
			sys_max_p = val;                                  \
		}                                                         \
	}

static int sys_max_i = 0;
static int sys_max_p = 0;

static int pdc_battery_overload_init(void)
{
	DT_FOREACH_STATUS_OKAY(usbc_port_policy, PDC_UPDATE_MAX_I)
	DT_FOREACH_STATUS_OKAY(usbc_port_policy, PDC_UPDATE_MAX_P)
	return 0;
}
SYS_INIT(pdc_battery_overload_init, APPLICATION,
	 CONFIG_APPLICATION_INIT_PRIORITY);

static void pdc_monitor_battery_overload(void)
{
	const struct batt_params *batt;
	int discharging_current_ma = 0;
	int discharging_power_mw = 0;
	int i;

	if (sys_max_i == 0 && sys_max_p == 0) {
		return;
	}

	if (extpower_is_present()) {
		overload_counter = 0;
		return;
	}

	batt = charger_current_battery_params();
	if (batt->flags & (BATT_FLAG_BAD_CURRENT | BATT_FLAG_BAD_VOLTAGE)) {
		return;
	}

	discharging_current_ma = batt->current < 0 ? -batt->current :
						     batt->current;
	discharging_power_mw = (discharging_current_ma * batt->voltage) / 1000;

	if ((sys_max_i > 0 && discharging_current_ma > sys_max_i) ||
	    (sys_max_p > 0 && discharging_power_mw > sys_max_p)) {
		overload_counter++;
		if (overload_counter >= 3) {
			if (atomic_cas(&is_battery_overloaded, 0, 1)) {
				for (i = 0; i < board_get_usb_pd_port_count();
				     i++) {
					pdc_power_mgmt_set_current_limit(
						i, TC_CURRENT_1_5A);
					pdc_power_mgmt_set_new_power_request(i);
				}
			}
		}
	} else {
		overload_counter = 0;
	}
}
DECLARE_HOOK(HOOK_SECOND, pdc_monitor_battery_overload, HOOK_PRIO_DEFAULT);

#endif /* CONFIG_PDC_POWER_MGMT_SRC_THROTTLING */
