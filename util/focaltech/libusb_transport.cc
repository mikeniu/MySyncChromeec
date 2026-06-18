/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "libusb_transport.h"

#include <libusb.h>

#include <chrono>
#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <thread>

#include "ft_log.h"
#include "ft_util.h"
#include "usb_device.h"

namespace focaltech {

namespace {

using namespace std::chrono_literals;
constexpr auto kUsbEnumerationDelay = 200ms;
constexpr auto kPollInterval = 100ms;
constexpr uint8_t kDefaultUsbConfigIndex = 0;

// libusb_set_auto_detach_kernel_driver expects 1 to enable.
constexpr int kEnableAutoDetach = 1;

struct LibusbDeviceListDeleter {
  void operator()(libusb_device** list) const {
    if (list) {
      constexpr int kUnrefDevices = 1;
      libusb_free_device_list(list, kUnrefDevices);
    }
  }
};

using ScopedDeviceList =
    std::unique_ptr<libusb_device*, LibusbDeviceListDeleter>;

struct FoundDevice {
  libusb_device* handle;
  UsbDeviceId id;
};

std::optional<FoundDevice> FindFocalDevice(std::span<libusb_device*> devices) {
  for (libusb_device* dev : devices) {
    libusb_device_descriptor desc;
    if (libusb_get_device_descriptor(dev, &desc) != LIBUSB_SUCCESS) {
      continue;
    }

    const UsbDeviceId current_id{.vid = desc.idVendor, .pid = desc.idProduct};

    if (GetDeviceMode(current_id).has_value()) {
      return FoundDevice{.handle = dev, .id = current_id};
    }
  }
  return std::nullopt;
}

struct EndpointInfo {
  uint8_t interface_num = 0;
  uint8_t ep_bulk_in = 0;
  uint8_t ep_bulk_out = 0;
};

std::optional<EndpointInfo> ParseAltSetting(
    const libusb_interface_descriptor& alt) {
  EndpointInfo info{.interface_num = alt.bInterfaceNumber};

  for (const auto& ep : std::span(alt.endpoint, alt.bNumEndpoints)) {
    if ((ep.bmAttributes & LIBUSB_TRANSFER_TYPE_MASK) !=
        LIBUSB_TRANSFER_TYPE_BULK) {
      continue;
    }

    if ((ep.bEndpointAddress & LIBUSB_ENDPOINT_DIR_MASK) ==
        LIBUSB_ENDPOINT_IN) {
      info.ep_bulk_in = ep.bEndpointAddress;
    } else {
      info.ep_bulk_out = ep.bEndpointAddress;
    }

    if (info.ep_bulk_in != 0 && info.ep_bulk_out != 0) {
      return info;
    }
  }

  return std::nullopt;
}

std::optional<EndpointInfo> FindBulkEndpoints(libusb_device* dev) {
  libusb_config_descriptor* raw_config = nullptr;
  if (libusb_get_config_descriptor(dev, kDefaultUsbConfigIndex, &raw_config) !=
      LIBUSB_SUCCESS) {
    return std::nullopt;
  }

  ScopedConfig config(raw_config, libusb_free_config_descriptor);

  for (const auto& iface :
       std::span(config->interface, config->bNumInterfaces)) {
    for (const auto& alt : std::span(iface.altsetting, iface.num_altsetting)) {
      if (auto info = ParseAltSetting(alt)) {
        return info;
      }
    }
  }

  return std::nullopt;
}

}  // namespace

std::expected<LibusbTransport, Error> LibusbTransport::Create() {
  libusb_context* raw_ctx = nullptr;
  if (libusb_init(&raw_ctx) != LIBUSB_SUCCESS) {
    FT_LOGE("Failed to initialize libusb");
    return std::unexpected(Error::kHardwareFailure);
  }
  ScopedContext ctx(raw_ctx, libusb_exit);

  libusb_device** raw_list = nullptr;
  const ssize_t count = libusb_get_device_list(ctx.get(), &raw_list);
  if (count < 0) {
    FT_LOGE("Failed to get USB device list: {}", libusb_error_name(count));
    return std::unexpected(Error::kDeviceNotFound);
  }
  ScopedDeviceList dev_list(raw_list);

  const auto devices = std::span(raw_list, static_cast<size_t>(count));
  const auto found = FindFocalDevice(devices);
  if (!found) {
    FT_LOGE("No matching Focal device found");
    return std::unexpected(Error::kDeviceNotFound);
  }

  // Increase reference count on the device so it remains valid after the device
  // list is freed.
  libusb_ref_device(found->handle);
  dev_list.reset();

  FT_LOGI("Found device VID {:04x}: PID {:04x}", found->id.vid, found->id.pid);

  LibusbTransport transport(std::move(ctx), found->handle, found->id);
  const auto open_res = transport.Open();
  if (!open_res) {
    return std::unexpected(open_res.error());
  }
  return transport;
}

LibusbTransport::LibusbTransport(ScopedContext ctx, libusb_device* dev,
                                 UsbDeviceId id)
    : ctx_(std::move(ctx)), device_(dev, libusb_unref_device), device_id_(id) {}

std::expected<void, Error> LibusbTransport::Open() {
  if (opened_) {
    return {};
  }

  FT_LOGD("Opening USB device");

  if (!device_) {
    FT_LOGE("Invalid device pointer");
    return std::unexpected(Error::kDeviceNotFound);
  }

  libusb_device_handle* raw_handle = nullptr;
  const int r_open = libusb_open(device_.get(), &raw_handle);
  if (r_open != LIBUSB_SUCCESS || !raw_handle) {
    FT_LOGE("Failed to open device: {}", libusb_error_name(r_open));
    return std::unexpected(Error::kDeviceNotFound);
  }
  handle_.reset(raw_handle);

  const auto ep_info = FindBulkEndpoints(device_.get());
  if (!ep_info) {
    FT_LOGE("Could not find required bulk IN/OUT endpoints");
    return std::unexpected(Error::kDeviceNotFound);
  }
  endpoint_in_ = ep_info->ep_bulk_in;
  endpoint_out_ = ep_info->ep_bulk_out;
  interface_num_ = ep_info->interface_num;

  // Enable auto-detach. libusb will automatically detach the kernel driver
  // on claim, and reattach it on release.
  const int r_auto =
      libusb_set_auto_detach_kernel_driver(handle_.get(), kEnableAutoDetach);
  if (r_auto != LIBUSB_SUCCESS) {
    FT_LOGW("libusb_set_auto_detach_kernel_driver failed: {}", r_auto);
    // Continue anyway, as the interface may still work.
  }

  if (libusb_claim_interface(handle_.get(), interface_num_) != LIBUSB_SUCCESS) {
    FT_LOGE("Failed to claim interface");
    return std::unexpected(Error::kHardwareFailure);
  }

  device_.reset();
  opened_ = true;
  return {};
}

std::expected<size_t, Error> LibusbTransport::Send(
    std::span<const uint8_t> data, std::chrono::milliseconds timeout) {
  if (!opened_) {
    FT_LOGE("Device not opened");
    return std::unexpected(Error::kDeviceNotFound);
  }

  int actual = 0;
  const int r = libusb_bulk_transfer(
      handle_.get(), endpoint_out_, const_cast<uint8_t*>(data.data()),
      static_cast<int>(data.size()), &actual,
      static_cast<unsigned int>(timeout.count()));
  if (r != LIBUSB_SUCCESS) return HandleTransferError(r);
  return static_cast<size_t>(actual);
}

std::expected<size_t, Error> LibusbTransport::Recv(
    std::span<uint8_t> data, std::chrono::milliseconds timeout) {
  if (!opened_) {
    FT_LOGE("Device not opened");
    return std::unexpected(Error::kDeviceNotFound);
  }

  int actual = 0;
  const int r = libusb_bulk_transfer(
      handle_.get(), endpoint_in_, data.data(), static_cast<int>(data.size()),
      &actual, static_cast<unsigned int>(timeout.count()));
  if (r != LIBUSB_SUCCESS) return HandleTransferError(r);
  return static_cast<size_t>(std::min(actual, static_cast<int>(data.size())));
}

std::expected<void, Error> LibusbTransport::WaitForReset(
    std::chrono::milliseconds timeout) {
  if (!opened_) {
    FT_LOGE("Device not opened");
    return std::unexpected(Error::kDeviceNotFound);
  }

  const auto start = std::chrono::steady_clock::now();
  while (true) {
    int config = 0;
    const int r = libusb_get_configuration(handle_.get(), &config);
    if (r == LIBUSB_ERROR_NO_DEVICE) {
      FT_LOGI("USB device removal/reset detected");
      return {};
    } else if (r != LIBUSB_SUCCESS) {
      FT_LOGV("libusb_get_configuration returned {}", r);
    }

    if (timeout != std::chrono::milliseconds::zero() &&
        (std::chrono::steady_clock::now() - start) >= timeout) {
      break;
    }
    std::this_thread::sleep_for(kPollInterval);
  }

  FT_LOGW("Timeout after {} ms waiting for device removal", timeout.count());
  return std::unexpected(Error::kHardwareFailure);
}

std::expected<size_t, Error> LibusbTransport::HandleTransferError(
    int libusb_error) const {
  if (!handle_) return std::unexpected(Error::kDeviceNotFound);
  const int r = libusb_reset_device(handle_.get());
  if (r != LIBUSB_SUCCESS) {
    FT_LOGW("libusb_reset_device failed: {}", r);
  }
  std::this_thread::sleep_for(kUsbEnumerationDelay);

  if (libusb_error == LIBUSB_ERROR_NO_DEVICE)
    return std::unexpected(Error::kDeviceNotFound);
  return std::unexpected(Error::kHardwareFailure);
}

LibusbTransport::~LibusbTransport() {
  if (opened_ && handle_) {
    const int r = libusb_release_interface(handle_.get(), interface_num_);
    if (r != LIBUSB_SUCCESS) {
      FT_LOGV("Failed to release interface during cleanup: {}", r);
    }
  }
}

}  // namespace focaltech
