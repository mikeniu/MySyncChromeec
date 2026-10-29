/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * RtsUsbDevice: RTS5817 (Realtek USB DFU device) access for the flashing
 * tool. All hardware access goes through RtsUsbBackend.
 */

#ifndef PLATFORM_EC_UTIL_RTS5817_USB_DEVICE_H_
#define PLATFORM_EC_UTIL_RTS5817_USB_DEVICE_H_

#include <cstdint>
#include <functional>
#include <memory>

#include "rts5817_usb_backend.h"

class RtsUsbDevice {
    public:
	using SleepFn = std::function<void(int)>;

	/**
	 * @backend  USB transport; takes ownership.
	 * @sleep_ms Sleep implementation, replaceable by tests.
	 */
	explicit RtsUsbDevice(std::unique_ptr<RtsUsbBackend> backend,
			      SleepFn sleep_ms = default_sleep_ms);
	RtsUsbDevice(const RtsUsbDevice &) = delete;
	RtsUsbDevice &operator=(const RtsUsbDevice &) = delete;
	~RtsUsbDevice();

	/** Open the device. */
	bool init(uint16_t vid, uint16_t pid);

	/**
	 * Poll every poll_interval_ms, up to timeout_ms, for the device to
	 * (re-)appear on the USB bus, e.g. after it re-enumerates.
	 * Returns true once the device is opened, false on timeout.
	 */
	bool wait_for_device(uint16_t vid, uint16_t pid, int timeout_ms,
			     int poll_interval_ms);

	/** Release USB resources. */
	void release();

    private:
	static void default_sleep_ms(int ms);

	std::unique_ptr<RtsUsbBackend> backend_;
	SleepFn sleep_ms_;
};

#endif /* PLATFORM_EC_UTIL_RTS5817_USB_DEVICE_H_ */
