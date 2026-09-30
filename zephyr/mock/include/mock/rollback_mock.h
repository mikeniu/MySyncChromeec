/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @file
 * @brief Controls for the mock rollback block library
 */

#ifndef PLATFORM_EC_ZEPHYR_MOCK_INCLUDE_MOCK_ROLLBACK_MOCK_H_
#define PLATFORM_EC_ZEPHYR_MOCK_INCLUDE_MOCK_ROLLBACK_MOCK_H_

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

struct mock_ctrl_rollback {
	bool get_secret_fail;
};

#define MOCK_CTRL_DEFAULT_ROLLBACK        \
	(struct mock_ctrl_rollback)       \
	{                                 \
		.get_secret_fail = false, \
	}

extern struct mock_ctrl_rollback mock_ctrl_rollback;

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_EC_ZEPHYR_MOCK_INCLUDE_MOCK_ROLLBACK_MOCK_H_ */
