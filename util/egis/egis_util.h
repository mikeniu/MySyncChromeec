/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef UTIL_EGIS_EGIS_UTIL_H_
#define UTIL_EGIS_EGIS_UTIL_H_

#include <algorithm>
#include <bit>
#include <concepts>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <expected>
#include <print>
#include <ranges>
#include <span>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <vector>

namespace egis {

template <typename T>
concept TriviallyCopyable = std::is_trivially_copyable_v<T>;

template <TriviallyCopyable T>
std::expected<void, std::errc> WriteStruct(std::span<uint8_t> buffer,
                                           const T& obj, size_t offset = 0) {
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
std::expected<T, std::errc> ReadLittleEndian(std::span<const uint8_t> buffer,
                                             size_t offset = 0) {
  return ReadStruct<T>(buffer, offset).transform(FromLittleEndian<T>);
}

template <TriviallyCopyable T>
std::span<const uint8_t> AsBytes(const T& obj) {
  return {reinterpret_cast<const uint8_t*>(&obj), sizeof(T)};
}

template <TriviallyCopyable T>
std::vector<uint8_t> PackMessage(const T& base_object,
                                 std::span<const uint8_t> payload = {}) {
  std::vector<uint8_t> buffer;
  // Pre-allocate the exact size to prevent reallocations
  buffer.reserve(sizeof(T) + payload.size());

  buffer.append_range(AsBytes(base_object));
  buffer.append_range(payload);

  return buffer;
}

// Safely creates a string_view from a fixed-size character array that may
// or may not be null-terminated.
template <size_t N>
constexpr std::string_view MakeStringView(const char (&str)[N]) {
  return std::string_view(str, std::ranges::find(str, '\0'));
}

inline void PrintProgress(std::string_view prefix, size_t done, size_t total) {
  if (total == 0) return;
  std::print("\r{}: {} / {} ({:.1f}%)", prefix, done, total,
             (static_cast<float>(done) / total * 100.0f));
  std::fflush(stdout);
}

}  // namespace egis

#endif  // UTIL_EGIS_EGIS_UTIL_H_
