/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ec_version.h"
#include "hooks.h"
#include "ipi_chip.h"

#include <zephyr/device.h>
#include <zephyr/drivers/ipm.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(scp, LOG_LEVEL_INF);

__attribute__((weak)) uint32_t video_get_dec_capability(void)
{
	return 0;
}

__attribute__((weak)) uint32_t video_get_enc_capability(void)
{
	return 0;
}

static void scp_ipi_callback(const struct device *ipmdev, void *user_data,
			     uint32_t id, volatile void *data)
{
	uint32_t len = 3;

	LOG_INF("IPI callback: id=%d, len=%d, data=%.*s", id, len, len,
		(char *)data);

	/* Send ACK back to AP directly from ISR (wait=0 to avoid block) */
	int ret = ipm_send(ipmdev, 0, id, (void *)data, len);
	if (ret) {
		LOG_ERR("Failed to send direct IPI ACK: %d", ret);
	}
}

static int scp_boot_handshake(void)
{
	const struct device *ipm_dev = DEVICE_DT_GET(DT_NODELABEL(ipi));
	struct scp_run_t scp_run;
	int ret;

	if (!device_is_ready(ipm_dev)) {
		LOG_ERR("IPM device not ready");
		return -ENODEV;
	}

	/* Register callback to handle incoming messages (including boot ACK) */
	ipm_register_callback(ipm_dev, scp_ipi_callback, NULL);

	/* Inform AP that SCP is up and ready */
	scp_run.signaled = 1;
	strncpy((char *)scp_run.fw_ver, CROS_EC_VERSION32, SCP_FW_VERSION_LEN);
	scp_run.fw_ver[SCP_FW_VERSION_LEN - 1] = '\0';
	scp_run.dec_capability = video_get_dec_capability();
	scp_run.enc_capability = video_get_enc_capability();

	LOG_INF("Sending SCP_IPI_INIT to AP, version: %s",
		(char *)scp_run.fw_ver);

	k_msleep(10);

	ret = ipm_send(ipm_dev, 1, 0, &scp_run, sizeof(scp_run));
	if (ret) {
		LOG_ERR("failed to send initialization IPC messages: %d", ret);
		return ret;
	}

	/* Enable incoming interrupts */
	ipm_set_enabled(ipm_dev, 1);

	return 0;
}

SYS_INIT(scp_boot_handshake, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
