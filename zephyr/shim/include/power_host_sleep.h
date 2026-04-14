/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __POWER_HOST_SLEEP_H
#define __POWER_HOST_SLEEP_H

/*
 * This file is for Zephyr ap_pwrseq to reuse legacy EC code.
 * Eventually this file should be removed.
 *
 * TODO: Declaration in this file should be removed once it can be replaced
 * by implementation in Zephyr code.
 */

#include "ec_commands.h"
#include "host_command.h"
#include "lpc.h"
#include "power_state_defs.h"
#include "system.h"

#ifdef __cplusplus
extern "C" {
#endif

#if CONFIG_AP_PWRSEQ_HOST_SLEEP
/* Context to pass to a host sleep command handler. */
struct host_sleep_event_context {
	uint32_t sleep_transitions; /* Number of sleep transitions observed */
	uint16_t sleep_timeout_ms; /* Timeout in milliseconds */
};

void ap_power_chipset_handle_host_sleep_event(
	enum host_sleep_event state, struct host_sleep_event_context *ctx);
void power_set_host_sleep_state(enum host_sleep_event state);
#endif /* CONFIG_AP_PWRSEQ_HOST_SLEEP */

#ifdef __cplusplus
}
#endif

#endif /* __POWER_HOST_SLEEP_H */
