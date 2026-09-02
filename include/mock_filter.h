/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Filter mocklists for makefile relevant items.
 * A mocklist is the .mocklist file in test/ directory
 * See test/mock/README.md for more information.
 */

#ifndef PLATFORM_EC_INCLUDE_MOCK_FILTER_H_
#define PLATFORM_EC_INCLUDE_MOCK_FILTER_H_

/* If included directly from Makefile, dump mock list. */
#ifdef _MAKEFILE
#define MOCK(n) n
CONFIG_TEST_MOCK_LIST
#endif

#endif /* PLATFORM_EC_INCLUDE_MOCK_FILTER_H_ */
