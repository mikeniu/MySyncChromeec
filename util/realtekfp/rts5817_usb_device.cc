/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "rts5817_usb_device.h"

#include <array>
#include <bit>
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
    case kUsbErrorPipe:
      return RtsError::kStall;
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

std::expected<void, RtsError> RtsUsbDevice::EnableDownload() {
  if (!backend_->IsOpen()) {
    return std::unexpected(RtsError::kDeviceNotFound);
  }

  // Only the Boot ROM implements GET_STATUS / SET_STATUS. A Flash Image
  // stalls them, so check which image is running first.
  // bmRequestType = 0xC0: Device-to-host, Vendor, Device
  std::array<uint8_t, sizeof(uint32_t)> init_vector{};
  auto mode = backend_->ControlTransferIn(
      kBmReqD2hVendorDev, kRtsVreqXmemRead,
      static_cast<uint16_t>(kRtsInitVtorAddr & 0xFFFF),
      static_cast<uint16_t>(kRtsInitVtorAddr >> 16), init_vector,
      kUsbCtrlTimeout);

  if (!mode) {
    if (mode.error() == kUsbErrorPipe) {
      std::println("Mode query stalled; leaving download mode untouched.");
      return {};
    }
    return Fail(*backend_, "Reading R_INITVTOR", mode.error());
  }
  if (*mode < init_vector.size()) {
    std::println(stderr, "Error: Reading R_INITVTOR failed: short read");
    return std::unexpected(RtsError::kProtocolError);
  }

  const uint32_t vector = std::bit_cast<uint32_t>(init_vector);
  if (vector != kRtsInitVtorBootRom) {
    std::println("Not running from Boot ROM (R_INITVTOR = {:#010x}).", vector);
    return {};
  }

  // bmRequestType = 0x80: Device-to-host, Standard, Device
  // bRequest = kRtsStdGetStatus (0x1A), wValue = 0, wIndex = 0
  std::array<uint8_t, 1> status{};
  auto got = backend_->ControlTransferIn(kBmReqD2hStdDev, kRtsStdGetStatus, 0,
                                         0, status, kUsbCtrlTimeout);

  if (!got) {
    return Fail(*backend_, "GET_STATUS", got.error());
  }
  if (*got < status.size()) {
    std::println(stderr, "Error: GET_STATUS failed: short read");
    return std::unexpected(RtsError::kProtocolError);
  }

  if (status[0] != 0) {
    std::println("Download mode already active (status = {}).", status[0]);
    return {};
  }

  // bmRequestType = 0x00: Host-to-device, Standard, Device
  // bRequest = kRtsStdSetStatus (0x1B), wValue = kRtsEnableDownloadMode,
  // wIndex = 0
  auto set = backend_->ControlTransferOut(kBmReqH2dStdDev, kRtsStdSetStatus,
                                          kRtsEnableDownloadMode, 0, {},
                                          kUsbCtrlTimeout);

  if (!set) {
    return Fail(*backend_, "SET_STATUS", set.error());
  }
  std::println("Download mode enabled on device.");
  return {};
}

}  // namespace realtekfp
