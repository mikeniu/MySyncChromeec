/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef UTIL_FOCALTECH_TEST_UTILS_H_
#define UTIL_FOCALTECH_TEST_UTILS_H_

#include <gmock/gmock.h>
#include <openssl/sha.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <span>

#include "ft_scsi.h"
#include "ft_util.h"
#include "updater.h"

namespace focaltech::testing {

inline constexpr size_t kUsbCbwSize = sizeof(BulkCbw);
inline constexpr size_t kUsbCswSize = sizeof(BulkCsw);
inline constexpr uint32_t kUsbMsCswSignature = 0x53425355;

inline constexpr uint8_t kArbitraryPayloadByte = 0xAA;

inline void BuildValidCsw(std::span<uint8_t> data, uint32_t tag,
                          CswStatus status = CswStatus::kCommandPassed) {
  const BulkCsw csw{
      .signature = ToLittleEndian(kUsbMsCswSignature),
      .tag = ToLittleEndian(tag),
      .data_residue = 0,
      .status = status,
  };
  std::ranges::copy(AsUint8Span(csw), data.begin());
}

ACTION_P(ReturnValidCsw, tag) {
  focaltech::testing::BuildValidCsw(arg0, tag);
  return std::expected<size_t, focaltech::Error>(
      focaltech::testing::kUsbCswSize);
}

}  // namespace focaltech::testing

#endif  // UTIL_FOCALTECH_TEST_UTILS_H_
