/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_INCLUDE_USBC_PDC_UTILS_H_
#define PLATFORM_EC_ZEPHYR_INCLUDE_USBC_PDC_UTILS_H_

#define __TALLY_CCD_PROP(node) COND_CODE_1(DT_PROP(node, ccd), (1), (0)) +

/**
 * @brief Count the number of nodes with the `ccd` property true under the
 *        provided compat string.
 */
#define CCD_PORTS_BY_DT_DRV_COMPAT_COUNT \
	(DT_FOREACH_STATUS_OKAY(DT_DRV_COMPAT, __TALLY_CCD_PROP) 0)

/**
 * @brief If PDC-driven CCD is used, one of the PDC driver nodes must be marked
 *        with the `ccd` property. Check the devicetree and trigger a build
 *        assert if this not the case.
 */
#define CHECK_ONE_CCD_PORT_COUNT_FOR_DRIVER()                           \
	BUILD_ASSERT(1 == CCD_PORTS_BY_DT_DRV_COMPAT_COUNT,             \
		     "Exactly one " STRINGIFY(                          \
			     DT_DRV_COMPAT) " PDC node must be tagged " \
					    "with the `ccd` property")

#endif /* PLATFORM_EC_ZEPHYR_INCLUDE_USBC_PDC_UTILS_H_ */
