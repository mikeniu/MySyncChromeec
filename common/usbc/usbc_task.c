/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "battery_smart.h"
#include "builtin/assert.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "cros_version.h"
#include "ec_commands.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "tcpm/tcpm.h"
#include "timer.h"
#include "usb_charge.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usb_pd_dpm_sm.h"
#include "usb_pd_timer.h"
#include "usb_pe_sm.h"
#include "usb_prl_sm.h"
#include "usb_sm.h"
#include "usb_tc_sm.h"
#include "usbc_ppc.h"
#include "util.h"

#include <zephyr/init.h>
#include <zephyr/kernel.h>

#define USBC_EVENT_TIMEOUT (5 * MSEC)
#define USBC_MIN_EVENT_TIMEOUT (1 * MSEC)

#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ##args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ##args)

/*
 * If CONFIG_ASSERT_CCD_MODE_ON_DTS_CONNECT is not defined then
 * _GPIO_CCD_MODE_ODL is not needed. Declare as extern so IS_ENABLED will work.
 */
#ifndef CONFIG_ASSERT_CCD_MODE_ON_DTS_CONNECT
extern int _GPIO_CCD_MODE_ODL;
#else
#define _GPIO_CCD_MODE_ODL GPIO_CCD_MODE_ODL
#endif /* CONFIG_ASSERT_CCD_MODE_ON_DTS_CONNECT */

static uint8_t paused[CONFIG_USB_PD_PORT_MAX_COUNT];

#if defined(HAS_TASK_PD_C0) && defined(CONFIG_USB_PD_PORT_MAX_COUNT)
static struct k_event pd_port_events[CONFIG_USB_PD_PORT_MAX_COUNT];

static int init_pd_port_events(void)
{
	for (int port = 0; port < CONFIG_USB_PD_PORT_MAX_COUNT; port++)
		k_event_init(&pd_port_events[port]);

	return 0;
}
SYS_INIT(init_pd_port_events, PRE_KERNEL_1, 0);

void pd_set_event(int port, uint32_t events)
{
	if (port >= 0 && port < CONFIG_USB_PD_PORT_MAX_COUNT)
		k_event_post(&pd_port_events[port], events);
}

void pd_clear_port_event(int port, uint32_t events)
{
	if (port >= 0 && port < CONFIG_USB_PD_PORT_MAX_COUNT)
		k_event_clear(&pd_port_events[port], events);
}

uint32_t pd_wait_event_mask(int port, uint32_t event_mask, int timeout_us)
{
	if (port < 0 || port >= CONFIG_USB_PD_PORT_MAX_COUNT)
		return 0;

	return k_event_wait_safe(&pd_port_events[port], event_mask, false,
				 timeout_us < 0 ? K_FOREVER :
						  K_USEC(timeout_us));
}
#endif /* HAS_TASK_PD_C0 && CONFIG_USB_PD_PORT_MAX_COUNT */

void tc_pause_event_loop(int port)
{
	paused[port] = 1;
}

bool tc_event_loop_is_paused(int port)
{
	return paused[port];
}

void tc_start_event_loop(int port)
{
	/*
	 * Only generate PD_EVENT_WAKE event if state
	 * machine is transitioning to un-paused
	 */
	if (paused[port]) {
		paused[port] = 0;
		pd_wake(port);
	}
}

static void pd_task_init(int port)
{
	tc_state_init(port);
	paused[port] = 0;

	/*
	 * Since most boards configure the TCPC interrupt as edge
	 * and it is possible that the interrupt line was asserted between init
	 * and calling set_state, we need to process any pending interrupts now.
	 * Otherwise future interrupts will never fire because another edge
	 * never happens. Note this needs to happen after set_state() is called.
	 */
	if (IS_ENABLED(CONFIG_HAS_TASK_PD_INT))
		schedule_deferred_pd_interrupt(port);

	/*
	 * GPIO_CCD_MODE_ODL must be initialized with GPIO_ODR_HIGH
	 * when CONFIG_ASSERT_CCD_MODE_ON_DTS_CONNECT is enabled
	 */
	if (IS_ENABLED(CONFIG_ASSERT_CCD_MODE_ON_DTS_CONNECT))
		ASSERT(gpio_get_default_flags(_GPIO_CCD_MODE_ODL) &
		       GPIO_ODR_HIGH);
}

static int pd_task_timeout(int port)
{
	int timeout;

	if (paused[port])
		timeout = -1;
	else {
		timeout = pd_timer_next_expiration(port);
		if (timeout < 0 || timeout > USBC_EVENT_TIMEOUT)
			timeout = USBC_EVENT_TIMEOUT;
		else if (timeout < USBC_MIN_EVENT_TIMEOUT)
			timeout = USBC_MIN_EVENT_TIMEOUT;
	}
	return timeout;
}

static bool pd_task_loop(int port)
{
	/* wait for next event/packet or timeout expiration */
	const uint32_t evt =
		pd_wait_event_mask(port, UINT32_MAX, pd_task_timeout(port));

	/* Manage expired PD Timers on timeouts */
	if (evt == 0)
		pd_timer_manage_expired(port);

	/*
	 * Re-use PD_EVENT_RESET_DONE in tests to restart the USB task
	 * if this code is running in a unit test.
	 */
	if (IS_ENABLED(TEST_BUILD) && (evt & PD_EVENT_RESET_DONE))
		return false;

	/* handle events that affect the state machine as a whole */
	tc_event_check(port, evt);

	/*
	 * run port controller task to check CC and/or read incoming
	 * messages
	 */
	if (IS_ENABLED(CONFIG_USB_PD_TCPC))
		tcpc_run(port, evt);

	/* Run Device Policy Manager */
	dpm_run(port, evt, tc_get_pd_enabled(port));

	/* Run policy engine state machine */
	pe_run(port, evt, tc_get_pd_enabled(port));

	/* Run protocol state machine */
	prl_run(port, evt, tc_get_pd_enabled(port));

	/* Run TypeC state machine */
	tc_run(port);

	return true;
}

void pd_task(void *u)
{
	int port = TASK_ID_TO_PD_PORT(task_get_current());

	/*
	 * If port does not exist, return
	 */
	if (port >= board_get_usb_pd_port_count())
		return;

#if CONFIG_USB_PD_STARTUP_DELAY_MS > 0
	crec_msleep(CONFIG_USB_PD_STARTUP_DELAY_MS);
#endif

	while (1) {
		pd_timer_init(port);
		pd_task_init(port);

		/* As long as pd_task_loop returns true, keep running the loop.
		 * pd_task_loop returns false when the code needs to re-init
		 * the task, so once the code breaks out of the inner while
		 * loop, the re-init code at the top of the outer while loop
		 * will run.
		 */
		while (pd_task_loop(port))
			continue;
	}
}
