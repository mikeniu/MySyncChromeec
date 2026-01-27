/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Intel STC (Skin Temperature Control) driver.
 *
 * This driver periodically reports skin temperature readings to the CPU via
 * PECI, allowing the platform to throttle performance to maintain thermal
 * targets. The driver manages its lifecycle based on AP power state transitions
 * (S0, S0ix, Sx).
 */

#define DT_DRV_COMPAT intel_stc_sensors

#include "ap_power/ap_pwrseq.h"
#include "temp_sensor/temp_sensor.h"

#include <stdlib.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/peci.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>

/* PECI PCS (Package Configuration Space) command indices */
#define PCS_STC_CMD 66
#define PCS_TEMP_REP_CMD 69

/* Encode target temperature and gain into PCS STC control fields */
#define PCS_STC_SET_TARGET_TEMP(temp) ((temp) << 1)
#define PCS_STC_SET_GAIN(gain) (((gain) & 0x7) << 1)

/* Minimum temperature delta (in Kelvin) required to send a new report */
#define STC_TEMP_HYSTERESIS_K 1

#define STC_NODE DT_NODELABEL(stc)

LOG_MODULE_REGISTER(stc, LOG_LEVEL_INF);

/* STC sensor state machine: NONE -> CTRL -> DATA (periodic) */
enum stc_sensor_state {
	STC_SENSOR_STATE_SEND_NONE, /* Inactive, no PECI transactions */
	STC_SENSOR_STATE_SEND_CTRL, /* Send control config on next tick */
	STC_SENSOR_STATE_SEND_DATA, /* Periodically send temperature data */
};

struct stc_sensor_cfg {
	const struct device *interface_dev;
	int temp_sensor_id;
	int period_s0_ms;
	int target;
	int gain;
	int min_perf_level;
};

struct stc_sensor_data {
	const struct device *self;
	struct k_work_delayable dwork;
	enum stc_sensor_state state;
	int prev_temp_read;
};

/* PECI WrPkgConfig qword payload layout */
struct pcs_wr_qword {
	uint8_t host_id;
	uint8_t index;
	uint8_t parameter[2];
	uint8_t data[4];
};

/* Send STC control parameters (target temp, gain, min perf) to the CPU */
static void stc_send_control(const struct device *dev)
{
	const struct stc_sensor_cfg *cfg = dev->config;
	uint8_t read;
	struct peci_msg msg;
	struct pcs_wr_qword ctrl;
	int ret;

	ctrl.host_id = 0;
	ctrl.index = PCS_STC_CMD;
	ctrl.parameter[0] = 0;
	ctrl.parameter[1] = 0;
	ctrl.data[0] = PCS_STC_SET_TARGET_TEMP(cfg->target);
	ctrl.data[1] = 1;
	ctrl.data[1] |= PCS_STC_SET_GAIN(cfg->gain);
	ctrl.data[2] = cfg->min_perf_level;
	ctrl.data[3] = 0;

	msg.addr = 0x10;
	msg.cmd_code = PECI_CMD_WR_PKG_CFG0;
	msg.oob_addr = 0x30;
	msg.tx_buffer.buf = (uint8_t *)&ctrl;
	msg.tx_buffer.len = sizeof(ctrl);

	msg.rx_buffer.buf = &read;
	msg.rx_buffer.len = 1;

	ret = peci_transfer(cfg->interface_dev, &msg);
	if (ret) {
		LOG_ERR("Unable to send STC");
	}
}

/* Read the skin temperature sensor and report it to the CPU via PECI */
static void stc_send_skin_temp(const struct device *dev)
{
	const struct stc_sensor_cfg *cfg = dev->config;
	struct stc_sensor_data *data = dev->data;
	uint8_t resp;
	int temp_read;
	struct peci_msg msg;
	struct pcs_wr_qword rep;
	int ret;

	ret = temp_sensor_read(cfg->temp_sensor_id, &temp_read);
	if (ret) {
		LOG_ERR("STC: Unable to get sensor ID %d", cfg->temp_sensor_id);
		return;
	}
	/* Skip reporting if temperature hasn't changed beyond hysteresis */
	if (data->prev_temp_read &&
	    (abs(data->prev_temp_read - temp_read) <= STC_TEMP_HYSTERESIS_K)) {
		return;
	}

	int temp_c_x10 = K_TO_C(temp_read) * 10;

	rep.host_id = 0;
	rep.index = PCS_TEMP_REP_CMD;
	rep.parameter[0] = 0;
	rep.parameter[1] = 0;
	rep.data[0] = temp_c_x10 & 0xFF;
	rep.data[1] = (temp_c_x10 >> 8) & 0xFF;
	rep.data[2] = 0;
	rep.data[3] = 0;

	msg.addr = 0x10;
	msg.cmd_code = PECI_CMD_WR_PKG_CFG0;
	msg.oob_addr = 0x30;
	msg.tx_buffer.buf = (uint8_t *)&rep;
	msg.tx_buffer.len = sizeof(rep);

	msg.rx_buffer.buf = &resp;
	msg.rx_buffer.len = 1;

	ret = peci_transfer(cfg->interface_dev, &msg);
	if (ret) {
		LOG_ERR("STC: Unable to send Temperature report");
		return;
	}
	data->prev_temp_read = temp_read;
}

