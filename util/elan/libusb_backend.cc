// Copyright 2026 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libusb_backend.h"

#include <memory>

struct DeviceListDeleter {
  void operator()(libusb_device** l) const { libusb_free_device_list(l, 1); }
};

namespace elan {

LibusbBackend::LibusbBackend() = default;

LibusbBackend::~LibusbBackend() {
  Release();
  if (libusb_initialized_) {
    libusb_exit(nullptr);
  }
}

int LibusbBackend::Initialize() {
  if (!libusb_initialized_) {
    int err = libusb_init(nullptr);
    if (err == 0) {
      libusb_initialized_ = true;
    }
    return err;
  }
  return 0;
}

int LibusbBackend::OpenDevice(uint16_t vid, uint16_t pid) {
  libusb_device** raw_list = nullptr;
  ssize_t usb_dev_count = libusb_get_device_list(nullptr, &raw_list);
  if (usb_dev_count < 0) return static_cast<int>(usb_dev_count);

  std::unique_ptr<libusb_device*, DeviceListDeleter> list(raw_list);

  libusb_device* found_dev = nullptr;
  for (ssize_t i = 0; i < usb_dev_count; i++) {
    struct libusb_device_descriptor desc = {0};
    if (libusb_get_device_descriptor(list.get()[i], &desc) == 0) {
      if (desc.idVendor == vid && desc.idProduct == pid) {
        found_dev = list.get()[i];
        break;
      }
    }
  }

  if (!found_dev) return LIBUSB_ERROR_NO_DEVICE;

  libusb_config_descriptor* config = nullptr;
  int err = libusb_get_config_descriptor(found_dev, 0, &config);
  if (err != 0) return err;

  for (int i = 0; i < config->bNumInterfaces; i++) {
    const struct libusb_interface* inter = &config->interface[i];
    for (int j = 0; j < inter->num_altsetting; j++) {
      if (inter->altsetting[j].bInterfaceClass == LIBUSB_CLASS_HID) {
        claimed_interfaces_.push_back(inter->altsetting[j].bInterfaceNumber);
        break;
      }
    }
  }
  libusb_free_config_descriptor(config);

  err = libusb_open(found_dev, &dev_handle_);
  if (err != 0) return err;

  for (int interface_num : claimed_interfaces_) {
    if (libusb_kernel_driver_active(dev_handle_, interface_num) == 1) {
      libusb_detach_kernel_driver(dev_handle_, interface_num);
      detached_kernel_drivers_.push_back(interface_num);
    }
    err = libusb_claim_interface(dev_handle_, interface_num);
    if (err != 0) {
      Release();
      return err;
    }
  }
  return 0;
}

int LibusbBackend::InterruptTransfer(uint8_t endpoint, std::span<uint8_t> data,
                                     std::chrono::milliseconds timeout) {
  if (!dev_handle_) return LIBUSB_ERROR_NO_DEVICE;

  int transferred = 0;
  unsigned int c_timeout = static_cast<unsigned int>(timeout.count());

  int err = libusb_interrupt_transfer(dev_handle_, endpoint, data.data(),
                                      static_cast<int>(data.size()),
                                      &transferred, c_timeout);

  if (err != 0) return err;
  if (transferred != static_cast<int>(data.size())) return LIBUSB_ERROR_IO;

  return 0;
}

void LibusbBackend::Release() {
  if (!dev_handle_) return;

  for (int interface_num : claimed_interfaces_) {
    libusb_release_interface(dev_handle_, interface_num);
  }
  claimed_interfaces_.clear();

  for (int interface_num : detached_kernel_drivers_) {
    libusb_attach_kernel_driver(dev_handle_, interface_num);
  }
  detached_kernel_drivers_.clear();

  libusb_close(dev_handle_);
  dev_handle_ = nullptr;
}

}  // namespace elan
