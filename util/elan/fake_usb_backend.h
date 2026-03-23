// Copyright 2026 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UTIL_ELAN_FAKE_USB_BACKEND_H_
#define UTIL_ELAN_FAKE_USB_BACKEND_H_

#include <algorithm>
#include <cstring>
#include <vector>

#include "iap_control.h"
#include "usb_backend.h"
#include "utility.h"

namespace elan {

class FakeUsbBackend : public UsbBackend {
 public:
  int Initialize() override { return 0; }
  int OpenDevice(uint16_t vid, uint16_t pid) override { return 0; }
  void Release() override {}

  int InterruptTransfer(uint8_t endpoint, std::span<uint8_t> data,
                        std::chrono::milliseconds timeout) override {
    switch (endpoint) {
      case IapControl::kEpCmdOut:
        if (!data.empty()) last_cmd_ = data[0];
        return 0;

      case IapControl::kEpStatusIn: {
        IapStatusPacket status = {};
        status.cmd_echo = last_cmd_;
        status.status_code = IapControl::kErrStaSuccess;

        if (last_cmd_ == IapControl::kCmdGetFwVersion) {
          uint32_t fw_ver = (IapControl::kGoogleFwVerPrefix << 8);
          status.parameter_1 = elan::ToLittleEndian(fw_ver);
        }

        std::ranges::copy(status.AsByteSpan(), data.begin());
        return 0;
      }

      case IapControl::kEpDataOut:
        device_flash_.insert(device_flash_.end(), data.begin(), data.end());
        return 0;

      case IapControl::kEpDataIn:
        if (read_offset_ < device_flash_.size()) {
          size_t chunk =
              std::min(data.size(), device_flash_.size() - read_offset_);
          std::ranges::copy_n(device_flash_.begin() + read_offset_, chunk,
                              data.begin());
          read_offset_ += chunk;
        }
        return 0;
    }
    return -1;  // LIBUSB_ERROR_IO
  }

  const std::vector<uint8_t>& GetFlashedData() const { return device_flash_; }

 private:
  uint8_t last_cmd_ = 0;
  size_t read_offset_ = 0;
  std::vector<uint8_t> device_flash_;
};

}  // namespace elan
#endif
