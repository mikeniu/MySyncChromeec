/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "board.h"
#include "gpio.h"
#include "gpio_signal.h"
#include "include/system.h"
#include "lpc.h"
#include "system_boot_time.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#include <power_signals.h>
#ifdef CONFIG_AP_PWRSEQ_DRIVER
#include <ap_power/ap_pwrseq_sm.h>
#endif

LOG_MODULE_DECLARE(ap_pwrseq, LOG_LEVEL_INF);

#define X86_NON_DSX_FORCE_SHUTDOWN_TO_MS 50

/* Power cycling primary rail requires at least 30 ms 'off' time */
#define BOARD_PTL_RVP_MINIMUM_POWER_DOWN_DELAY_MS 30

void board_ap_power_force_shutdown(void)
{
	int timeout_ms = X86_NON_DSX_FORCE_SHUTDOWN_TO_MS;

	/* Turn off PCH_RMSRST to meet tPCH12 */
	power_signal_set(PWR_EC_PCH_RSMRST, 1);

	/* Turn off PRIM load switch. */
	power_signal_set(PWR_EN_PP3300_A, 0);

	power_signal_set(PWR_EN_PP5000_A, 0);
	/* Wait RSMRST to be off. */
	while (power_signal_get(PWR_RSMRST_PWRGD) && (timeout_ms > 0)) {
		k_msleep(1);
		timeout_ms--;
	};

	if (power_signal_get(PWR_RSMRST_PWRGD))
		LOG_WRN("RSMRST_PWRGD didn't go low!  Assuming G3.");

	k_msleep(BOARD_PTL_RVP_MINIMUM_POWER_DOWN_DELAY_MS);
}

#ifdef CONFIG_AP_PWRSEQ_DRIVER
int board_ap_power_action_g3_entry(void *data)
{
	board_ap_power_force_shutdown();

	return 0;
}

static int board_ap_power_action_g3_run(void *data)
{
	const struct gpio_dt_spec *gpio_dt;
	int board_id;

	if (ap_pwrseq_sm_is_event_set(data, AP_PWRSEQ_EVENT_POWER_STARTUP)) {
		power_signal_set(PWR_EN_PP5000_A, 1);
		/* Turn on the PP3300_PRIM rail. */
		power_signal_set(PWR_EN_PP3300_A, 1);

		/* Indication to soc on recovery boot */

		board_id = system_get_board_version();
		if (board_id == PTL_RVP_BOARD_ID) {
			gpio_dt = GPIO_DT_FROM_NODELABEL(rvp_cse_early_rec_sw);
		} else if (board_id == PTL_GCS_BOARD_ID) {
			gpio_dt = GPIO_DT_FROM_NODELABEL(gcs_cse_early_rec_sw);
		} else {
			gpio_dt = NULL;
		}
		if (gpio_dt) {
			gpio_pin_set_dt(
				gpio_dt, system_is_manual_recovery());
		}

		update_ap_boot_time(ARAIL);
	}

	/* Return 0 only if power rails have been enabled  */
	return !power_signal_get(PWR_EN_PP3300_A);
}

AP_POWER_APP_STATE_DEFINE(G3, board_ap_power_action_g3_entry,
			  board_ap_power_action_g3_run, NULL);
#endif /* CONFIG_AP_PWRSEQ_DRIVER */

const struct gpio_dt_spec *board_get_power_signal_gpio(enum power_signal signal)
{
	const struct gpio_dt_spec *gpio_dt = NULL;
	int board_id;

	board_id = system_get_board_version();
	switch(signal) {
	case PWR_RSMRST_PWRGD:
		if (board_id == PTL_RVP_BOARD_ID) {
			gpio_dt = GPIO_DT_FROM_NODELABEL(rvp_rsmrst_pwrgd);
		} else if (board_id == PTL_GCS_BOARD_ID) {
			gpio_dt = GPIO_DT_FROM_NODELABEL(gcs_rsmrst_pwrgd);
		}
	break;
	case PWR_EC_PCH_RSMRST:
		if (board_id == PTL_RVP_BOARD_ID) {
			gpio_dt = GPIO_DT_FROM_NODELABEL(rvp_ec_pch_rsmrst_l);
		} else if (board_id == PTL_GCS_BOARD_ID) {
			gpio_dt = GPIO_DT_FROM_NODELABEL(gcs_ec_pch_rsmrst_l);
		}
	break;
	case PWR_SYS_RST:
		if (board_id == PTL_RVP_BOARD_ID) {
			gpio_dt = GPIO_DT_FROM_NODELABEL(rvp_sys_rst_odl);
		} else if (board_id == PTL_GCS_BOARD_ID) {
			gpio_dt = GPIO_DT_FROM_NODELABEL(gcs_sys_rst_odl);
		}
	default:
	}

	return gpio_dt;
}

