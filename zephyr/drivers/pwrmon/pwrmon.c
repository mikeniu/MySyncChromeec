/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define DT_DRV_COMPAT cros_pwrmon

#include "ec_commands.h"
#include "host_command.h"
#include "mkbp_event.h"
#include "power_monitor.h"

#include <stdio.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(pwrmon, LOG_LEVEL_INF);

#define GET_POWER_MONITOR_DEV(node_id, prop, idx) \
	DEVICE_DT_GET(DT_PHANDLE_BY_IDX(node_id, prop, idx))

#define COUNT_CHANNELS(node_id, prop, idx) \
	DT_CHILD_NUM_STATUS_OKAY(DT_PHANDLE_BY_IDX(node_id, prop, idx)) +

#define CHANNEL_INIT(child_node, pm_idx)                                     \
	{                                                                    \
		.channel_name = DT_PROP_OR(child_node, friendly_name, NULL), \
		.power_monitor_idx = pm_idx,                                 \
		.reg_idx = DT_REG_ADDR(child_node),                          \
	},

#define CHANNELS_INIT(node_id, prop, idx)   \
	DT_FOREACH_CHILD_STATUS_OKAY_VARGS( \
		DT_PHANDLE_BY_IDX(node_id, prop, idx), CHANNEL_INIT, idx)

struct channel_config {
	const char *channel_name;
	const uint8_t power_monitor_idx;
	const uint8_t reg_idx;
};

struct pwrmon_config {
	const uint8_t power_monitors_count;
	const uint8_t channels_count;
	const struct gpio_dt_spec pwrmon_gpio;
	const struct device *power_monitors[DT_INST_PROP_LEN(0, power_monitors)];
	const struct channel_config channels[];
};

struct pwrmon_event {
	int64_t value;
	uint32_t samples;
	uint8_t channel_id;
};

struct pwrmon_data {
	uint16_t sample_rate;
	bool pwrmon_enable;
	char msgq_buf[CONFIG_PWRMON_MSGQ_SIZE * sizeof(struct pwrmon_event)];
	struct k_sem pwrmon_sem;
	struct k_msgq pwrmon_msgq;
};

/* clang-format off */
const static struct pwrmon_config pwrmon_config = {
	.power_monitors = { DT_INST_FOREACH_PROP_ELEM_SEP(
			    0, power_monitors, GET_POWER_MONITOR_DEV, (, )) },
	.power_monitors_count = DT_INST_PROP_LEN(0, power_monitors),
	.channels = { DT_INST_FOREACH_PROP_ELEM(0, power_monitors,
		      CHANNELS_INIT) },
	.channels_count = (DT_INST_FOREACH_PROP_ELEM(0, power_monitors,
			   COUNT_CHANNELS) 0),
	.pwrmon_gpio = GPIO_DT_SPEC_INST_GET(0, gpios),
};
/* clang-format on */

static struct pwrmon_data pwrmon_data = {
	/* EC starts with EC_EN_PWRMON in high state */
	.pwrmon_enable = true,
	.sample_rate = DT_INST_PROP(0, sample_rate),
};

static enum ec_status pwrmon_sample_get(uint8_t channel_id, uint32_t *samples,
					int64_t *power);
static void pwrmon_thread_entry(void *p1, void *p2, void *p3);

K_THREAD_DEFINE(pwrmon_thread_tid, CONFIG_PWRMON_THREAD_STACK_SIZE,
		pwrmon_thread_entry, NULL, NULL, NULL,
		CONFIG_PWRMON_THREAD_PRIORITY, 0, SYS_FOREVER_MS);

static int pwrmon_get_next_event(uint8_t *out)
{
	union ec_response_get_next_data_v3 *data =
		(union ec_response_get_next_data_v3 *)out;
	struct pwrmon_event event;

	if (k_msgq_get(&pwrmon_data.pwrmon_msgq, &event, K_NO_WAIT) != 0) {
		return 0;
	}

	data->pwrmon_data.channel_id = event.channel_id;
	data->pwrmon_data.samples = event.samples;
	data->pwrmon_data.value = event.value;

	if (k_msgq_num_used_get(&pwrmon_data.pwrmon_msgq) > 0) {
		mkbp_send_event(EC_MKBP_EVENT_PWRMON);
	}

	return sizeof(event);
}
DECLARE_EVENT_SOURCE(EC_MKBP_EVENT_PWRMON, pwrmon_get_next_event);

static void pwrmon_thread_entry(void *p1, void *p2, void *p3)
{
	uint32_t sample_count = 0;
	int64_t power = 0;
	int ret;

	while (1) {
		/* Wait for trigger from LATCH */
		k_sem_take(&pwrmon_data.pwrmon_sem, K_FOREVER);

		for (uint8_t i = 0; i < pwrmon_config.channels_count; ++i) {
			ret = pwrmon_sample_get(i, &sample_count, &power);
			if (ret == EC_RES_SUCCESS) {
				struct pwrmon_event event = {
					.channel_id = i,
					.samples = sample_count,
					.value = power,
				};
				k_msgq_put(&pwrmon_data.pwrmon_msgq, &event,
					   K_FOREVER);
				mkbp_send_event(EC_MKBP_EVENT_PWRMON);
			}
		}
	}
}

