/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "accelgyro.h"
#include "cbi_config.h"
#include "cros_cbi.h"
#include "driver/accelgyro_bmi3xx.h"
#include "driver/accelgyro_lsm6dsm.h"
#include "ec_commands.h"
#include "ec_version.h"
#include "hooks.h"
#include "host_command.h"
#include "hwtimer.h"
#include "ipi_chip.h"
#include "mkbp_event.h"
#include "motionsense_sensors.h"

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/ipm.h>
#include <zephyr/init.h>
#include <zephyr/irq.h>
#include <zephyr/irq_multilevel.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/mgmt/ec_host_cmd/backend.h>
#include <zephyr/mgmt/ec_host_cmd/ec_host_cmd.h>
#include <zephyr/sys/sys_io.h>

#include <tinysys_reg.h>

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

/*
 * Custom MKBP delivery hook (CONFIG_PLATFORM_EC_MKBP_USE_CUSTOM).
 *
 * Unlike a standalone EC that toggles a physical GPIO_EC_INT_L line to notify
 * the AP of pending MKBP events, the on-die SCP signals the AP's cros_ec_rpmsg
 * driver by sending an RPMSG packet with type = HOST_EVENT_MARK over
 * SCP_IPI_HOST_COMMAND when active is non-zero. Deasserting (active == 0) is a
 * no-op because RPMSG uses message-based signaling rather than a
 * level-triggered interrupt line.
 */
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
	ARG_UNUSED(ipmdev);
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

	/* Inform AP that SCP is up and ready */
	scp_run.signaled = 1;
	strncpy((char *)scp_run.fw_ver, CROS_EC_VERSION32, SCP_FW_VERSION_LEN);
	scp_run.fw_ver[SCP_FW_VERSION_LEN - 1] = '\0';

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
		return;
	}

	scp_ipi_ready = true;

	/* Enable incoming interrupts */
	ipm_set_enabled(ipm_dev, 1);
}
DECLARE_HOOK(HOOK_INIT, scp_boot_handshake, HOOK_PRIO_FIRST);

#if DT_NODE_EXISTS(DT_NODELABEL(alt_lid_accel))
static struct motion_sensor_t lid_accel_primary;
static struct motion_sensor_t lid_gyro_primary;
static bool scp_lid_use_alt;

static void scp_alt_sensor_init(void)
{
	lid_accel_primary = motion_sensors[SENSOR_ID(DT_NODELABEL(lid_accel))];
	lid_gyro_primary = motion_sensors[SENSOR_ID(DT_NODELABEL(lid_gyro))];
	scp_lid_use_alt = cros_cbi_ufsc_check_match(
		CBI_UFSC_VALUE_ID(DT_NODELABEL(ufsc_lid_sensor_lsm6dsm)));
	motion_sensors_check_ufsc();
}
DECLARE_HOOK(HOOK_INIT, scp_alt_sensor_init, HOOK_PRIO_POST_I2C);

void cbi_ram_store_notify(void)
{
	const int lid_accel_id = SENSOR_ID(DT_NODELABEL(lid_accel));
	const int lid_gyro_id = SENSOR_ID(DT_NODELABEL(lid_gyro));
	bool use_alt = cros_cbi_ufsc_check_match(
		CBI_UFSC_VALUE_ID(DT_NODELABEL(ufsc_lid_sensor_lsm6dsm)));
	const struct accelgyro_drv *target_drv =
		use_alt ? motion_sensors_alt[SENSOR_ID(DT_NODELABEL(
						     alt_lid_accel))]
				  .drv :
			  lid_accel_primary.drv;

	if (motion_sensors[lid_accel_id].drv == target_drv) {
		return;
	}

	scp_lid_use_alt = use_alt;
	motion_sensors[lid_accel_id] = lid_accel_primary;
	motion_sensors[lid_gyro_id] = lid_gyro_primary;
	motion_sensors_check_ufsc();

	for (int i = 0; i < motion_sensor_count; i++) {
		motion_sensors[i].state = SENSOR_NOT_INITIALIZED;
	}
	hook_notify(HOOK_CHIPSET_RESUME);
}
#endif

#define SCP_EINT_INTC_IRQ 6
#define SCP_EINT_INTC_IRQN \
	(IRQ_TO_L2(SCP_EINT_INTC_IRQ) | DT_IRQN(DT_NODELABEL(intc)))
#define SCP_EINT_IMU_BIT BIT(16) /* GPIO 6 (IMU_INT_SCP_L) -> EINT bit 16 */
#define SCP_EINT_STA (EINT_BASE + 0x000)
#define SCP_EINT_INTACK (EINT_BASE + 0x040)
#define SCP_EINT_MASK_SET (EINT_BASE + 0x0c0)
#define SCP_EINT_MASK_CLR (EINT_BASE + 0x100)
#define SCP_EINT_SENS_CLR (EINT_BASE + 0x1c0)
#define SCP_EINT_POL_CLR (EINT_BASE + 0x380)

static void scp_eint_isr(const void *arg)
{
	ARG_UNUSED(arg);
	uint32_t sta = sys_read32(SCP_EINT_STA) & SCP_EINT_IMU_BIT;

	if (!sta) {
		return;
	}

	sys_write32(sta, SCP_EINT_MASK_SET);
	sys_write32(sta, SCP_EINT_INTACK);
#if DT_NODE_EXISTS(DT_NODELABEL(alt_lid_accel))
	if (scp_lid_use_alt) {
		lsm6dsm_interrupt(0);
	} else {
		bmi3xx_interrupt(0);
	}
#else
	bmi3xx_interrupt(0);
#endif
	sys_write32(sta, SCP_EINT_MASK_CLR);
}

static void scp_eint_init(void)
{
	/*
	 * mt8196_scp has no Zephyr GPIO controller driver (CONFIG_GPIO=n) and
	 * routes external sensor interrupts through the MediaTek SCP EINT
	 * controller rather than a GPIO device, so cros-ec,gpio-interrupts
	 * cannot be used; configure SCP EINT bit 16 and INTC IRQ 6 directly.
	 */
	sys_write32(SCP_EINT_IMU_BIT, SCP_EINT_MASK_SET);
	sys_write32(SCP_EINT_IMU_BIT, SCP_EINT_POL_CLR);
	sys_write32(SCP_EINT_IMU_BIT, SCP_EINT_SENS_CLR);
	sys_set_bits(EINT_D0_EN, SCP_EINT_IMU_BIT);
	sys_write32(SCP_EINT_IMU_BIT, SCP_EINT_INTACK);

	sys_clear_bits(INTC_IRQ_POL0, BIT(SCP_EINT_INTC_IRQ));
	sys_set_bits(CORE0_INTC_IRQ_GRP4_0, BIT(SCP_EINT_INTC_IRQ));
	IRQ_CONNECT(SCP_EINT_INTC_IRQN, 0, scp_eint_isr, NULL, 0);
	irq_enable(SCP_EINT_INTC_IRQN);

	sys_write32(SCP_EINT_IMU_BIT, SCP_EINT_MASK_CLR);
}
DECLARE_HOOK(HOOK_INIT, scp_eint_init, HOOK_PRIO_DEFAULT);
