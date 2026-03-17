/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef UTIL_EGIS_UTIL_H_
#define UTIL_EGIS_UTIL_H_

#include <bit>
#include <concepts>
#include <cstdint>
#include <cstring>
#include <expected>
#include <span>
#include <vector>

namespace egis {

template <std::integral T>
constexpr T ToLittleEndian(T value) {
  return (std::endian::native == std::endian::little) ? value
                                                      : std::byteswap(value);
}

template <std::integral T>
constexpr T FromLittleEndian(T value) {
  return ToLittleEndian(value);
}

template <std::integral T>
void AppendLittleEndian(std::vector<uint8_t> &vec, T value) {
  T le_value = ToLittleEndian(value);
  const auto *bytes = reinterpret_cast<const uint8_t *>(&le_value);
  vec.append_range(std::span(bytes, sizeof(T)));
}

template <std::integral T>
std::expected<T, int> ReadLittleEndian(std::span<const uint8_t> buffer) {
  if (buffer.size() < sizeof(T)) return std::unexpected(-1);

  T value;
  std::memcpy(&value, buffer.data(), sizeof(T));
  return FromLittleEndian(value);
}

}  // namespace egis

#endif  // UTIL_EGIS_UTIL_H_
