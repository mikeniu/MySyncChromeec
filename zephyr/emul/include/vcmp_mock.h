/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_EMUL_INCLUDE_VCMP_MOCK_H_
#define ZEPHYR_EMUL_INCLUDE_VCMP_MOCK_H_

#include <zephyr/device.h>

/*
 * Manually trigger the vcmp handler
 *
 * @param dev pointer to the vcmp emulator device
 */
void vcmp_mock_trigger(const struct device *dev);

#endif /* ZEPHYR_EMUL_INCLUDE_VCMP_MOCK_H_ */
