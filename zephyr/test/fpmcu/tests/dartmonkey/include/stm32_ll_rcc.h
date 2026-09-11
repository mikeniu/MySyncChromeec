/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __STM32_LL_RCC_H
#define __STM32_LL_RCC_H

#include <stdint.h>

#define LL_RCC_SYS_CLKSOURCE_HSI 0x00000000U
#define LL_RCC_SYS_CLKSOURCE_PLL1 0x00000003U

#define LL_RCC_SYS_CLKSOURCE_STATUS_HSI 0x00000000U
#define LL_RCC_SYS_CLKSOURCE_STATUS_PLL1 0x00000018U

#define LL_RCC_AHB_DIV_1 0x00000000U
#define LL_RCC_AHB_DIV_2 0x00000008U

#define LL_RCC_APB1_DIV_1 0x00000000U
#define LL_RCC_APB1_DIV_2 0x00000004U

#define LL_RCC_APB2_DIV_1 0x00000000U
#define LL_RCC_APB2_DIV_2 0x00000004U

#define LL_RCC_APB3_DIV_1 0x00000000U
#define LL_RCC_APB3_DIV_2 0x00000004U

#define LL_RCC_APB4_DIV_1 0x00000000U
#define LL_RCC_APB4_DIV_2 0x00000004U

void LL_RCC_PLL1_Enable(void);
uint32_t LL_RCC_PLL1_IsReady(void);
void LL_RCC_PLL1_Disable(void);
void LL_RCC_PLL1Q_Disable(void);
void LL_RCC_PLL1R_Disable(void);

void LL_RCC_SetAHBPrescaler(uint32_t Prescaler);
void LL_RCC_SetAPB1Prescaler(uint32_t Prescaler);
void LL_RCC_SetAPB2Prescaler(uint32_t Prescaler);
void LL_RCC_SetAPB3Prescaler(uint32_t Prescaler);
void LL_RCC_SetAPB4Prescaler(uint32_t Prescaler);

void LL_RCC_SetSysClkSource(uint32_t Source);
uint32_t LL_RCC_GetSysClkSource(void);

uint32_t LL_RCC_HSI_IsReady(void);
void LL_RCC_HSI_Enable(void);

#endif /* __STM32_LL_RCC_H */
