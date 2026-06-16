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

#include "ft_scsi.h"
#include "ft_util.h"
#include "updater.h"

namespace focaltech::testing {

inline constexpr size_t kUsbCbwSize = sizeof(BulkCbWrap);
inline constexpr size_t kUsbCswSize = sizeof(BulkCsWrap);
inline constexpr uint32_t kUsbMsCswSignature = 0x53425355;

inline void BuildValidCsw(std::span<uint8_t> data, uint32_t tag,
                          CswStatus status = CswStatus::kCommandPassed) {
  BulkCsWrap csw{
      .signature = ToLittleEndian(kUsbMsCswSignature),
      .tag = ToLittleEndian(tag),
      .data_residue = 0,
      .status = status,
  };
  std::ranges::copy(AsUint8Span(csw), data.begin());
}

}  // namespace focaltech::testing

#endif  // UTIL_FOCALTECH_TEST_UTILS_H_
