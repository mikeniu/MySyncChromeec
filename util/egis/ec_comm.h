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

	bool send_command(uint16_t id, uint8_t ver,
			  const std::vector<uint8_t> &payload,
			  std::vector<uint8_t> &resp_out);

    private:
	UsbComm &usb_;
};