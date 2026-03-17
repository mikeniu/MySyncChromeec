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
#include <optional>
#include <span>
#include <system_error>
#include <type_traits>
#include <vector>

namespace egis {

template <typename T>
concept TriviallyCopyable = std::is_trivially_copyable_v<T>;

template <TriviallyCopyable T>
std::expected<void, std::errc> WriteStruct(std::span<uint8_t> buffer,
                                           const T &obj, size_t offset = 0) {
  if (offset > buffer.size() || buffer.size() - offset < sizeof(T))
    return std::unexpected(std::errc::no_buffer_space);
  std::memcpy(buffer.data() + offset, &obj, sizeof(T));
  return {};
}

template <TriviallyCopyable T>
std::expected<T, std::errc> ReadStruct(std::span<const uint8_t> buffer,
                                       size_t offset = 0) {
  if (offset > buffer.size() || buffer.size() - offset < sizeof(T))
    return std::unexpected(std::errc::message_size);
  T obj;
  std::memcpy(&obj, buffer.data() + offset, sizeof(T));
  return obj;
}

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
std::optional<T> ReadLittleEndian(std::span<const uint8_t> buffer) {
  if (buffer.size() < sizeof(T)) return std::nullopt;

  T value;
  std::memcpy(&value, buffer.data(), sizeof(T));
  return FromLittleEndian(value);
}

}  // namespace egis

#endif  // UTIL_EGIS_UTIL_H_
