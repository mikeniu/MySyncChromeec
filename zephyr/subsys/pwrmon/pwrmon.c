/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ec_commands.h"
#include "host_command.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/sensor/pac194x.h>

LOG_MODULE_REGISTER(pwrmon, LOG_LEVEL_INF);

struct channel_data {
    const struct device *power_monitor;
    const char *channel_name;
    uint8_t reg_idx;
};

struct pwrmon_data {
    int pacs_count;
    int channels_count;
    uint16_t sampling_rate;
    bool pwrmon_enable;
    const struct gpio_dt_spec pwrmon_gpio;
    const struct channel_data *channels;
    const struct device **pacs;
};

#define GPIO_INIT(node_id)                                      \
	{                                                           \
		.port = DEVICE_DT_GET(DT_GPIO_CTLR(node_id, gpios)),    \
		.pin = DT_GPIO_PIN(node_id, gpios),                     \
		.dt_flags = 0xFFFF & (DT_GPIO_FLAGS(node_id, gpios)),   \
	}

#define CHANNEL_INIT(child_node)                                \
	{                                                           \
		.power_monitor = DEVICE_DT_GET(DT_PARENT(child_node)),  \
		.channel_name = DT_PROP(child_node, friendly_name),     \
		.reg_idx = DT_REG_ADDR(child_node),                     \
    },

#define PWRMON_INIT(node_id)                                    \
    DT_FOREACH_CHILD_STATUS_OKAY(node_id, CHANNEL_INIT)

#define GET_DEVICE_PTR(node_id) DEVICE_DT_GET(node_id),

const struct channel_data channels_info[] = {
    DT_FOREACH_STATUS_OKAY(microchip_pac194x, PWRMON_INIT)
};

const struct device *pacs_info[] = {
    DT_FOREACH_STATUS_OKAY(microchip_pac194x, GET_DEVICE_PTR)
};

static struct pwrmon_data pwrmon_data = {
    // EC starts with EC_EN_PWRMON in high state
    .pwrmon_enable = true,
    .pwrmon_gpio = GPIO_INIT(DT_NODELABEL(gpio_ec_en_pwrmon)),
    .pacs = pacs_info,
    .pacs_count = ARRAY_SIZE(pacs_info),
    .sampling_rate = 1024,
    .channels = channels_info,
    .channels_count = ARRAY_SIZE(channels_info),
};

static int pwrmon_init(void)
{
    struct sensor_value val;

    // Set manual refresh mode for all power monitors
     for (int i = 0; i < pwrmon_data.pacs_count; ++i) {
         val.val1 = PAC194X_SENSOR_ATTR_REFRESH_MODE_MANUAL;
         sensor_attr_set(pwrmon_data.pacs[i], SENSOR_CHAN_ALL, SENSOR_ATTR_REFRESH_MODE, &val);
     }

    // Set sample rate
     for (int i = 0; i < pwrmon_data.pacs_count; ++i) {
         val.val1 = pwrmon_data.sampling_rate;
         sensor_attr_set(pwrmon_data.pacs[i], SENSOR_CHAN_ALL, SENSOR_ATTR_SAMPLING_FREQUENCY, &val);
     }

	gpio_pin_set_dt(&pwrmon_data.pwrmon_gpio, 0);
	pwrmon_data.pwrmon_enable = false;

	return 0;
}
SYS_INIT(pwrmon_init, POST_KERNEL, CONFIG_APPLICATION_INIT_PRIORITY);

static enum ec_status pwrmon_enable(bool start)
{
    int enabled_channels = pwrmon_data.channels_count;
    struct sensor_value val = { .val1 = (start ? 1 : 0), };

    for (int i = 0; i < pwrmon_data.channels_count; ++i) {
        int channel_id = PAC194X_CHAN_ACC1 + pwrmon_data.channels[i].reg_idx - 1;
        val.val1 = 1;
        int ret = sensor_attr_set(pwrmon_data.channels[i].power_monitor, channel_id,
                        SENSOR_ATTR_CHANNEL_ENABLED, &val);
        if (ret) {
            enabled_channels--;
            LOG_ERR("Unable to start channel %s err: %d", pwrmon_data.channels[i].channel_name, ret);
        }
    }

    if (enabled_channels != pwrmon_data.channels_count)
        return EC_RES_ERROR;

    return EC_RES_SUCCESS;
}

static enum ec_status pwrmon_set_rate(uint16_t sampling_rate)
{
    int ret = 0;
    struct sensor_value val = { .val1 = sampling_rate };

    // Set sample rate
     for (int i = 0; i < pwrmon_data.pacs_count; ++i) {
        ret = sensor_attr_set(pwrmon_data.pacs[i], SENSOR_CHAN_ALL, SENSOR_ATTR_SAMPLING_FREQUENCY, &val);
        if (ret)
            return EC_RES_ERROR;
     }

    pwrmon_data.sampling_rate = sampling_rate;
    return EC_RES_SUCCESS;
}

static enum ec_status pwrmon_latch()
{
    int ret = 0;
    struct sensor_value val = { .val1 = PAC194X_SENSOR_ATTR_FORCE_REFRESH_CMD_SINGLE };

