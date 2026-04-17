/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef OCELOTRVP_NPCX_BOARD_H_
#define OCELOTRVP_NPCX_BOARD_H_

/* Power Signals */
#define PWR_EN_PP3300_S5 &gpio0 0
#define PWR_RSMRST_PWRGD &gpio0 1
#define PWR_EC_PCH_RSMRST &gpio0 2
#define PWR_SLP_S0 &gpio0 3
#define PWR_PCH_PWROK &gpio0 4
#define PWR_EC_PCH_SYS_PWROK &gpio0 5
#define PWR_SYS_RST &gpio0 6
#define PWR_ALL_SYS_PWRGD &gpio0 7

#endif /* OCELOTRVP_NPCX_BOARD_H_ */
