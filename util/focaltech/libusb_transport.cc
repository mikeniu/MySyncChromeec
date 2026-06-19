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
  for (libusb_device* device : devices) {
    libusb_device_descriptor descriptor;
    if (libusb_get_device_descriptor(device, &descriptor) != LIBUSB_SUCCESS) {
      continue;
    }

    const UsbDeviceId current_id{.vid = descriptor.idVendor,
                                 .pid = descriptor.idProduct};

    if (GetDeviceMode(current_id).has_value()) {
      return FoundDevice{.handle = device, .id = current_id};
    }
  }
  return std::nullopt;
}

struct EndpointInfo {
  uint8_t interface_number = 0;
  uint8_t bulk_in = 0;
  uint8_t bulk_out = 0;
};

std::optional<EndpointInfo> ParseAltSetting(
    const libusb_interface_descriptor& alternate_setting) {
  EndpointInfo info{.interface_number = alternate_setting.bInterfaceNumber};

  for (const auto& endpoint :
       std::span(alternate_setting.endpoint, alternate_setting.bNumEndpoints)) {
    if ((endpoint.bmAttributes & LIBUSB_TRANSFER_TYPE_MASK) !=
        LIBUSB_TRANSFER_TYPE_BULK) {
      continue;
    }

    if ((endpoint.bEndpointAddress & LIBUSB_ENDPOINT_DIR_MASK) ==
        LIBUSB_ENDPOINT_IN) {
      info.bulk_in = endpoint.bEndpointAddress;
    } else {
      info.bulk_out = endpoint.bEndpointAddress;
    }

    if (info.bulk_in != 0 && info.bulk_out != 0) {
      return info;
    }
  }

  return std::nullopt;
}

std::optional<EndpointInfo> FindBulkEndpoints(libusb_device* device) {
  libusb_config_descriptor* raw_config_descriptor = nullptr;
  if (libusb_get_config_descriptor(device, kDefaultUsbConfigIndex,
                                   &raw_config_descriptor) != LIBUSB_SUCCESS) {
    return std::nullopt;
  }

  ScopedConfig config(raw_config_descriptor, libusb_free_config_descriptor);

  for (const auto& interface :
       std::span(config->interface, config->bNumInterfaces)) {
    for (const auto& alt_setting :
         std::span(interface.altsetting, interface.num_altsetting)) {
      if (auto endpoint_info = ParseAltSetting(alt_setting)) {
        return endpoint_info;
      }
    }
  }

  return std::nullopt;
}

}  // namespace

std::expected<LibusbTransport, Error> LibusbTransport::Create() {
  libusb_context* raw_context = nullptr;
  if (libusb_init(&raw_context) != LIBUSB_SUCCESS) {
    FT_LOGE("Failed to initialize libusb");
    return std::unexpected(Error::kHardwareFailure);
  }
  ScopedContext context(raw_context, libusb_exit);

  libusb_device** raw_device_list = nullptr;
  const ssize_t device_count =
      libusb_get_device_list(context.get(), &raw_device_list);
  if (device_count < 0) {
    FT_LOGE("Failed to get USB device list: {}",
            libusb_error_name(device_count));
    return std::unexpected(Error::kDeviceNotFound);
  }
  ScopedDeviceList device_list(raw_device_list);

  const auto devices =
      std::span(raw_device_list, static_cast<size_t>(device_count));
  const auto found = FindFocalDevice(devices);
  if (!found) {
    FT_LOGE("No matching Focal device found");
    return std::unexpected(Error::kDeviceNotFound);
  }

  // Increase reference count on the device so it remains valid after the device
  // list is freed.
  libusb_ref_device(found->handle);
  device_list.reset();

  FT_LOGI("Found device VID {:04x}: PID {:04x}", found->id.vid, found->id.pid);

  LibusbTransport transport(std::move(context), found->handle, found->id);
  const auto open_result = transport.Open();
  if (!open_result) {
    return std::unexpected(open_result.error());
  }
  return transport;
}

