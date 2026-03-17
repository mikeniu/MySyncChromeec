/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef EC_COMM_H_
#define EC_COMM_H_

#include "ec_protocol_defs.h"
#include "usb_comm.h"

#include <cstdint>
#include <expected>
#include <span>
#include <vector>

class EcComm {
    public:
	explicit EcComm(UsbComm &usb);

	std::expected<std::vector<uint8_t>, bool> SendCommand(
		uint16_t id, uint8_t ver,
		std::span<const uint8_t> payload = std::span<const uint8_t>());

    private:
	UsbComm &usb_;
};

#endif // EC_COMM_H_
