/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/drivers/pinctrl.h>
#include <zephyr/logging/log.h>

#include "chipset.h"
#include "hooks.h"
#include "host_command.h"
#include "system.h"

#include <errno.h>

#include <ap_power/ap_power.h>

#define SPI_RX_MAX_FIFO_SIZE 256
#define SPI_TX_MAX_FIFO_SIZE 256

#define EC_SPI_PREAMBLE_LENGTH 4
#define EC_SPI_PAST_END_LENGTH 4

/* Max data size for a version 3 request/response packet. */
#define SPI_MAX_REQUEST_SIZE SPI_RX_MAX_FIFO_SIZE
#define SPI_MAX_RESPONSE_SIZE \
	(SPI_TX_MAX_FIFO_SIZE - EC_SPI_PREAMBLE_LENGTH - EC_SPI_PAST_END_LENGTH)

LOG_MODULE_REGISTER(shim_cros_shi, LOG_LEVEL_ERR);

#define SHI_NODE DT_NODELABEL(shi0)
PINCTRL_DT_DEFINE(SHI_NODE);

static void shi_disable(void)
{
	/* Enable sleep mask of SHI to enter deep sleep of power plicy. */
	enable_sleep(SLEEP_MASK_SPI);
}

static void shi_apply_pinctrl_state(uint8_t state)
{
	const struct pinctrl_dev_config *pcfg =
		PINCTRL_DT_DEV_CONFIG_GET(SHI_NODE);
	int ret;

	ret = pinctrl_apply_state(pcfg, state);
	if (ret == -ENOENT) {
		LOG_ERR("Pins of pinctrl state %u are not defined", state);
	} else if (ret < 0) {
		LOG_ERR("Failed to apply pinctrl state %u (%d)", state, ret);
	}
}

static void shi_power_shutdown_handler(struct ap_power_ev_callback *cb,
				       struct ap_power_ev_data data)
{
	switch (data.event) {
	case AP_POWER_PRE_INIT:
		shi_apply_pinctrl_state(PINCTRL_STATE_DEFAULT);
		break;
	case AP_POWER_SHUTDOWN_COMPLETE:
		/* Disable SHI bus */
		shi_disable();
		break;
	case AP_POWER_HARD_OFF:
		shi_apply_pinctrl_state(PINCTRL_STATE_SLEEP);
		break;
	default:
		__ASSERT(false, "%s: unhandled event: %d", __func__,
			 data.event);
		break;
	}
}

static void install_power_change_handler(void)
{
	static struct ap_power_ev_callback cb;

	/* Add a callback of power shutdown complete to enable sleep mask. */
	ap_power_ev_init_callback(&cb, shi_power_shutdown_handler,
				  AP_POWER_SHUTDOWN_COMPLETE |
				  AP_POWER_PRE_INIT | AP_POWER_HARD_OFF);
	ap_power_ev_add_callback(&cb);
}
/* Call hook after chipset sets initial power state */
DECLARE_HOOK(HOOK_INIT, install_power_change_handler, HOOK_PRIO_POST_CHIPSET);

#ifndef CONFIG_EC_HOST_CMD
/* Get protocol information */
enum ec_status spi_get_protocol_info(struct host_cmd_handler_args *args)
{
	struct ec_response_get_protocol_info *r = args->response;

	memset(r, 0, sizeof(*r));
	r->protocol_versions = BIT(3);
	r->max_request_packet_size = SPI_MAX_REQUEST_SIZE;
	r->max_response_packet_size = SPI_MAX_RESPONSE_SIZE;
	r->flags = EC_PROTOCOL_INFO_IN_PROGRESS_SUPPORTED;

	args->response_size = sizeof(*r);

	return EC_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_GET_PROTOCOL_INFO, spi_get_protocol_info,
		     EC_VER_MASK(0));
#endif /* !#ifdef CONFIG_EC_HOST_CMD */