LibusbTransport::LibusbTransport(ScopedContext context,
                                 libusb_device* usb_device, UsbDeviceId id)
    : context_(std::move(context)),
      device_(usb_device, libusb_unref_device),
      device_id_(id) {}

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
  const int open_status = libusb_open(device_.get(), &raw_handle);
  if (open_status != LIBUSB_SUCCESS || !raw_handle) {
    FT_LOGE("Failed to open device: {}", libusb_error_name(open_status));
    return std::unexpected(Error::kDeviceNotFound);
  }
  handle_.reset(raw_handle);

  const auto endpoint_info = FindBulkEndpoints(device_.get());
  if (!endpoint_info) {
    FT_LOGE("Could not find required bulk IN/OUT endpoints");
    return std::unexpected(Error::kDeviceNotFound);
  }
  endpoint_in_ = endpoint_info->bulk_in;
  endpoint_out_ = endpoint_info->bulk_out;
  interface_number_ = endpoint_info->interface_number;

  // Enable auto-detach. libusb will automatically detach the kernel driver
  // on claim, and reattach it on release.
  const int auto_detach_status =
      libusb_set_auto_detach_kernel_driver(handle_.get(), kEnableAutoDetach);
  if (auto_detach_status != LIBUSB_SUCCESS) {
    FT_LOGW("libusb_set_auto_detach_kernel_driver failed: {}",
            auto_detach_status);
    // Continue anyway, as the interface may still work.
  }

  if (libusb_claim_interface(handle_.get(), interface_number_) !=
      LIBUSB_SUCCESS) {
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

  int transferred_bytes = 0;
  const int transfer_status = libusb_bulk_transfer(
      handle_.get(), endpoint_out_, const_cast<uint8_t*>(data.data()),
      static_cast<int>(data.size()), &transferred_bytes,
      static_cast<unsigned int>(timeout.count()));
  if (transfer_status != LIBUSB_SUCCESS)
    return HandleTransferError(transfer_status);
  return static_cast<size_t>(transferred_bytes);
}

std::expected<size_t, Error> LibusbTransport::Recv(
    std::span<uint8_t> data, std::chrono::milliseconds timeout) {
  if (!opened_) {
    FT_LOGE("Device not opened");
    return std::unexpected(Error::kDeviceNotFound);
  }

  int transferred_bytes = 0;
  const int transfer_status = libusb_bulk_transfer(
      handle_.get(), endpoint_in_, data.data(), static_cast<int>(data.size()),
      &transferred_bytes, static_cast<unsigned int>(timeout.count()));
  if (transfer_status != LIBUSB_SUCCESS)
    return HandleTransferError(transfer_status);
  return static_cast<size_t>(
      std::min(transferred_bytes, static_cast<int>(data.size())));
}

std::expected<void, Error> LibusbTransport::WaitForReset(
    std::chrono::milliseconds timeout) {
  if (!opened_) {
    FT_LOGE("Device not opened");
    return std::unexpected(Error::kDeviceNotFound);
  }

  const auto start = std::chrono::steady_clock::now();
  while (true) {
    int active_config_value = 0;
    const int config_status =
        libusb_get_configuration(handle_.get(), &active_config_value);
    if (config_status == LIBUSB_ERROR_NO_DEVICE) {
      FT_LOGI("USB device removal/reset detected");
      return {};
    } else if (config_status != LIBUSB_SUCCESS) {
      FT_LOGV("libusb_get_configuration returned {}", config_status);
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
  const int reset_status = libusb_reset_device(handle_.get());
  if (reset_status != LIBUSB_SUCCESS) {
    FT_LOGW("libusb_reset_device failed: {}", reset_status);
  }
  std::this_thread::sleep_for(kUsbEnumerationDelay);

  if (libusb_error == LIBUSB_ERROR_NO_DEVICE)
    return std::unexpected(Error::kDeviceNotFound);
  return std::unexpected(Error::kHardwareFailure);
}

LibusbTransport::~LibusbTransport() {
  if (opened_ && handle_) {
    const int release_status =
        libusb_release_interface(handle_.get(), interface_number_);
    if (release_status != LIBUSB_SUCCESS) {
      FT_LOGV("Failed to release interface during cleanup: {}", release_status);
    }
  }
}

}  // namespace focaltech
