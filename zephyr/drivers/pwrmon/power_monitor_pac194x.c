#include "power_monitor.h"

#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/sensor/pac194x.h>

int power_monitor_init_attr(struct sensor_value *val)
{
	return -1;
}

int power_monitor_channel_enable_attr(struct sensor_value *val, bool enable)
{
	val->val1 = (enable ? 1 : 0);
	return SENSOR_ATTR_CHANNEL_ENABLED;
}

int power_monitor_sensor_channel(int channel_id)
{
	return (int)PAC194X_CHAN_ACC1_AVG + channel_id;
}

inline int power_monitor_sample_count_channel(void)
{
	return (int)PAC194X_CHAN_ACC_COUNT;
}

int power_monitor_latch_attr(struct sensor_value *val)
{
	val->val1 = PAC194X_SENSOR_ATTR_FORCE_REFRESH_CMD_SINGLE;
	return (int)SENSOR_ATTR_FORCE_REFRESH_CMD;
}
