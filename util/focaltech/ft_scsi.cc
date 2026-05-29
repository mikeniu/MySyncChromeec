/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <endian.h>

#include <bit>

#include "ft_log.h"
#include "ft_util.h"
#include "usb_context.h"
// To prevent the formatter from rearranging the header include order.
#include "ft_scsi.h"

namespace {

constexpr uint32_t kUsbMsCbwSignature = 0x43425355;
constexpr uint32_t kUsbMsCswSignature = 0x53425355;
constexpr uint8_t kBotDirectionIn = static_cast<uint8_t>(LIBUSB_ENDPOINT_IN);
constexpr uint8_t kBotDirectionOut = static_cast<uint8_t>(LIBUSB_ENDPOINT_OUT);
constexpr uint8_t kBotCommandPassed = 0x00;

struct BulkCbWrap {
  uint32_t signature;
  uint32_t tag;
  uint32_t data_transfer_length;
  uint8_t flags;
  uint8_t lun;
  uint8_t cmd_len;
  uint8_t cmd[16];
} __attribute__((packed));

struct BulkCsWrap {
  uint32_t signature;
  uint32_t tag;
  uint32_t data_residue;
  uint8_t status;
} __attribute__((packed));

struct FtCmdFormat {
  uint8_t opcode;
  uint32_t address;
  uint32_t length;
  uint32_t aux_address;
  uint8_t subcmd;
  uint8_t flags;
  uint8_t reserved;
} __attribute__((packed));

std::expected<BulkCbWrap, FtError> SendCbw(
    const UsbDevice& device, std::span<const uint8_t> scsi_cmd,
    size_t data_length, TransferDirection dir,
    std::chrono::milliseconds timeout_ms) {
  BulkCbWrap cbw{
      .signature = htole32(kUsbMsCbwSignature),
      .tag = htole32(device.GetNextCdbTag()),  // Using atomic thread-safe tag
      .data_transfer_length = htole32(data_length),
      .flags =
          (dir == TransferDirection::kIn) ? kBotDirectionIn : kBotDirectionOut,
      .lun = 0,
      .cmd_len = static_cast<uint8_t>(scsi_cmd.size()),
  };
  std::memcpy(cbw.cmd, scsi_cmd.data(), scsi_cmd.size());

  auto cbw_span = std::span<const uint8_t>(
      reinterpret_cast<const uint8_t*>(&cbw), sizeof(cbw));
  auto send_res = device.Send(cbw_span, timeout_ms);

  if (!send_res || *send_res != sizeof(cbw)) {
    LOGE("CBW transfer failed.");
    return std::unexpected(FtError::kResultFail);
  }
  return cbw;
}

std::expected<void, FtError> ReceiveAndValidateCsw(
    const UsbDevice& device, const BulkCbWrap& cbw,
    std::chrono::milliseconds timeout_ms) {
  BulkCsWrap csw{};
  auto csw_span =
      std::span<uint8_t>(reinterpret_cast<uint8_t*>(&csw), sizeof(csw));
  auto recv_res = device.Recv(csw_span, timeout_ms);

  if (!recv_res || *recv_res != sizeof(csw)) {
    LOGE("CSW transfer failed.");
    return std::unexpected(FtError::kResultFail);
  }

  if (le32toh(csw.signature) != kUsbMsCswSignature ||
      le32toh(csw.tag) != le32toh(cbw.tag)) {
    LOGE("CSW signature/tag mismatch.");
    return std::unexpected(FtError::kResultFail);
  }

  if (csw.status != kBotCommandPassed) {
    LOGW("Command failed with CSW status: {:02x}", csw.status);
    return std::unexpected(FtError::kResultFail);
  }
  return {};
}

}  // namespace

std::array<uint8_t, 16> BuildFtCmd(uint8_t opcode, uint8_t subcmd,
                                   uint32_t address, uint32_t length,
                                   uint32_t aux_address, uint8_t flags) {
  FtCmdFormat cmd{
      .opcode = opcode,
      .address = address,
      .length = length,
      .aux_address = aux_address,
      .subcmd = subcmd,
      .flags = flags,
      .reserved = 0,
  };
  return std::bit_cast<std::array<uint8_t, 16>>(cmd);
}

std::expected<void, FtError> SendBulkDataOut(
    const UsbDevice& device, std::span<const uint8_t> scsi_cmd,
    std::span<const uint8_t> data_buffer, TransferDirection dir,
    std::chrono::milliseconds timeout_ms) {
  auto cbw_res = SendCbw(device, scsi_cmd, data_buffer.size(), dir, timeout_ms);
  if (!cbw_res) return std::unexpected(cbw_res.error());

  if (dir == TransferDirection::kSendOnly) {
    if (auto res = device.ResetComplete(kResetWaitTimeout); !res) {
      return std::unexpected(FtError::kResultFail);
    }
    return {};
  }

  if (!data_buffer.empty()) {
    auto send_res = device.Send(data_buffer, timeout_ms);
    if (!send_res || *send_res != static_cast<int>(data_buffer.size())) {
      LOGE("Data OUT transfer failed.");
      return std::unexpected(FtError::kResultFail);
    }
  }

  return ReceiveAndValidateCsw(device, *cbw_res, timeout_ms);
}

std::expected<void, FtError> ReceiveBulkDataIn(
    const UsbDevice& device, std::span<const uint8_t> scsi_cmd,
    std::span<uint8_t> data_buffer, std::chrono::milliseconds timeout_ms) {
  auto cbw_res = SendCbw(device, scsi_cmd, data_buffer.size(),
                         TransferDirection::kIn, timeout_ms);
  if (!cbw_res) return std::unexpected(cbw_res.error());

  if (!data_buffer.empty()) {
    auto recv_res = device.Recv(data_buffer, timeout_ms);
    if (!recv_res) {
      LOGE("Data IN transfer failed.");
      return std::unexpected(recv_res.error());
    }
  }

  return ReceiveAndValidateCsw(device, *cbw_res, timeout_ms);
}
