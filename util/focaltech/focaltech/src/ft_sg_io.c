/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ff_common.h"
#include "ff_log.h"
#include "ff_sg_io.h"
#include "net_usb.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <endian.h>
#include <libusb.h>
#include <unistd.h>

// device Handle
static libusb_device_handle *dev_handle = NULL;
static uint32_t cdb_tag = 1;
static libusb_context *ctx;
static libusb_hotplug_callback_handle callback_handle = 0;

static volatile int got_usb_remove = 0;

static struct ft_boot_ep_t {
	uint8_t ep_bulk_out;
	uint8_t ep_bulk_in;
} g_ft_boot_ep;

// init libusb
int ft_sg_init(void)
{
	int r = libusb_init(&ctx);
	if (r < 0) {
		FF_LOGE("Failed to initialize libusb: %s\n",
			libusb_error_name(r));
		return FT_RESULT_FAIL;
	}

#ifdef LIBUSB_OPTION_LOG_LEVEL
	libusb_set_option(ctx, LIBUSB_OPTION_LOG_LEVEL, 3);
#else
	libusb_set_debug(ctx, 3);
#endif

	return FT_SUCCESS;
}

// open device by VID/PID
int ft_sg_open(uint16_t vendor_id, uint16_t product_id)
{
	dev_handle =
		libusb_open_device_with_vid_pid(ctx, vendor_id, product_id);
	if (!dev_handle) {
		FF_LOGE("Device not found");
		return FT_NO_SUCH_DEV;
	}
	libusb_reset_device(dev_handle);
	usleep(200 * 1000);

	if (!libusb_has_capability(LIBUSB_CAP_HAS_HOTPLUG)) {
		FF_LOGW("Hotplug capabilities are not supported on this platform: %s",
			libusb_error_name(0));
	} else {
		int ret = libusb_hotplug_register_callback(
			ctx,
			LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED |
				LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT,
			0, vendor_id, product_id, LIBUSB_HOTPLUG_MATCH_ANY,
			usb_remove_event_callback, (void *)&got_usb_remove,
			&callback_handle);
		if (ret) {
			FF_LOGW("hotplug_register failed");
		}
	}

	// check MASS_STORAGE device
	struct libusb_device_descriptor desc;
	libusb_device *dev = libusb_get_device(dev_handle);
	libusb_get_device_descriptor(dev, &desc);

	FF_LOGD("Device opened: %04x:%04x", desc.idVendor, desc.idProduct);
	FF_LOGD("bDeviceClass: 0x%02x", desc.bDeviceClass);

	// find interface
	struct libusb_config_descriptor *config;
	libusb_get_config_descriptor(dev, 0, &config);

	int found_msc = 0;
	int msc_iface = 0;
	memset(&g_ft_boot_ep, 0, sizeof(g_ft_boot_ep));

	for (int i = 0; i < config->bNumInterfaces; i++) {
		const struct libusb_interface *interface =
			&config->interface[i];
		for (int j = 0; j < interface->num_altsetting; j++) {
			const struct libusb_interface_descriptor *iface_desc =
				&interface->altsetting[j];
			FF_LOGD("Interface %d: bInterfaceClass=0x%02x, bInterfaceSubClass=0x%02x",
				iface_desc->bInterfaceNumber,
				iface_desc->bInterfaceClass,
				iface_desc->bInterfaceSubClass);

			if (iface_desc->bInterfaceClass ==
			    LIBUSB_CLASS_MASS_STORAGE) { // SCSI transparent
				found_msc = 1;
				msc_iface = iface_desc->bInterfaceNumber;

				// find bulk endpoint
				for (int k = 0; k < iface_desc->bNumEndpoints;
				     k++) {
					const struct libusb_endpoint_descriptor
						*ep = &iface_desc->endpoint[k];
					if ((ep->bmAttributes ==
					     LIBUSB_ENDPOINT_TRANSFER_TYPE_BULK) &&
					    (((ep->bEndpointAddress) &
					      LIBUSB_ENDPOINT_DIR_MASK) ==
					     LIBUSB_ENDPOINT_IN)) {
						g_ft_boot_ep.ep_bulk_in =
							ep->bEndpointAddress;
					}

					if ((ep->bmAttributes ==
					     LIBUSB_ENDPOINT_TRANSFER_TYPE_BULK) &&
					    (((ep->bEndpointAddress) &
					      LIBUSB_ENDPOINT_DIR_MASK) ==
					     LIBUSB_ENDPOINT_OUT)) {
						g_ft_boot_ep.ep_bulk_out =
							ep->bEndpointAddress;
					}
				}
			}
		}
	}
	libusb_free_config_descriptor(config);

	if (!found_msc) {
		FF_LOGE("Not a Mass Storage Class device");
		libusb_close(dev_handle);
		dev_handle = NULL;
		return FT_NO_SUCH_DEV;
	}

	if (libusb_kernel_driver_active(dev_handle, 0) == 1) {
		FF_LOGD("Detaching kernel driver");
		libusb_detach_kernel_driver(dev_handle, 0);
	}

	int r = libusb_claim_interface(dev_handle, msc_iface);
	if (r < 0) {
		FF_LOGE("Failed to claim interface: %s", libusb_error_name(r));
		libusb_release_interface(dev_handle, 0);
		libusb_close(dev_handle);
		dev_handle = NULL;
		return FT_NO_SUCH_DEV;
	}

	if (!g_ft_boot_ep.ep_bulk_out || !g_ft_boot_ep.ep_bulk_in) {
		FF_LOGE("can't find bulk endpoint,ep in=%x,ep out=%x",
			g_ft_boot_ep.ep_bulk_in, g_ft_boot_ep.ep_bulk_out);
		libusb_release_interface(dev_handle, 0);
		libusb_close(dev_handle);
		dev_handle = NULL;
		return FT_NO_SUCH_DEV;
	}

	return FT_SUCCESS;
}

