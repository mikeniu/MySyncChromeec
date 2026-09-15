/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_ZEPHYR_SUBSYS_BT_PASSTHRU_CROS_HOSTCMD_TRANSPORT_H_
#define PLATFORM_EC_ZEPHYR_SUBSYS_BT_PASSTHRU_CROS_HOSTCMD_TRANSPORT_H_

#include "chre/platform/shared/host_transport.h"
#include "ec_commands.h"

#include <cstddef>
#include <cstdint>

namespace chre
{

/**
 * AP Host Transport utilizing ChromeOS EC Host Commands and MKBP.
 *
 * Implements the chre::HostTransport interface.
 */
class CrosHostcmdTransport : public HostTransport {
    public:
	CrosHostcmdTransport() = default;
	~CrosHostcmdTransport() override = default;

	pw::Status Start(HandleHostMsg &&msgHandler) override;
	pw::Status Write(uint32_t type, pw::span<const uint8_t> data,
			 bool wakeUp) override;

	/* Expose static for the Host Command handlers */
	static pw::Status
	HandleInboundHostCommand(pw::span<const uint8_t> payload);
	static size_t GetPendingEvents(uint8_t *out_buffer, size_t max_size,
				       uint32_t *num_events);

	using ResetHandler = void (*)();
	static void SetResetHandler(ResetHandler handler);
	static void Reset();

    private:
	/*
	 * Ring buffer to store HCI events arriving from the controller until
	 * the AP pulls them. Size is ~2.5KB to handle multiple large
	 * Advertising Reports.
	 */
	static constexpr size_t kBtEventBufSize = 256 * 10;
	static uint8_t bt_event_data_[kBtEventBufSize];
	static struct ring_buf bt_events_ring_buf_;
	static struct k_spinlock rb_lock_;

	/* Callback to process AP commands */
	static HandleHostMsg msg_handler_;

	/* Callback to notify subsystem of host reset */
	static ResetHandler reset_handler_;
};

} // namespace chre

#endif /* PLATFORM_EC_ZEPHYR_SUBSYS_BT_PASSTHRU_CROS_HOSTCMD_TRANSPORT_H_ */
