/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_UTIL_RTS5817_LIBUSB_BACKEND_H_
#define PLATFORM_EC_UTIL_RTS5817_LIBUSB_BACKEND_H_

#include <libusb-1.0/libusb.h>

#include "rts5817_usb_backend.h"

class RtsLibusbBackend : public RtsUsbBackend {
    public:
	RtsLibusbBackend() = default;
	RtsLibusbBackend(const RtsLibusbBackend &) = delete;
	RtsLibusbBackend &operator=(const RtsLibusbBackend &) = delete;
	~RtsLibusbBackend() override;

	int init() override;
	bool open(uint16_t vid, uint16_t pid) override;
	void close() override;
	bool is_open() const override;
	int control_transfer(uint8_t request_type, uint8_t request,
			     uint16_t value, uint16_t index, uint8_t *data,
			     uint16_t length, unsigned int timeout_ms) override;
	int get_interfaces(std::vector<RtsUsbInterface> *interfaces) override;
	int reset_device() override;
	const char *error_name(int error) const override;

    private:
	libusb_device_handle *dev_handle_ = nullptr;
	libusb_context *ctx_ = nullptr;
};

#endif /* PLATFORM_EC_UTIL_RTS5817_LIBUSB_BACKEND_H_ */
