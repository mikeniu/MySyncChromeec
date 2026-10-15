/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_TEST_CHRE_MOCK_CHRE_TARGET_PLATFORM_CONTEXT_H_
#define ZEPHYR_TEST_CHRE_MOCK_CHRE_TARGET_PLATFORM_CONTEXT_H_

#include "pw_thread/thread.h"

namespace chre
{

void registerChreEventLoopTask(pw::thread::Id id);

} // namespace chre

#endif // ZEPHYR_TEST_CHRE_MOCK_CHRE_TARGET_PLATFORM_CONTEXT_H_
