/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * USB transport seam for the RTS5817 flashing tool. RtsUsbDevice talks to
 * the hardware only through this interface so that it can be unit tested
 * without a physical device.
 */

#ifndef PLATFORM_EC_UTIL_RTS5817_USB_BACKEND_H_
#define PLATFORM_EC_UTIL_RTS5817_USB_BACKEND_H_

#include <cstdint>
#include <vector>

/* One alternate setting of a USB interface of the active configuration. */
struct RtsUsbInterface {
	int number = 0;
	uint8_t usb_class = 0;
	uint8_t usb_subclass = 0;
	/* Class-specific descriptors that follow the interface descriptor. */
	std::vector<uint8_t> extra;
};

class RtsUsbBackend {
    public:
	virtual ~RtsUsbBackend() = default;

	/** Create the USB context. Returns 0 or a negative libusb error. */
	virtual int init() = 0;

	/** Open the device by VID:PID. Returns true if it was found. */
	virtual bool open(uint16_t vid, uint16_t pid) = 0;

	/** Close the device and destroy the context. Safe to call twice. */
	virtual void close() = 0;

	/** True while a device is open. */
	virtual bool is_open() const = 0;

	/**
	 * Issue a control transfer.
	 * Returns the number of bytes transferred or a negative libusb error.
	 */
	virtual int control_transfer(uint8_t request_type, uint8_t request,
				     uint16_t value, uint16_t index,
				     uint8_t *data, uint16_t length,
				     unsigned int timeout_ms) = 0;

	/**
	 * List the interfaces of the active configuration.
	 * Returns 0 or a negative libusb error.
	 */
	virtual int get_interfaces(std::vector<RtsUsbInterface> *interfaces) = 0;

	/** Reset the device; the handle may become invalid afterwards. */
	virtual int reset_device() = 0;

	/** Name of a negative libusb error code, for diagnostics. */
	virtual const char *error_name(int error) const = 0;
};

#endif /* PLATFORM_EC_UTIL_RTS5817_USB_BACKEND_H_ */
