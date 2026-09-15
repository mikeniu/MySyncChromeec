/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#pragma once

#include "chre/platform/shared/bt_hci_interceptor.h"
#include "chre/platform/shared/host_transport.h"

namespace chre
{

class BtPassthruHciInterceptor : public BtHciInterceptor {
    public:
	explicit BtPassthruHciInterceptor(HostTransport &transport)
		: mTransport(transport)
	{
	}

	bool interceptCommand(const uint8_t *h4Buffer, size_t length,
			      BtClientType client) override;

	bool interceptCommandResponseEvent(const uint8_t *h4Buffer,
					   size_t length) override
	{
		ARG_UNUSED(h4Buffer);
		ARG_UNUSED(length);
		return false;
	}

	void notifyReset() override
	{
	}

    private:
	void synthesizeReadSupportedCommands();

	HostTransport &mTransport;
};

} // namespace chre
