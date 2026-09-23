/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "stubs.h"

DEFINE_FAKE_VALUE_FUNC(enum power_on_event_t, chipset_get_power_on_reason);
DEFINE_FAKE_VOID_FUNC(fan_set_duty, int, int);
DEFINE_FAKE_VALUE_FUNC(enum fan_status, fan_smart_control, int);
DEFINE_FAKE_VOID_FUNC(fan_set_rpm_mode, int, int);
DEFINE_FAKE_VOID_FUNC(fan_set_rpm_target, int, int);
DEFINE_FAKE_VALUE_FUNC(int, chipset_in_state, int);
DEFINE_FAKE_VOID_FUNC(chipset_force_shutdown, enum chipset_shutdown_reason);
DEFINE_FAKE_VOID_FUNC(chipset_reset, enum chipset_shutdown_reason);
DEFINE_FAKE_VALUE_FUNC(enum battery_present, battery_is_present);
DEFINE_FAKE_VALUE_FUNC(int, lb_set_diag_policy, int, int);
DEFINE_FAKE_VALUE_FUNC(int, adc_read_channel, enum adc_channel);
DEFINE_FAKE_VOID_FUNC(host_set_single_event, enum host_event_code);
DEFINE_FAKE_VOID_FUNC(extpower_interrupt, enum gpio_signal);

static uint8_t fake_memmap_buf[256];
uint8_t *host_get_memmap(int offset)
{
	if (offset < 0 || offset >= (int)sizeof(fake_memmap_buf))
		return NULL;
	return fake_memmap_buf + offset;
}
