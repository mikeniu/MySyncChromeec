/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "rts5817_libusb_backend.h"

#include <cstdint>
#include <memory>
#include <span>

namespace realtekfp {

// The error codes of the backend interface are libusb's.
static_assert(kUsbErrorAccess == LIBUSB_ERROR_ACCESS);
static_assert(kUsbErrorNoDevice == LIBUSB_ERROR_NO_DEVICE);
static_assert(kUsbErrorNotFound == LIBUSB_ERROR_NOT_FOUND);
static_assert(kUsbErrorPipe == LIBUSB_ERROR_PIPE);

namespace {

// Frees a device list and drops the references libusb took on its devices.
void FreeDeviceList(libusb_device** list) {
  libusb_free_device_list(list, /*unref_devices=*/1);
}

using DeviceList = std::unique_ptr<libusb_device*, CDeleter<FreeDeviceList>>;

// Runs a libusb control transfer on `buf`, which libusb takes as mutable even
// for host-to-device requests. `length` is bounded by the 16-bit wLength.
std::expected<size_t, int> DoControlTransfer(
    libusb_device_handle* handle, uint8_t request_type, uint8_t request,
    uint16_t value, uint16_t index, uint8_t* buf, size_t length,
    std::chrono::milliseconds timeout) {
  if (!handle) {
    return std::unexpected(LIBUSB_ERROR_NO_DEVICE);
  }
  if (length > UINT16_MAX) {
    return std::unexpected(LIBUSB_ERROR_INVALID_PARAM);
  }

  int ret = libusb_control_transfer(handle, request_type, request, value, index,
                                    buf, static_cast<uint16_t>(length),
                                    static_cast<unsigned int>(timeout.count()));
  if (ret < 0) {
    return std::unexpected(ret);
  }
  return static_cast<size_t>(ret);
}

}  // namespace

std::expected<void, int> RtsLibusbBackend::Init() {
  Close();

  int ret = libusb_init(std::out_ptr(ctx_));
  if (ret < 0) {
    return std::unexpected(ret);
  }
  return {};
}

std::expected<void, int> RtsLibusbBackend::Open(UsbDeviceId id) {
  if (!ctx_) {
    return std::unexpected(LIBUSB_ERROR_NO_DEVICE);
  }
  dev_handle_.reset();

  // libusb_open_device_with_vid_pid() returns nullptr both when the device is
  // absent and when it exists but cannot be opened (for instance for lack of
  // permission). Enumerate instead, so that the two can be told apart.
  DeviceList list;
  const ssize_t count = libusb_get_device_list(ctx_.get(), std::out_ptr(list));
  if (count < 0) {
    return std::unexpected(static_cast<int>(count));
  }

  for (libusb_device* device :
       std::span(list.get(), static_cast<size_t>(count))) {
    libusb_device_descriptor desc;

    if (libusb_get_device_descriptor(device, &desc) < 0 ||
        desc.idVendor != id.vid || desc.idProduct != id.pid) {
      continue;
    }

    int ret = libusb_open(device, std::out_ptr(dev_handle_));
    if (ret < 0) {
      return std::unexpected(ret);
    }

    // Let claim/release detach and re-attach a kernel driver automatically. Not
    // every platform supports this; there a bound driver makes the claim fail
    // with LIBUSB_ERROR_BUSY, which the caller reports.
    ret = libusb_set_auto_detach_kernel_driver(dev_handle_.get(), 1);
    if (ret < 0 && ret != LIBUSB_ERROR_NOT_SUPPORTED) {
      dev_handle_.reset();
      return std::unexpected(ret);
    }
    return {};
  }
  return std::unexpected(LIBUSB_ERROR_NOT_FOUND);
}

void RtsLibusbBackend::Close() {
  // The handle goes before the context it belongs to.
  dev_handle_.reset();
  ctx_.reset();
}

bool RtsLibusbBackend::IsOpen() const { return dev_handle_ != nullptr; }

std::expected<size_t, int> RtsLibusbBackend::ControlTransferIn(
    uint8_t request_type, uint8_t request, uint16_t value, uint16_t index,
    std::span<uint8_t> data, std::chrono::milliseconds timeout) {
  return DoControlTransfer(dev_handle_.get(), request_type, request, value,
                           index, data.data(), data.size(), timeout);
}

std::expected<size_t, int> RtsLibusbBackend::ControlTransferOut(
    uint8_t request_type, uint8_t request, uint16_t value, uint16_t index,
    std::span<const uint8_t> data, std::chrono::milliseconds timeout) {
  return DoControlTransfer(dev_handle_.get(), request_type, request, value,
                           index, const_cast<uint8_t*>(data.data()),
                           data.size(), timeout);
}

std::expected<std::vector<RtsUsbInterface>, int>
RtsLibusbBackend::GetInterfaces() {
  if (!dev_handle_) {
    return std::unexpected(LIBUSB_ERROR_NO_DEVICE);
  }

  std::unique_ptr<libusb_config_descriptor,
                  CDeleter<libusb_free_config_descriptor>>
      config;
  int ret = libusb_get_active_config_descriptor(
      libusb_get_device(dev_handle_.get()), std::out_ptr(config));

  if (ret < 0) {
    return std::unexpected(ret);
  }

  std::vector<RtsUsbInterface> interfaces;
  for (const libusb_interface& iface :
       std::span(config->interface, config->bNumInterfaces)) {
    for (const libusb_interface_descriptor& alt : std::span(
             iface.altsetting, static_cast<size_t>(iface.num_altsetting))) {
      const std::span<const uint8_t> extra(
          alt.extra, alt.extra && alt.extra_length > 0
                         ? static_cast<size_t>(alt.extra_length)
                         : 0);

      interfaces.push_back({
          .number = alt.bInterfaceNumber,
          .usb_class = alt.bInterfaceClass,
          .usb_subclass = alt.bInterfaceSubClass,
          .extra = {extra.begin(), extra.end()},
      });
    }
  }
  return interfaces;
}

std::expected<void, int> RtsLibusbBackend::ClaimInterface(
    int interface_number) {
  if (!dev_handle_) {
    return std::unexpected(LIBUSB_ERROR_NO_DEVICE);
  }

  int ret = libusb_claim_interface(dev_handle_.get(), interface_number);
  if (ret < 0) {
    return std::unexpected(ret);
  }
  return {};
}

void RtsLibusbBackend::ReleaseInterface(int interface_number) {
  if (dev_handle_) {
    libusb_release_interface(dev_handle_.get(), interface_number);
  }
}

std::string_view RtsLibusbBackend::ErrorName(int error) const {
  return libusb_error_name(error);
}

}  // namespace realtekfp