static int ft_check_mcu_reset(void)
{
	int retry = 5;
	int complete = 0;
	struct timeval timeout;

	while (retry--) {
		memset(&timeout, 0, sizeof(timeout));
		timeout.tv_sec = 1;

		libusb_handle_events_timeout_completed(ctx, &timeout,
						       &complete);

		if (got_usb_remove == FT_USB_DEVICE_REMOVE_MAGIC) {
			FF_LOGI("got usb remove event");
			return FT_SUCCESS;
		}
	}

	return FT_RESULT_FAIL;
}

// send SG_IO
int ft_sg_io(struct sg_io_hdr *io_hdr)
{
	if (!dev_handle) {
		FF_LOGE("Device not opened");
		return FT_NO_SUCH_DEV;
	}

	// create CBW
	struct bulk_cb_wrap cbw = { 0 };

	if (!io_hdr->cmdp || io_hdr->cmd_len == 0 ||
	    io_hdr->cmd_len > sizeof(cbw.cmd)) {
		FF_LOGE("Invalid SCSI command");
		return FT_NO_SUCH_DEV;
	}

	cbw.signature = htole32(USB_MS_CBW_SIGNATURE);
	cbw.tag = htole32(cdb_tag++);

	// set transfer DIRECTION
	switch (io_hdr->dxfer_direction) {
	case DIRECTION_IN:
		cbw.flags = 0x80; // Device-to-host
		break;
	case DIRECTION_OUT:
		cbw.flags = 0x00; // Host-to-device
		break;
	case DIRECTION_NONE:
	default:
		cbw.flags = 0x00;
		break;
	}

	cbw.lun = 0;
	cbw.cmd_len = io_hdr->cmd_len;
	memcpy(cbw.cmd, io_hdr->cmdp, io_hdr->cmd_len);
	cbw.data_transfer_length = htole32(io_hdr->dxfer_len);

	// send CBW
	int transferred;
	int r = libusb_bulk_transfer(dev_handle, g_ft_boot_ep.ep_bulk_out,
				     (unsigned char *)&cbw, sizeof(cbw),
				     &transferred, io_hdr->timeout);
	if (r < 0) {
		if (io_hdr->dxfer_direction == DIRECTION_NONE) {
			FF_LOGW("CBW transfer failed: %s,recheck mcu reset",
				libusb_error_name(r));
			if (ft_check_mcu_reset() == 0) {
				return 0;
			}
		}

		FF_LOGE("CBW transfer failed: %s", libusb_error_name(r));
		io_hdr->driver_status = SG_IO_DRIVER_ERROR;

		return FT_NO_SUCH_DEV;
	}

	if (transferred != sizeof(cbw)) {
		if (io_hdr->dxfer_direction == DIRECTION_NONE) {
			FF_LOGW("Incomplete CBW transfer: %d/%zu bytes,recheck mcu reset",
				transferred, sizeof(cbw));
			if (ft_check_mcu_reset() == 0) {
				return FT_SUCCESS;
			}
		}
		FF_LOGE("Incomplete CBW transfer: %d/%zu bytes", transferred,
			sizeof(cbw));
		io_hdr->driver_status = SG_IO_DRIVER_ERROR;
		return FT_NO_SUCH_DEV;
	}

	// data transfer
	if (io_hdr->dxfer_len > 0 && io_hdr->dxferp) {
		int ep;
		if (io_hdr->dxfer_direction == DIRECTION_IN) {
			ep = g_ft_boot_ep.ep_bulk_in; // Bulk IN endpoint
		} else {
			ep = g_ft_boot_ep.ep_bulk_out; // Bulk OUT endpoint
		}

		r = libusb_bulk_transfer(dev_handle, ep,
					 (unsigned char *)io_hdr->dxferp,
					 io_hdr->dxfer_len, &transferred,
					 io_hdr->timeout);
		if (r < 0) {
			if (io_hdr->dxfer_direction == DIRECTION_NONE) {
				FF_LOGW("Data transfer failed: %s,recheck mcu reset",
					libusb_error_name(r));
				if (ft_check_mcu_reset() == 0) {
					return FT_SUCCESS;
				}
			}

			FF_LOGE("Data transfer failed: %s",
				libusb_error_name(r));
			io_hdr->driver_status = SG_IO_DRIVER_ERROR;
			return FT_NO_SUCH_DEV;
		}

		io_hdr->resid = io_hdr->dxfer_len - transferred;
	} else {
		io_hdr->resid = 0;
	}

	// mcu reset ,can't recv any data when mcu reset
	if (io_hdr->dxfer_direction != DIRECTION_NONE) {
		// recv CSW
		struct bulk_cs_wrap csw;
		r = libusb_bulk_transfer(dev_handle,
					 g_ft_boot_ep.ep_bulk_in, // Bulk IN
								  // endpoint
					 (unsigned char *)&csw, sizeof(csw),
					 &transferred, io_hdr->timeout);
		if (r < 0) {
			FF_LOGE("CSW transfer failed: %s",
				libusb_error_name(r));
			io_hdr->driver_status = SG_IO_DRIVER_ERROR;
			return FT_NO_SUCH_DEV;
		}

		if (transferred != sizeof(csw)) {
			FF_LOGE("Incomplete CSW transfer: %d/%zu bytes",
				transferred, sizeof(csw));
			io_hdr->driver_status = SG_IO_DRIVER_ERROR;
			return FT_NO_SUCH_DEV;
		}

		// check CSW
		if (le32toh(csw.signature) != USB_MS_CSW_SIGNATURE) {
			FF_LOGE("Invalid CSW signature: 0x%08x",
				le32toh(csw.signature));
			io_hdr->driver_status = SG_IO_DRIVER_ERROR;
			return FT_NO_SUCH_DEV;
		}

		if (le32toh(csw.tag) != le32toh(cbw.tag)) {
			FF_LOGE("CSW tag mismatch: sent=0x%08x, received=0x%08x",
				le32toh(cbw.tag), le32toh(csw.tag));
			io_hdr->driver_status = SG_IO_DRIVER_ERROR;
			return FT_NO_SUCH_DEV;
		}

		// set status
		io_hdr->status = csw.status;
		io_hdr->resid = le32toh(csw.data_residue);

		switch (csw.status) {
		case 0x00: // Command Passed
			io_hdr->masked_status = SG_IO_SUCCESS;
			break;
		case 0x01: // Command Failed
			io_hdr->masked_status = SG_IO_CHECK_CONDITION;
			break;
		case 0x02: // Phase Error
		default:
			io_hdr->masked_status = SG_IO_BAD_TARGET;
			break;
		}
	}

	return FT_SUCCESS;
}

// close device
void ft_sg_close(void)
{
	if (dev_handle) {
		libusb_release_interface(dev_handle, 0);

		libusb_attach_kernel_driver(dev_handle, 0);

		libusb_close(dev_handle);
		dev_handle = NULL;
	}

	if (ctx) {
		if (callback_handle) {
			FF_LOGD("deregister hot plug sgio");
			libusb_hotplug_deregister_callback(ctx,
							   callback_handle);
			callback_handle = 0;
		}
		libusb_exit(ctx);
		ctx = NULL;
	}

	cdb_tag = 1;
}
