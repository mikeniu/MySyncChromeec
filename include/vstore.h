/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_INCLUDE_VSTORE_H_
#define PLATFORM_EC_INCLUDE_VSTORE_H_

#ifdef TEST_BUILD

/* Clear all vstore locks */
void vstore_clear_lock(void);

#endif /* TEST_BUILD */

#endif /* PLATFORM_EC_INCLUDE_VSTORE_H_ */
