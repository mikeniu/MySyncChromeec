/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef UTIL_FOCALTECH_FT_UTIL_H_
#define UTIL_FOCALTECH_FT_UTIL_H_

#include <bit>
#include <cassert>
#include <concepts>
#include <cstdint>
#include <expected>
#include <span>
#include <string_view>
#include <type_traits>
#include <vector>

namespace focaltech {

template <typename T>
constexpr T DivRoundUp(T numerator, T denominator) {
  assert(denominator != 0);
  return (numerator + denominator - 1) / denominator;
}

// C++23 native endianness swapping
template <std::integral T>
constexpr T ToLittleEndian(T value) {
  if constexpr (std::endian::native == std::endian::big) {
    return std::byteswap(value);
  }
  return value;
}

// Convert a little-endian value from the wire to host endianness.
template <std::integral T>
constexpr T FromLittleEndian(T value) {
  return ToLittleEndian(value);
}

// Safely view any TriviallyCopyable object as a read-only byte span
template <typename T>
  requires std::is_trivially_copyable_v<T>
inline std::span<const uint8_t> AsUint8Span(const T& obj) {
  return {reinterpret_cast<const uint8_t*>(&obj), sizeof(T)};
}

// Safely view any TriviallyCopyable object as a mutable byte span
template <typename T>
  requires std::is_trivially_copyable_v<T>
inline std::span<uint8_t> AsWritableUint8Span(T& obj) {
  return {reinterpret_cast<uint8_t*>(&obj), sizeof(T)};
}

enum class Error {
  kSuccess = 0,
  kDeviceNotFound = -1,
  kInvalidParameter = -2,
  kFileNotFound = -3,
  kHardwareFailure = -4,
  kInvalidMode = -5,
  kInvalidFormat = -6,
  kVerificationFailed = -7,
};

inline std::string_view ToString(Error err) {
  switch (err) {
    case Error::kSuccess:
      return "Success";
    case Error::kDeviceNotFound:
      return "Device not found";
    case Error::kInvalidParameter:
      return "Invalid parameter";
    case Error::kFileNotFound:
      return "File not found";
    case Error::kHardwareFailure:
      return "Hardware failure";
    case Error::kInvalidMode:
      return "Invalid mode";
    case Error::kInvalidFormat:
      return "Invalid firmware format";
    case Error::kVerificationFailed:
      return "Verification failed";
    default:
      return "Unknown error";
  }
}

std::expected<std::vector<uint8_t>, Error> ReadFileToVector(
    std::string_view path);

}  // namespace focaltech

#endif  // UTIL_FOCALTECH_FT_UTIL_H_
