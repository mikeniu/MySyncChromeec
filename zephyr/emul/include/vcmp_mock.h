/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_EMUL_INCLUDE_VCMP_MOCK_H_
#define PLATFORM_EC_ZEPHYR_EMUL_INCLUDE_VCMP_MOCK_H_

#include <zephyr/device.h>

/*
 * Manually trigger the vcmp handler
 *
 * @param dev pointer to the vcmp emulator device
 */
void vcmp_mock_trigger(const struct device *dev);

#endif /* PLATFORM_EC_ZEPHYR_EMUL_INCLUDE_VCMP_MOCK_H_ */
