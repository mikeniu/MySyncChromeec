// Copyright 2026 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UTIL_ELAN_IAPCONTROL_H_
#define UTIL_ELAN_IAPCONTROL_H_

#include <libusb.h>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "utility.h"

struct FwVerInfo {
  uint32_t fw_ver;
  uint8_t boot_type;
};

namespace elan {
constexpr uint16_t kBootVid = 0x04F3;
constexpr uint16_t kBootPid = 0x0910;

class IapControl {
  struct LibusbDeviceListDeleter {
    void operator()(libusb_device **list) const {
      if (list) libusb_free_device_list(list, 1);
    }
  };

  using LibusbDeviceListPtr =
      std::unique_ptr<libusb_device *, LibusbDeviceListDeleter>;

  struct LibusbDeviceHandleDeleter {
    void operator()(libusb_device_handle *handle) const {
      if (handle) {
        libusb_close(handle);
      }
    }
  };

  using LibusbDeviceHandlePtr =
      std::unique_ptr<libusb_device_handle, LibusbDeviceHandleDeleter>;

 public:
  IapControl();
  ~IapControl();

  [[nodiscard]] std::expected<void, IapError> Initialize(uint16_t vid,
                                                         uint16_t pid);

  std::expected<void, IapError> RunIapProcess(std::span<const uint8_t> rom);

  std::expected<void, IapError> SetProgressCallback(
      std::function<void(size_t, size_t)> callback);

 private:
  std::expected<void, IapError> RunIap(uint8_t iap_mode);
  std::expected<void, IapError> FinishIap(uint32_t checksum);
  std::expected<void, IapError> SoftwareReset();
  std::expected<FwVerInfo, IapError> GetFwVersion();
  std::expected<void, IapError> AbortProcess();
  std::expected<void, IapError> WriteRom(uint32_t length);
  std::expected<void, IapError> WriteRomFinish(uint32_t checksum);
  std::expected<void, IapError> SetRomAddress(uint32_t address);
  std::expected<void, IapError> ReadRom(uint32_t length);
  std::expected<void, IapError> ReadRomFinish(uint32_t checksum);
  std::expected<void, IapError> WriteRomData(std::span<const uint8_t> buff);
  std::expected<void, IapError> ReadRomData(std::span<uint8_t> buff);

  std::expected<void, IapError> OpenDevice(uint16_t vid, uint16_t pid);
  std::expected<void, IapError> IapCommand(std::span<const uint8_t> buff);
  std::expected<void, IapError> IapSendCommand(std::span<const uint8_t> buff);
  std::expected<void, IapError> IapRecvStatus(std::span<uint8_t> buff);
  std::expected<void, IapError> IapSendData(std::span<const uint8_t> buff);
  std::expected<void, IapError> IapRecvData(std::span<uint8_t> buff);
  std::expected<void, IapError> Set32Le(std::span<uint8_t> buff,
                                        uint32_t value);
  std::expected<uint32_t, IapError> Get32Le(std::span<const uint8_t> buff);
  std::expected<uint32_t, IapError> GetChecksum(std::span<const uint8_t> buff);
  std::expected<void, IapError> ReleaseResource();

  std::function<void(size_t, size_t)> progress_callback_;
  std::vector<uint8_t> hid_interfaces_;
  std::vector<uint8_t> hid_detach_interfaces_;
  LibusbDeviceHandlePtr dev_handle_ptr_;
  bool libusb_initialized_;
};
}  // namespace elan

#endif
