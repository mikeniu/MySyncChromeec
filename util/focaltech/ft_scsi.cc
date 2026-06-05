/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ft_scsi.h"

#include <bit>

#include "ft_log.h"
#include "ft_util.h"
#include "usb_device.h"

namespace focaltech {

namespace {

constexpr uint8_t kDefaultLun = 0;

using namespace std::chrono_literals;

std::expected<BulkCbWrap, Error> SendCbw(const UsbDevice& device,
                                         const CmdFormat& scsi_cmd,
                                         size_t data_length,
                                         TransferDirection dir,
                                         std::chrono::milliseconds timeout_ms) {
  BulkCbWrap cbw{
      .signature = ToLittleEndian(kUsbMsCbwSignature),
      .tag = ToLittleEndian(device.GetNextCdbTag()),
      .data_transfer_length =
          ToLittleEndian(static_cast<uint32_t>(data_length)),
      .flags = (dir == TransferDirection::kIn) ? CbwDirection::kIn
                                               : CbwDirection::kOut,
      .lun = kDefaultLun,
      .cmd_len = sizeof(CmdFormat),
      .cmd = scsi_cmd,
  };

  auto cbw_span = AsUint8Span(cbw);
  auto send_res = device.Send(cbw_span, timeout_ms);

  if (!send_res) {
    return std::unexpected(send_res.error());
  }
  if (*send_res != sizeof(cbw)) {
    FT_LOGE("CBW transfer failed: short write.");
    return std::unexpected(Error::kResultFail);
  }
  return cbw;
}

std::expected<void, Error> ReceiveAndValidateCsw(
    const UsbDevice& device, const BulkCbWrap& cbw,
    std::chrono::milliseconds timeout_ms) {
  BulkCsWrap csw{};
  auto csw_span = AsWritableUint8Span(csw);
  auto recv_res = device.Recv(csw_span, timeout_ms);

  if (!recv_res || *recv_res != sizeof(csw)) {
    FT_LOGE("CSW transfer failed.");
    return std::unexpected(Error::kResultFail);
  }

  if (FromLittleEndian(csw.signature) != kUsbMsCswSignature ||
      csw.tag != cbw.tag) {
    FT_LOGE("CSW signature/tag mismatch.");
    return std::unexpected(Error::kResultFail);
  }

  if (csw.status != CswStatus::kCommandPassed) {
    FT_LOGW("Command failed with CSW status: {:02x}",
            std::to_underlying(csw.status));
    return std::unexpected(Error::kResultFail);
  }

  return {};
}

}  // namespace

CmdFormat BuildFtCmd(Opcode opcode, Subcmd subcmd, uint32_t address,
                     uint32_t length, uint32_t aux_address, uint8_t flags) {
  return CmdFormat{
      .opcode = opcode,
      .address = ToLittleEndian(address),
      .length = ToLittleEndian(length),
      .aux_address = ToLittleEndian(aux_address),
      .subcmd = subcmd,
      .flags = flags,
      .reserved = 0,
  };
}

std::expected<void, Error> SendBulkDataOut(
    const UsbDevice& device, const CmdFormat& scsi_cmd,
    std::span<const uint8_t> data_buffer, TransferDirection dir,
    std::chrono::milliseconds timeout_ms) {
  auto cbw_res = SendCbw(device, scsi_cmd, data_buffer.size(), dir, timeout_ms);
  if (!cbw_res) return std::unexpected(cbw_res.error());

  if (dir == TransferDirection::kSendOnly) {
    if (auto res = device.WaitForReset(kDeviceRebootTimeout); !res) {
      return std::unexpected(Error::kResultFail);
    }
    return {};
  }

  if (!data_buffer.empty()) {
    auto send_res = device.Send(data_buffer, timeout_ms);
    if (!send_res) {
      return std::unexpected(send_res.error());
    }
    if (*send_res != data_buffer.size()) {
      FT_LOGE("Data OUT transfer failed: short write.");
      return std::unexpected(Error::kResultFail);
    }
  }

  return ReceiveAndValidateCsw(device, *cbw_res, timeout_ms);
}

std::expected<void, Error> ReceiveBulkDataIn(
    const UsbDevice& device, const CmdFormat& scsi_cmd,
    std::span<uint8_t> data_buffer, std::chrono::milliseconds timeout_ms) {
  auto cbw_res = SendCbw(device, scsi_cmd, data_buffer.size(),
                         TransferDirection::kIn, timeout_ms);
  if (!cbw_res) return std::unexpected(cbw_res.error());

  if (!data_buffer.empty()) {
    auto recv_res = device.Recv(data_buffer, timeout_ms);
    if (!recv_res) {
      return std::unexpected(recv_res.error());
    }

    if (*recv_res != data_buffer.size()) {
      FT_LOGW("Data IN transfer length mismatch (expected {}, got {})",
              data_buffer.size(), *recv_res);
    }
  }

  return ReceiveAndValidateCsw(device, *cbw_res, timeout_ms);
}

}  // namespace focaltech
