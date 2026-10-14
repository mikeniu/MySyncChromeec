/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "plat_time.h"
#include "timer.h"

unsigned long long plat_get_time(void)
{
	return (get_time().val / MSEC);
}

unsigned long plat_get_diff_time(unsigned long long begin)
{
	unsigned long long nowTime = plat_get_time();

	return (unsigned long)(nowTime - begin);
}

void plat_wait_time(unsigned long msecs)
{
	k_busy_wait(msecs * MSEC);
	return;
}

void plat_sleep_time(unsigned long timeInMs)
{
	k_usleep(timeInMs * MSEC);
	return;
}