/* Periodic work handler: sends control on first tick, then temperature data */
static void stc_work_handler(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct stc_sensor_data *data =
		CONTAINER_OF(dwork, struct stc_sensor_data, dwork);
	const struct device *dev = data->self;
	const struct stc_sensor_cfg *cfg = dev->config;

	switch (data->state) {
	case STC_SENSOR_STATE_SEND_CTRL:
		stc_send_control(dev);
		data->state = STC_SENSOR_STATE_SEND_DATA;
		break;
	case STC_SENSOR_STATE_SEND_DATA:
		stc_send_skin_temp(dev);
		break;
	default:
		return;
	}

	k_work_schedule(&data->dwork, K_MSEC(cfg->period_s0_ms));
}

static int stc_init(const struct device *dev)
{
	struct stc_sensor_data *data = dev->data;

	data->self = dev;
	k_work_init_delayable(&data->dwork, stc_work_handler);

	return 0;
}

#define STC_SENSOR_DEFINE(node_id)                                             \
	static struct stc_sensor_data stc_data_##node_id;                      \
                                                                               \
	static const struct stc_sensor_cfg stc_cfg_##node_id = {               \
		.interface_dev =                                               \
			DEVICE_DT_GET(DT_PHANDLE(STC_NODE, interface)),        \
		.temp_sensor_id = DT_NODE_CHILD_IDX(DT_PROP(node_id, sensor)), \
		.period_s0_ms = DT_PROP(node_id, period_s0),                   \
		.target = DT_PROP(node_id, target),                            \
		.gain = DT_PROP(node_id, gain),                                \
		.min_perf_level = DT_PROP(node_id, min_perf_level),            \
	};                                                                     \
                                                                               \
	DEVICE_DT_DEFINE(node_id, stc_init, NULL, &stc_data_##node_id,         \
			 &stc_cfg_##node_id, POST_KERNEL,                      \
			 CONFIG_PDC_DRIVER_INIT_PRIORITY, NULL);

#define STC_SENSOR_NODE_DEFINE(inst) \
	DT_INST_FOREACH_CHILD(inst, STC_SENSOR_DEFINE)

DT_INST_FOREACH_STATUS_OKAY(STC_SENSOR_NODE_DEFINE)

#define STC_SENSOR_DEV_DEFINE(node_id) DEVICE_DT_GET(node_id),

#define STC_SENSOR_NODE_DEV_DEFINE(inst) \
	DT_INST_FOREACH_CHILD(inst, STC_SENSOR_DEV_DEFINE)

const struct device *sensors[] = { DT_INST_FOREACH_STATUS_OKAY(
	STC_SENSOR_NODE_DEV_DEFINE) };

/* Start STC: enable PECI, send control config, then begin periodic reports */
static void stc_start(const struct device *dev)
{
	const struct stc_sensor_cfg *cfg = dev->config;
	struct stc_sensor_data *data = dev->data;

	k_work_cancel_delayable(&data->dwork);
	peci_enable(cfg->interface_dev);
	data->state = STC_SENSOR_STATE_SEND_CTRL;
	k_work_schedule(&data->dwork, K_MSEC(cfg->period_s0_ms));
}

/* Stop STC: cancel work and disable PECI interface */
static void stc_stop(const struct device *dev)
{
	const struct stc_sensor_cfg *cfg = dev->config;
	struct stc_sensor_data *data = dev->data;

	data->state = STC_SENSOR_STATE_SEND_NONE;
	k_work_cancel_delayable(&data->dwork);
	peci_disable(cfg->interface_dev);
}

/* Pause STC periodic work (e.g., during S0ix) without full teardown */
static void stc_pause(const struct device *dev)
{
	struct stc_sensor_data *data = dev->data;

	k_work_cancel_delayable(&data->dwork);
}

/* Resume STC periodic work after a pause (S0ix -> S0 transition) */
static void stc_resume(const struct device *dev)
{
	const struct stc_sensor_cfg *cfg = dev->config;
	struct stc_sensor_data *data = dev->data;

	if (data->state == STC_SENSOR_STATE_SEND_NONE) {
		return;
	}
	k_work_schedule(&data->dwork, K_MSEC(cfg->period_s0_ms));
}

/* Handle AP power state entry: start/resume STC in S0, pause in S0ix */
static void stc_power_state_entry_cb(const struct device *pwrseq_dev,
				     const enum ap_pwrseq_state entry,
				     const enum ap_pwrseq_state exit)
{
	switch (entry) {
	case AP_POWER_STATE_S0:
		for (int i = 0; i < ARRAY_SIZE(sensors); i++) {
			if (exit == AP_POWER_STATE_S0ix) {
				stc_resume(sensors[i]);
			} else {
				stc_start(sensors[i]);
			}
		}
		break;
	case AP_POWER_STATE_S0ix:
		for (int i = 0; i < ARRAY_SIZE(sensors); i++) {
			stc_pause(sensors[i]);
		}
		break;
	default:
		break;
	}
}
AP_PWRSEQ_STATE_ENTRY_CALLBACK_DEFINE(stc_power_state_entry_cb,
				      AP_POWER_STATE_S0, AP_POWER_STATE_S0ix);

/* Handle AP power state exit: stop STC when leaving S0 (except to S0ix) */
static void stc_power_state_exit_cb(const struct device *pwrseq_dev,
				    const enum ap_pwrseq_state entry,
				    const enum ap_pwrseq_state exit)
{
	switch (exit) {
	case AP_POWER_STATE_S0:
		if (entry == AP_POWER_STATE_S0ix) {
			break;
		}
		for (int i = 0; i < ARRAY_SIZE(sensors); i++) {
			stc_stop(sensors[i]);
		}
		break;
	default:
		break;
	}
}
AP_PWRSEQ_STATE_EXIT_CALLBACK_DEFINE(stc_power_state_exit_cb,
				     AP_POWER_STATE_S0);
