/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ft_scsi.h"

#include <bit>
#include <utility>

#include "ft_log.h"
#include "ft_util.h"
#include "usb_device.h"

namespace focaltech {

namespace {

constexpr uint8_t kDefaultLun = 0;

using namespace std::chrono_literals;

std::expected<BulkCbw, Error> SendCbw(const UsbDevice& device,
                                      const CmdFormat& scsi_cmd,
                                      size_t data_length, TransferDirection dir,
                                      std::chrono::milliseconds timeout_ms) {
  const BulkCbw cbw{
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

  const auto cbw_span = AsUint8Span(cbw);
  const auto send_res = device.Send(cbw_span, timeout_ms);

  if (!send_res) {
    return std::unexpected(send_res.error());
  }
  if (*send_res != sizeof(cbw)) {
    FT_LOGE("CBW transfer failed: short write.");
    return std::unexpected(Error::kHardwareFailure);
  }
  return cbw;
}

std::expected<void, Error> ReceiveAndValidateCsw(
    const UsbDevice& device, const BulkCbw& cbw,
    std::chrono::milliseconds timeout_ms) {
  BulkCsw csw{};
  const auto csw_span = AsWritableUint8Span(csw);
  const auto recv_res = device.Recv(csw_span, timeout_ms);

  if (!recv_res || *recv_res != sizeof(csw)) {
    FT_LOGE("CSW transfer failed.");
    return std::unexpected(Error::kHardwareFailure);
  }

  if (FromLittleEndian(csw.signature) != kUsbMsCswSignature ||
      csw.tag != cbw.tag) {
    FT_LOGE("CSW signature/tag mismatch.");
    return std::unexpected(Error::kHardwareFailure);
  }

  if (csw.status != CswStatus::kCommandPassed) {
    FT_LOGW("Command failed with CSW status: {:02x}",
            std::to_underlying(csw.status));
    return std::unexpected(Error::kHardwareFailure);
  }

  return {};
}

}  // namespace

std::expected<void, Error> SendBulkDataOut(
    const UsbDevice& device, const CmdFormat& scsi_cmd,
    std::span<const uint8_t> data_buffer, TransferDirection dir,
    std::chrono::milliseconds timeout_ms) {
  const auto cbw_res =
      SendCbw(device, scsi_cmd, data_buffer.size(), dir, timeout_ms);
  if (!cbw_res) return std::unexpected(cbw_res.error());

  if (dir == TransferDirection::kSendOnly) {
    const auto res = device.WaitForReset(kDeviceRebootTimeout);
    if (!res) {
      return std::unexpected(Error::kHardwareFailure);
    }
    return {};
  }

  if (!data_buffer.empty()) {
    const auto send_res = device.Send(data_buffer, timeout_ms);
    if (!send_res) {
      return std::unexpected(send_res.error());
    }
    if (*send_res != data_buffer.size()) {
      FT_LOGE("Data OUT transfer failed: short write.");
      return std::unexpected(Error::kHardwareFailure);
    }
  }

  return ReceiveAndValidateCsw(device, *cbw_res, timeout_ms);
}

std::expected<void, Error> ReceiveBulkDataIn(
    const UsbDevice& device, const CmdFormat& scsi_cmd,
    std::span<uint8_t> data_buffer, std::chrono::milliseconds timeout_ms) {
  const auto cbw_res = SendCbw(device, scsi_cmd, data_buffer.size(),
                               TransferDirection::kIn, timeout_ms);
  if (!cbw_res) return std::unexpected(cbw_res.error());

  if (!data_buffer.empty()) {
    const auto recv_res = device.Recv(data_buffer, timeout_ms);
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
