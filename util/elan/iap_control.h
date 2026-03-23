// Copyright 2026 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UTIL_ELAN_IAP_CONTROL_H_
#define UTIL_ELAN_IAP_CONTROL_H_

#include <libusb-1.0/libusb.h>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <source_location>
#include <span>
#include <string>
#include <vector>

#include "utility.h"

// Unified 8-byte packet for Endpoint 1 (Send)
struct IapCommandPacket {
  uint8_t cmd_code;
  uint8_t reserved[3];

  union {
    uint32_t payload_le;
    uint8_t iap_mode;
  };

  std::span<const uint8_t> AsByteSpan() const {
    return {reinterpret_cast<const uint8_t *>(this), sizeof(*this)};
  }
} __attribute__((packed));

static_assert(sizeof(IapCommandPacket) == 8,
              "IapCommandPacket must be exactly 8 bytes");

// Unified 8-byte packet for Endpoint 2 (Receive)
struct IapStatusPacket {
  uint8_t cmd_echo;
  uint8_t status_code;
  uint8_t boot_type;
  uint8_t reserved;
  uint32_t return_value;

  std::span<uint8_t> AsByteSpan() {
    return {reinterpret_cast<uint8_t *>(this), sizeof(*this)};
  }
} __attribute__((packed));

static_assert(sizeof(IapStatusPacket) == 8,
              "IapStatusPacket must be exactly 8 bytes");

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
  std::expected<void, IapError> SendCmdOnly(uint8_t cmd_code);
  std::expected<void, IapError> SendCmdWithU32Payload(uint8_t cmd_code,
                                                      uint32_t payload_val);
  std::expected<void, IapError> IapCommand(const IapCommandPacket &packet);
  std::expected<void, IapError> IapSendCommand(std::span<const uint8_t> buff);
  std::expected<void, IapError> IapRecvStatus(std::span<uint8_t> buff);
  std::expected<void, IapError> IapSendData(std::span<const uint8_t> buff);
  std::expected<void, IapError> IapRecvData(std::span<uint8_t> buff);
  std::expected<void, IapError> DoUsbTransfer(
      uint8_t endpoint, std::span<uint8_t> buff,
      std::source_location location = std::source_location::current());
  std::expected<void, IapError> DoUsbTransfer(
      uint8_t endpoint, std::span<const uint8_t> buff,
      std::source_location location = std::source_location::current());
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
