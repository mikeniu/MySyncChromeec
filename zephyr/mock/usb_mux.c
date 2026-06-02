/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "usb_mux.h"

#include <zephyr/sys/util.h>

__weak mux_state_t usb_mux_get(int port)
{
	return 0;
}
