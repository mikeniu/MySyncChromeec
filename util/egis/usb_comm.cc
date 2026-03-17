/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "usb_comm.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <print>
#include <string>

static std::string LibusbErrorToString(int errcode)
{
	return std::string(libusb_error_name(errcode));
}

UsbComm::UsbComm(uint16_t vid, uint16_t pid)
	: vid_(vid)
	, pid_(pid)
{
	int rc = libusb_init(&ctx_);
	if (rc != 0) {
		std::println(stderr, "[ERROR] Failed to initialize libusb: {}",
			     LibusbErrorToString(rc));
		ctx_ = nullptr;
	}
}

UsbComm::~UsbComm()
{
	if (handle_) {
		libusb_release_interface(handle_, INTERFACE_NUMBER);
		if (kernel_driver_detached_) {
			int rc = libusb_attach_kernel_driver(handle_,
							     INTERFACE_NUMBER);
			if (rc == 0) {
				std::println(
					"Kernel driver successfully reattached.");
			} else {
				std::println(
					stderr,
					"[WARN] Failed to reattach kernel driver: {}",
					LibusbErrorToString(rc));
			}
		}
		libusb_close(handle_);
	}
	if (ctx_) {
		libusb_exit(ctx_);
	}
}

int UsbComm::BulkWrite(uint8_t endpoint, std::span<const uint8_t> data,
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

int UsbComm::BulkRead(uint8_t endpoint, std::span<uint8_t> data, int timeout_ms)
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

int UsbComm::Connect()
{
	if (handle_)
		return 0;

	if (!ctx_) {
		std::println(stderr,
			     "[ERROR] libusb context is not initialized.");
		return -1;
	}

	handle_ = libusb_open_device_with_vid_pid(ctx_, vid_, pid_);
	if (!handle_) {
		std::println(
			stderr,
			"[ERROR] Cannot open device VID: 0x{:04x} PID: 0x{:04x}",
			vid_, pid_);
		return -1;
	}

	if (libusb_kernel_driver_active(handle_, INTERFACE_NUMBER) == 1) {
		if (libusb_detach_kernel_driver(handle_, INTERFACE_NUMBER) ==
		    0) {
			std::println("Kernel driver successfully detached.");
			kernel_driver_detached_ = true;
		}
	}

	int rc = libusb_claim_interface(handle_, INTERFACE_NUMBER);
	if (rc != 0) {
		libusb_close(handle_);
		handle_ = nullptr;
		return rc;
	}

	std::println("--- Device connected: VID=0x{:x}, PID=0x{:x} ---", vid_,
		     pid_);
	return 0;
}

bool UsbComm::Send(std::span<const uint8_t> data)
{
	if (!handle_) {
		std::println(stderr,
			     "[ERROR] Not connected. Call connect() first.");
		return false;
	}

	int transferred = 0;
	int rc = libusb_bulk_transfer(handle_, kEpOutBulk,
				      const_cast<uint8_t *>(data.data()),
				      data.size(), &transferred,
				      kTransferTimeoutMs);

	if (rc != 0 || transferred != static_cast<int>(data.size())) {
		std::println(stderr, "[ERROR] Failed to send command: {}",
			     LibusbErrorToString(rc));
		return false;
	}
	return true;
}

bool UsbComm::Wait()
{
	if (!handle_) {
		std::println(stderr,
			     "[ERROR] Not connected. Call connect() first.");
		return false;
	}

	std::array<uint8_t, 4> irq_buf{};
	int transferred = 0;
	int retries = 0;
	const int kMaxRetries = 10;

	while (retries < kMaxRetries) {
		int rc = libusb_interrupt_transfer(handle_, kEpInInt,
						   irq_buf.data(),
						   irq_buf.size(), &transferred,
						   kTransferTimeoutMs);

		if (rc == 0) {
			if (transferred > 0 &&
			    irq_buf[0] == kResponseReadyIrq) {
				return true;
			}
		} else if (rc == LIBUSB_ERROR_TIMEOUT) {
			retries++;
			continue;
		} else {
			std::println(stderr, "[ERROR] Interrupt IN error: {}",
				     LibusbErrorToString(rc));
			break;
		}
	}
	std::println(
		stderr,
		"[ERROR] Failed to receive RESPONSE_READY_IRQ after retries or error.");
	return false;
}

int UsbComm::Receive(std::span<uint8_t> data)
{
	if (!handle_) {
		std::println(stderr,
			     "[ERROR] Not connected. Call connect() first.");
		return -1;
	}

	int transferred = 0;
	int rc = libusb_bulk_transfer(handle_, kEpInBulk, data.data(),
				      data.size(), &transferred,
				      kTransferTimeoutMs);

	if (rc != 0) {
		std::println(stderr, "[ERROR] Failed to receive response: {}",
			     LibusbErrorToString(rc));
		return -1;
	}

	return transferred;
}

void UsbComm::SetIds(uint16_t vid, uint16_t pid)
{
	vid_ = vid;
	pid_ = pid;
}
