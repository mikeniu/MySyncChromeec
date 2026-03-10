/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef UTIL_FOCALTECH_FT_SCSI_H_
#define UTIL_FOCALTECH_FT_SCSI_H_

#include <assert.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <span>

#include "usb_device.h"

namespace focaltech {

constexpr size_t kScsiCmdSize = 16;

struct BulkCbWrap {
  uint32_t signature;
  uint32_t tag;
  uint32_t data_transfer_length;
  uint8_t flags;
  uint8_t lun;
  uint8_t cmd_len;
  uint8_t cmd[kScsiCmdSize];
} __attribute__((packed));

static_assert(sizeof(BulkCbWrap) == 31, "USB CBW must be exactly 31 bytes");

struct BulkCsWrap {
  uint32_t signature;
  uint32_t tag;
  uint32_t data_residue;
  uint8_t status;
} __attribute__((packed));

static_assert(sizeof(BulkCsWrap) == 13, "USB CSW must be exactly 13 bytes");

struct CmdFormat {
  uint8_t opcode;
  uint32_t address;
  uint32_t length;
  uint32_t aux_address;
  uint8_t subcmd;
  uint8_t flags;
  uint8_t reserved;
} __attribute__((packed));

static_assert(sizeof(CmdFormat) == 16, "CmdFormat must be exactly 16 bytes");

enum class TransferDirection { kIn, kOut, kSendOnly };

std::array<uint8_t, kScsiCmdSize> BuildFtCmd(uint8_t opcode, uint8_t subcmd,
                                             uint32_t address = 0,
                                             uint32_t length = 0,
                                             uint32_t aux_address = 0,
                                             uint8_t flags = 0);

// For sending data TO the device (Data buffer is const)
std::expected<void, Error> SendBulkDataOut(
    const UsbDevice& device, std::span<const uint8_t> scsi_cmd,
    std::span<const uint8_t> data_buffer, TransferDirection dir,
    std::chrono::milliseconds timeout_ms);

// For receiving data FROM the device (Data buffer is mutable)
std::expected<void, Error> ReceiveBulkDataIn(
    const UsbDevice& device, std::span<const uint8_t> scsi_cmd,
    std::span<uint8_t> data_buffer, std::chrono::milliseconds timeout_ms);

}  // namespace focaltech

#endif  // UTIL_FOCALTECH_FT_SCSI_H_
