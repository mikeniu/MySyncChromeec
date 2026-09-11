/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __STM32_LL_UTILS_H
#define __STM32_LL_UTILS_H

#include <stdint.h>

extern uint32_t SystemCoreClock;

void LL_SetFlashLatency(uint32_t HCLK_Frequency);

#endif /* __STM32_LL_UTILS_H */
