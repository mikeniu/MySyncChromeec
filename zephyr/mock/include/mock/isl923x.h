/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_MOCK_INCLUDE_MOCK_ISL923X_H_
#define PLATFORM_EC_ZEPHYR_MOCK_INCLUDE_MOCK_ISL923X_H_

#include "charger.h"

enum ec_error_list raa489000_is_acok_absent(int charger, bool *acok);
enum ec_error_list raa489000_is_acok_present(int charger, bool *acok);
enum ec_error_list raa489000_is_acok_error(int charger, bool *acok);

#endif /* PLATFORM_EC_ZEPHYR_MOCK_INCLUDE_MOCK_ISL923X_H_ */
