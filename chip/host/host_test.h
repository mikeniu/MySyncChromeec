/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Unit testing for Chrome EC */

#ifndef PLATFORM_EC_CHIP_HOST_HOST_TEST_H_
#define PLATFORM_EC_CHIP_HOST_HOST_TEST_H_

/* Emulator exit codes */
#define EXIT_CODE_HIBERNATE BIT(7)

/* Get emulator executable name */
const char *__get_prog_name(void);

#endif /* PLATFORM_EC_CHIP_HOST_HOST_TEST_H_ */
