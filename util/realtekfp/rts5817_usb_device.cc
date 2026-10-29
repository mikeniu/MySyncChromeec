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
