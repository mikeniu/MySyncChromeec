/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __STM32_LL_PWR_H
#define __STM32_LL_PWR_H

#include <stdint.h>

#define LL_PWR_REGU_VOLTAGE_SCALE1 1
#define LL_PWR_REGU_VOLTAGE_SCALE3 3

void LL_PWR_SetRegulVoltageScaling(uint32_t VoltageScaling);
uint32_t LL_PWR_IsActiveFlag_VOS(void);

#endif /* __STM32_LL_PWR_H */
