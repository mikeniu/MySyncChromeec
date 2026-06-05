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
constexpr T DivRoundUp(T n, T d) {
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

// Convert a little-endian value from the wire to host endianness.
template <typename T>
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
  kNoSuchDevice = -1,
  kParameterError = -2,
  kNoSuchFile = -3,
  kResultFail = -4,
  kInvalidMode = -5,
};

inline std::string_view ToString(Error err) {
  switch (err) {
    case Error::kSuccess:
      return "Success";
    case Error::kNoSuchDevice:
      return "No such device";
    case Error::kParameterError:
      return "Parameter error";
    case Error::kNoSuchFile:
      return "No such file";
    case Error::kResultFail:
      return "Result fail";
    case Error::kInvalidMode:
      return "Invalid mode";
    default:
      return "Unknown error";
  }
}

std::expected<std::vector<uint8_t>, Error> ReadFileToVector(
    std::string_view path);

}  // namespace focaltech

#endif  // UTIL_FOCALTECH_FT_UTIL_H_
