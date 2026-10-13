/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_TEST_CHRE_MOCK_CHRE_UTIL_UNIQUE_PTR_H_
#define ZEPHYR_TEST_CHRE_MOCK_CHRE_UTIL_UNIQUE_PTR_H_

#include <stddef.h>

namespace std
{
template <typename T> struct remove_reference {
	typedef T type;
};
template <typename T> struct remove_reference<T &> {
	typedef T type;
};
template <typename T> struct remove_reference<T &&> {
	typedef T type;
};

template <typename T>
constexpr typename remove_reference<T>::type &&move(T &&t) noexcept
{
	return static_cast<typename remove_reference<T>::type &&>(t);
}

template <typename T>
constexpr T &&forward(typename remove_reference<T>::type &t) noexcept
{
	return static_cast<T &&>(t);
}
template <typename T>
constexpr T &&forward(typename remove_reference<T>::type &&t) noexcept
{
	return static_cast<T &&>(t);
}
} // namespace std

namespace chre
{

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
	~UniquePtr()
	{
		delete ptr_;
	}
	UniquePtr(const UniquePtr &) = delete;
	UniquePtr &operator=(const UniquePtr &) = delete;
	UniquePtr(UniquePtr &&other) noexcept : ptr_(other.ptr_)
	{
		other.ptr_ = nullptr;
	}
	UniquePtr &operator=(UniquePtr &&other) noexcept
	{
		if (this != &other) {
			delete ptr_;
			ptr_ = other.ptr_;
			other.ptr_ = nullptr;
		}
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
	T &operator*() const
	{
		return *ptr_;
	}
	bool isNull() const
	{
		return ptr_ == nullptr;
	}
	void reset(T *ptr = nullptr)
	{
		delete ptr_;
		ptr_ = ptr;
	}

    private:
	T *ptr_ = nullptr;
};

template <typename T, typename... Args> UniquePtr<T> MakeUnique(Args &&...args)
{
	return UniquePtr<T>(new T(std::forward<Args>(args)...));
}

} // namespace chre

#endif // ZEPHYR_TEST_CHRE_MOCK_CHRE_UTIL_UNIQUE_PTR_H_
