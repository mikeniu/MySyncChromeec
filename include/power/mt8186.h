/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_INCLUDE_POWER_MT8186_H_
#define PLATFORM_EC_INCLUDE_POWER_MT8186_H_

#include "common.h"
#include "ec_commands.h"
#include "power.h"

__override_proto void
board_handle_host_sleep_event(enum host_sleep_event state);
#ifdef CONFIG_POWER_TRACK_HOST_SLEEP_STATE
__override_proto void board_handle_sleep_hang(enum sleep_hang_type hang_type);
#endif

#endif /* PLATFORM_EC_INCLUDE_POWER_MT8186_H_ */
