/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_TEST_CHRE_MOCK_CHRE_CORE_STATIC_NANOAPPS_H_
#define ZEPHYR_TEST_CHRE_MOCK_CHRE_CORE_STATIC_NANOAPPS_H_

#include <stddef.h>

namespace chre
{

class Nanoapp {};

template <typename T> class UniquePtr {
    public:
	constexpr UniquePtr()
		: ptr_(nullptr)
	{
	}
	explicit UniquePtr(T *ptr)
		: ptr_(ptr)
	{
	}
	~UniquePtr() = default;
	UniquePtr(const UniquePtr &) = delete;
	UniquePtr &operator=(const UniquePtr &) = delete;
	UniquePtr(UniquePtr &&other) noexcept : ptr_(other.ptr_)
	{
		other.ptr_ = nullptr;
	}
	UniquePtr &operator=(UniquePtr &&other) noexcept
	{
		ptr_ = other.ptr_;
		other.ptr_ = nullptr;
		return *this;
	}
	T *get() const
	{
		return ptr_;
	}
	T *operator->() const
	{
		return ptr_;
	}
	bool isNull() const
	{
		return ptr_ == nullptr;
	}

    private:
	T *ptr_ = nullptr;
};

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