    for (int i = 0; i < pwrmon_data.pacs_count; ++i) {
        ret = sensor_attr_set(pwrmon_data.pacs[i], SENSOR_CHAN_ALL, SENSOR_ATTR_FORCE_REFRESH_CMD, &val);
        if (ret)
            return EC_RES_ERROR;
    }

    return EC_RES_SUCCESS;
}

static enum ec_status pwrmon_sample_fetch()
{
    int ret = 0;

    for (int i = 0; i < pwrmon_data.pacs_count; ++i) {
        ret = sensor_sample_fetch(pwrmon_data.pacs[i]);
        if (ret)
            return EC_RES_ERROR;
    }

    return EC_RES_SUCCESS;

}

static enum ec_status pwrmon_sample_get(uint8_t channel_id, uint64_t *samples, uint64_t *power)
{
    int ret = 0;
    int sensor_channel = PAC194X_CHAN_ACC1_AVG + pwrmon_data.channels[channel_id].reg_idx - 1 ;
    struct sensor_value val;

    ret = sensor_channel_get(pwrmon_data.channels[channel_id].power_monitor, PAC194X_CHAN_ACC_COUNT, &val);
    if (ret)
        LOG_WRN("Failed to get sample count for channel %s err: %d", pwrmon_data.channels[channel_id].channel_name, ret);

    *samples = val.val1;

    ret = sensor_channel_get(pwrmon_data.channels[channel_id].power_monitor, sensor_channel, &val);
    if (ret)
        LOG_WRN("Failed to get channel power for channel %s err: %d", pwrmon_data.channels[channel_id].channel_name, ret);

    *power = (val.val1 * 1000000LL) + val.val2;

    return EC_RES_SUCCESS;

}

static enum ec_status hc_pwrmon_handler(struct host_cmd_handler_args *args)
{
	const struct ec_params_pwrmon *p = args->params;
	struct ec_response_pwrmon *r = args->response;
    enum ec_status ret = EC_RES_SUCCESS;

	switch (p->cmd) {
	case EC_PWRMON_GET_CHANNEL_COUNT:
        LOG_INF("EC_PWRMON_GET_CHANNEL_COUNT");
        r->channel_count.count = pwrmon_data.channels_count;
        args->response_size = sizeof(struct pwrmon_get_channel_count);
		break;

	case EC_PWRMON_DUMP_INFO:
        LOG_INF("EC_PWRMON_DUMP_INFO");
        int channel_id = p->get_dump_info.channel_id;
        r->dump_info.channel_id = channel_id;
        strcpy(r->dump_info.channel_name, pwrmon_data.channels[channel_id - 1].channel_name);
        args->response_size = sizeof(struct pwrmon_dump_info);
		break;

	case EC_PWRMON_GET_RATE:
        LOG_INF("EC_PWRMON_GET_RATE");
        r->get_rate.rate = pwrmon_data.sampling_rate;
        args->response_size = sizeof(struct pwrmon_get_rate);
		break;

	case EC_PWRMON_SET_RATE:
        LOG_INF("EC_PWRMON_SET_RATE");

		// Check if I2C switch to EC
		if (!pwrmon_data.pwrmon_enable) {
			gpio_pin_set_dt(&pwrmon_data.pwrmon_gpio, 1);
		}

        ret = pwrmon_set_rate(p->set_rate.rate);

		// Switch I2C switch back to GSC
		gpio_pin_set_dt(&pwrmon_data.pwrmon_gpio, 0);

		break;

	case EC_PWRMON_START:
        LOG_INF("EC_PWRMON_START");

        // Switch I2C channel to EC
		if (!pwrmon_data.pwrmon_enable) {
			gpio_pin_set_dt(&pwrmon_data.pwrmon_gpio, 1);
			pwrmon_data.pwrmon_enable = true;
		}

        ret = pwrmon_enable(true);
		break;

	case EC_PWRMON_STOP:
        LOG_INF("EC_PWRMON_STOP");

        // Switch I2C channel to GSC
		if (pwrmon_data.pwrmon_enable) {
	        ret = pwrmon_enable(false);
			gpio_pin_set_dt(&pwrmon_data.pwrmon_gpio, 0);
		}

		break;

	case EC_PWRMON_GET:
        LOG_INF("EC_PWRMON_GET");
        uint64_t sample_count = 0;
        uint64_t power = 0;

        if (!pwrmon_data.pwrmon_enable) {
            LOG_ERR("Power monitoring not started");
            ret = EC_RES_ERROR;
            break;
        }

        ret = pwrmon_sample_get(p->get_channel_info.channel_id, &sample_count, &power);
        if (ret)
            return ret;

        r->channel_info.channel_id = p->get_channel_info.channel_id;
        r->channel_info.samples = sample_count;
        r->channel_info.value = power;
        args->response_size = sizeof(struct pwrmon_channel_info);
		break;

	case EC_PWRMON_LATCH:
        LOG_INF("EC_PWRMON_LATCH");

        ret = pwrmon_latch();
        if (ret)
            return ret;

        ret = pwrmon_sample_fetch();
        if (ret)
            return ret;
        break;

	default:
        ret = EC_RES_INVALID_COMMAND;
		break;
	}

	return ret;
}
DECLARE_HOST_COMMAND(EC_CMD_PWRMON, hc_pwrmon_handler, EC_VER_MASK(0));
