/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define DT_DRV_COMPAT cros_pwrmon

#include "ec_commands.h"
#include "host_command.h"
#include "power_monitor.h"

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
	const int power_monitors_count;
	const int channels_count;
	const struct gpio_dt_spec pwrmon_gpio;
	const struct device *power_monitors[DT_INST_PROP_LEN(0, power_monitors)];
	const struct channel_config channels[];
};

struct pwrmon_data {
	int8_t power_monitor_iter;
	uint16_t sampling_rate;
	bool pwrmon_enable;
};

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

static struct pwrmon_data pwrmon_data = {
	/* EC starts with EC_EN_PWRMON in high state */
	.pwrmon_enable = true,
	.sampling_rate = DT_INST_PROP(0, sample_rate),
};

static enum ec_status pwrmon_set_rate(uint16_t sampling_rate);

static int pwrmon_init(const struct device *dev)
{
	const struct pwrmon_config *config = dev->config;
	struct pwrmon_data *data = dev->data;
	enum sensor_attribute init_attr;
	struct sensor_value val;
	int ret;

	if (!gpio_is_ready_dt(&config->pwrmon_gpio)) {
		LOG_ERR_DEVICE_NOT_READY(config->pwrmon_gpio.port);
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&config->pwrmon_gpio, GPIO_OUTPUT_HIGH);
	if (ret) {
		LOG_ERR("Failed to initialize gpio err: %d", ret);
		return ret;
	}

	// Give I2C switch some time to settle
	k_msleep(10);
	for (int i = 0; i < config->power_monitors_count; ++i) {
		ret = device_init(config->power_monitors[i]);
		if (ret) {
			LOG_ERR("Failed to initialize power monitor %d err: %d",
				i, ret);
			return ret;
		}

		init_attr = power_monitor_init_attr(&val);
		if ((int)init_attr < 0) {
			continue;
		}
		ret = sensor_attr_set(config->power_monitors[i],
				      SENSOR_CHAN_ALL, init_attr, &val);
		if (ret) {
			LOG_ERR("Failed to set init attrubte for power monitor %d err: %d",
				i, ret);
			return ret;
		}
	}

	pwrmon_set_rate(data->sampling_rate);

	return 0;
}

static enum ec_status pwrmon_enable(bool start)
{
	const struct device *power_monitor;
	struct sensor_value val;
	enum sensor_attribute channel_enable_attr;
	enum sensor_channel sensor_channel;
	int enabled_channels = pwrmon_config.channels_count;
	int ret;

	channel_enable_attr = power_monitor_channel_enable_attr(&val, start);

	if ((int)channel_enable_attr >= 0) {
		for (int i = 0; i < pwrmon_config.channels_count; ++i) {
			power_monitor = pwrmon_config.power_monitors
						[pwrmon_config.channels[i]
							 .power_monitor_idx];
			sensor_channel = power_monitor_sensor_channel(
				pwrmon_config.channels[i].reg_idx - 1);
			ret = sensor_attr_set(power_monitor, sensor_channel,
					      channel_enable_attr, &val);
			if (ret) {
				enabled_channels--;
				LOG_ERR("Unable to start channel %d %s err: %d",
					i,
					pwrmon_config.channels[i].channel_name,
					ret);
			}
		}
	}

	if (enabled_channels != pwrmon_config.channels_count) {
		return EC_RES_ERROR;
	}

	return EC_RES_SUCCESS;
}

static enum ec_status pwrmon_set_rate(uint16_t sampling_rate)
{
	struct sensor_value val = { .val1 = sampling_rate };
	int ret;

	for (int i = 0; i < pwrmon_config.power_monitors_count; ++i) {
		ret = sensor_attr_set(pwrmon_config.power_monitors[i],
				      SENSOR_CHAN_ALL,
				      SENSOR_ATTR_SAMPLING_FREQUENCY, &val);
		if (ret) {
			return EC_RES_ERROR;
		}
	}

	pwrmon_data.sampling_rate = sampling_rate;
	return EC_RES_SUCCESS;
}

static enum ec_status pwrmon_latch(void)
{
	enum sensor_attribute latch_attr;
	struct sensor_value val;
	int ret;

	latch_attr = power_monitor_latch_attr(&val);
	if ((int)latch_attr >= 0) {
		for (int i = 0; i < pwrmon_config.power_monitors_count; ++i) {
			latch_attr = power_monitor_latch_attr(&val);
			ret = sensor_attr_set(pwrmon_config.power_monitors[i],
					      SENSOR_CHAN_ALL, latch_attr,
					      &val);
			if (ret) {
				LOG_WRN("Failed to latch power monitor %d err: %d",
					i, ret);
				return EC_RES_ERROR;
			}
		}
	}

	return EC_RES_SUCCESS;
}

