/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_TEST_CHRE_MOCK_CHRE_CORE_MULTI_THREADING_API_MUTEX_H_
#define ZEPHYR_TEST_CHRE_MOCK_CHRE_CORE_MULTI_THREADING_API_MUTEX_H_

namespace chre
{

class Mutex {
    public:
	void lock()
	{
	}
	void unlock()
	{
	}
};

inline Mutex *getMultiThreadingApiMutex()
{
	static Mutex mutex;
	return &mutex;
}

} // namespace chre

#endif // ZEPHYR_TEST_CHRE_MOCK_CHRE_CORE_MULTI_THREADING_API_MUTEX_H_
