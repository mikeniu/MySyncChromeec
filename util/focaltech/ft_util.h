/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef UTIL_FOCALTECH_FT_UTIL_H_
#define UTIL_FOCALTECH_FT_UTIL_H_

#include <bit>
#include <cassert>
#include <concepts>
#include <string_view>

template <typename T>
constexpr T FtDivRoundUp(T n, T d) {
  assert(d != 0);
  return (n + d - 1) / d;
}

// C++23 native endianness swapping
template <std::integral T>
constexpr T ToLittleEndian(T value) {
  if constexpr (std::endian::native == std::endian::big) {
    return std::byteswap(value);
  }
  return value;
}

enum class FtError {
  kSuccess = 0,
  kNoSuchDevice = -1,
  kParameterError = -2,
  kNoSuchFile = -3,
  kResultFail = -4,
  kInvalidMode = -5,
};

inline std::string_view ToString(FtError err) {
  switch (err) {
    case FtError::kSuccess:
      return "Success";
    case FtError::kNoSuchDevice:
      return "No such device";
    case FtError::kParameterError:
      return "Parameter error";
    case FtError::kNoSuchFile:
      return "No such file";
    case FtError::kResultFail:
      return "Result fail";
    case FtError::kInvalidMode:
      return "Invalid mode";
    default:
      return "Unknown error";
  }
}

#endif  // UTIL_FOCALTECH_FT_UTIL_H_
