#ifndef ZEPHYR_DRIVERS_PWRMON_POWER_MONITOR_H_
#define ZEPHYR_DRIVERS_PWRMON_POWER_MONITOR_H_

#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>

int power_monitor_init_attr(struct sensor_value *val);
int power_monitor_channel_enable_attr(struct sensor_value *val, bool enable);
int power_monitor_sensor_channel(int channel_id);
int power_monitor_sample_count_channel(void);
int power_monitor_latch_attr(struct sensor_value *val);

#ifdef CONFIG_PWRMON_POWER_MONITOR_COMMON

inline int power_monitor_init_attr(struct sensor_value *val)
{
	return -1;
}

inline int power_monitor_channel_enable_attr(struct sensor_value *val,
					     bool enable)
{
	return -1;
}

inline int power_monitor_sensor_channel(int channel_id)
{
	return (int)SENSOR_CHAN_POWER;
}

inline int power_monitor_sample_count_channel(void)
{
	return (int)SENSOR_CHAN_FREQUENCY;
}

inline int power_monitor_latch_attr(struct sensor_value *val)
{
	return -1;
}

#endif

#endif /* ZEPHYR_DRIVERS_PWRMON_POWER_MONITOR_H_ */
