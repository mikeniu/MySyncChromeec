/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

// RtsUsbDevice: RTS5817 (Realtek USB DFU device) access for the flashing
// tool. All hardware access goes through RtsUsbBackend.

#ifndef PLATFORM_EC_UTIL_REALTEKFP_RTS5817_USB_DEVICE_H_
#define PLATFORM_EC_UTIL_REALTEKFP_RTS5817_USB_DEVICE_H_

#include <chrono>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <string_view>
#include <thread>

#include "rts5817_usb_backend.h"

namespace realtekfp {

enum class RtsError {
  kDeviceNotFound,
  kAccessDenied,
  kUsbError,
  kInvalidParameter,
  kTimeout,
  kStall,
  kProtocolError,
};

inline std::string_view ToString(RtsError error) {
  switch (error) {
    case RtsError::kDeviceNotFound:
      return "Device not found";
    case RtsError::kAccessDenied:
      return "Permission denied accessing the USB device";
    case RtsError::kUsbError:
      return "USB error";
    case RtsError::kInvalidParameter:
      return "Invalid parameter";
    case RtsError::kTimeout:
      return "Timed out waiting for the device to re-enumerate";
    case RtsError::kStall:
      return "The device stalled the request";
    case RtsError::kProtocolError:
      return "Unexpected response from the device";
  }
  return "Unknown error";
}

// RTS5817 vendor-specific USB control requests (same as rtsflash.py).
//
// XMEM_READ reads 4 bytes (little endian) of device memory; the address is
// split into wValue (low 16 bits) and wIndex (high 16 bits).
inline constexpr uint8_t kRtsVreqXmemRead = 0x41;
// The initial vector table address tells which image is running.
inline constexpr uint32_t kRtsInitVtorAddr = 0x401E2090;
inline constexpr uint32_t kRtsInitVtorBootRom = 0x00000000;
inline constexpr uint32_t kRtsInitVtorFlashImage = 0x01800000;
// GET_STATUS / SET_STATUS only exist in the Boot ROM.
inline constexpr uint8_t kRtsStdGetStatus = 0x1A;
inline constexpr uint8_t kRtsStdSetStatus = 0x1B;
// wValue for kRtsStdSetStatus: enable download mode.
inline constexpr uint16_t kRtsEnableDownloadMode = 1;

// Pre-computed bmRequestType values (raw uint8_t values per USB spec). Avoids
// -Wdeprecated-enum-enum-conversion when OR'ing different libusb enum types.
//   0x80 = Device-to-host, Standard, Device
//   0x00 = Host-to-device, Standard, Device
//   0xC0 = Device-to-host, Vendor, Device
inline constexpr uint8_t kBmReqD2hStdDev = 0x80;
inline constexpr uint8_t kBmReqH2dStdDev = 0x00;
inline constexpr uint8_t kBmReqD2hVendorDev = 0xC0;

// USB I/O timeouts.
inline constexpr std::chrono::milliseconds kUsbCtrlTimeout{1000};

class RtsUsbDevice {
 public:
  using SleepFn = std::function<void(std::chrono::milliseconds)>;

  // `backend` is the USB transport; ownership is taken. `sleep` can be
  // replaced by tests.
  explicit RtsUsbDevice(
      std::unique_ptr<RtsUsbBackend> backend,
      SleepFn sleep = [](std::chrono::milliseconds delay) {
        std::this_thread::sleep_for(delay);
      });
  RtsUsbDevice(const RtsUsbDevice&) = delete;
  RtsUsbDevice& operator=(const RtsUsbDevice&) = delete;
  ~RtsUsbDevice();

  // Opens the device.
  std::expected<void, RtsError> Init(UsbDeviceId id);

  // Polls every `poll_interval`, up to `timeout`, for the device to
  // (re-)appear on the USB bus, e.g. after it re-enumerates. Retries only
  // while the device is absent; any other failure is returned immediately.
  std::expected<void, RtsError> WaitForDevice(
      UsbDeviceId id, std::chrono::milliseconds timeout,
      std::chrono::milliseconds poll_interval);

  // Releases USB resources.
  void Release();

  // Enables DFU download mode via vendor control request. Only the Boot ROM
  // implements the request; a running Flash Image (or a device that stalls the
  // mode query) is left untouched so the caller can go straight to
  // DFU_DETACH.
  std::expected<void, RtsError> EnableDownload();

 private:
  std::unique_ptr<RtsUsbBackend> backend_;
  SleepFn sleep_;
};

}  // namespace realtekfp

#endif  // PLATFORM_EC_UTIL_REALTEKFP_RTS5817_USB_DEVICE_H_
