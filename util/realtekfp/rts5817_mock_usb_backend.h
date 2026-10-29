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
	MOCK_METHOD(const char *, error_name, (int error), (const, override));
};

#endif /* PLATFORM_EC_UTIL_RTS5817_MOCK_USB_BACKEND_H_ */
