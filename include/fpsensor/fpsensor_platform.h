/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_PLATFORM_H_
#define PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_PLATFORM_H_

#include "fpsensor/fpsensor.h"
#include "fpsensor/fpsensor_console.h"
#include "fpsensor/fpsensor_state_driver.h"
#include "gpio.h"
#include "spi.h"
#include "task.h"

#ifdef CONFIG_ZEPHYR
#include <zephyr/pm/policy.h>
#endif

#include <cstdint>

struct FpTaskEvent {
	uint32_t raw = 0;

	[[nodiscard]] constexpr bool is_update_config() const noexcept
	{
		return (raw & TASK_EVENT_UPDATE_CONFIG) != 0;
	}
	[[nodiscard]] constexpr bool is_sensor_irq() const noexcept
	{
		return (raw & TASK_EVENT_SENSOR_IRQ) != 0;
	}
	[[nodiscard]] constexpr bool is_timer() const noexcept
	{
		return (raw & TASK_EVENT_TIMER) != 0;
	}
	[[nodiscard]] constexpr bool is_sensor_or_timer() const noexcept
	{
		return is_sensor_irq() || is_timer();
	}
};

namespace fp_platform
{

inline void lock_pm()
{
#ifdef CONFIG_ZEPHYR
	pm_policy_state_all_lock_get();
#endif
}

class ScopedPmUnlock {
    public:
	explicit ScopedPmUnlock(bool should_unlock)
		: unlocked_(should_unlock)
	{
#ifdef CONFIG_ZEPHYR
		if (unlocked_) {
			pm_policy_state_all_lock_put();
		}
#endif
	}
	~ScopedPmUnlock()
	{
		if (unlocked_) {
			lock_pm();
		}
	}
	ScopedPmUnlock(const ScopedPmUnlock &) = delete;
	ScopedPmUnlock &operator=(const ScopedPmUnlock &) = delete;

    private:
	bool unlocked_;
};

#ifdef HAVE_FP_PRIVATE_DRIVER
inline void enable_sensor_irq()
{
#ifndef CONFIG_ZEPHYR
	gpio_clear_pending_interrupt(GPIO_FPS_INT);
	gpio_enable_interrupt(GPIO_FPS_INT);
#endif
}

inline void enter_sensor_low_power(FpTaskEvent evt = {})
{
#ifndef CONFIG_ZEPHYR
	if (evt.is_sensor_irq()) {
		gpio_clear_pending_interrupt(GPIO_FPS_INT);
	}
#else
	(void)evt;
#endif
	fp_sensor_low_power();
}

inline void prepare_for_mode_change()
{
#ifdef CONFIG_ZEPHYR
	fp_idle();
#else
	gpio_disable_interrupt(GPIO_FPS_INT);
#endif
}

inline void prepare_for_sensor_or_timer_event(FpTaskEvent evt)
{
#ifdef CONFIG_ZEPHYR
	if (evt.is_timer()) {
		fp_idle();
	}
#else
	(void)evt;
	gpio_disable_interrupt(GPIO_FPS_INT);
#endif
}

inline void flush_spi_after_acquire()
{
#ifndef CONFIG_ZEPHYR
	if (const int res = spi_transaction_flush(SPI_FP_DEVICE); res != 0) {
		CPRINTS("Failed to flush SPI: 0x%x", res);
	}
#endif
}
#endif /* HAVE_FP_PRIVATE_DRIVER */

} /* namespace fp_platform */

#endif /* PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_PLATFORM_H_ */
