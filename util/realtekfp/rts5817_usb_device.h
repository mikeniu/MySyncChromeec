/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * RtsUsbDevice: RTS5817 (Realtek USB DFU device) access for the flashing
 * tool. All hardware access goes through RtsUsbBackend.
 */

#ifndef PLATFORM_EC_UTIL_RTS5817_USB_DEVICE_H_
#define PLATFORM_EC_UTIL_RTS5817_USB_DEVICE_H_

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>

#include "rts5817_usb_backend.h"

/* RTS5817 vendor-specific USB control requests (same as rtsflash.py) */
inline constexpr uint8_t kRtsStdGetStatus = 0x1A;
inline constexpr uint8_t kRtsStdSetStatus = 0x1B;
/* wValue for kRtsStdSetStatus: enable download mode */
inline constexpr uint16_t kRtsEnableDownloadMode = 1;

/*
 * Pre-computed bmRequestType values (raw uint8_t values per USB spec).
 * Avoids -Wdeprecated-enum-enum-conversion when OR'ing different libusb
 * enum types.
 *   0x80 = Device-to-host, Standard, Device
 *   0x00 = Host-to-device, Standard, Device
 */
inline constexpr uint8_t kBmReqD2hStdDev = 0x80;
inline constexpr uint8_t kBmReqH2dStdDev = 0x00;

/* DFU class-specific requests (bmRequestType = 0x21 or 0xA1) */
inline constexpr uint8_t kDfuDetach = 0x00;

/* DFU descriptor type */
inline constexpr uint8_t kDfuDescriptorType = 0x21;
/* DFU functional descriptor layout (byte offsets and minimum length) */
inline constexpr size_t kDfuDescTypeOff = 1;
inline constexpr size_t kDfuDescXferSizeOff = 5;
inline constexpr size_t kDfuDescMinLen = 9;

/* DFU class/subclass/protocol codes */
inline constexpr uint8_t kDfuClass = 0xFE;
inline constexpr uint8_t kDfuSubclass = 0x01;

/* bmRequestType: 0x21 = Host-to-device, Class, Interface */
inline constexpr uint8_t kBmReqH2dClsIf = 0x21;

/* USB I/O timeouts */
inline constexpr unsigned int kUsbCtrlTimeoutMs = 1000;

/* Default transfer size if we cannot parse the DFU functional descriptor */
inline constexpr uint16_t kDfuDefaultTransferSize = 1024;

/**
 * Extract wTransferSize from the class-specific descriptors that follow a
 * DFU interface descriptor.
 * Returns kDfuDefaultTransferSize if there is no valid DFU functional
 * descriptor or if it reports a zero transfer size.
 */
uint16_t parse_dfu_transfer_size(std::span<const uint8_t> extra);

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

	/** Enable DFU download mode via vendor control request. */
	bool enable_download();

	/**
	 * Send DFU_DETACH to transition the device from Runtime mode to DFU
	 * mode.  After this call the device re-enumerates; close the handle,
	 * wait, and re-open before issuing DFU_DOWNLOAD.
	 *
	 * @detach_timeout_ms  Time the device waits before detaching (ms).
	 */
	bool dfu_detach(uint16_t detach_timeout_ms);

	/** wTransferSize of the DFU interface found by the last lookup. */
	uint16_t transfer_size() const
	{
		return transfer_size_;
	}

    private:
	static void default_sleep_ms(int ms);

	/* Find the DFU interface and parse the DFU functional descriptor. */
	bool find_dfu_interface();

	std::unique_ptr<RtsUsbBackend> backend_;
	SleepFn sleep_ms_;
	int dfu_interface_ = -1;
	uint16_t transfer_size_ = kDfuDefaultTransferSize;
};

#endif /* PLATFORM_EC_UTIL_RTS5817_USB_DEVICE_H_ */
