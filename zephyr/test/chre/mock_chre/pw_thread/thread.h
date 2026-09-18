/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_TEST_CHRE_MOCK_PW_THREAD_THREAD_H_
#define ZEPHYR_TEST_CHRE_MOCK_PW_THREAD_THREAD_H_

#include <stdint.h>

namespace pw
{
namespace thread
{

	class Id {
	    public:
		constexpr Id()
			: id_(0)
		{
		}
		constexpr explicit Id(uintptr_t id)
			: id_(id)
		{
		}
		bool operator==(const Id &other) const
		{
			return id_ == other.id_;
		}
		bool operator!=(const Id &other) const
		{
			return id_ != other.id_;
		}

	    private:
		uintptr_t id_;
	};

} // namespace thread

namespace this_thread
{

	inline thread::Id get_id()
	{
		return thread::Id(1);
	}

} // namespace this_thread
} // namespace pw

#endif // ZEPHYR_TEST_CHRE_MOCK_PW_THREAD_THREAD_H_
