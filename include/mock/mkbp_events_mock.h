/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @file
 * @brief Controls for the mock MKBP keyboard protocol
 */

#ifndef PLATFORM_EC_INCLUDE_MOCK_MKBP_EVENTS_MOCK_H_
#define PLATFORM_EC_INCLUDE_MOCK_MKBP_EVENTS_MOCK_H_

struct mock_ctrl_mkbp_events {
	int mkbp_send_event_return;
};

#define MOCK_CTRL_DEFAULT_MKBP_EVENTS        \
	(struct mock_ctrl_mkbp_events)       \
	{                                    \
		.mkbp_send_event_return = 1, \
	}

extern struct mock_ctrl_mkbp_events mock_ctrl_mkbp_events;

#endif /* PLATFORM_EC_INCLUDE_MOCK_MKBP_EVENTS_MOCK_H_ */
