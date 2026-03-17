/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "ec_comm.h"

// C system files
#include <cstdlib>
#include <cstring>

// C++ system files
#include <algorithm>
#include <bit>
#include <cstddef>
#include <limits>
#include <print>
#include <string>
#include <vector>

// Project headers
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

    // 1. Pack the header and payload instantly without memcpy
    cmd_bytes_ = PackMessage(hdr, payload);

    // 2. Apply the checksum
    UpdateChecksum();
  }

  const std::vector<uint8_t> &bytes() const { return cmd_bytes_; }

 private:
  std::vector<uint8_t> cmd_bytes_;

  void UpdateChecksum() {
    uint32_t sum = 0;
    for (uint8_t byte : cmd_bytes_) sum += byte;

    // Safely map the header back over the vector rather than using offsetof
    auto *hdr = reinterpret_cast<EcHostCommandRequest *>(cmd_bytes_.data());
    hdr->checksum = static_cast<uint8_t>(-sum);
  }
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
  if (actual_res.value() < static_cast<int>(sizeof(EcHostCommandResponse))) {
    std::println(stderr, "[ERROR] EC response was too short to be valid.");
    return std::unexpected(EcError::kResponseTooShort);
  }
  resp_buf.resize(actual_res.value());

  if (!VerifyResponseChecksum(resp_buf)) {
    std::println(stderr, "[ERROR] EC response checksum verification failed.");
    return std::unexpected(EcError::kInvalidChecksum);
  }

  auto hdr_res = ReadStruct<EcHostCommandResponse>(resp_buf);
  if (!hdr_res) {
    return std::unexpected(EcError::kResponseTooShort);
  }
  EcHostCommandResponse resp_hdr = hdr_res.value();

  uint16_t result = FromLittleEndian(resp_hdr.result);
  uint16_t data_length = FromLittleEndian(resp_hdr.data_length);

  if (result != 0) {
    std::println(stderr, "[ERROR] EC device returned error code: {}", result);
    return std::unexpected(EcError::kDeviceError);
  }

  std::vector<uint8_t> resp_out;
  if (data_length > 0) {
    size_t header_len = sizeof(EcHostCommandResponse);
    if (header_len + data_length > resp_buf.size()) {
      std::println(
          stderr, "[ERROR] EC response data truncated. Claimed: {}, Actual: {}",
          data_length, resp_buf.size() - header_len);
      return std::unexpected(EcError::kResponseTooShort);
    }
    resp_out.assign(resp_buf.begin() + header_len,
                    resp_buf.begin() + header_len + data_length);
  }

  return resp_out;
}

}  // namespace egis
