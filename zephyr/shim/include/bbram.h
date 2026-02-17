/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_BBRAM_H
#define __CROS_EC_BBRAM_H

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/toolchain.h>

#ifdef __cplusplus
extern "C" {
#endif

#if DT_HAS_COMPAT_STATUS_OKAY(named_bbram_regions)
BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(named_bbram_regions) == 1,
	     "only one named-bbram-regions compatible node may be present");
#endif

#define BBRAM_NODE DT_COMPAT_GET_ANY_STATUS_OKAY(named_bbram_regions)

/*
 * Check if a specific region has been defined under the named-bbram-region
 * node.
 */
#define BBRAM_HAS_REGION(name) (DT_NODE_EXISTS(DT_CHILD(BBRAM_NODE, name)))

/*
 * Get the size of a specific region.
 */
#define BBRAM_REGION_SIZE(name) (DT_PROP(DT_CHILD(BBRAM_NODE, name), size))

/*
 * Get the offset of a specific region.
 */
#define BBRAM_REGION_OFFSET(name) (DT_PROP(DT_CHILD(BBRAM_NODE, name), offset))

/**
 * @brief		Write to BBRAM based on retained memory.
 *
 * @param dev		Retained memory device.
 * @param offset	Offset to write data to.
 * @param size		Size of data to write.
 * @param data		Data to write.
 *
 * @return		0 on success else negative errno code.
 */
int retained_mem_bbram_write(const struct device *dev, size_t offset,
			     size_t size, const uint8_t *data);

/**
 * @brief		Initialize BBRAM based on retained memory device.
 *
 * @param dev		Retained memory device.
 *
 * @return		0 on success else negative errno code.
 */
int retained_mem_bbram_init(const struct device *dev);

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_BBRAM_H */