static int pwrmon_init(const struct device *dev)
{
	const struct pwrmon_config *config = dev->config;
	struct pwrmon_data *data = dev->data;

	if (!gpio_is_ready_dt(&config->pwrmon_gpio)) {
		LOG_ERR_DEVICE_NOT_READY(config->pwrmon_gpio.port);
		return -ENODEV;
	}

	k_sem_init(&data->pwrmon_sem, 0, 1);
	k_msgq_init(&data->pwrmon_msgq, data->msgq_buf,
		    sizeof(struct pwrmon_event), CONFIG_PWRMON_MSGQ_SIZE);
	k_thread_name_set(pwrmon_thread_tid, "pwrmon");
	k_thread_start(pwrmon_thread_tid);

	return 0;
}

static int pwrmon_monitors_start(void)
{
	const struct device *power_monitor;
	int ret;

	for (uint8_t i = 0; i < pwrmon_config.power_monitors_count; ++i) {
		power_monitor = pwrmon_config.power_monitors[i];
		if (device_is_ready(power_monitor)) {
			continue;
		}
		ret = device_init(power_monitor);
		if (ret) {
			LOG_ERR("Failed to initialize power monitor %d err: %d",
				i, ret);
			return ret;
		}
	}
	return 0;
}

static enum ec_status pwrmon_channels_enable(bool start)
{
	const struct device *power_monitor;
	const struct channel_config *channel;
	struct sensor_value val;
	enum sensor_attribute channel_enable_attr;
	enum sensor_channel sensor_channel;
	int enabled_channels = pwrmon_config.channels_count;
	int ret;

	channel_enable_attr = power_monitor_channel_enable_attr(&val, start);

	if ((int)channel_enable_attr >= 0) {
		for (uint8_t i = 0; i < pwrmon_config.channels_count; ++i) {
			channel = &pwrmon_config.channels[i];
			power_monitor = pwrmon_config.power_monitors
						[channel->power_monitor_idx];
			sensor_channel = power_monitor_sensor_channel(
				channel->reg_idx - 1);
			ret = sensor_attr_set(power_monitor, sensor_channel,
					      channel_enable_attr, &val);
			if (ret) {
				enabled_channels--;
				LOG_ERR("Unable to start channel %d %s err: %d",
					i, channel->channel_name, ret);
			}
		}
	}

	if (enabled_channels != pwrmon_config.channels_count) {
		return EC_RES_ERROR;
	}

	return EC_RES_SUCCESS;
}

static enum ec_status pwrmon_set_rate(uint16_t sample_rate)
{
	struct sensor_value val = { .val1 = sample_rate };
	int ret;

	for (uint8_t i = 0; i < pwrmon_config.power_monitors_count; ++i) {
		ret = sensor_attr_set(pwrmon_config.power_monitors[i],
				      SENSOR_CHAN_ALL,
				      SENSOR_ATTR_SAMPLING_FREQUENCY, &val);
		if (ret) {
			return EC_RES_ERROR;
		}
	}

	pwrmon_data.sample_rate = sample_rate;
	return EC_RES_SUCCESS;
}

static enum ec_status pwrmon_latch(void)
{
	enum sensor_channel sample_count_channel;
	enum sensor_attribute latch_attr;
	struct sensor_value val;
	int ret;

	latch_attr = power_monitor_latch_attr(&val);
	sample_count_channel = power_monitor_sample_count_channel();
	for (uint8_t i = 0; i < pwrmon_config.power_monitors_count; ++i) {
		if ((int)latch_attr >= 0) {
			ret = sensor_attr_set(pwrmon_config.power_monitors[i],
					      SENSOR_CHAN_ALL, latch_attr,
					      &val);
			if (ret) {
				LOG_WRN("Failed to latch power monitor %d err: %d",
					i, ret);
				return EC_RES_ERROR;
			}

			/* Give power monitor time to capture accumulators */
			k_msleep(2);
			ret = sensor_sample_fetch_chan(
				pwrmon_config.power_monitors[i],
				sample_count_channel);
			if (ret) {
				LOG_WRN("Failed to fetch sample count for power monitor %d err: %d",
					i, ret);
				return EC_RES_ERROR;
			}
		}
	}

	/* Trigger the measurement thread to read latched data and
	 * send it via MKBP.
	 */
	k_sem_give(&pwrmon_data.pwrmon_sem);

	return EC_RES_SUCCESS;
}

