// Copyright 2026 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UTIL_ELAN_UTILITY_H
#define UTIL_ELAN_UTILITY_H

#include <format>
#include <iostream>

template <typename... Args>
void logInfo(std::format_string<Args...> fmt, Args &&...args)
{
	std::cout << std::format(fmt, std::forward<Args>(args)...);
}

#endif
