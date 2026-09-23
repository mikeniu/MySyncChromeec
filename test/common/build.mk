# -*- makefile -*-
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Test-only common files

test_common-y += console.o
test_common-y += irq_locking.o
test_common-y += recursive_mutex.o
test_common-y += test_util.o
test_common-y += uart_buffering.o
