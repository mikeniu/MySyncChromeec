/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_PLATFORM_H_
#define PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_PLATFORM_H_

#include "fpsensor/fpsensor_state_driver.h"
#include "task.h"

#ifdef CONFIG_ZEPHYR
#include <zephyr/kernel.h>
#endif

#include <cstdint>

/**
 * Typed wrapper around a 32-bit FPSENSOR RTOS task event mask.
 */
class FpTaskEvent {
    public:
	constexpr FpTaskEvent() noexcept = default;
	explicit constexpr FpTaskEvent(uint32_t raw) noexcept : raw_(raw)
	{
	}

	[[nodiscard]] static constexpr FpTaskEvent update_config() noexcept
	{
		return FpTaskEvent(TASK_EVENT_UPDATE_CONFIG);
	}

	[[nodiscard]] static constexpr FpTaskEvent sensor_irq() noexcept
	{
		return FpTaskEvent(TASK_EVENT_SENSOR_IRQ);
	}

	[[nodiscard]] static constexpr FpTaskEvent timer() noexcept
	{
		return FpTaskEvent(TASK_EVENT_TIMER);
	}

	[[nodiscard]] constexpr uint32_t raw() const noexcept
	{
		return raw_;
	}

	[[nodiscard]] constexpr bool is_update_config() const noexcept
	{
		return (raw_ & TASK_EVENT_UPDATE_CONFIG) != 0;
	}

	[[nodiscard]] constexpr bool is_sensor_irq() const noexcept
	{
		return (raw_ & TASK_EVENT_SENSOR_IRQ) != 0;
	}

	[[nodiscard]] constexpr bool is_timer() const noexcept
	{
		return (raw_ & TASK_EVENT_TIMER) != 0;
	}

	[[nodiscard]] constexpr bool is_sensor_or_timer() const noexcept
	{
		return is_sensor_irq() || is_timer();
	}

    private:
	uint32_t raw_ = 0;
};

namespace fp_platform
{

#ifdef CONFIG_ZEPHYR
/**
 * RAII guard that acquires a Zephyr semaphore for the duration of a scope.
 */
class ScopedSemLock {
    public:
	explicit ScopedSemLock(k_sem &sem)
		: sem_(sem)
	{
		k_sem_take(&sem_, K_FOREVER);
	}

	~ScopedSemLock()
	{
		k_sem_give(&sem_);
	}

	ScopedSemLock(const ScopedSemLock &) = delete;
	ScopedSemLock &operator=(const ScopedSemLock &) = delete;

    private:
	k_sem &sem_;
};
#endif /* CONFIG_ZEPHYR */

/** Acquire the platform power-management lock (no-op on legacy EC). */
void lock_pm();

/** Release the platform power-management lock (no-op on legacy EC). */
void unlock_pm();

/**
 * RAII guard that temporarily releases the PM lock while waiting for a task
 * event (unless an enrollment session is active).
 */
class ScopedPmUnlock {
    public:
	explicit ScopedPmUnlock(bool should_unlock)
		: unlocked_(should_unlock)
	{
		if (unlocked_) {
			unlock_pm();
		}
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

/** Enable the sensor interrupt line when waiting for a finger IRQ. */
void enable_sensor_irq();

/** Put the sensor into low-power state, clearing pending IRQs if needed. */
void enter_sensor_low_power(FpTaskEvent evt = {});

/** Prepare sensor/IRQ state before applying a mode change. */
void prepare_for_mode_change();

/** Prepare sensor/IRQ state when handling a sensor IRQ or timer event. */
void prepare_for_sensor_or_timer_event(FpTaskEvent evt);

/** Flush SPI transactions after image acquisition before clocking up CPU. */
void flush_spi_after_acquire();

} /* namespace fp_platform */

#endif /* PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_PLATFORM_H_ */
