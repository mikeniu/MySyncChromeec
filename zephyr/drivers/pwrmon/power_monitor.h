#ifndef ZEPHYR_DRIVERS_PWRMON_POWER_MONITOR_H_
#define ZEPHYR_DRIVERS_PWRMON_POWER_MONITOR_H_

#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>

enum sensor_attribute power_monitor_init_attr(struct sensor_value *val);
enum sensor_attribute power_monitor_channel_enable_attr(struct sensor_value *val, bool enable);
enum sensor_channel power_monitor_sensor_channel(int channel_id);
enum sensor_channel power_monitor_sample_count_channel(void);
enum sensor_attribute power_monitor_latch_attr(struct sensor_value *val);

#endif /* ZEPHYR_DRIVERS_PWRMON_POWER_MONITOR_H_ */
