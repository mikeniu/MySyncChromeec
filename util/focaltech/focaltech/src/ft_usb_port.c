/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ff_common.h"
#include "ff_log.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <asm/byteorder.h>
#include <fcntl.h>
#include <libusb.h>
#include <linux/usb/ch9.h>
#include <linux/usbdevice_fs.h>
#include <pthread.h>

#include <sys/ioctl.h>
#include <unistd.h>

#define USB_HUB_TIMEOUT 5000 /* milliseconds */

#define USB_PORT_FEAT_ENABLE 1
#define USB_PORT_FEAT_RESET 4

#define V_INIT_SUCCESS 0x5a

typedef struct focal_usb_device_port_st {
	uint8_t init;
	uint8_t bus;
	uint8_t hub;
	uint8_t port;
	uint8_t disabled;
} focal_usb_device_port_t;

typedef struct focal_usb_device_port_method_st {
	uint8_t init;
	int (*enable)(void);
	int (*disable)(void);
	int (*reset)(void);
	int (*get_port_status)(void);
	int (*reset_port_status)(void);
} focal_usb_device_port_method_t;

static focal_usb_device_port_t focal_dev_port;

/* Helper to directly manipulate the hub's port feature without string parsing
 */
static int ft_set_hub_port_feature(int busnum, int devnum, int portnum,
				   int feature, int enable)
{
	char dev_path[64];
	int fd, rc;
	struct usbdevfs_ctrltransfer ctrl;

	// Safely format the device path
	snprintf(dev_path, sizeof(dev_path), "/dev/bus/usb/%03d/%03d", busnum,
		 devnum);

	fd = open(dev_path, O_RDWR);
	if (fd < 0) {
		FF_LOGE("Failed to open %s: %s", dev_path, strerror(errno));
		return FT_NO_SUCH_DEV;
	}

	// Build the exact ioctl command that change_port_status used to build
	ctrl.bRequestType = USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_OTHER;
	ctrl.bRequest = enable ? USB_REQ_SET_FEATURE : USB_REQ_CLEAR_FEATURE;
	ctrl.wValue = feature;
	ctrl.wIndex = portnum;
	ctrl.wLength = 0;
	ctrl.timeout = USB_HUB_TIMEOUT;
	ctrl.data = NULL;

	rc = ioctl(fd, USBDEVFS_CONTROL, &ctrl);
	if (rc) {
		FF_LOGE("ioctl failed for port %d feature %d: %s", portnum,
			feature, strerror(errno));
		rc = FT_NO_SUCH_DEV;
	}

	close(fd);
	return rc;
}

static void ft_init_hub_device(libusb_device **devs)
{
	libusb_device *dev, *parent;
	int i = 0;

	if (focal_dev_port.init != V_INIT_SUCCESS) {
		memset(&focal_dev_port, 0, sizeof(focal_usb_device_port_t));

	} else {
		return;
	}

	while ((dev = devs[i++]) != NULL) {
		struct libusb_device_descriptor desc;
		int r = libusb_get_device_descriptor(dev, &desc);
		if (r < 0) {
			FF_LOGE("failed to get device descriptor");
			return;
		}

		if (desc.idVendor == FOCAL_EC_VID) {
			if (desc.idProduct != FOCAL_EC_PID_9865 &&
			    desc.idProduct != FOCAL_EC_PID_9869) {
				continue;
			}

			FF_LOGD("%04x:%04x (bus %d, device %d)", desc.idVendor,
				desc.idProduct, libusb_get_bus_number(dev),
				libusb_get_device_address(dev));

			focal_dev_port.port = libusb_get_port_number(dev);

			FF_LOGD("port=%d", focal_dev_port.port);

			parent = libusb_get_parent(dev);
			if (parent) {
				focal_dev_port.bus =
					libusb_get_bus_number(parent);
				focal_dev_port.hub =
					libusb_get_device_address(parent);

				FF_LOGD("parent info:  (bus %d, device %d)",
					focal_dev_port.bus, focal_dev_port.hub);

				focal_dev_port.init = V_INIT_SUCCESS;

				FF_LOGD("get port success");
			}

			break;
		}
	}
}

static int focal_dev_reset(void)
{
	int ret;
	FF_LOGD("%s enter\n", __func__);
	if (focal_dev_port.init != V_INIT_SUCCESS) {
		return FT_NO_SUCH_DEV;
	}

	// USB_PORT_FEAT_RESET is 4. '1' means set/enable the feature.
	ret = ft_set_hub_port_feature(focal_dev_port.bus, focal_dev_port.hub,
				      focal_dev_port.port, USB_PORT_FEAT_RESET,
				      1);
	if (ret != FT_NO_SUCH_DEV) {
		focal_dev_port.disabled = 0;
	}
	return (ret == FT_NO_SUCH_DEV) ? FT_NO_SUCH_DEV : FT_SUCCESS;
}

static int focal_dev_disable(void)
{
	int ret;
	FF_LOGI("%s enter", __func__);
	if (focal_dev_port.init != V_INIT_SUCCESS) {
		return FT_NO_SUCH_DEV;
	}

	if (focal_dev_port.disabled) {
		return FT_RESULT_FAIL;
	}

	// USB_PORT_FEAT_ENABLE is 1. '0' means clear/disable the feature.
	ret = ft_set_hub_port_feature(focal_dev_port.bus, focal_dev_port.hub,
				      focal_dev_port.port, USB_PORT_FEAT_ENABLE,
				      0);
	if (ret != FT_NO_SUCH_DEV) {
		focal_dev_port.disabled = 1;
	}
	return (ret == FT_NO_SUCH_DEV) ? FT_NO_SUCH_DEV : FT_SUCCESS;
}

static int
focal_usb_device_port_method_init(focal_usb_device_port_method_t *method)
{
	FF_LOGD("%s enter\n", __func__);

	if (method == NULL) {
		return FT_NO_SUCH_DEV;
	}

	libusb_device **devs;
	int r;
	ssize_t cnt;

	memset(method, 0, sizeof(focal_usb_device_port_method_t));

	r = libusb_init(NULL);
	if (r < 0)
		return r;

	cnt = libusb_get_device_list(NULL, &devs);
	if (cnt < 0) {
		libusb_exit(NULL);
		return (int)cnt;
	}

	ft_init_hub_device(devs);
	libusb_free_device_list(devs, 1);

	libusb_exit(NULL);

	method->reset = focal_dev_reset;
	method->disable = focal_dev_disable;

	method->init = V_INIT_SUCCESS;

	focal_dev_port.disabled = 0;
	return FT_SUCCESS;
}

int focal_device_disable(void)
{
	int ret = FT_SUCCESS;
	focal_usb_device_port_method_t method;
	memset(&method, 0, sizeof(focal_usb_device_port_method_t));

	focal_usb_device_port_method_init(&method);

	if (method.init == V_INIT_SUCCESS) {
		if (method.disable) {
			ret = method.disable();
		}
	}

	return ret;
}
