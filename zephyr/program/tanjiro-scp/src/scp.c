/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ec_commands.h"
#include "ec_version.h"
#include "hooks.h"
#include "host_command.h"
#include "hwtimer.h"
#include "ipi_chip.h"
#include "mkbp_event.h"

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/ipm.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/mgmt/ec_host_cmd/backend.h>
#include <zephyr/mgmt/ec_host_cmd/ec_host_cmd.h>

LOG_MODULE_REGISTER(scp, LOG_LEVEL_INF);

#define RPMSG_NAME_SIZE 32
#define HOST_COMMAND_MARK 1
#define HOST_EVENT_MARK 2

struct rpmsg_ns_msg {
	char name[RPMSG_NAME_SIZE];
	uint32_t addr;
} __packed;

struct cros_ec_rpmsg_response {
	uint8_t type;
	uint8_t data[] __aligned(4);
};

#define RPMSG_PREAMBLE_SIZE offsetof(struct cros_ec_rpmsg_response, data)
BUILD_ASSERT(RPMSG_PREAMBLE_SIZE == 4);

static const struct device *const ipm_dev = DEVICE_DT_GET(DT_NODELABEL(ipi));
static struct ec_host_cmd_rx_ctx *scp_rx_ctx;
static struct ec_host_cmd_tx_buf *scp_tx_buf;
static uint8_t *scp_tx_rpmsg_buf;

__attribute__((weak)) uint32_t video_get_dec_capability(void)
{
	return 0;
}

__attribute__((weak)) uint32_t video_get_enc_capability(void)
{
	return 0;
}

static int scp_host_cmd_init(const struct ec_host_cmd_backend *backend,
			     struct ec_host_cmd_rx_ctx *rx_ctx,
			     struct ec_host_cmd_tx_buf *tx)
{
	ARG_UNUSED(backend);

	scp_rx_ctx = rx_ctx;
	scp_tx_buf = tx;

	/* Reserve 4-byte cros_ec_rpmsg_response preamble at start of tx->buf */
	scp_tx_rpmsg_buf = tx->buf;
	tx->buf = scp_tx_rpmsg_buf + RPMSG_PREAMBLE_SIZE;
	tx->len_max -= RPMSG_PREAMBLE_SIZE;

	return 0;
}

static int scp_host_cmd_send(const struct ec_host_cmd_backend *backend)
{
	ARG_UNUSED(backend);

	memset(scp_tx_rpmsg_buf, 0, RPMSG_PREAMBLE_SIZE);
	scp_tx_rpmsg_buf[0] = HOST_COMMAND_MARK;

	return ipm_send(ipm_dev, 1, SCP_IPI_HOST_COMMAND, scp_tx_rpmsg_buf,
			RPMSG_PREAMBLE_SIZE + scp_tx_buf->len);
}

static const struct ec_host_cmd_backend_api scp_host_cmd_api = {
	.init = scp_host_cmd_init,
	.send = scp_host_cmd_send,
};

static struct ec_host_cmd_backend scp_host_cmd_backend = {
	.api = &scp_host_cmd_api,
};

int mkbp_set_host_active_via_custom(int active, uint32_t *timestamp)
{
	struct cros_ec_rpmsg_response event_resp = {
		.type = HOST_EVENT_MARK,
	};
	int ret;

	if (timestamp) {
		*timestamp = __hw_clock_source_read();
	}

	if (!active) {
		return EC_SUCCESS;
	}

	ret = ipm_send(ipm_dev, 1, SCP_IPI_HOST_COMMAND, &event_resp,
		       sizeof(event_resp));
	return ret == 0 ? EC_SUCCESS : EC_ERROR_BUSY;
}

