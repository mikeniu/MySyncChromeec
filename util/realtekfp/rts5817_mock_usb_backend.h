/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_UTIL_RTS5817_MOCK_USB_BACKEND_H_
#define PLATFORM_EC_UTIL_RTS5817_MOCK_USB_BACKEND_H_

#include <gmock/gmock.h>

#include "rts5817_usb_backend.h"

class MockRtsUsbBackend : public RtsUsbBackend {
    public:
	MOCK_METHOD(int, init, (), (override));
	MOCK_METHOD(bool, open, (uint16_t vid, uint16_t pid), (override));
	MOCK_METHOD(void, close, (), (override));
	MOCK_METHOD(bool, is_open, (), (const, override));
	MOCK_METHOD(int, control_transfer,
		    (uint8_t request_type, uint8_t request, uint16_t value,
		     uint16_t index, uint8_t *data, uint16_t length,
		     unsigned int timeout_ms),
		    (override));
	MOCK_METHOD(int, get_interfaces, (std::vector<RtsUsbInterface> *),
		    (override));
	MOCK_METHOD(int, detach_kernel_driver, (int interface_number),
		    (override));
	MOCK_METHOD(int, claim_interface, (int interface_number), (override));
	MOCK_METHOD(void, release_interface, (int interface_number),
		    (override));
	MOCK_METHOD(int, reset_device, (), (override));
	MOCK_METHOD(const char *, error_name, (int error), (const, override));
};

#endif /* PLATFORM_EC_UTIL_RTS5817_MOCK_USB_BACKEND_H_ */
