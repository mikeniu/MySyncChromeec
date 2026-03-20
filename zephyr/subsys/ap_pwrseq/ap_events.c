/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/kernel.h>

#include <ap_power/ap_power.h>
#include <ap_power/ap_power_events.h>

/*
 * Run the callback list
 */
void ap_power_ev_send_callbacks(uint32_t event)
{
	struct ap_power_ev_data data;

	data.event = event;
	STRUCT_SECTION_FOREACH(ap_power_ev_callback, cb)
	{
		if (cb->events & event) {
			cb->handler(cb, data);
		}
	}
}
