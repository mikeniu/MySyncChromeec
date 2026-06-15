/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef UTIL_EGIS_UTIL_H_
#define UTIL_EGIS_UTIL_H_

#include <algorithm>
#include <bit>
#include <concepts>
#include <cstdint>
#include <cstring>
#include <expected>
#include <ranges>
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
std::expected<T, std::errc> ReadLittleEndian(std::span<const uint8_t> buffer) {
  if (buffer.size() < sizeof(T))
    return std::unexpected(std::errc::message_size);

  T value;
  std::memcpy(&value, buffer.data(), sizeof(T));
  return FromLittleEndian(value);
}

template <TriviallyCopyable T>
std::span<const uint8_t> AsBytes(const T &obj) {
  return {reinterpret_cast<const uint8_t *>(&obj), sizeof(T)};
}

template <TriviallyCopyable T>
std::vector<uint8_t> PackMessage(const T &header,
                                 std::span<const uint8_t> payload = {}) {
  std::vector<uint8_t> buffer;
  // Pre-allocate the exact size to prevent reallocations
  buffer.reserve(sizeof(T) + payload.size());

  auto header_bytes = AsBytes(header);

  buffer.insert(buffer.end(), header_bytes.begin(), header_bytes.end());
  buffer.insert(buffer.end(), payload.begin(), payload.end());

  return buffer;
}

// Safely creates a string_view from a fixed-size character array that may
// or may not be null-terminated.
template <size_t N>
constexpr std::string_view MakeStringView(const char (&str)[N]) {
  return std::string_view(str, std::ranges::find(str, '\0'));
}

}  // namespace egis

#endif  // UTIL_EGIS_UTIL_H_
