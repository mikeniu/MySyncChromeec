/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Emulator self-reboot procedure */

#ifndef PLATFORM_EC_CHIP_HOST_REBOOT_H_
#define PLATFORM_EC_CHIP_HOST_REBOOT_H_

#if !(defined(CONFIG_ZTEST))
__noreturn
#endif
	void emulator_reboot(void);

#endif /* PLATFORM_EC_CHIP_HOST_REBOOT_H_ */
