/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charge_manager.h"
#include "console.h"
#include "gpio.h"
#include "system.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usbc_ppc.h"
#include "usb_pd_tcpm.h"

#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ## args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ## args)

int pd_set_power_supply_ready(int port)
{
	int rv;

	/* Disable charging. */
#ifdef CONFIG_USB_PD_PPC
	rv = tcpc_config[port].drv->set_snk_ctrl(port, 0);
#elif defined(CONFIG_USBC_PPC)
	rv = ppc_vbus_sink_enable(port, 0);
#endif

	if (rv)
		return rv;

	pd_set_vbus_discharge(port, 0);

	/* Provide Vbus. */
#ifdef CONFIG_USB_PD_PPC
	rv = tcpc_config[port].drv->set_src_ctrl(port, 1);
#elif defined(CONFIG_USBC_PPC)
	rv = ppc_vbus_source_enable(port, 1);
#endif

	if (rv)
		return rv;

	/* Notify host of power info change. */
	pd_send_host_event(PD_EVENT_POWER_CHANGE);

	return EC_SUCCESS;
}

void pd_power_supply_reset(int port)
{
	int prev_en;

	prev_en = board_vbus_source_enabled(port);

	/* Disable VBUS. */
#ifdef CONFIG_USB_PD_PPC
	tcpc_config[port].drv->set_src_ctrl(port, 0);
#elif defined(CONFIG_USBC_PPC)
	ppc_vbus_source_enable(port, 0);
#endif

	/* Enable discharge if we were previously sourcing 5V */
	if (prev_en)
		pd_set_vbus_discharge(port, 1);

	/* Notify host of power info change. */
	pd_send_host_event(PD_EVENT_POWER_CHANGE);
}

int pd_check_vconn_swap(int port)
{
	/* Only allow vconn swap if PP3300 rail is enabled */
	return gpio_get_level(GPIO_EN_PP3300_A);
}

int pd_snk_is_vbus_provided(int port)
{
#ifdef CONFIG_USB_PD_PPC
	return tcpc_config[port].drv->check_vbus_level(port, VBUS_PRESENT);
#elif defined(CONFIG_USBC_PPC)
	return ppc_is_vbus_present(port);
#endif
}

int board_vbus_source_enabled(int port)
{
	if (is_typec_port(port)) {
#ifdef CONFIG_USB_PD_PPC
		return tcpc_config[port].drv->get_src_ctrl(port);
#elif defined(CONFIG_USBC_PPC)
		return ppc_is_sourcing_vbus(port);
#endif
	}
	return 0;
}