int board_power_signal_get(enum power_signal signal)
{
	const struct gpio_dt_spec *gpio_dt;

	switch (signal) {
	case PWR_EC_PCH_SYS_PWROK:
		return power_signal_get(PWR_PCH_PWROK);
	case PWR_SYS_RST:
		gpio_dt = board_get_power_signal_gpio(PWR_SYS_RST);
		return gpio_dt ? gpio_pin_get_dt(gpio_dt) : -ENOSYS;
	case PWR_EC_PCH_RSMRST:
		gpio_dt = board_get_power_signal_gpio(PWR_EC_PCH_RSMRST);
		return gpio_dt ? gpio_pin_get_dt(gpio_dt) : -ENOSYS;
	case PWR_RSMRST_PWRGD:
		gpio_dt = board_get_power_signal_gpio(PWR_RSMRST_PWRGD);
		return gpio_dt ? gpio_pin_get_dt(gpio_dt) : -ENOSYS;
	default:
		return -EINVAL;
	}
	return -EINVAL;
}

int board_power_signal_set(enum power_signal signal, int value)
{
	const struct gpio_dt_spec *gpio_dt;

	switch (signal) {
	case PWR_SYS_RST:
		gpio_dt = board_get_power_signal_gpio(PWR_SYS_RST);
		return gpio_dt ? gpio_pin_set_dt(gpio_dt, value) : -ENOSYS;
	case PWR_RSMRST_PWRGD:
		gpio_dt = board_get_power_signal_gpio(PWR_RSMRST_PWRGD);
		return gpio_dt ? gpio_pin_set_dt(gpio_dt, value) : -ENOSYS;
	case PWR_EC_PCH_RSMRST:
		gpio_dt = board_get_power_signal_gpio(PWR_EC_PCH_RSMRST);
		return gpio_dt ? gpio_pin_set_dt(gpio_dt, value) : -ENOSYS;
	default:
		return -EINVAL;
	}
}

__override void board_pulse_entering_rw(void)
{
	const struct gpio_dt_spec *gpio_dt;
	int board_id;

	board_id = system_get_board_version();
	if (board_id == PTL_RVP_BOARD_ID) {
		gpio_dt = GPIO_DT_FROM_NODELABEL(rvp_entering_rw);
	} else if (board_id == PTL_GCS_BOARD_ID) {
		gpio_dt = GPIO_DT_FROM_NODELABEL(gcs_entering_rw);
	} else {
		return;
	}

	gpio_pin_set_dt(gpio_dt, 1);
	crec_usleep(MSEC);
	gpio_pin_set_dt(gpio_dt, 0);
}

static struct gpio_callback int_cb;

static void power_signal_gpio_interrupt(const struct device *port,
				 struct gpio_callback *cb,
				 gpio_port_pins_t pins)
{
	const struct gpio_dt_spec *gpio_dt;

	gpio_dt = board_get_power_signal_gpio(PWR_RSMRST_PWRGD);
	power_signal_interrupt(PWR_RSMRST_PWRGD,
			      gpio_pin_get_dt(gpio_dt));
}

static int board_gpio_init(void)
{
	const struct gpio_dt_spec *gpio_dt;

	gpio_dt = board_get_power_signal_gpio(PWR_RSMRST_PWRGD);

	gpio_pin_configure_dt(gpio_dt, GPIO_INPUT);
	gpio_init_callback(&int_cb,
			   power_signal_gpio_interrupt,
			   BIT(gpio_dt->pin));
	gpio_add_callback(gpio_dt->port, &int_cb);

	gpio_pin_interrupt_configure_dt(gpio_dt, GPIO_INT_EDGE_BOTH);

	/*TODO: Handle `reset-val`. */
	gpio_dt = board_get_power_signal_gpio(PWR_EC_PCH_RSMRST);

	gpio_pin_configure_dt(gpio_dt, GPIO_OUTPUT);

	return 0;
}

SYS_INIT(board_gpio_init, APPLICATION, CONFIG_CHARGE_MANAGER_SYS_INIT_PRIORITY);

int lpc_wake_signal(host_event_t wake_events)
{
	const struct gpio_dt_spec *gpio_dt;
	int board_id;

	board_id = system_get_board_version();
	if (board_id == PTL_RVP_BOARD_ID) {
		gpio_dt = GPIO_DT_FROM_NODELABEL(rvp_gpio_ec_pch_wake_odl);
	} else if (board_id == PTL_GCS_BOARD_ID) {
		gpio_dt = GPIO_DT_FROM_NODELABEL(gcs_gpio_ec_pch_wake_odl);
	} else {
		return -EINVAL;
	}
	
	gpio_pin_set_dt(gpio_dt, !wake_events);

	return 0;
}