static enum ec_host_cmd_status
host_command_host_sleep_event(struct ec_host_cmd_handler_args *args)
{
	const struct ec_params_host_sleep_event_v1 *p = args->input_buf;
	enum host_sleep_event state = p->sleep_event;

	switch (state) {
	case HOST_SLEEP_EVENT_S0IX_SUSPEND:
	case HOST_SLEEP_EVENT_S3_SUSPEND:
	case HOST_SLEEP_EVENT_S3_WAKEABLE_SUSPEND:
		hook_notify(HOOK_CHIPSET_SUSPEND);
		break;

	case HOST_SLEEP_EVENT_S0IX_RESUME:
	case HOST_SLEEP_EVENT_S3_RESUME:
		hook_notify(HOOK_CHIPSET_RESUME);
		break;

	default:
		break;
	}

	return EC_HOST_CMD_SUCCESS;
}
EC_HOST_CMD_HANDLER_REQ_ONLY(EC_CMD_HOST_SLEEP_EVENT,
			     host_command_host_sleep_event,
			     EC_VER_MASK(0) | EC_VER_MASK(1),
			     struct ec_params_host_sleep_event_v1);

static void scp_ipi_callback(const struct device *ipmdev, void *user_data,
			     uint32_t id, volatile void *data)
{
	ARG_UNUSED(user_data);

	if (id == SCP_IPI_HOST_COMMAND) {
		const struct ec_host_cmd_request_header *hdr =
			(const struct ec_host_cmd_request_header *)data;
		size_t pkt_size;

		if (!scp_rx_ctx || !scp_rx_ctx->buf) {
			return;
		}

		pkt_size = sizeof(*hdr) + hdr->data_len;
		if (pkt_size <= scp_rx_ctx->len_max) {
			memcpy(scp_rx_ctx->buf, (const void *)data, pkt_size);
			scp_rx_ctx->len = pkt_size;
		} else {
			scp_rx_ctx->len = 0;
		}

		ec_host_cmd_rx_notify();
		return;
	}

	if (id == SCP_IPI_VDEC_H264) {
		uint32_t len = 3;

		LOG_INF("IPI callback: id=%d, len=%d, data=%.*s", id, len, len,
			(const char *)data);
		ipm_send(ipmdev, 0, id, (const void *)data, len);
	}
}

static int scp_host_cmd_backend_init(void)
{
	if (!device_is_ready(ipm_dev)) {
		LOG_ERR("IPM device not ready");
		return -ENODEV;
	}

	return ec_host_cmd_init(&scp_host_cmd_backend);
}
SYS_INIT(scp_host_cmd_backend_init, POST_KERNEL,
	 CONFIG_EC_HOST_CMD_INIT_PRIORITY);

static void scp_boot_handshake(void)
{
	struct scp_run_t scp_run = { 0 };
	struct rpmsg_ns_msg ns_msg = { 0 };
	int ret;

	if (!device_is_ready(ipm_dev)) {
		LOG_ERR("IPM device not ready");
		return;
	}

	/* Register callback to handle incoming messages */
	ipm_register_callback(ipm_dev, scp_ipi_callback, NULL);

	/* Enable incoming interrupts before signaling AP */
	ipm_set_enabled(ipm_dev, 1);

	/* Inform AP that SCP is up and ready */
	scp_run.signaled = 1;
	strncpy((char *)scp_run.fw_ver, CROS_EC_VERSION32, SCP_FW_VERSION_LEN);
	scp_run.fw_ver[SCP_FW_VERSION_LEN - 1] = '\0';
	scp_run.dec_capability = video_get_dec_capability();
	scp_run.enc_capability = video_get_enc_capability();

	LOG_INF("Sending SCP_IPI_INIT to AP, version: %s",
		(char *)scp_run.fw_ver);

	ret = ipm_send(ipm_dev, 1, SCP_IPI_INIT, &scp_run, sizeof(scp_run));
	if (ret) {
		LOG_ERR("failed to send initialization IPC messages: %d", ret);
		return;
	}

	/* Announce cros-ec-rpmsg channel to AP mtk_rpmsg name service */
	strncpy(ns_msg.name, "cros-ec-rpmsg", RPMSG_NAME_SIZE - 1);
	ns_msg.addr = SCP_IPI_HOST_COMMAND;

	ret = ipm_send(ipm_dev, 1, SCP_IPI_NS_SERVICE, &ns_msg, sizeof(ns_msg));
	if (ret) {
		LOG_ERR("failed to send rpmsg NS announcement: %d", ret);
	}
}
DECLARE_HOOK(HOOK_INIT, scp_boot_handshake, HOOK_PRIO_LAST);
