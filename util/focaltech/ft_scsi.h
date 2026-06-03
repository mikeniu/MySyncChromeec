/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef UTIL_FOCALTECH_FT_SCSI_H_
#define UTIL_FOCALTECH_FT_SCSI_H_

#include <array>
#include <cstdint>
#include <cstring>
#include <span>

#include "usb_device.h"

constexpr size_t kFtScsiCmdSize = 16;

struct BulkCbWrap {
  uint32_t signature;
  uint32_t tag;
  uint32_t data_transfer_length;
  uint8_t flags;
  uint8_t lun;
  uint8_t cmd_len;
  uint8_t cmd[kFtScsiCmdSize];
} __attribute__((packed));

struct BulkCsWrap {
  uint32_t signature;
  uint32_t tag;
  uint32_t data_residue;
  uint8_t status;
} __attribute__((packed));

enum class TransferDirection { kIn, kOut, kSendOnly };

std::array<uint8_t, kFtScsiCmdSize> BuildFtCmd(uint8_t opcode, uint8_t subcmd,
                                               uint32_t address = 0,
                                               uint32_t length = 0,
                                               uint32_t aux_address = 0,
                                               uint8_t flags = 0);

// For sending data TO the device (Data buffer is const)
std::expected<void, FtError> SendBulkDataOut(
    const UsbDevice& device, std::span<const uint8_t> scsi_cmd,
    std::span<const uint8_t> data_buffer, TransferDirection dir,
    std::chrono::milliseconds timeout_ms);

// For receiving data FROM the device (Data buffer is mutable)
std::expected<void, FtError> ReceiveBulkDataIn(
    const UsbDevice& device, std::span<const uint8_t> scsi_cmd,
    std::span<uint8_t> data_buffer, std::chrono::milliseconds timeout_ms);

#endif  // UTIL_FOCALTECH_FT_SCSI_H_
