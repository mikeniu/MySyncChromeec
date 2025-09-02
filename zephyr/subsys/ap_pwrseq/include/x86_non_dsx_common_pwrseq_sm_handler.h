/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __X86_NON_DSX_COMMON_PWRSEQ_SM_HANDLER_H__
#define __X86_NON_DSX_COMMON_PWRSEQ_SM_HANDLER_H__

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/types.h>

#ifdef CONFIG_AP_PWRSEQ_DRIVE
#include <ap_power/ap_pwrseq.h>
#else
#include <ap_power/ap_power.h>
#include <ap_power/ap_power_events.h>
#endif
#include <ap_power_host_sleep.h>
#include <x86_common_pwrseq.h>

#ifndef CONFIG_AP_PWRSEQ_DRIVER
/* The wait time is ~150 msec, allow for safety margin. */
#define IN_PCH_SLP_SUS_WAIT_TIME_MS 250

enum power_states_ndsx chipset_pwr_sm_run(enum power_states_ndsx curr_state);
enum power_states_ndsx chipset_pwr_seq_get_state(void);
enum power_states_ndsx pwr_sm_get_state(void);
const char *const pwr_sm_get_state_name(enum power_states_ndsx state);
#else
enum ap_pwrseq_state chipset_pwr_seq_get_state(void);
const char *const pwr_sm_get_state_name(enum ap_pwrseq_state state);
#endif
/**
 * @brief Request AP to start power on sequence.
 *
 */
void request_start_from_g3(void);

/**
 * @brief Handle reset on chipset level.
 *
 */
void ap_pwrseq_handle_chipset_reset(void);

/**
 * @brief Set delay to start AP power on sequence.
 *
 * @param AP power sequence delay in seconds.
 *
 */
void set_start_from_g3_delay_seconds(uint32_t d_time);

/**
 * @brief Start delay to request AP to start power on sequence.
 *
 */
void start_start_from_g3_timer(void);

/**
 * @brief Get time remaining for starting AP power sequence.
 *
 * @return Remaining time (in milliseconds).
 */
uint32_t get_remaining_start_from_g3_timer(void);

/**
 * @brief Start timer for waiting to move from S5 into G3.
 *
 */
void start_s5_inactive_timer(void);

/**
 * @brief Get time remaining for moving from S5 into G3.
 *
 * @return Remaining time (in milliseconds).
 */
uint32_t get_remaining_s5_inactive_timer(void);

/**
 * @brief Stop timer of S5 innactivity.
 *
 */
void stop_s5_inactive_timer(void);

/**
 * @brief Check if primary AP power rail is good.
 *
 */
int rsmrst_power_is_good(void);

/**
 * @brief Set AP rsmrst signal value based on primary rail.
 *
 */
void rsmrst_pass_thru_handler(void);

/**
 * @brief Check if primary AP power rail is good.
 *
 * @return true if primary AP power is good, and false otherwise.
 */
bool chipset_is_prim_power_good(void);

/**
 * @brief Check if AP power state is good to have operational Virtual Wire (VW)
 * interface.
 *
 * @return true if AP Power state is good for VW, and false otherwise.
 */
bool chipset_is_vw_power_good(void);

/**
 * @brief Check if all AP power rails are good.
 *
 * @return true if all AP power rails are good, and false otherwise.
 */
bool chipset_is_all_power_good(void);

bool ap_power_in_debug_mode(void);

void ap_power_set_start_from_g3_delay_seconds(uint32_t d_time);

#endif /* __X86_NON_DSX_COMMON_PWRSEQ_SM_HANDLER_H__ */
