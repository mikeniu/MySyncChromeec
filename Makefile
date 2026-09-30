# Copyright 2011 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Embedded Controller firmware build system
#

BOARD ?= host

# Only host utilities are built with this Makefile. EC firmware is built with
# zmake.
ifneq ($(BOARD),host)
$(error Only BOARD=host is supported, use zmake to build EC firmware)
endif

PROJECT?=ec

# An empty string.
# "-DMACRO" leads to MACRO=1.  Define an empty string "-DMACRO=" to take
# advantage of IS_ENABLED magic macro, which only allows an empty string.
EMPTY=

# Output directory for build objects
out?=build/$(BOARD)

include Makefile.toolchain

# Define the traditional first target.
.PHONY: all
all:
	@echo "make all is deprecated"

CHIP:=host
CORE:=host

# Include paths.
includes=include include/driver $(out) third_party

# Object helper
objs_from_dir_p=$(foreach obj, $($(2)-$(3)), $(1)/$(obj))
objs_from_dir=$(call objs_from_dir_p,$(1),$(2),y)

# Get build configuration for host tools
include util/build.mk
include util/lock/build.mk

host-utils := $(call objs_from_dir,$(out)/util,host-util-bin)
host-utils-cxx := $(call objs_from_dir,$(out)/util,host-util-bin-cxx)
host-srcs := $(foreach u,$(host-util-bin-y),$(sort $($(u)-objs:%.o=util/%.c) \
               $(wildcard util/$(u).c)))
host-srcs-cxx := $(foreach u,$(host-util-bin-cxx-y), \
	$(sort $($(u)-objs:%.o=util/%.cc) $(wildcard util/$(u).cc)))

common_dirs=util

host-deps := $(addsuffix .d, $(host-utils) $(host-utils-cxx))

deps := $(host-deps)

include Makefile.rules
