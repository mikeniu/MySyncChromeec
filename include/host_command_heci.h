/* Copyright 2018 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_INCLUDE_HOST_COMMAND_HECI_H_
#define PLATFORM_EC_INCLUDE_HOST_COMMAND_HECI_H_

/* send an event message to the ap */
int heci_send_mkbp_event(uint32_t *timestamp);

#endif /* PLATFORM_EC_INCLUDE_HOST_COMMAND_HECI_H_ */
