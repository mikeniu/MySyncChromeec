/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "usb_comm.h"

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>

static void die(const std::string &msg)
{
	std::cerr << "[USB FATAL] " << msg << std::endl;
	std::exit(EXIT_FAILURE);
}

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
		die("Failed to initialize libusb: " +
		    libusb_error_to_string(rc));
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

	handle_ = libusb_open_device_with_vid_pid(ctx_, vid_, pid_);
	if (!handle_) {
		std::cerr << "[ERROR] Cannot open device VID: 0x" << std::hex
			  << vid_ << " PID: 0x" << pid_ << std::dec
			  << std::endl;
		return -1;
	}

	// Attempt to detach kernel driver if it's already attached to an
	// interface
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

void UsbComm::send(const uint8_t *data, int len)
{
	if (!handle_) {
		die("Not connected. Call connect() first.");
	}

	int transferred = 0;
	int rc = libusb_bulk_transfer(handle_, EP_OUT_BULK,
				      const_cast<uint8_t *>(data), len,
				      &transferred, TRANSFER_TIMEOUT_MS);

	if (rc != 0 || transferred != len) {
		die("Failed to send command (Bulk OUT error: " +
		    libusb_error_to_string(rc) + ")");
	}
}

void UsbComm::wait()
{
	if (!handle_) {
		die("Not connected. Call connect() first.");
	}

	uint8_t irq_buf[4];
	int transferred = 0;

	while (true) {
		int rc = libusb_interrupt_transfer(handle_, EP_IN_INT, irq_buf,
						   sizeof(irq_buf),
						   &transferred,
						   TRANSFER_TIMEOUT_MS);

		if (rc == 0) {
			if (transferred > 0 &&
			    irq_buf[0] == RESPONSE_READY_IRQ) {
				return;
			}
		} else if (rc == LIBUSB_ERROR_TIMEOUT) {
			continue;
		} else {
			die("Interrupt IN error: " +
			    libusb_error_to_string(rc));
		}
	}
}

int UsbComm::receive(uint8_t *data, int len)
{
	if (!handle_) {
		die("Not connected. Call connect() first.");
	}

	int transferred = 0;
	int rc = libusb_bulk_transfer(handle_, EP_IN_BULK, data, len,
				      &transferred, TRANSFER_TIMEOUT_MS);

	if (rc != 0) {
		die("Failed to receive response (Bulk IN error: " +
		    libusb_error_to_string(rc) + ")");
	}

	return transferred;
}

void UsbComm::set_ids(uint16_t vid, uint16_t pid)
{
	vid_ = vid;
	pid_ = pid;
}