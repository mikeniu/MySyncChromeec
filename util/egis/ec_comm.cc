/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "ec_comm.h"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <print>
#include <string>
#include <vector>

#include "ec_protocol_defs.h"
#include "egis_util.h"
#include "usb_comm.h"

namespace egis {

namespace {
bool VerifyResponseChecksum(std::span<const uint8_t> response) {
  uint8_t sum = 0;
  for (uint8_t byte : response) sum += byte;
  return sum == 0;
}
}  // namespace

class EcCommand {
 public:
  EcCommand(uint16_t cmd_id, uint8_t cmd_ver,
            std::span<const uint8_t> payload = {}) {
    if (payload.size() > std::numeric_limits<uint16_t>::max()) {
      // This is a programming error, the payload is too large for the protocol.
      std::println(stderr,
                   "[FATAL] EC command payload exceeds protocol limit.");
      std::abort();
    }
    EcHostCommandRequest hdr = {
        .command_id = ToLittleEndian(cmd_id),
        .command_version = cmd_ver,
        .data_length = ToLittleEndian(static_cast<uint16_t>(payload.size()))};

    cmd_bytes_ = PackMessage(hdr, payload);

    ApplyEcChecksum(cmd_bytes_);
  }

  const std::vector<uint8_t> &bytes() const { return cmd_bytes_; }

 private:
  std::vector<uint8_t> cmd_bytes_;
};

EcComm::EcComm(UsbInterface &usb) : usb_(usb) {}

std::expected<std::vector<uint8_t>, EcError> EcComm::SendCommand(
    uint16_t cmd_id, uint8_t cmd_ver, std::span<const uint8_t> payload) {
  EcCommand cmd(cmd_id, cmd_ver, payload);
  const auto &tx_bytes = cmd.bytes();
  if (auto res = usb_.Send(tx_bytes); !res) {
    std::println(stderr, "[ERROR] Failed to send EC command: {}",
                 ToString(res.error()));
    return std::unexpected(EcError::kUsbSendFailed);
  }

  std::vector<uint8_t> resp_buf(kMaxResponseSize);
  auto actual_res = usb_.Receive(resp_buf);
  if (!actual_res) {
    std::println(stderr, "[ERROR] Failed to receive EC response: {}",
                 ToString(actual_res.error()));
    return std::unexpected(EcError::kUsbReceiveFailed);
  }
  size_t rx_size = actual_res.value();
  if (rx_size < sizeof(EcHostCommandResponse)) {
    std::println(stderr, "[ERROR] EC response was too short to be valid.");
    return std::unexpected(EcError::kResponseTooShort);
  }

  auto resp_hdr = ReadStruct<EcHostCommandResponse>(resp_buf).value();
  uint16_t data_length = FromLittleEndian(resp_hdr.data_length);
  size_t exact_packet_size = sizeof(EcHostCommandResponse) + data_length;

  if (rx_size < exact_packet_size) {
    std::println(stderr,
                 "[ERROR] EC response data truncated. Claimed: {}, Actual: {}",
                 data_length, rx_size - sizeof(EcHostCommandResponse));
    return std::unexpected(EcError::kResponseTooShort);
  }

  resp_buf.resize(exact_packet_size);

  if (!VerifyResponseChecksum(resp_buf)) {
    std::println(stderr, "[ERROR] EC response checksum verification failed.");
    return std::unexpected(EcError::kInvalidChecksum);
  }

  if (resp_hdr.protocol_version != kEcProtocolVersion3) {
    std::println(stderr, "[ERROR] EC response has invalid protocol version: {}",
                 resp_hdr.protocol_version);
    return std::unexpected(EcError::kInvalidChecksum);
  }

  uint16_t result = FromLittleEndian(resp_hdr.result);

  if (result != 0) {
    std::println(stderr, "[ERROR] EC device returned error code: {}", result);
    return std::unexpected(EcError::kDeviceError);
  }

  return std::vector<uint8_t>(resp_buf.begin() + sizeof(EcHostCommandResponse),
                              resp_buf.end());
}

}  // namespace egis
