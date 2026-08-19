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
                                      size_t data_length,
                                      TransferDirection direction,
                                      std::chrono::milliseconds timeout) {
  const BulkCbw cbw{
      .signature = ToLittleEndian(kUsbMsCbwSignature),
      .tag = ToLittleEndian(device.GetNextCdbTag()),
      .data_transfer_length =
          ToLittleEndian(static_cast<uint32_t>(data_length)),
      .flags = (direction == TransferDirection::kIn) ? CbwDirection::kIn
                                                     : CbwDirection::kOut,
      .lun = kDefaultLun,
      .cmd_len = sizeof(CmdFormat),
      .cmd = scsi_cmd,
  };

  const auto cbw_span = AsUint8Span(cbw);
  const auto send_status = device.Send(cbw_span, timeout);

  if (!send_status) {
    return std::unexpected(send_status.error());
  }
  if (*send_status != sizeof(cbw)) {
    FT_LOGE("CBW transfer failed: short write.");
    return std::unexpected(Error::kHardwareFailure);
  }
  return cbw;
}

std::expected<void, Error> ReceiveAndValidateCsw(
    const UsbDevice& device, const BulkCbw& cbw,
    std::chrono::milliseconds timeout) {
  BulkCsw csw{};
  const auto csw_span = AsWritableUint8Span(csw);
  const auto receive_status = device.Recv(csw_span, timeout);

  if (!receive_status || *receive_status != sizeof(csw)) {
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

std::expected<void, Error> SendBulkDataOut(const UsbDevice& device,
                                           const CmdFormat& scsi_cmd,
                                           std::span<const uint8_t> data_buffer,
                                           TransferDirection direction,
                                           std::chrono::milliseconds timeout) {
  const auto cbw_result =
      SendCbw(device, scsi_cmd, data_buffer.size(), direction, timeout);
  if (!cbw_result) return std::unexpected(cbw_result.error());

  if (direction == TransferDirection::kSendOnly) {
    const auto reset_wait_status = device.WaitForReset(kDeviceRebootTimeout);
    if (!reset_wait_status) {
      return std::unexpected(Error::kHardwareFailure);
    }
    return {};
  }

  if (!data_buffer.empty()) {
    const auto send_status = device.Send(data_buffer, timeout);
    if (!send_status) {
      return std::unexpected(send_status.error());
    }
    if (*send_status != data_buffer.size()) {
      FT_LOGE("Data OUT transfer failed: short write.");
      return std::unexpected(Error::kHardwareFailure);
    }
  }

  const auto csw_validation =
      ReceiveAndValidateCsw(device, *cbw_result, timeout);
  if (!csw_validation) return std::unexpected(csw_validation.error());

  return {};
}

std::expected<size_t, Error> ReceiveBulkDataIn(
    const UsbDevice& device, const CmdFormat& scsi_cmd,
    std::span<uint8_t> data_buffer, std::chrono::milliseconds timeout) {
  const auto cbw_result = SendCbw(device, scsi_cmd, data_buffer.size(),
                                  TransferDirection::kIn, timeout);
  if (!cbw_result) return std::unexpected(cbw_result.error());

  size_t bytes_read = 0;
  if (!data_buffer.empty()) {
    const auto receive_status = device.Recv(data_buffer, timeout);
    if (!receive_status) {
      return std::unexpected(receive_status.error());
    }
    bytes_read = *receive_status;
  }

  const auto csw_validation =
      ReceiveAndValidateCsw(device, *cbw_result, timeout);
  if (!csw_validation) return std::unexpected(csw_validation.error());

  return bytes_read;
}

}  // namespace focaltech
