/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_TEST_CHRE_MOCK_CHRE_CORE_STATIC_NANOAPPS_H_
#define ZEPHYR_TEST_CHRE_MOCK_CHRE_CORE_STATIC_NANOAPPS_H_

#include "chre/core/nanoapp.h"
#include "chre/util/unique_ptr.h"

#include <stddef.h>

namespace chre
{

typedef UniquePtr<Nanoapp> (*StaticNanoappInitFunction)();

extern const StaticNanoappInitFunction kStaticNanoappList[];
extern const size_t kStaticNanoappCount;

#ifdef CONFIG_CHRE_TIMER_WORLD
UniquePtr<Nanoapp> initializeStaticNanoappTimerWorld();
#endif
#ifdef CONFIG_CHRE_HELLO_WORLD
UniquePtr<Nanoapp> initializeStaticNanoappHelloWorld();
#endif

} // namespace chre

#endif // ZEPHYR_TEST_CHRE_MOCK_CHRE_CORE_STATIC_NANOAPPS_H_
