/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "rts5817_usb_device.h"

#include <chrono>
#include <cstdio>
#include <thread>
#include <utility>

void RtsUsbDevice::default_sleep_ms(int ms)
{
	std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

RtsUsbDevice::RtsUsbDevice(std::unique_ptr<RtsUsbBackend> backend,
			   SleepFn sleep_ms)
	: backend_(std::move(backend)), sleep_ms_(std::move(sleep_ms))
{
}

RtsUsbDevice::~RtsUsbDevice()
{
	release();
}

bool RtsUsbDevice::init(uint16_t vid, uint16_t pid)
{
	release();

	int ret = backend_->init();
	if (ret < 0) {
		fprintf(stderr, "Error: libusb_init() failed: %s\n",
			backend_->error_name(ret));
		return false;
	}

	if (!backend_->open(vid, pid)) {
		fprintf(stderr,
			"Error: device %04x:%04x not found.\n"
			"Is the device connected and accessible?\n",
			vid, pid);
		release();
		return false;
	}

	printf("Device %04x:%04x opened.\n", vid, pid);
	return true;
}

bool RtsUsbDevice::wait_for_device(uint16_t vid, uint16_t pid, int timeout_ms,
				   int poll_interval_ms)
{
	if (timeout_ms <= 0 || poll_interval_ms <= 0)
		return false;

	release();

	int ret = backend_->init();
	if (ret < 0) {
		fprintf(stderr, "Error: libusb_init() failed: %s\n",
			backend_->error_name(ret));
		return false;
	}

	for (int waited = 0; waited < timeout_ms; waited += poll_interval_ms) {
		if (backend_->open(vid, pid)) {
			printf("Device %04x:%04x opened.\n", vid, pid);
			return true;
		}
		sleep_ms_(poll_interval_ms);
	}

	release();
	fprintf(stderr,
		"Error: device %04x:%04x did not re-enumerate within %d ms.\n",
		vid, pid, timeout_ms);
	return false;
}

void RtsUsbDevice::release()
{
	backend_->close();
}

bool RtsUsbDevice::enable_download()
{
	if (!backend_->is_open())
		return false;

	/*
	 * bmRequestType = 0x80: Device-to-host, Standard, Device
	 * bRequest = kRtsStdGetStatus (0x1A)
	 * wValue = 0, wIndex = 0
	 */
	uint8_t status = 0;
	int ret = backend_->control_transfer(
		kBmReqD2hStdDev, kRtsStdGetStatus, 0, 0, &status, sizeof(status),
		kUsbCtrlTimeoutMs);

	if (ret < static_cast<int>(sizeof(status))) {
		fprintf(stderr, "Error: GET_STATUS failed: %s\n",
			ret < 0 ? backend_->error_name(ret) : "short read");
		return false;
	}

	if (status == 0) {
		/*
		 * bmRequestType = 0x00: Host-to-device, Standard, Device
		 * bRequest = kRtsStdSetStatus (0x1B)
		 * wValue = kRtsEnableDownloadMode, wIndex = 0
		 */
		ret = backend_->control_transfer(
			kBmReqH2dStdDev, kRtsStdSetStatus, kRtsEnableDownloadMode, 0,
			nullptr, 0, kUsbCtrlTimeoutMs);

		if (ret < 0) {
			fprintf(stderr, "Error: SET_STATUS failed: %s\n",
				backend_->error_name(ret));
			return false;
		}
		printf("Download mode enabled on device.\n");
	} else {
		printf("Download mode already active (status = %d).\n", status);
	}

	return true;
}

uint16_t parse_dfu_transfer_size(std::span<const uint8_t> extra)
{
	/*
	 * Walk the descriptor stream.  The DFU functional descriptor has type
	 * 0x21 and contains wTransferSize at byte offset 5-6.
	 */
	while (extra.size() > kDfuDescTypeOff) {
		uint8_t desc_len = extra[0];

		if (desc_len == 0 || desc_len > extra.size())
			break;

		if (extra[kDfuDescTypeOff] == kDfuDescriptorType &&
		    desc_len >= kDfuDescMinLen) {
			uint16_t size = extra[kDfuDescXferSizeOff] |
					(extra[kDfuDescXferSizeOff + 1] << 8);

			return size != 0 ? size : kDfuDefaultTransferSize;
		}
		extra = extra.subspan(desc_len);
	}

	return kDfuDefaultTransferSize;
}
