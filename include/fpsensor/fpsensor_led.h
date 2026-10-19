/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_LED_H_
#define PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_LED_H_

#include "ec_commands.h"
#include "fpsensor/fpsensor_modes.h"

#include <chrono>
#include <cstdint>

namespace fp_led
{

#if defined(CONFIG_PLATFORM_EC_FP_LED)
class FpLedController {
    public:
	enum class Mode {
		kOff,
		kEnroll,
		kMatch,
		kMatchOk,
		kMatchNok,
	};

	int init();
	void update_mode(FpSensorMode sensor_mode);
	void update_match(bool success);
	void on_timeout();

    private:
	void set_brightness(uint8_t percentage);
	void set_led(uint8_t brightness);
	void set_led_with_timeout(uint8_t brightness,
				  std::chrono::milliseconds delay);
	void enroll();
	void match();
	void off();

	Mode mode_ = Mode::kOff;
	int64_t timer_scheduled_at_ticks_ = 0;
	bool pm_locked_ = false;
};

void update_mode(FpSensorMode sensor_mode);
void update_match(bool success);
#else
inline void update_mode(FpSensorMode sensor_mode)
{
}
inline void update_match(bool success)
{
}
#endif

} /* namespace fp_led */

#endif /* PLATFORM_EC_INCLUDE_FPSENSOR_FPSENSOR_LED_H_ */
