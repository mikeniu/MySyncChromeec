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
static bool scp_ipi_ready;

#ifdef CONFIG_SCP_CHRE
/*
 * MT8196 SCP IPI share_buf payload size is 288 bytes. Use a 4-byte framing
 * header to chunk and reassemble FlatBuffers messages up to 4096 bytes over
 * the "rpmsg_chrdev" endpoint on SCP_IPI_CHRE.
 */
#define CHRE_RPMSG_MAX_PKT_SIZE 288
#define CHRE_RPMSG_HDR_SIZE 4
#define CHRE_RPMSG_MAX_CHUNK_DATA \
	(CHRE_RPMSG_MAX_PKT_SIZE - CHRE_RPMSG_HDR_SIZE)
#define CHRE_RPMSG_FIRST_CHUNK_FLAG 0x8000
#define CHRE_RPMSG_TOTAL_LEN_MASK 0x7FFF
#define CHRE_RX_MSG_MAX_SIZE 4096
#define CHRE_RX_QUEUE_DEPTH 16

struct chre_rpmsg_chunk_hdr {
	uint16_t chunk_len;
	uint16_t total_len;
} __packed;
BUILD_ASSERT(sizeof(struct chre_rpmsg_chunk_hdr) == CHRE_RPMSG_HDR_SIZE);

struct chre_rx_msg_slot {
	uint32_t len;
	uint8_t data[CHRE_RX_MSG_MAX_SIZE] __aligned(8);
};

extern void chrePlatformReceiveMessageFromHost(const uint8_t *message,
					       size_t messageLen);
extern bool chre_is_ready(void);

static K_MUTEX_DEFINE(chre_tx_mutex);
static uint8_t chre_rx_staging_buf[CHRE_RX_MSG_MAX_SIZE];
static uint16_t chre_rx_staging_len;
static uint16_t chre_rx_expected_len;

static struct chre_rx_msg_slot chre_rx_queue[CHRE_RX_QUEUE_DEPTH];
static uint8_t chre_rx_q_head;
static uint8_t chre_rx_q_tail;
static uint8_t chre_rx_q_count;
static struct k_spinlock chre_rx_lock;

static void chre_rx_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	while (true) {
		k_spinlock_key_t key = k_spin_lock(&chre_rx_lock);

		if (chre_rx_q_count == 0) {
			k_spin_unlock(&chre_rx_lock, key);
			break;
		}

		uint8_t slot_idx = chre_rx_q_head;
		uint16_t msg_len = chre_rx_queue[slot_idx].len;

		k_spin_unlock(&chre_rx_lock, key);

		for (int retry = 0; retry < 50 && !chre_is_ready(); retry++) {
			k_msleep(2);
		}

		chrePlatformReceiveMessageFromHost(chre_rx_queue[slot_idx].data,
						   msg_len);

		key = k_spin_lock(&chre_rx_lock);
		chre_rx_q_head = (chre_rx_q_head + 1) % CHRE_RX_QUEUE_DEPTH;
		chre_rx_q_count--;
		k_spin_unlock(&chre_rx_lock, key);
	}
}
static K_WORK_DEFINE(chre_rx_work, chre_rx_work_handler);

bool chrePlatformSendMessageToHost(const uint8_t *data, size_t size)
{
	uint8_t pkt[CHRE_RPMSG_MAX_PKT_SIZE] __aligned(4);
	size_t offset = 0;

	if (!scp_ipi_ready || data == NULL || size == 0 ||
	    size > CHRE_RPMSG_TOTAL_LEN_MASK) {
		return false;
	}

	k_mutex_lock(&chre_tx_mutex, K_FOREVER);
	while (offset < size) {
		struct chre_rpmsg_chunk_hdr *hdr =
			(struct chre_rpmsg_chunk_hdr *)pkt;
		size_t chunk_len = size - offset;
		int ret = -EBUSY;

		if (chunk_len > CHRE_RPMSG_MAX_CHUNK_DATA) {
			chunk_len = CHRE_RPMSG_MAX_CHUNK_DATA;
		}

		hdr->chunk_len = (uint16_t)chunk_len;
		hdr->total_len = (uint16_t)size;
		if (offset == 0) {
			hdr->total_len |= CHRE_RPMSG_FIRST_CHUNK_FLAG;
		}
		memcpy(pkt + CHRE_RPMSG_HDR_SIZE, data + offset, chunk_len);

		for (int retry = 0; retry < 3; retry++) {
			ret = ipm_send(ipm_dev, 1, SCP_IPI_CHRE, pkt,
				       CHRE_RPMSG_HDR_SIZE + chunk_len);
			if (ret == 0) {
				break;
			}
			k_busy_wait(200);
		}
		if (ret != 0) {
			LOG_ERR("CHRE IPI send failed: %d (off=%zu/%zu)", ret,
				offset, size);
			k_mutex_unlock(&chre_tx_mutex);
			return false;
		}
		offset += chunk_len;
	}
	k_mutex_unlock(&chre_tx_mutex);
	return true;
}
#endif /* CONFIG_SCP_CHRE */

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

	if (!scp_ipi_ready) {
		return -EBUSY;
	}

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

	if (!scp_ipi_ready) {
		return EC_ERROR_BUSY;
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
		return;
	}

