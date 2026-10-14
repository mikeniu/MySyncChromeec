/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_TEST_CHRE_MOCK_CHRE_PLATFORM_SYSTEM_TIME_H_
#define ZEPHYR_TEST_CHRE_MOCK_CHRE_PLATFORM_SYSTEM_TIME_H_

#include <stdint.h>

namespace chre
{

class Nanoseconds {
    public:
	constexpr explicit Nanoseconds(uint64_t ns)
		: ns_(ns)
	{
	}
	constexpr uint64_t toRawNanoseconds() const
	{
		return ns_;
	}

    private:
	uint64_t ns_;
};

class SystemTime {
    public:
	static Nanoseconds getMonotonicTime();
};

} // namespace chre

#endif /* ZEPHYR_TEST_CHRE_MOCK_CHRE_PLATFORM_SYSTEM_TIME_H_ */
