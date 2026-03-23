// Copyright 2026 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UTIL_ELAN_UTILITY_H_
#define UTIL_ELAN_UTILITY_H_

#include <cstdio>
#include <print>
#include <utility>

template <typename... Args>
void LogInfo(std::format_string<Args...> fmt, Args &&...args) {
  std::print(stdout, fmt, std::forward<Args>(args)...);
}

template <typename... Args>
void LogErr(std::format_string<Args...> fmt, Args &&...args) {
  std::print(stderr, fmt, std::forward<Args>(args)...);
}

#endif
