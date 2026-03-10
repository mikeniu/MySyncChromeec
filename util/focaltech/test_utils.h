/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef UTIL_FOCALTECH_TEST_UTILS_H_
#define UTIL_FOCALTECH_TEST_UTILS_H_

#include <openssl/sha.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <span>

#include "ft_util.h"

namespace focaltech::testing {

inline constexpr size_t kUsbCbwSize = 31;
inline constexpr size_t kUsbCswSize = 13;
inline constexpr uint32_t kUsbMsCswSignature = 0x53425355;

// Safely builds a valid USB Mass Storage Command Status Wrapper (CSW)
inline void BuildValidCsw(std::span<uint8_t> data, uint32_t tag,
                          uint8_t status = 0) {
  struct __attribute__((packed)) Csw {
    uint32_t sig;
    uint32_t tag;
    uint32_t residue;
    uint8_t status;
  };

  Csw csw{
      .sig = ToLittleEndian<uint32_t>(kUsbMsCswSignature),
      .tag = ToLittleEndian<uint32_t>(tag),
      .residue = 0,
      .status = status,
  };
  std::memcpy(data.data(), &csw, sizeof(csw));
}

// Standardized SHA256 computation for test payloads
inline std::array<uint8_t, SHA256_DIGEST_LENGTH> ComputeSha256(
    std::span<const uint8_t> data) {
  std::array<uint8_t, SHA256_DIGEST_LENGTH> hash{};
  SHA256(data.data(), data.size(), hash.data());
  return hash;
}

}  // namespace focaltech::testing

#endif  // UTIL_FOCALTECH_TEST_UTILS_H
