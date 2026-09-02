/* Copyright 2018 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef INCLUDE_HOST_COMMAND_HECI_H_
#define INCLUDE_HOST_COMMAND_HECI_H_

/* send an event message to the ap */
int heci_send_mkbp_event(uint32_t *timestamp);

#endif /* INCLUDE_HOST_COMMAND_HECI_H_ */
