/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "rts5817_usb_device.h"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstdio>
#include <expected>
#include <print>
#include <span>
#include <utility>
#include <vector>

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

void RtsUsbDevice::Release() {
  dfu_interface_ = -1;
  transfer_size_ = kDfuDefaultTransferSize;
  backend_->Close();
}

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

std::expected<void, RtsError> RtsUsbDevice::DfuDetach(
    std::chrono::milliseconds detach_timeout) {
  // wValue is 16 bits wide.
  if (detach_timeout < std::chrono::milliseconds::zero() ||
      detach_timeout > std::chrono::milliseconds(UINT16_MAX)) {
    return std::unexpected(RtsError::kInvalidParameter);
  }

  // DFU_DETACH tells the device to switch from Runtime mode to DFU mode. The
  // device responds by re-enumerating on the USB bus.

  // Find the DFU interface first.
  auto info = FindDfuInterface();
  if (!info) {
    return std::unexpected(info.error());
  }
  dfu_interface_ = info->number;
  transfer_size_ = info->transfer_size;

  // An interface-recipient request needs the interface claimed (which also
  // detaches any kernel driver); otherwise usbfs warns and may return EBUSY.
  InterfaceClaim claim(*backend_, dfu_interface_);
  if (auto claimed = claim.Claim(); !claimed) {
    return Fail(*backend_, "Claiming the DFU interface", claimed.error());
  }

  // bmRequestType = 0x21: Host-to-Device, Class, Interface
  // bRequest = kDfuDetach (0x00)
  // wValue = detach_timeout in ms
  // wIndex = interface number
  auto res = backend_->ControlTransferOut(
      kBmReqH2dClsIf, kDfuDetach, static_cast<uint16_t>(detach_timeout.count()),
      static_cast<uint16_t>(dfu_interface_), {}, kUsbCtrlTimeout);

  if (!res) {
    return Fail(*backend_, "DFU_DETACH", res.error());
  }

  std::println("DFU_DETACH sent.  Waiting for re-enumeration...");
  return {};
}

uint16_t ParseDfuTransferSize(std::span<const uint8_t> extra) {
  // Walk the descriptor stream. The DFU functional descriptor has type 0x21
  // and contains wTransferSize at byte offset 5-6.
  while (extra.size() > kDfuDescTypeOff) {
    uint8_t desc_len = extra[0];

    if (desc_len == 0 || desc_len > extra.size()) {
      break;
    }

    if (extra[kDfuDescTypeOff] == kDfuDescriptorType &&
        desc_len >= kDfuDescMinLen) {
      uint16_t size =
          extra[kDfuDescXferSizeOff] | (extra[kDfuDescXferSizeOff + 1] << 8);

      return size != 0 ? size : kDfuDefaultTransferSize;
    }
    extra = extra.subspan(desc_len);
  }

  return kDfuDefaultTransferSize;
}

std::expected<DfuInterfaceInfo, RtsError> RtsUsbDevice::FindDfuInterface() {
  if (!backend_->IsOpen()) {
    return std::unexpected(RtsError::kDeviceNotFound);
  }

  auto interfaces = backend_->GetInterfaces();

  if (!interfaces) {
    return Fail(*backend_, "Reading the config descriptor", interfaces.error());
  }

  // Walk all interfaces and alternate settings.
  const auto it =
      std::ranges::find_if(*interfaces, [](const RtsUsbInterface& alt) {
        return alt.usb_class == kDfuClass && alt.usb_subclass == kDfuSubclass;
      });

  if (it == interfaces->end()) {
    std::println(stderr, "Error: no DFU interface found on device.");
    return std::unexpected(RtsError::kNoDfuInterface);
  }

  const DfuInterfaceInfo info = {
      .number = it->number,
      .transfer_size = ParseDfuTransferSize(it->extra),
  };

  std::println("DFU transfer size: {} bytes", info.transfer_size);
  std::println("DFU interface found: #{}", info.number);
  return info;
}

}  // namespace realtekfp
