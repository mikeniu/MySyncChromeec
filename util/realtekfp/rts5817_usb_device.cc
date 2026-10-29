/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "rts5817_usb_device.h"

#include <chrono>
#include <cstdio>
#include <expected>
#include <print>
#include <utility>

namespace realtekfp {

namespace {

RtsError ToRtsError(int usb_error) {
  switch (usb_error) {
    case kUsbErrorNoDevice:
    case kUsbErrorNotFound:
      return RtsError::kDeviceNotFound;
    case kUsbErrorAccess:
      return RtsError::kAccessDenied;
    default:
      return RtsError::kUsbError;
  }
}

// Maps a backend failure to an RtsError. Generic USB failures carry no
// detail in RtsError, so log the libusb error name for them.
std::unexpected<RtsError> Fail(const RtsUsbBackend& backend,
                               std::string_view what, int usb_error) {
  const RtsError error = ToRtsError(usb_error);

  if (error == RtsError::kUsbError) {
    std::println(stderr, "Error: {} failed: {}", what,
                 backend.ErrorName(usb_error));
  }
  return std::unexpected(error);
}

}  // namespace

RtsUsbDevice::RtsUsbDevice(std::unique_ptr<RtsUsbBackend> backend,
                           SleepFn sleep)
    : backend_(std::move(backend)), sleep_(std::move(sleep)) {}

RtsUsbDevice::~RtsUsbDevice() { Release(); }

std::expected<void, RtsError> RtsUsbDevice::Init(UsbDeviceId id) {
  Release();

  if (auto res = backend_->Init(); !res) {
    return Fail(*backend_, "libusb_init()", res.error());
  }

  if (auto res = backend_->Open(id); !res) {
    Release();
    return Fail(*backend_, "Opening the device", res.error());
  }

  std::println("Device {:04x}:{:04x} opened.", id.vid, id.pid);
  return {};
}

std::expected<void, RtsError> RtsUsbDevice::WaitForDevice(
    UsbDeviceId id, std::chrono::milliseconds timeout,
    std::chrono::milliseconds poll_interval) {
  if (timeout <= std::chrono::milliseconds::zero() ||
      poll_interval <= std::chrono::milliseconds::zero()) {
    return std::unexpected(RtsError::kInvalidParameter);
  }

  Release();

  if (auto res = backend_->Init(); !res) {
    return Fail(*backend_, "libusb_init()", res.error());
  }

  for (std::chrono::milliseconds waited{0}; waited < timeout;
       waited += poll_interval) {
    auto res = backend_->Open(id);

    if (res) {
      std::println("Device {:04x}:{:04x} opened.", id.vid, id.pid);
      return {};
    }
    if (ToRtsError(res.error()) != RtsError::kDeviceNotFound) {
      Release();
      return Fail(*backend_, "Opening the device", res.error());
    }
    sleep_(poll_interval);
  }

  Release();
  return std::unexpected(RtsError::kTimeout);
}

void RtsUsbDevice::Release() { backend_->Close(); }

}  // namespace realtekfp
