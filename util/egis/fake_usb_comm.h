/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef UTIL_EGIS_FAKE_USB_COMM_H_
#define UTIL_EGIS_FAKE_USB_COMM_H_

#include <algorithm>
#include <deque>
#include <optional>
#include <vector>

#include "usb_interface.h"

namespace egis {

// A fake implementation of the UsbInterface for testing. It allows queueing
// device responses and inspecting host transmissions without real hardware.
class FakeUsbComm : public UsbInterface {
 public:
  std::expected<void, UsbError> Connect() override {
    // For the fake, connect always succeeds.
    return {};
  }

 protected:
  std::expected<void, UsbError> DoSend(
      std::span<const uint8_t> data,
      std::chrono::milliseconds timeout) override {
    if (send_error_) {
      auto err = *send_error_;
      // Clear the error after it's been triggered.
      send_error_.reset();
      return std::unexpected(err);
    }
    host_transmissions_.emplace_back(data.begin(), data.end());
    return {};
  }

  std::expected<int, UsbError> DoReceive(
      std::span<uint8_t> rx_buffer,
      std::chrono::milliseconds timeout) override {
    // One-shot responses have priority over sticky responses.
    if (!device_responses_.empty()) {
      const auto& response = device_responses_.front();
      if (rx_buffer.size() < response.size()) {
        return std::unexpected(UsbError::kOverflow);
      }

      std::ranges::copy(response, rx_buffer.begin());
      int response_size = static_cast<int>(response.size());
      device_responses_.pop_front();

      return response_size;
    }

    if (sticky_response_) {
      const auto& response = *sticky_response_;
      if (rx_buffer.size() < response.size()) {
        return std::unexpected(UsbError::kOverflow);
      }
      std::ranges::copy(response, rx_buffer.begin());
      return static_cast<int>(response.size());
    }

    return std::unexpected(UsbError::kTimeout);
  }

 public:
  void PushDeviceResponse(std::vector<uint8_t> response) {
    device_responses_.push_back(std::move(response));
  }

  void SetSendError(UsbError error) { send_error_ = error; }

  void SetStickyDeviceResponse(std::vector<uint8_t> response) {
    sticky_response_ = std::move(response);
  }

  void Reset() {
    device_responses_.clear();
    sticky_response_.reset();
    send_error_.reset();
  }

  const std::vector<std::vector<uint8_t>>& GetHostTransmissions() const {
    return host_transmissions_;
  }

 private:
  std::deque<std::vector<uint8_t>> device_responses_;
  std::vector<std::vector<uint8_t>> host_transmissions_;
  std::optional<UsbError> send_error_;
  std::optional<std::vector<uint8_t>> sticky_response_;
};

}  // namespace egis

#endif  // UTIL_EGIS_FAKE_USB_COMM_H_
