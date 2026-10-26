/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "rts5817_libusb_backend.h"

RtsLibusbBackend::~RtsLibusbBackend()
{
	close();
}

int RtsLibusbBackend::init()
{
	close();
	return libusb_init(&ctx_);
}

bool RtsLibusbBackend::open(uint16_t vid, uint16_t pid)
{
	if (!ctx_)
		return false;

	if (dev_handle_) {
		libusb_close(dev_handle_);
		dev_handle_ = nullptr;
	}

	dev_handle_ = libusb_open_device_with_vid_pid(ctx_, vid, pid);
	return dev_handle_ != nullptr;
}

void RtsLibusbBackend::close()
{
	if (dev_handle_) {
		libusb_close(dev_handle_);
		dev_handle_ = nullptr;
	}
	if (ctx_) {
		libusb_exit(ctx_);
		ctx_ = nullptr;
	}
}

bool RtsLibusbBackend::is_open() const
{
	return dev_handle_ != nullptr;
}

int RtsLibusbBackend::control_transfer(uint8_t request_type, uint8_t request,
				       uint16_t value, uint16_t index,
				       uint8_t *data, uint16_t length,
				       unsigned int timeout_ms)
{
	if (!dev_handle_)
		return LIBUSB_ERROR_NO_DEVICE;

	return libusb_control_transfer(dev_handle_, request_type, request,
				       value, index, data, length, timeout_ms);
}

int RtsLibusbBackend::get_interfaces(std::vector<RtsUsbInterface> *interfaces)
{
	if (!dev_handle_)
		return LIBUSB_ERROR_NO_DEVICE;

	libusb_config_descriptor *config = nullptr;
	int ret = libusb_get_active_config_descriptor(
		libusb_get_device(dev_handle_), &config);

	if (ret < 0)
		return ret;

	interfaces->clear();
	for (uint8_t i = 0; i < config->bNumInterfaces; i++) {
		for (int j = 0; j < config->interface[i].num_altsetting; j++) {
			const libusb_interface_descriptor &alt =
				config->interface[i].altsetting[j];
			RtsUsbInterface &info =
				interfaces->emplace_back(RtsUsbInterface{
					.number = alt.bInterfaceNumber,
					.usb_class = alt.bInterfaceClass,
					.usb_subclass = alt.bInterfaceSubClass,
				});

			if (alt.extra && alt.extra_length > 0)
				info.extra.assign(alt.extra,
						  alt.extra + alt.extra_length);
		}
	}

	libusb_free_config_descriptor(config);
	return 0;
}

int RtsLibusbBackend::reset_device()
{
	if (!dev_handle_)
		return LIBUSB_ERROR_NO_DEVICE;

	return libusb_reset_device(dev_handle_);
}

const char *RtsLibusbBackend::error_name(int error) const
{
	return libusb_error_name(error);
}
