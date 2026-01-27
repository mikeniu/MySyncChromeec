/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ap_power/ap_pwrseq.h"
#include "temp_sensor/temp_sensor.h"

#include <stdlib.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/espi.h>
#include <zephyr/drivers/peci.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>

#define PCS_STC_CMD 66
#define PCS_TEMP_REP_CMD 69

#define PCS_STC_SET_TARGET_TEMP(temp) (temp << 1)
#define PCS_STC_SET_GAIN(gain) ((gain & 0x7) << 1)

#define DTT_NODE DT_NODELABEL(dtt)
#define DTT_DEV DEVICE_DT_GET(DTT_NODE)

LOG_MODULE_REGISTER(dtt, LOG_LEVEL_INF);

enum dtt_sensor_type { TEMP_SKIN, TEMP_DDR, SENSOR_MAX };

struct dtt_channel_data {
	int *sensors_val;
};

struct dtt_skin_sensor_cfg {
	int target;
	int gain;
	int min_perf_level;
};

struct dtt_sensor_cfg {
	int temp_sensor_id;
	enum dtt_sensor_type type;
	int period_s0_ms;
	int period_s5_ms;
	const void *params;
};

struct dtt_channel_cfg {
	const struct device *interface_dev;
	const struct dtt_sensor_cfg *sensors;
	size_t num_sensors;
};

struct pcs_wr_qword {
	uint8_t host_id;
	uint8_t index;
	uint8_t parameter[2];
	uint8_t data[4];
};

#define DTT_SKIN_SENSOR_DEFINE(child)                                       \
	static const struct dtt_skin_sensor_cfg dtt_skin_sensor_##child = { \
		.target = DT_PROP(child, target),                           \
		.gain = DT_PROP(child, gain),                               \
		.min_perf_level = DT_PROP(child, min_perf_level),           \
	};

DT_FOREACH_CHILD(DTT_NODE, DTT_SKIN_SENSOR_DEFINE)

#define DTT_SENSOR_DEFINE(child)                                             \
	{                                                                    \
		.temp_sensor_id = DT_NODE_CHILD_IDX(DT_PROP(child, sensor)), \
		.type = DT_STRING_TOKEN(child, type),                        \
		.period_s0_ms = DT_PROP(child, period_s0),                   \
		.period_s5_ms = DT_PROP(child, period_s5),                   \
		.params = (const void *)&dtt_skin_sensor_##child,            \
	},

static const struct dtt_sensor_cfg dtt_sensors[] = { DT_FOREACH_CHILD(
	DTT_NODE, DTT_SENSOR_DEFINE) };

static const struct dtt_channel_cfg dtt_config = {
	.interface_dev = DEVICE_DT_GET(DT_PHANDLE(DTT_NODE, interface)),
	.sensors = dtt_sensors,
	.num_sensors = ARRAY_SIZE(dtt_sensors),
};

static int sensors_value[ARRAY_SIZE(dtt_sensors)];

static struct dtt_channel_data dtt_data = {
	.sensors_val = sensors_value,
};

static struct k_work_delayable stc_work;
static bool send_stc;
static bool send_temp;

static void dtt_send_stc_send_control(void)
{
	uint8_t read;
	struct peci_msg msg;
	struct pcs_wr_qword ctrl;
	int ret;

	peci_enable(dtt_config.interface_dev);

	for (int i = 0; i < dtt_config.num_sensors; i++) {
		const struct dtt_skin_sensor_cfg *sk_cfg;

		if (dtt_sensors[i].type != TEMP_SKIN) {
			continue;
		}
		sk_cfg = (struct dtt_skin_sensor_cfg *)dtt_sensors[i].params;
		ctrl.host_id = 0;
		ctrl.index = PCS_STC_CMD;
		ctrl.parameter[0] = 0;
		ctrl.parameter[1] = 0;
		ctrl.data[0] = PCS_STC_SET_TARGET_TEMP(sk_cfg->target);
		ctrl.data[1] = 1;
		ctrl.data[1] |= PCS_STC_SET_GAIN(sk_cfg->gain);
		ctrl.data[2] = sk_cfg->min_perf_level;
		ctrl.data[3] = 0;

		msg.addr = 0x10;
		msg.cmd_code = PECI_CMD_WR_PKG_CFG0;
		msg.oob_addr = 0x30;
		msg.tx_buffer.buf = (uint8_t *)&ctrl;
		msg.tx_buffer.len = sizeof(ctrl);

		msg.rx_buffer.buf = &read;
		msg.rx_buffer.len = 1;

		ret = peci_transfer(dtt_config.interface_dev, &msg);
		if (ret) {
			LOG_ERR("Unable to send STC");
		}
	}
}

