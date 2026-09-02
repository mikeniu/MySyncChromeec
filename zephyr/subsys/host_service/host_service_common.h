/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_SUBSYS_HOST_SERVICE_HOST_SERVICE_COMMON_H_
#define ZEPHYR_SUBSYS_HOST_SERVICE_HOST_SERVICE_COMMON_H_

#include <zephyr/device.h>
#include <zephyr/init.h>

#if defined(CONFIG_HOST_SERVICE)
extern struct k_thread host_service_thread;

int heci_init(struct device *arg);
int mng_and_boot_init(struct device *dev);

void process_host_msgs(void);

#endif
#endif /* ZEPHYR_SUBSYS_HOST_SERVICE_HOST_SERVICE_COMMON_H_ */
