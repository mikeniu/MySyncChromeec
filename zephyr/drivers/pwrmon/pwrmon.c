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

#define CHANNEL_INIT(child_node)                                             \
	{                                                                    \
		.power_monitor = DEVICE_DT_GET(DT_PARENT(child_node)),       \
		.channel_name = DT_PROP_OR(child_node, friendly_name, NULL), \
		.reg_idx = DT_REG_ADDR(child_node),                          \
	},

#define PWRMON_INIT(node_id, prop, idx)                                     \
	DT_FOREACH_CHILD_STATUS_OKAY(DT_PHANDLE_BY_IDX(node_id, prop, idx), \
				     CHANNEL_INIT)

struct channel_data {
	const struct device *power_monitor;
	const char *channel_name;
	const uint8_t reg_idx;
};

struct pwrmon_config {
	const int power_monitors_count;
	const int channels_count;
	const struct gpio_dt_spec pwrmon_gpio;
	const struct channel_data *channels;
	const struct device **power_monitors;
};

struct pwrmon_data {
	uint16_t sampling_rate;
	bool pwrmon_enable;
};

const struct device *power_monitors_info[] = { DT_INST_FOREACH_PROP_ELEM_SEP(
	0, power_monitors, GET_POWER_MONITOR_DEV, (, )) };

const struct channel_data channels_info[] = { DT_INST_FOREACH_PROP_ELEM(
	0, power_monitors, PWRMON_INIT) };

const static struct pwrmon_config pwrmon_config = {
	.power_monitors = power_monitors_info,
	.power_monitors_count = ARRAY_SIZE(power_monitors_info),
	.channels = channels_info,
	.channels_count = ARRAY_SIZE(channels_info),
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
	int ret;

	if (!gpio_is_ready_dt(&config->pwrmon_gpio)) {
		LOG_ERR("Pwrmon gpio not ready");
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&config->pwrmon_gpio, GPIO_OUTPUT_HIGH);
	if (ret) {
		LOG_ERR("Failed to initialize gpio err: %d", ret);
		return ret;
	}

	return 0;
}

static enum ec_status pwrmon_enable(bool start)
{
	int enabled_channels = pwrmon_config.channels_count;
	int ret;

	for (int i = 0; i < pwrmon_config.channels_count; ++i) {
		ret = power_monitor_channel_enable(
			pwrmon_config.channels[i].power_monitor,
			pwrmon_config.channels[i].reg_idx - 1, start);
		if (ret) {
			enabled_channels--;
			LOG_ERR("Unable to start channel %s err: %d",
				pwrmon_config.channels[i].channel_name, ret);
		}
	}

	if (enabled_channels != pwrmon_config.channels_count) {
		return EC_RES_ERROR;
	}

	return EC_RES_SUCCESS;
}

static enum ec_status pwrmon_set_rate(uint16_t sampling_rate)
{
	int ret;

	/* If Power Monitoring is disabled temporary set I2C switch to EC */
	if (!pwrmon_data.pwrmon_enable) {
		gpio_pin_set_dt(&pwrmon_config.pwrmon_gpio, 1);
	}

	for (int i = 0; i < pwrmon_config.power_monitors_count; ++i) {
		ret = power_monitor_set_sample_rate(
			pwrmon_config.power_monitors[i], sampling_rate);
		if (ret) {
			return EC_RES_ERROR;
		}
	}

	if (!pwrmon_data.pwrmon_enable) {
		gpio_pin_set_dt(&pwrmon_config.pwrmon_gpio, 0);
	}

	pwrmon_data.sampling_rate = sampling_rate;
	return EC_RES_SUCCESS;
}

static enum ec_status pwrmon_latch(void)
{
	int ret;

	for (int i = 0; i < pwrmon_config.power_monitors_count; ++i) {
		ret = power_monitor_latch(pwrmon_config.power_monitors[i]);
		if (ret) {
			LOG_WRN("Failed to latch power monitor %d err: %d", i,
				ret);
			return EC_RES_ERROR;
		}
	}

	return EC_RES_SUCCESS;
}

static enum ec_status pwrmon_sample_get(uint8_t channel_id, uint32_t *samples,
					int64_t *power)
{
	int ret;

	ret = power_monitor_sample_count_get(
		pwrmon_config.channels[channel_id].power_monitor,
		pwrmon_config.channels[channel_id].reg_idx - 1, samples);
	if (ret) {
		LOG_WRN("Failed to get sample count for channel %s err: %d",
			pwrmon_config.channels[channel_id].channel_name, ret);
		return EC_RES_ERROR;
	}

	ret = power_monitor_channel_get(
		pwrmon_config.channels[channel_id].power_monitor,
		pwrmon_config.channels[channel_id].reg_idx - 1, power);
	if (ret) {
		LOG_WRN("Failed to get average power for channel %s err: %d",
			pwrmon_config.channels[channel_id].channel_name, ret);
		return EC_RES_ERROR;
	}

	return EC_RES_SUCCESS;
}
DEVICE_DT_INST_DEFINE(0, &pwrmon_init, NULL, &pwrmon_data, &pwrmon_config,
		      POST_KERNEL, CONFIG_APPLICATION_INIT_PRIORITY, NULL);

static enum ec_status hc_pwrmon_handler(struct host_cmd_handler_args *args)
{
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
		strcpy(r->dump_info.channel_name,
		       pwrmon_config.channels[channel_id].channel_name);
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
		/* Switch I2C channel to EC */
		if (!pwrmon_data.pwrmon_enable) {
			gpio_pin_set_dt(&pwrmon_config.pwrmon_gpio, 1);
			pwrmon_data.pwrmon_enable = true;
		}

		for (int i = 0; i < pwrmon_config.power_monitors_count; ++i) {
			ret = power_monitor_init(pwrmon_config.power_monitors[i]);
			if (ret) {
				LOG_ERR("Failed to initialize power monitor %d",
					i);
				return ret;
			}
		}

		pwrmon_set_rate(pwrmon_data.sampling_rate);

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
			LOG_ERR("Channel index %d bigger than channel_count: %d",
				channel_id, pwrmon_config.channels_count);
			ret = EC_RES_ERROR;
			break;
		}

		if (channel_id > pwrmon_config.channels_count) {
			LOG_ERR("Channel index too big");
			ret = EC_RES_ERROR;
			break;
		}

		ret = pwrmon_sample_get(channel_id, &sample_count, &power);
		if (ret) {
			return ret;
		}

		r->channel_info.channel_id = channel_id;
		r->channel_info.samples = sample_count;
		r->channel_info.value = power;
		args->response_size = sizeof(struct pwrmon_channel_info);
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

		break;
	default:
		ret = EC_RES_INVALID_COMMAND;
		break;
	}

	return ret;
}
DECLARE_HOST_COMMAND(EC_CMD_PWRMON, hc_pwrmon_handler, EC_VER_MASK(0));
