/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#pragma once

#include "chre/platform/shared/bt_controller_sender.h"

#include <cstddef>
#include <cstdint>

namespace chre
{

/**
 * Hardware transport for sending Raw HCI packets to Zephyr.
 *
 * Implements the chre::BtControllerSender interface.
 */
class ZephyrBtSender : public BtControllerSender {
    public:
	ZephyrBtSender() = default;
	~ZephyrBtSender() override = default;

	void sendH4HciPacketToController(const uint8_t *h4Buf,
					 size_t bytes) override;
};

} // namespace chre
