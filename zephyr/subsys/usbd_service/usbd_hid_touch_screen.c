/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "hooks.h"
#include "queue.h"
#include "task.h"
#include "usbd_hid_touch_screen.h"
#include "usbd_init.h"
#include "util.h"

#include <zephyr/logging/log.h>
#include <zephyr/usb/class/usbd_hid.h>
#include <zephyr/usb/usb_device.h>
#include <zephyr/usb/usbd.h>
LOG_MODULE_DECLARE(usb_hid_touch_screen, LOG_LEVEL_INF);

#define TOUCH_SCREEN_NODE DT_ALIAS(usb_hid_touch_screen)
BUILD_ASSERT(DT_NODE_EXISTS(TOUCH_SCREEN_NODE),
	     "Unsupported board: usb-hid-touch-screen alias is not defined.");

static struct hid_dev_t touch_screen = {
	.report_queue = QUEUE_NULL(CONFIG_USBD_HID_TOUCH_SCREEN_QUEUE_SIZE,
				   struct usb_hid_touch_screen_report),
};

static void hid_touch_screen_proc_queue(void);
DECLARE_DEFERRED(hid_touch_screen_proc_queue);

static const uint8_t touch_screen_report_desc[] =
	TOUCH_SCREEN_USAGE(DT_PROP_OR(TOUCH_SCREEN_NODE, reportid_max_count, 0));

static int write_touch_screen_report(struct usb_hid_touch_screen_report *report)
{
	int ret = -EBUSY;

	if (touch_screen.report_protocol == HID_PROTOCOL_BOOT) {
		LOG_DBG("%s: protocol error", touch_screen.dev->name);
		return -EPROTO;
	}

	if (!atomic_test_bit(&touch_screen.state, HID_CLASS_IFACE_READY)) {
		LOG_ERR("%s iface is not ready", touch_screen.dev->name);
		return -EACCES;
	}

	if (atomic_test_bit(&touch_screen.state, HID_CLASS_SUSPENDED)) {
		ret = request_usb_wake();
		if (ret) {
			return ret;
		}
	}

	if (!atomic_test_and_set_bit(&touch_screen.state, HID_EP_IN_BUSY)) {
		ret = hid_device_submit_report(touch_screen.dev, sizeof(*report),
					       (uint8_t *)report);
		if (ret) {
			LOG_ERR("%s: failed to submit report, %d",
				touch_screen.dev->name, ret);
			atomic_clear_bit(&touch_screen.state, HID_EP_IN_BUSY);
			return ret;
		}
	}
	return ret;
}

static void touch_screen_iface_ready(const struct device *dev, const bool ready)
{
	ARG_UNUSED(dev);

	if (ready) {
		atomic_set_bit(&touch_screen.state, HID_CLASS_IFACE_READY);
	} else {
		atomic_clear_bit(&touch_screen.state, HID_CLASS_IFACE_READY);
		k_mutex_lock(&touch_screen.report_queue_mutex, K_FOREVER);
		if (queue_count(&touch_screen.report_queue) != 0) {
			queue_remove_units(&touch_screen.report_queue, NULL,
					   queue_count(&touch_screen.report_queue));
		}
		k_mutex_unlock(&touch_screen.report_queue_mutex);
	}
}

static int touch_screen_get_report(const struct device *dev, const uint8_t type,
			 const uint8_t id, const uint16_t len,
			 uint8_t *const buf)
{
	ARG_UNUSED(dev);

	/* TODO: SUPPORT? */

	return -ENOTSUP;
}

static void touch_screen_in_ready(const struct device *dev, const uint8_t *const report)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(report);

	atomic_clear_bit(&touch_screen.state, HID_EP_IN_BUSY);
}

static void touch_screen_set_protocol(const struct device *dev, uint8_t protocol)
{
	ARG_UNUSED(dev);

	touch_screen.report_protocol = protocol;
}

static int touch_screen_set_report(const struct device *dev, const uint8_t type,
			 const uint8_t id, const uint16_t len,
			 const uint8_t *const buf)
{
	LOG_HEXDUMP_ERR(buf, len, "unsupported to set report:");
	return -ENOTSUP;
}

