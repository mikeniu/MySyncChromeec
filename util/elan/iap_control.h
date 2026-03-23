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

#include "usb_backend.h"
#include "utility.h"

// Unified 8-byte packet for Endpoint 1 (Send)
struct IapCommandPacket {
  uint8_t cmd_code;        // Command ID
  uint8_t reserved;        // Reserved
  uint8_t parameter_2[2];  // Parameter 2: Return value encoded in 2 bytes.

  union {
    uint32_t payload_le;  // Parameter 1: Return value encoded in 4 bytes.
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
  uint8_t cmd_echo;        // Command ID
  uint8_t status_code;     // Status
  uint8_t parameter_2[2];  // Parameter 2: Return value encoded in 2 bytes.
  uint32_t parameter_1;    // Parameter 1: Return value encoded in 4 bytes.

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
  explicit IapControl(std::unique_ptr<UsbBackend> usb_backend);
  ~IapControl() = default;

  [[nodiscard]] std::expected<void, IapError> Initialize(uint16_t vid,
                                                         uint16_t pid);

  std::expected<void, IapError> RunIapProcess(std::span<const uint8_t> rom);

  std::expected<void, IapError> SetProgressCallback(
      std::function<void(size_t, size_t)> callback);

  // --- USB Endpoints & Lengths ---
  static constexpr uint8_t kEpCmdOut = 0x01;
  static constexpr uint8_t kEpStatusIn = 0x82;
  static constexpr uint8_t kEpDataOut = 0x03;
  static constexpr uint8_t kEpDataIn = 0x84;
  static constexpr size_t kEpDataLength = 64;

  // --- Command Codes ---
  static constexpr uint8_t kCmdRunIap = 0x20;
  static constexpr uint8_t kCmdFinishedIap = 0x21;
  static constexpr uint8_t kCmdSoftwareReset = 0x22;
  static constexpr uint8_t kCmdGetFwVersion = 0x41;
  static constexpr uint8_t kCmdAbortProcess = 0x48;
  static constexpr uint8_t kCmdWriteRom = 0xA0;
  static constexpr uint8_t kCmdWriteRomFinish = 0xA1;
  static constexpr uint8_t kCmdSetRomAddress = 0xA4;
  static constexpr uint8_t kCmdReadRom = 0xE0;
  static constexpr uint8_t kCmdReadRomFinish = 0xE1;

  // --- Status & Magic Values ---
  static constexpr uint8_t kErrStaReady = 0x00;
  static constexpr uint8_t kErrStaSuccess = 0x01;
  static constexpr uint8_t kGoogleFwVerPrefix = 0x10;
  static constexpr uint32_t kBaseRomAddress = 0x0000;

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
  std::expected<uint32_t, IapError> GetChecksum(std::span<const uint8_t> buff);

  std::function<void(size_t, size_t)> progress_callback_;
  std::unique_ptr<UsbBackend> usb_;
};
}  // namespace elan

#endif
