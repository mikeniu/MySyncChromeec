/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Renesas (Intersil) ISL-95522 battery charger public header
 */

#ifndef PLATFORM_EC_INCLUDE_DRIVER_CHARGER_ISL95522_PUBLIC_H_
#define PLATFORM_EC_INCLUDE_DRIVER_CHARGER_ISL95522_PUBLIC_H_

#define ISL95522_ADDR_FLAGS 0x09

extern const struct charger_drv isl95522_drv;

/**
 * Set AC prochot threshold
 *
 * @param chgnum: Index into charger chips
 * @param ma: AC prochot threshold current in mA, multiple of 128mA
 * @return EC_SUCCESS or error
 */
int isl95522_set_ac_prochot(int chgnum, int ma);

/**
 * Set DC prochot threshold
 *
 * @param chgnum: Index into charger chips
 * @param ma: DC prochot threshold current in mA, multiple of 256mA
 * @return EC_SUCCESS or error
 */
int isl95522_set_dc_prochot(int chgnum, int ma);

#define ISL95522_AC_PROCHOT_CURRENT_MIN 128 /* mA */
#define ISL95522_AC_PROCHOT_CURRENT_MAX 8064 /* mA */
#define ISL95522_DC_PROCHOT_CURRENT_MIN 128 /* mA */
#define ISL95522_DC_PROCHOT_CURRENT_MAX 8064 /* mA */

#endif /* PLATFORM_EC_INCLUDE_DRIVER_CHARGER_ISL95522_PUBLIC_H_ */
