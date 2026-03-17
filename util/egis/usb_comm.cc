/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "usb_comm.h"

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>

static std::string libusb_error_to_string(int errcode)
{
	return std::string(libusb_error_name(errcode));
}

UsbComm::UsbComm(uint16_t vid, uint16_t pid)
	: vid_(vid)
	, pid_(pid)
{
	int rc = libusb_init(&ctx_);
	if (rc != 0) {
		std::cerr << "[ERROR] Failed to initialize libusb: "
			  << libusb_error_to_string(rc) << std::endl;
		ctx_ = nullptr;
	}
}

UsbComm::~UsbComm()
{
	if (handle_) {
		libusb_release_interface(handle_, INTERFACE_NUMBER);
		libusb_close(handle_);
	}
	if (ctx_) {
		libusb_exit(ctx_);
	}
}

int UsbComm::bulk_write(uint8_t endpoint, const std::vector<uint8_t> &data,
			int timeout_ms)
{
	if (!handle_)
		return -1;

	int transferred = 0;
	int rc = libusb_bulk_transfer(handle_, endpoint,
				      const_cast<uint8_t *>(data.data()),
				      static_cast<int>(data.size()),
				      &transferred, timeout_ms);
	if (rc != 0) {
		return rc;
	}
	return transferred;
}

int UsbComm::bulk_read(uint8_t endpoint, std::vector<uint8_t> &data,
		       int timeout_ms)
{
	if (!handle_)
		return -1;

	int transferred = 0;
	int rc = libusb_bulk_transfer(handle_, endpoint, data.data(),
				      static_cast<int>(data.size()),
				      &transferred, timeout_ms);
	if (rc != 0 && rc != LIBUSB_ERROR_TIMEOUT) {
		return rc;
	}
	return transferred;
}

int UsbComm::connect()
{
	if (handle_)
		return 0;

	if (!ctx_) {
		std::cerr << "[ERROR] libusb context is not initialized."
			  << std::endl;
		return -1;
	}

	handle_ = libusb_open_device_with_vid_pid(ctx_, vid_, pid_);
	if (!handle_) {
		std::cerr << "[ERROR] Cannot open device VID: 0x" << std::hex
			  << vid_ << " PID: 0x" << pid_ << std::dec
			  << std::endl;
		return -1;
	}

	if (libusb_kernel_driver_active(handle_, INTERFACE_NUMBER) == 1) {
		if (libusb_detach_kernel_driver(handle_, INTERFACE_NUMBER) ==
		    0) {
			std::cout << "Kernel driver successfully detached."
				  << std::endl;
		}
	}

	int rc = libusb_claim_interface(handle_, INTERFACE_NUMBER);
	if (rc != 0) {
		libusb_close(handle_);
		handle_ = nullptr;
		return rc;
	}

	std::cout << "--- Device connected: VID=0x" << std::hex << vid_
		  << ", PID=0x" << pid_ << std::dec << " ---\n";
	return 0;
}

bool UsbComm::send(const uint8_t *data, int len)
{
	if (!handle_) {
		std::cerr << "[ERROR] Not connected. Call connect() first."
			  << std::endl;
		return false;
	}

	int transferred = 0;
	int rc = libusb_bulk_transfer(handle_, EP_OUT_BULK,
				      const_cast<uint8_t *>(data), len,
				      &transferred, TRANSFER_TIMEOUT_MS);

	if (rc != 0 || transferred != len) {
		std::cerr << "[ERROR] Failed to send command: "
			  << libusb_error_to_string(rc) << std::endl;
		return false;
	}
	return true;
}

bool UsbComm::wait()
{
	if (!handle_) {
		std::cerr << "[ERROR] Not connected. Call connect() first."
			  << std::endl;
		return false;
	}

	uint8_t irq_buf[4];
	int transferred = 0;
	int retries = 0;
	const int MAX_RETRIES = 10;

	while (retries < MAX_RETRIES) {
		int rc = libusb_interrupt_transfer(handle_, EP_IN_INT, irq_buf,
						   sizeof(irq_buf),
						   &transferred,
						   TRANSFER_TIMEOUT_MS);

		if (rc == 0) {
			if (transferred > 0 &&
			    irq_buf[0] == RESPONSE_READY_IRQ) {
				return true;
			}
		} else if (rc == LIBUSB_ERROR_TIMEOUT) {
			retries++;
			continue;
		} else {
			std::cerr << "[ERROR] Interrupt IN error: "
				  << libusb_error_to_string(rc) << std::endl;
			break;
		}
	}
	std::cerr
		<< "[ERROR] Failed to receive RESPONSE_READY_IRQ after retries or error."
		<< std::endl;
	return false;
}

int UsbComm::receive(uint8_t *data, int len)
{
	if (!handle_) {
		std::cerr << "[ERROR] Not connected. Call connect() first."
			  << std::endl;
		return -1;
	}

	int transferred = 0;
	int rc = libusb_bulk_transfer(handle_, EP_IN_BULK, data, len,
				      &transferred, TRANSFER_TIMEOUT_MS);

	if (rc != 0) {
		std::cerr << "[ERROR] Failed to receive response: "
			  << libusb_error_to_string(rc) << std::endl;
		return -1;
	}

	return transferred;
}

void UsbComm::set_ids(uint16_t vid, uint16_t pid)
{
	vid_ = vid;
	pid_ = pid;
}