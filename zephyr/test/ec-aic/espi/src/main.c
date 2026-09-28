/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdlib.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/espi.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(espi_aic_test, LOG_LEVEL_INF);

static const struct device *const espi_dev = DEVICE_DT_GET(DT_NODELABEL(espi0));

static struct espi_callback espi_rst_cb;
static struct espi_callback espi_ch_cb;
static struct espi_callback espi_vw_cb;
static struct k_work boot_done_work;

static atomic_t reset_count;
static atomic_t vw_ready_count;
static atomic_t vw_rx_count;

struct vw_name_map {
	const char *name;
	enum espi_vwire_signal sig;
};

static const struct vw_name_map vw_signals[] = {
	{ "slp_s3", ESPI_VWIRE_SIGNAL_SLP_S3 },
	{ "slp_s4", ESPI_VWIRE_SIGNAL_SLP_S4 },
	{ "slp_s5", ESPI_VWIRE_SIGNAL_SLP_S5 },
	{ "sus_stat", ESPI_VWIRE_SIGNAL_SUS_STAT },
	{ "pltrst", ESPI_VWIRE_SIGNAL_PLTRST },
	{ "oob_rst_warn", ESPI_VWIRE_SIGNAL_OOB_RST_WARN },
	{ "oob_rst_ack", ESPI_VWIRE_SIGNAL_OOB_RST_ACK },
	{ "wake", ESPI_VWIRE_SIGNAL_WAKE },
	{ "pme", ESPI_VWIRE_SIGNAL_PME },
	{ "slp_wlan", ESPI_VWIRE_SIGNAL_SLP_WLAN },
	{ "slp_lan", ESPI_VWIRE_SIGNAL_SLP_LAN },
	{ "slp_a", ESPI_VWIRE_SIGNAL_SLP_A },
	{ "sus_warn", ESPI_VWIRE_SIGNAL_SUS_WARN },
	{ "sus_pwrdn_ack", ESPI_VWIRE_SIGNAL_SUS_PWRDN_ACK },
	{ "sus_ack", ESPI_VWIRE_SIGNAL_SUS_ACK },
	{ "host_rst_warn", ESPI_VWIRE_SIGNAL_HOST_RST_WARN },
	{ "host_rst_ack", ESPI_VWIRE_SIGNAL_HOST_RST_ACK },
	{ "sci", ESPI_VWIRE_SIGNAL_SCI },
	{ "smi", ESPI_VWIRE_SIGNAL_SMI },
	{ "rcin", ESPI_VWIRE_SIGNAL_RST_CPU_INIT },
	{ "err_fatal", ESPI_VWIRE_SIGNAL_ERR_FATAL },
	{ "err_nonfatal", ESPI_VWIRE_SIGNAL_ERR_NON_FATAL },
	{ "boot_done", ESPI_VWIRE_SIGNAL_TARGET_BOOT_DONE },
	{ "boot_sts", ESPI_VWIRE_SIGNAL_TARGET_BOOT_STS },
};

static int lookup_vw_signal(const char *name, enum espi_vwire_signal *out)
{
	for (size_t i = 0; i < ARRAY_SIZE(vw_signals); i++) {
		if (strcmp(name, vw_signals[i].name) == 0) {
			*out = vw_signals[i].sig;
			return 0;
		}
	}
	return -EINVAL;
}

static void boot_done_work_handler(struct k_work *work)
{
	uint8_t boot_done = 0;

	ARG_UNUSED(work);

	if (espi_receive_vwire(espi_dev, ESPI_VWIRE_SIGNAL_TARGET_BOOT_DONE,
			       &boot_done) == 0 &&
	    !boot_done) {
		(void)espi_send_vwire(espi_dev,
				      ESPI_VWIRE_SIGNAL_TARGET_BOOT_STS, 1);
		(void)espi_send_vwire(espi_dev,
				      ESPI_VWIRE_SIGNAL_TARGET_BOOT_DONE, 1);
	}
}

static void on_espi_bus_reset(const struct device *dev,
			      struct espi_callback *cb, struct espi_event event)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(cb);

	if (event.evt_type == ESPI_BUS_RESET) {
		atomic_inc(&reset_count);
		LOG_DBG("eSPI bus reset callback: data=%u", event.evt_data);
	}
}

static void on_espi_ch_ready(const struct device *dev, struct espi_callback *cb,
			     struct espi_event event)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(cb);

	if (event.evt_type == ESPI_BUS_EVENT_CHANNEL_READY &&
	    event.evt_details == ESPI_CHANNEL_VWIRE && event.evt_data == 1) {
		atomic_inc(&vw_ready_count);
		k_work_submit(&boot_done_work);
	}
}

static void on_espi_vw_received(const struct device *dev,
				struct espi_callback *cb,
				struct espi_event event)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(cb);

	if (event.evt_type == ESPI_BUS_EVENT_VWIRE_RECEIVED) {
		atomic_inc(&vw_rx_count);
		LOG_DBG("eSPI VW received: sig=%u level=%u", event.evt_details,
			event.evt_data);
	}
}

