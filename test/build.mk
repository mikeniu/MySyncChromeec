# -*- makefile -*-
# Copyright 2013 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Device test binaries
test-list-y ?= stdlib

# Emulator tests
ifneq ($(TEST_LIST_HOST),)
test-list-host=$(TEST_LIST_HOST)
else
test-list-host =


# TODO(b/237823627): When building for the host, we're linking against the
# toolchain's C standard library, so these tests are actually testing the
# toolchain's C standard library.
endif