static const struct hid_device_ops ops = {
	.iface_ready = touch_screen_iface_ready,
	.get_report = touch_screen_get_report,
	.input_report_done = touch_screen_in_ready,
	.set_protocol = touch_screen_set_protocol,
	.set_report = touch_screen_set_report,
};

__overridable void set_touch_screen_report(struct usb_hid_touch_screen_report *report)
{
	static bool print_full = true;

	if (!touch_screen.dev ||
	    !atomic_test_bit(&touch_screen.state, HID_CLASS_IFACE_READY)) {
		return;
	}

	k_mutex_lock(&touch_screen.report_queue_mutex, K_FOREVER);

	if (queue_is_empty(&touch_screen.report_queue)) {
		int ret = write_touch_screen_report(report);

		if (ret != -EBUSY) {
			k_mutex_unlock(&touch_screen.report_queue_mutex);
			return;
		}
	}

	if (queue_is_full(&touch_screen.report_queue)) {
		if (print_full) {
			LOG_WRN("touch screen queue is full");
		}
		print_full = false;

		queue_advance_head(&touch_screen.report_queue, 1);
	} else {
		print_full = true;
	}
	queue_add_unit(&touch_screen.report_queue, report);

	k_mutex_unlock(&touch_screen.report_queue_mutex);

	hook_call_deferred(&hid_touch_screen_proc_queue_data, 0);
}

static void hid_touch_screen_proc_queue(void)
{
	struct usb_hid_touch_screen_report report;

	k_mutex_lock(&touch_screen.report_queue_mutex, K_FOREVER);

	if (!atomic_test_bit(&touch_screen.state, HID_CLASS_IFACE_READY) ||
	    touch_screen.report_protocol == HID_PROTOCOL_BOOT) {
		queue_remove_units(&touch_screen.report_queue, NULL,
				   queue_count(&touch_screen.report_queue));
		k_mutex_unlock(&touch_screen.report_queue_mutex);
		return;
	}

	if (queue_is_empty(&touch_screen.report_queue)) {
		k_mutex_unlock(&touch_screen.report_queue_mutex);
		return;
	}

	queue_peek_units(&touch_screen.report_queue, &report, 0, 1);

	if (write_touch_screen_report(&report) != -EBUSY) {
		queue_advance_head(&touch_screen.report_queue, 1);
	}

	k_mutex_unlock(&touch_screen.report_queue_mutex);
	hook_call_deferred(&hid_touch_screen_proc_queue_data, 1 * USEC_PER_MSEC);
}

__maybe_unused static void touch_screen_msg_deferred(void)
{
	switch (usb_message) {
	case USBD_MSG_RESET:
		touch_screen.report_protocol = HID_PROTOCOL_REPORT;
		atomic_clear_bit(&touch_screen.state, HID_EP_IN_BUSY);
		break;
	case USBD_MSG_SUSPEND:
		atomic_set_bit(&touch_screen.state, HID_CLASS_SUSPENDED);
		break;
	case USBD_MSG_RESUME:
		atomic_clear_bit(&touch_screen.state, HID_CLASS_SUSPENDED);
		break;
	default:
		break;
	}
}

DECLARE_DEFERRED(touch_screen_msg_deferred);

static int usb_hid_touch_screen_init(void)
{
	int ret;

	touch_screen.dev = DEVICE_DT_GET(DT_NODELABEL(hid_touch_screen_dev));

	if (!touch_screen.dev) {
		LOG_ERR("failed to get hid touch screen device");
		return -ENXIO;
	}

	k_mutex_init(&touch_screen.report_queue_mutex);

	ret = hid_device_register(touch_screen.dev, touch_screen_report_desc,
				  sizeof(touch_screen_report_desc), &ops);
	if (ret) {
		LOG_ERR("failed to register hid touch screen device, %d", ret);
		goto error;
	}

	ret = usb_msg_deferred_register(&touch_screen_msg_deferred_data);
	if (ret) {
		LOG_ERR("failed to register touch screen message deferred work");
		goto error;
	}

	atomic_clear_bit(&touch_screen.state, HID_EP_IN_BUSY);

	return 0;

error:
	touch_screen.dev = NULL;
	return ret;
}
SYS_INIT(usb_hid_touch_screen_init, APPLICATION, CONFIG_KERNEL_INIT_PRIORITY_DEVICE);
