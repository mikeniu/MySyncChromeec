/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#pragma once

#include "ec_protocol_defs.h"
#include "usb_comm.h"

#include <cstdint>
#include <vector>

class ECComm {
    public:
	explicit ECComm(UsbComm &usb);

	std::vector<uint8_t>
	send_command(uint16_t cmd_id, uint8_t cmd_ver,
		     const std::vector<uint8_t> &payload = {});

    private:
	UsbComm &usb_;
};