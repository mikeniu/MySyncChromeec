#ifndef ZEPHYR_DRIVERS_PWRMON_POWER_MONITOR_H_
#define ZEPHYR_DRIVERS_PWRMON_POWER_MONITOR_H_

#include <zephyr/device.h>

int power_monitor_init(const struct device *dev);
int power_monitor_set_sample_rate(const struct device *dev, int sample_rate);
int power_monitor_channel_enable(const struct device *dev, int channel_id,
				 bool enable);
int power_monitor_channel_get(const struct device *dev, int channel_id,
			      uint64_t *power);
int power_monitor_sample_count_get(const struct device *dev, int channel_id,
				   uint32_t *sample_count);
int power_monitor_latch(const struct device *dev);
#endif /* ZEPHYR_DRIVERS_PWRMON_POWER_MONITOR_H_ */
