/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

// RtsUsbDevice: RTS5817 (Realtek USB DFU device) access for the flashing
// tool. All hardware access goes through RtsUsbBackend.

#ifndef PLATFORM_EC_UTIL_REALTEKFP_RTS5817_USB_DEVICE_H_
#define PLATFORM_EC_UTIL_REALTEKFP_RTS5817_USB_DEVICE_H_

#include <chrono>
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
  }
  return "Unknown error";
}

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

 private:
  std::unique_ptr<RtsUsbBackend> backend_;
  SleepFn sleep_;
};

}  // namespace realtekfp

#endif  // PLATFORM_EC_UTIL_REALTEKFP_RTS5817_USB_DEVICE_H_