static enum ec_status pwrmon_sample_get(uint8_t channel_id, uint32_t *samples,
					int64_t *power)
{
	const struct device *power_monitor;
	struct sensor_value val;
	int channel;
	int ret;

	channel = power_monitor_sample_count_channel();
	power_monitor =
		pwrmon_config.power_monitors[pwrmon_config.channels[channel_id]
						     .power_monitor_idx];
	ret = sensor_channel_get(power_monitor, channel, &val);
	if (ret) {
		LOG_WRN("Failed to get sample count for channel %s err: %d",
			pwrmon_config.channels[channel_id].channel_name, ret);
		return EC_RES_ERROR;
	}
	*samples = val.val1;

	channel = power_monitor_sensor_channel(
		pwrmon_config.channels[channel_id].reg_idx - 1);
	ret = sensor_channel_get(power_monitor, channel, &val);
	if (ret) {
		LOG_WRN("Failed to get average power for channel %d err: %d",
			channel_id, ret);
		return EC_RES_ERROR;
	}

	*power = llabs(sensor_value_to_micro(&val));

	return EC_RES_SUCCESS;
}
DEVICE_DT_INST_DEFINE(0, &pwrmon_init, NULL, &pwrmon_data, &pwrmon_config,
		      POST_KERNEL, CONFIG_APPLICATION_INIT_PRIORITY, NULL);

static enum ec_status hc_pwrmon_handler(struct host_cmd_handler_args *args)
{
	const struct device *pwrmon_dev = DEVICE_DT_INST_GET(0);
	const struct ec_params_pwrmon *p = args->params;
	struct ec_response_pwrmon *r = args->response;
	enum ec_status ret = EC_RES_SUCCESS;
	int channel_id;

	switch (p->cmd) {
	case EC_PWRMON_GET_CHANNEL_COUNT:
		r->channel_count.count = pwrmon_config.channels_count;
		args->response_size = sizeof(struct pwrmon_get_channel_count);
		break;

	case EC_PWRMON_DUMP_INFO:
		channel_id = p->get_dump_info.channel_id;

		if (channel_id > pwrmon_config.channels_count) {
			LOG_ERR("Channel index %d bigger than channel_count: %d",
				channel_id, pwrmon_config.channels_count);
			ret = EC_RES_ERROR;
			break;
		}

		r->dump_info.channel_id = channel_id;
		if (strlen(pwrmon_config.channels[channel_id].channel_name)) {
			strncpy(r->dump_info.channel_name,
				pwrmon_config.channels[channel_id].channel_name,
				sizeof(pwrmon_config.channels[channel_id]
					       .channel_name));
		} else {
			sprintf(r->dump_info.channel_name, "%d", channel_id);
		}
		args->response_size = sizeof(struct pwrmon_dump_info);
		break;

	case EC_PWRMON_GET_RATE:
		r->get_rate.rate = pwrmon_data.sampling_rate;
		args->response_size = sizeof(struct pwrmon_get_rate);
		break;

	case EC_PWRMON_SET_RATE:
		ret = pwrmon_set_rate(p->set_rate.rate);
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
				return EC_RES_ERROR;
			}
		}

		ret = pwrmon_enable(true);
		break;

	case EC_PWRMON_STOP:
		/* Switch I2C channel to GSC */
		if (pwrmon_data.pwrmon_enable) {
			ret = pwrmon_enable(false);
			gpio_pin_set_dt(&pwrmon_config.pwrmon_gpio, 0);
			pwrmon_data.pwrmon_enable = false;
		}

		break;

	case EC_PWRMON_GET:
		uint32_t sample_count = 0;
		int64_t power = 0;
		channel_id = p->get_channel_info.channel_id;

		if (!pwrmon_data.pwrmon_enable) {
			LOG_ERR("Power monitoring not started");
			ret = EC_RES_ERROR;
			break;
		}

		if (channel_id > pwrmon_config.channels_count) {
			LOG_ERR("Channel index %d bigger than channel_count: %d",
				channel_id, pwrmon_config.channels_count);
			ret = EC_RES_ERROR;
			break;
		}

		for (int i = 0; i < ARRAY_SIZE(r->channels_info.channel_info);
		     i++) {
			int ch = channel_id + i;
			struct pwrmon_channel_info *info =
				&r->channels_info.channel_info[i];

			if (ch < pwrmon_config.channels_count) {
				uint8_t power_monitor_idx =
					pwrmon_config.channels[ch]
						.power_monitor_idx;

				// Fetch power monitors if it wasn't done before
				if (pwrmon_data.power_monitor_iter !=
				    power_monitor_idx) {
					pwrmon_data.power_monitor_iter =
						power_monitor_idx;
					ret = sensor_sample_fetch(
						pwrmon_config.power_monitors
							[power_monitor_idx]);
					if (ret) {
						LOG_ERR("Failed to fetch samples from power monitor %u err: %d",
							pwrmon_data
								.power_monitor_iter,
							ret);
					}
				}

				ret = pwrmon_sample_get(ch, &sample_count,
							&power);
				if (ret) {
					info->channel_id = -1;
				} else {
					info->channel_id = ch;
					info->samples = sample_count;
					info->value = power;
				}
			} else {
				info->channel_id = -1;
				continue;
			}
		}
		args->response_size = sizeof(struct pwrmon_channels_info);
		ret = EC_RES_SUCCESS;
		break;
	case EC_PWRMON_LATCH:
		if (!pwrmon_data.pwrmon_enable) {
			LOG_ERR("Power monitoring not started");
			ret = EC_RES_ERROR;
			break;
		}

		ret = pwrmon_latch();
		if (ret) {
			return ret;
		}
		// Reset power monitor iter
		pwrmon_data.power_monitor_iter = -1;
		break;
	default:
		ret = EC_RES_INVALID_COMMAND;
		break;
	}

	return ret;
}
DECLARE_HOST_COMMAND(EC_CMD_PWRMON, hc_pwrmon_handler, EC_VER_MASK(0));
