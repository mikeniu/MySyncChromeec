/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_TEST_CHRE_MOCK_CHRE_PLATFORM_MEMORY_H_
#define ZEPHYR_TEST_CHRE_MOCK_CHRE_PLATFORM_MEMORY_H_

#include <stddef.h>

namespace chre
{

void *memoryAlloc(size_t size);
void memoryFree(void *pointer);

} // namespace chre

#endif /* ZEPHYR_TEST_CHRE_MOCK_CHRE_PLATFORM_MEMORY_H_ */
