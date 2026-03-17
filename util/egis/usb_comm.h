/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#pragma once

#include <cstdint>
#include <libusb-1.0/libusb.h>
#include <stdexcept>
#include <string>
#include <vector>

#define INTERFACE_NUMBER 0

#define EP_OUT_BULK 0x01 // Bulk OUT
#define EP_IN_BULK 0x81 // Bulk IN
#define EP_IN_INT 0x82 // Interrupt IN

static constexpr int TRANSFER_TIMEOUT_MS = 500;
static constexpr uint8_t RESPONSE_READY_IRQ = 1;

class UsbCommunicationError : public std::runtime_error {
    public:
	UsbCommunicationError(const std::string &msg)
		: std::runtime_error(msg)
	{
	}
};

class UsbComm {
    public:
	UsbComm(uint16_t vid, uint16_t pid);
	~UsbComm();

	UsbComm(const UsbComm &) = delete;
	UsbComm &operator=(const UsbComm &) = delete;

	int bulk_write(uint8_t endpoint, const std::vector<uint8_t> &data,
		       int timeout_ms);
	int bulk_read(uint8_t endpoint, std::vector<uint8_t> &data,
		      int timeout_ms);

	int connect();
	bool send(const uint8_t *data, int len);
	bool wait();
	int receive(uint8_t *data, int len);
	void set_ids(uint16_t vid, uint16_t pid);

    private:
	libusb_context *ctx_ = nullptr;
	libusb_device_handle *handle_ = nullptr;
	uint16_t vid_;
	uint16_t pid_;
};