static enum ec_status pwrmon_sample_get(uint8_t channel_id, uint32_t *samples,
					int64_t *power)
{
	const struct channel_config *channel;
	const struct device *power_monitor;
	enum sensor_channel sensor_channel;
	struct sensor_value val;
	int ret;

	sensor_channel = power_monitor_sample_count_channel();
	channel = &pwrmon_config.channels[channel_id];
	power_monitor =
		pwrmon_config.power_monitors[channel->power_monitor_idx];
	ret = sensor_channel_get(power_monitor, sensor_channel, &val);
	if (ret) {
		LOG_WRN("Failed to get sample count for channel %s err: %d",
			channel->channel_name, ret);
		return EC_RES_ERROR;
	}
	*samples = val.val1;

	sensor_channel = power_monitor_sensor_channel(channel->reg_idx - 1);
	ret = sensor_sample_fetch_chan(power_monitor, sensor_channel);
	if (ret) {
		LOG_WRN("Failed to fetch average power for channel %d err: %d",
			channel_id, ret);
		return EC_RES_ERROR;
	}

	ret = sensor_channel_get(power_monitor, sensor_channel, &val);
	if (ret) {
		LOG_WRN("Failed to get average power for channel %d err: %d",
			channel_id, ret);
		return EC_RES_ERROR;
	}

	*power = llabs(sensor_value_to_micro(&val));

	return EC_RES_SUCCESS;
}
DEVICE_DT_INST_DEFINE(0, pwrmon_init, NULL, &pwrmon_data, &pwrmon_config,
		      POST_KERNEL, CONFIG_APPLICATION_INIT_PRIORITY, NULL);
BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) == 1,
	     "Exactly one instance of cros-ec,pwrmon should be defined.");

static enum ec_host_cmd_status
hc_pwrmon_handler(struct ec_host_cmd_handler_args *args)
{
	const struct device *pwrmon_dev = DEVICE_DT_INST_GET(0);
	const struct ec_params_pwrmon *p = args->input_buf;
	struct ec_response_pwrmon *r = args->output_buf;
	enum ec_host_cmd_status ret = EC_HOST_CMD_SUCCESS;
	int channel_id;

	switch (p->cmd) {
	case EC_PWRMON_GET_CHANNEL_COUNT:
		r->channel_count = pwrmon_config.channels_count;
		args->output_buf_size = sizeof(pwrmon_config.channels_count);
		break;

	case EC_PWRMON_DUMP_INFO:
		channel_id = p->channel_id;

		if (channel_id >= pwrmon_config.channels_count) {
			LOG_ERR("Channel index %d bigger than channel_count: %d",
				channel_id, pwrmon_config.channels_count);
			ret = EC_HOST_CMD_ERROR;
			break;
		}

		r->dump_info.channel_id = channel_id;
		if (pwrmon_config.channels[channel_id].channel_name) {
			strncpy(r->dump_info.channel_name,
				pwrmon_config.channels[channel_id].channel_name,
				sizeof(r->dump_info.channel_name));
		} else {
			snprintf(r->dump_info.channel_name,
				 sizeof(r->dump_info.channel_name), "%d",
				 channel_id);
		}
		args->output_buf_size = sizeof(struct pwrmon_dump_info);
		break;

	case EC_PWRMON_GET_RATE:
		r->sample_rate = pwrmon_data.sample_rate;
		args->output_buf_size = sizeof(pwrmon_data.sample_rate);
		break;

	case EC_PWRMON_SET_RATE:
		ret = pwrmon_set_rate(p->set_rate);
		if (ret) {
			LOG_WRN("Unable to set sample rate ret: %d", ret);
		}

		break;

	case EC_PWRMON_START:
		if (!device_is_ready(pwrmon_dev)) {
			ret = device_init(pwrmon_dev);
			if (ret) {
				LOG_ERR("Failed to initialize pwrmon err: %d",
					ret);
				return EC_HOST_CMD_ERROR;
			}
		}

		ret = gpio_pin_configure_dt(&pwrmon_config.pwrmon_gpio,
					    GPIO_OUTPUT_HIGH);
		if (ret) {
			LOG_ERR("Failed to initialize gpio err: %d", ret);
			return EC_HOST_CMD_ERROR;
		}
		/* Give I2C switch some time to settle */
		k_msleep(10);
		ret = pwrmon_monitors_start();
		if (ret) {
			LOG_ERR("Failed to start power monitors err: %d", ret);
			return EC_HOST_CMD_ERROR;
		}

		pwrmon_set_rate(pwrmon_data.sample_rate);

		ret = pwrmon_channels_enable(true);
		if (ret == EC_HOST_CMD_SUCCESS) {
			pwrmon_data.pwrmon_enable = true;
		}
		break;

	case EC_PWRMON_STOP:
		/* Switch I2C channel to GSC */
		if (pwrmon_data.pwrmon_enable) {
			ret = pwrmon_channels_enable(false);
			gpio_pin_set_dt(&pwrmon_config.pwrmon_gpio, 0);
			k_msgq_purge(&pwrmon_data.pwrmon_msgq);
			pwrmon_data.pwrmon_enable = false;
		}

		break;

	case EC_PWRMON_LATCH:
		if (!pwrmon_data.pwrmon_enable) {
			LOG_ERR("Power monitoring not started");
			ret = EC_HOST_CMD_ERROR;
			break;
		}

		ret = pwrmon_latch();
		if (ret) {
			return ret;
		}
		break;
	default:
		ret = EC_HOST_CMD_INVALID_COMMAND;
		break;
	}

	return ret;
}
EC_HOST_CMD_HANDLER_UNBOUND(EC_CMD_PWRMON, hc_pwrmon_handler, EC_VER_MASK(0));
