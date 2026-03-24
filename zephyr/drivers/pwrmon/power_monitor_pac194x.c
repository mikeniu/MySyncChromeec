#include "power_monitor.h"

#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/sensor/pac194x.h>

int power_monitor_init(const struct device *dev)
{
	return 0;
}

int power_monitor_set_sample_rate(const struct device *dev, int sample_rate)
{
	struct sensor_value val = { .val1 = sample_rate };
	int ret;

	ret = sensor_attr_set(dev, SENSOR_CHAN_ALL,
			      SENSOR_ATTR_SAMPLING_FREQUENCY, &val);
	if (ret) {
		return ret;
	}

	return 0;
}

int power_monitor_channel_enable(const struct device *dev, int channel_id,
				 bool enable)
{
	struct sensor_value val = {
		.val1 = (enable ? 1 : 0),
	};
	int channel = PAC194X_CHAN_ACC1_AVG + channel_id;
	int ret;

	ret = sensor_attr_set(dev, channel, SENSOR_ATTR_CHANNEL_ENABLED, &val);
	if (ret) {
		return ret;
	}

	return 0;
}

int power_monitor_channel_get(const struct device *dev, int channel_id,
			      uint64_t *power)
{
	struct sensor_value val;
	int channel = PAC194X_CHAN_ACC1_AVG + channel_id;
	int ret;

	ret = sensor_sample_fetch_chan(dev, channel);
	if (ret) {
		return ret;
	}

	ret = sensor_channel_get(dev, channel, &val);
	if (ret) {
		return ret;
	}

	val.val1 = abs(val.val1);
	val.val2 = abs(val.val2);

	*power = sensor_value_to_micro(&val);

	return 0;
}

int power_monitor_sample_count_get(const struct device *dev, int channel_id,
				   uint32_t *sample_count)
{
	struct sensor_value val;
	int ret;

	ret = sensor_channel_get(dev, PAC194X_CHAN_ACC_COUNT, &val);
	if (ret) {
		return ret;
	}

	*sample_count = val.val1;

	return 0;
}

int power_monitor_latch(const struct device *dev)
{
	struct sensor_value val = {
		.val1 = PAC194X_SENSOR_ATTR_FORCE_REFRESH_CMD_SINGLE,
	};
	int ret;

	ret = sensor_attr_set(dev, SENSOR_CHAN_ALL,
			      SENSOR_ATTR_FORCE_REFRESH_CMD, &val);
	if (ret) {
		return ret;
	}

	k_msleep(2);
	ret = sensor_sample_fetch_chan(dev, PAC194X_CHAN_ACC_COUNT);
	if (ret) {
		return ret;
	}

	return 0;
}
