/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * @file
 * @brief PD task to configure USB-C Alternate modes on Intel SoC.
 */

#ifndef PLATFORM_EC_ZEPHYR_INCLUDE_USBC_PD_TASK_INTEL_ALTMODE_H_
#define PLATFORM_EC_ZEPHYR_INCLUDE_USBC_PD_TASK_INTEL_ALTMODE_H_

/**
 * @brief Starts the Intel Alternate Mode configuration thread.
 */
void intel_altmode_task_start(void);

#if defined(CONFIG_USBC_INTEL_ALTMODE) || defined(CONFIG_ZTEST)
/*
 * @brief Suspend pd_intel_altmode_task.
 */
void suspend_pd_intel_altmode_task(void);

/*
 * @brief Resume pd_intel_altmode_task.
 */
void resume_pd_intel_altmode_task(void);

/*
 * @brief Check if the state of pd_intel_altmode_task is suspended.
 *
 * @return True if state of the pd_intel_altmode_task is suspended.
 *         Else false.
 */
bool is_pd_intel_altmode_task_suspended(void);
#else
static inline void suspend_pd_intel_altmode_task(void)
{
}
static inline void resume_pd_intel_altmode_task(void)
{
}
static inline bool is_pd_intel_altmode_task_suspended(void)
{
	return true;
}
#endif /* defined(CONFIG_USBC_INTEL_ALTMODE) */

#endif /* PLATFORM_EC_ZEPHYR_INCLUDE_USBC_PD_TASK_INTEL_ALTMODE_H_ */