static int espi_aic_test_init(void)
{
	struct espi_cfg cfg = {
		.io_caps = ESPI_IO_MODE_SINGLE_LINE | ESPI_IO_MODE_DUAL_LINES |
			   ESPI_IO_MODE_QUAD_LINES,
		.channel_caps = ESPI_CHANNEL_VWIRE | ESPI_CHANNEL_PERIPHERAL |
				ESPI_CHANNEL_OOB,
		.max_freq = 20,
	};
	int ret;

	if (!device_is_ready(espi_dev)) {
		LOG_ERR("eSPI device not ready");
		return -ENODEV;
	}

	k_work_init(&boot_done_work, boot_done_work_handler);

	espi_init_callback(&espi_rst_cb, on_espi_bus_reset, ESPI_BUS_RESET);
	espi_add_callback(espi_dev, &espi_rst_cb);

	espi_init_callback(&espi_ch_cb, on_espi_ch_ready,
			   ESPI_BUS_EVENT_CHANNEL_READY);
	espi_add_callback(espi_dev, &espi_ch_cb);

	espi_init_callback(&espi_vw_cb, on_espi_vw_received,
			   ESPI_BUS_EVENT_VWIRE_RECEIVED);
	espi_add_callback(espi_dev, &espi_vw_cb);

	ret = espi_config(espi_dev, &cfg);
	if (ret < 0) {
		LOG_ERR("espi_config failed: %d", ret);
		return ret;
	}

	LOG_INF("eSPI AIC test helper initialized");
	return 0;
}

SYS_INIT(espi_aic_test_init, APPLICATION, 50);

static int cmd_espi_test_status(const struct shell *sh, size_t argc,
				char **argv)
{
	bool ch_rdy = espi_get_channel_status(espi_dev, ESPI_CHANNEL_VWIRE);

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "ready=%d reset_cnt=%ld vw_ready_cnt=%ld vw_rx_cnt=%ld",
		    ch_rdy ? 1 : 0, atomic_get(&reset_count),
		    atomic_get(&vw_ready_count), atomic_get(&vw_rx_count));
	return 0;
}

static int cmd_espi_test_get_vw(const struct shell *sh, size_t argc,
				char **argv)
{
	enum espi_vwire_signal sig;
	uint8_t level = 0;
	int ret;

	if (argc < 2) {
		return -EINVAL;
	}

	ret = lookup_vw_signal(argv[1], &sig);
	if (ret < 0) {
		shell_error(sh, "Unknown VW signal: %s", argv[1]);
		return ret;
	}

	ret = espi_receive_vwire(espi_dev, sig, &level);
	if (ret < 0) {
		shell_error(sh, "espi_receive_vwire(%s) failed: %d", argv[1],
			    ret);
		return ret;
	}

	shell_print(sh, "VW %s=%u", argv[1], level);
	return 0;
}

static int cmd_espi_test_send_vw(const struct shell *sh, size_t argc,
				 char **argv)
{
	enum espi_vwire_signal sig;
	uint8_t level;
	int ret;

	if (argc < 3) {
		return -EINVAL;
	}

	ret = lookup_vw_signal(argv[1], &sig);
	if (ret < 0) {
		shell_error(sh, "Unknown VW signal: %s", argv[1]);
		return ret;
	}

	level = (uint8_t)strtoul(argv[2], NULL, 0);
	ret = espi_send_vwire(espi_dev, sig, level ? 1 : 0);
	if (ret < 0) {
		shell_error(sh, "espi_send_vwire(%s, %u) failed: %d", argv[1],
			    level, ret);
		return ret;
	}

	shell_print(sh, "SENT %s=%u", argv[1], level ? 1 : 0);
	return 0;
}

static int cmd_espi_test_pulse_vw(const struct shell *sh, size_t argc,
				  char **argv)
{
	enum espi_vwire_signal sig;
	int ret;

	if (argc < 2) {
		return -EINVAL;
	}

	ret = lookup_vw_signal(argv[1], &sig);
	if (ret < 0) {
		shell_error(sh, "Unknown VW signal: %s", argv[1]);
		return ret;
	}

	(void)espi_send_vwire(espi_dev, sig, 1);
	k_msleep(10);
	ret = espi_send_vwire(espi_dev, sig, 0);
	k_msleep(10);
	(void)espi_send_vwire(espi_dev, sig, 1);

	if (ret < 0) {
		shell_error(sh, "espi_send_vwire pulse (%s) failed: %d",
			    argv[1], ret);
		return ret;
	}

	shell_print(sh, "PULSED %s", argv[1]);
	return 0;
}

/* clang-format off */
SHELL_STATIC_SUBCMD_SET_CREATE(sub_espi_test,
	SHELL_CMD_ARG(status, NULL, "Print EC eSPI status and event counters",
		      cmd_espi_test_status, 1, 0),
	SHELL_CMD_ARG(get_vw, NULL, "Read VW level: espi_test get_vw <name>",
		      cmd_espi_test_get_vw, 2, 0),
	SHELL_CMD_ARG(send_vw, NULL,
		      "Send VW level: espi_test send_vw <name> <0|1>",
		      cmd_espi_test_send_vw, 3, 0),
	SHELL_CMD_ARG(pulse_vw, NULL,
		      "Pulse active-low VW (1->0->1): espi_test pulse_vw <name>",
		      cmd_espi_test_pulse_vw, 2, 0),
	SHELL_SUBCMD_SET_END
);
/* clang-format on */

SHELL_CMD_REGISTER(espi_test, &sub_espi_test, "EC AIC eSPI test commands",
		   NULL);