#ifdef CONFIG_SCP_CHRE
	if (id == SCP_IPI_CHRE) {
		const struct chre_rpmsg_chunk_hdr *hdr =
			(const struct chre_rpmsg_chunk_hdr *)data;
		uint16_t chunk_len = hdr->chunk_len;
		bool is_first =
			(hdr->total_len & CHRE_RPMSG_FIRST_CHUNK_FLAG) != 0;
		uint16_t total_len = hdr->total_len & CHRE_RPMSG_TOTAL_LEN_MASK;

		if (chunk_len == 0 || chunk_len > CHRE_RPMSG_MAX_CHUNK_DATA ||
		    total_len == 0 || total_len > CHRE_RX_MSG_MAX_SIZE) {
			chre_rx_staging_len = 0;
			chre_rx_expected_len = 0;
			return;
		}

		if (is_first) {
			chre_rx_staging_len = 0;
			chre_rx_expected_len = total_len;
		}

		if (chre_rx_expected_len != total_len ||
		    chre_rx_staging_len + chunk_len > chre_rx_expected_len) {
			chre_rx_staging_len = 0;
			chre_rx_expected_len = 0;
			return;
		}

		memcpy(chre_rx_staging_buf + chre_rx_staging_len,
		       (const uint8_t *)data + CHRE_RPMSG_HDR_SIZE, chunk_len);
		chre_rx_staging_len += chunk_len;

		if (chre_rx_staging_len == chre_rx_expected_len) {
			k_spinlock_key_t key = k_spin_lock(&chre_rx_lock);

			if (chre_rx_q_count < CHRE_RX_QUEUE_DEPTH) {
				struct chre_rx_msg_slot *slot =
					&chre_rx_queue[chre_rx_q_tail];

				slot->len = chre_rx_staging_len;
				memcpy(slot->data, chre_rx_staging_buf,
				       chre_rx_staging_len);
				chre_rx_q_tail = (chre_rx_q_tail + 1) %
						 CHRE_RX_QUEUE_DEPTH;
				chre_rx_q_count++;
				k_spin_unlock(&chre_rx_lock, key);
				k_work_submit(&chre_rx_work);
			} else {
				k_spin_unlock(&chre_rx_lock, key);
				LOG_WRN("CHRE RX queue full, dropping %uB msg",
					chre_rx_staging_len);
			}
			chre_rx_staging_len = 0;
			chre_rx_expected_len = 0;
		}
		return;
	}
#endif /* CONFIG_SCP_CHRE */
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

#ifdef CONFIG_SCP_CHRE
	/* Announce rpmsg_chrdev channel on SCP_IPI_CHRE for CHRE HAL */
	memset(&ns_msg, 0, sizeof(ns_msg));
	strncpy(ns_msg.name, "rpmsg_chrdev", RPMSG_NAME_SIZE - 1);
	ns_msg.addr = SCP_IPI_CHRE;

	ret = ipm_send(ipm_dev, 1, SCP_IPI_NS_SERVICE, &ns_msg, sizeof(ns_msg));
	if (ret) {
		LOG_ERR("failed to send CHRE rpmsg NS announcement: %d", ret);
	} else {
		LOG_INF("Announced rpmsg_chrdev on SCP_IPI_CHRE (%d)",
			SCP_IPI_CHRE);
	}
#endif /* CONFIG_SCP_CHRE */

	scp_ipi_ready = true;
}
DECLARE_HOOK(HOOK_INIT, scp_boot_handshake, HOOK_PRIO_FIRST);
