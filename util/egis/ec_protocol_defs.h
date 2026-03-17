/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef UTIL_EGIS_EC_PROTOCOL_DEFS_H_
#define UTIL_EGIS_EC_PROTOCOL_DEFS_H_

#include <cstddef>
#include <cstdint>
#include <span>

namespace egis {

constexpr uint8_t kEcProtocolVersion3 = 3;

enum class EcCommandResult : uint8_t {
  kSuccess = 0,
  kInvalidCommand = 1,
  kError = 2,
  kInvalidParam = 3,
  kAccessDenied = 4,
  kInvalidResponse = 5,
  kInvalidVersion = 6,
  kInvalidChecksum = 7,
  kInProgress = 8,
  kUnavailable = 9,
  kTimeout = 10,
  kOverflow = 11,
  kInvalidHeader = 12,
  kRequestTruncated = 13,
  kResponseTooBig = 14,
  kBusError = 15,
  kBusy = 16,
  kInvalidHeaderVersion = 17,
  kInvalidHeaderCrc = 18,
  kInvalidDataCrc = 19,
  kDupUnavailable = 20,
  kUnknown = 255,
};

enum class EcCommandId : uint16_t {
  kEcCmdGetVersion = 0x0002,
  kEcCmdGetVersions = 0x0008,
  kEcCmdProtocolInfo = 0x000B,
  kEcCmdFlashInfo = 0x0010,
  kEcCmdFlashRead = 0x0011,
  kEcCmdFlashWrite = 0x0012,
  kEcCmdFlashErase = 0x0013,
  kEcCmdFlashRegionInfo = 0x0016,
  kEcCmdRebootEc = 0x00D2,
  kEcCmdEnterBootloader = 0x00E2,
  kEcCmdFpMode = 0x0402,
  kEcCmdFpInfo = 0x0403,
};

struct EcHostCommandRequest {
  uint8_t protocol_version = kEcProtocolVersion3;
  uint8_t checksum = 0;
  uint16_t command_id;
  uint8_t command_version;
  uint8_t reserved = 0;
  uint16_t data_length;
} __attribute__((packed));

struct EcHostCommandResponse {
  uint8_t protocol_version = kEcProtocolVersion3;
  uint8_t checksum = 0;
  uint16_t result;
  uint16_t data_length;
  uint16_t reserved = 0;
} __attribute__((packed));

struct GetVersionResponse {
  char ro_ver[32];
  char rw_ver[32];
  char fwid_ro[32];
  char fwid_rw[32];
  uint32_t current_image;
} __attribute__((packed));

// Ensure requests and responses have the checksum at the exact same byte offset
static_assert(offsetof(EcHostCommandRequest, checksum) ==
                  offsetof(EcHostCommandResponse, checksum),
              "Checksum offset must be identical for requests and responses");

// Computes and applies the two's-complement checksum required by the EC
// protocol.
inline void ApplyEcChecksum(std::span<uint8_t> packet) {
  constexpr size_t kChecksumOffset = offsetof(EcHostCommandRequest, checksum);
  if (packet.size() <= kChecksumOffset) return;

  packet[kChecksumOffset] = 0;

  uint32_t sum = 0;
  for (uint8_t byte : packet) {
    sum += byte;
  }

  packet[kChecksumOffset] = static_cast<uint8_t>(-sum);
}

}  // namespace egis

#endif  // UTIL_EGIS_EC_PROTOCOL_DEFS_H_