static void dtt_send_stc_skin_temp(void)
{
	uint8_t resp;
	static int prev_temp_read = 0;
	int temp_read;
	struct peci_msg msg;
	struct pcs_wr_qword rep;
	int ret;

	peci_enable(dtt_config.interface_dev);

	ret = temp_sensor_read(dtt_sensors[0].temp_sensor_id, &temp_read);
	if (ret) {
		LOG_ERR("DTT: Unable to get sensor ID %d",
			dtt_sensors[0].temp_sensor_id);
		return;
	}
	if (prev_temp_read && (abs(prev_temp_read - temp_read) < 1)) {
		return;
	}
	prev_temp_read = temp_read;

	rep.host_id = 0;
	rep.index = PCS_TEMP_REP_CMD;
	rep.parameter[0] = 0;
	rep.parameter[1] = 0;
	rep.data[0] = (K_TO_C(temp_read) * 10) & 0xFF;
	rep.data[1] = ((K_TO_C(temp_read) * 10) >> 8) & 0xFF;
	rep.data[2] = 0;
	rep.data[3] = 0;

	msg.addr = 0x10;
	msg.cmd_code = PECI_CMD_WR_PKG_CFG0;
	msg.oob_addr = 0x30;
	msg.tx_buffer.buf = (uint8_t *)&rep;
	msg.tx_buffer.len = sizeof(rep);

	msg.rx_buffer.buf = &resp;
	msg.rx_buffer.len = 1;

	ret = peci_transfer(dtt_config.interface_dev, &msg);
	if (ret) {
		LOG_ERR("DTT: Unable to send Temperature report");
	}
}

static void dtt_power_state_entry_cb(const struct device *dev,
				     const enum ap_pwrseq_state entry,
				     const enum ap_pwrseq_state exit)
{
	if (entry == AP_POWER_STATE_S0) {
		LOG_INF("DTT: Start sending STC packets");
		/* Start sending STC information  */
		send_stc = true;
		k_work_schedule(&stc_work, K_MSEC(dtt_sensors[0].period_s0_ms));
	}
}

static void dtt_power_state_exit_cb(const struct device *dev,
				    const enum ap_pwrseq_state entry,
				    const enum ap_pwrseq_state exit)
{
	if (exit == AP_POWER_STATE_S0) {
		/* Stop sending STC information  */
		LOG_INF("DTT: Stop sending STC packets");
		send_stc = false;
		send_temp = false;
	}
}

static void dtt_stc_handler(struct k_work *dc_jack_work)
{
	if (send_stc) {
		dtt_send_stc_send_control();
		send_stc = false;
		send_temp = true;
	} else if (send_temp) {
		dtt_send_stc_skin_temp();
	} else {
		/* Exit without rescheduling */
		return;
	}
	k_work_schedule(&stc_work, K_MSEC(dtt_sensors[0].period_s0_ms));
}

static int dtt_init(const struct device *dev)
{
	const struct device *ap_pwrseq_dev = ap_pwrseq_get_instance();
	static struct ap_pwrseq_state_callback dtt_entry_cb = {
		.cb = dtt_power_state_entry_cb,
		.states_bit_mask = BIT(AP_POWER_STATE_S0),
	};
	static struct ap_pwrseq_state_callback dtt_exit_cb = {
		.cb = dtt_power_state_exit_cb,
		.states_bit_mask = BIT(AP_POWER_STATE_S0),
	};

	ap_pwrseq_register_state_entry_callback(ap_pwrseq_dev, &dtt_entry_cb);
	ap_pwrseq_register_state_exit_callback(ap_pwrseq_dev, &dtt_exit_cb);

	k_work_init_delayable(&stc_work, dtt_stc_handler);
	return 0;
}

DEVICE_DT_DEFINE(DTT_NODE, dtt_init, NULL, &dtt_data, &dtt_config, POST_KERNEL,
		 CONFIG_PDC_DRIVER_INIT_PRIORITY, NULL);

static int cmd_dtt_send_stc(const struct shell *sh, size_t argc, char **argv)
{
	dtt_send_stc_send_control();

	return 0;
}

static int cmd_dtt_send_temp(const struct shell *sh, size_t argc, char **argv)
{
	dtt_send_stc_skin_temp();

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	sub_dtt_cmds,
	SHELL_CMD_ARG(send_stc, NULL, "Send STC control request",
		      cmd_dtt_send_stc, 0, 0),
	SHELL_CMD_ARG(send_skin_temp, NULL,
		      "Send current skin sensor temperature", cmd_dtt_send_temp,
		      0, 0),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(dtt, &sub_dtt_cmds, "DTT commands", NULL);
