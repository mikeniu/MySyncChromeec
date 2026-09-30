# Copyright 2011 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Embedded Controller firmware build system
#

# Allow for masking of some targets based on the build architecture. When
# building using a portage package (such as chromeos-ec), this variable will
# already be set. To support the typical developer workflow a default value is
# provided matching the typical architecture of developer workstations. Note
# that amd64 represents the entire x84_64 architecture including intel CPUs.
# This is used to exclude build targets that depend on sanitizers on
# architectures that don't support sanitizers yet (e.g. arm).
ARCH?=amd64
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

# File containing configuration information
config=$(out)/.config

include Makefile.toolchain

# Define the traditional first target.
.PHONY: all
all:
	@echo "make all is deprecated"

# Run the given shell command and capture the output, but echo the command
# itself, if V is not 0 or empty.
# Usage: $(call shell_echo,<shell-command>)
shell_echo = $(if $(filter-out 0,$(V)),$(info $(1)))$(shell $(1))

CHIP:=host
CORE:=host
CROSS_COMPILE_HOST_DEFAULT:=x86_64-pc-linux-gnu-
CROSS_COMPILE_CC_NAME?=clang
$(call set-option,CROSS_COMPILE,$(CROSS_COMPILE_host),\
	$(CROSS_COMPILE_HOST_DEFAULT))
CFLAGS_CPU=-fno-builtin

# Include paths.
includes=include include/driver $(out) third_party

# Object helper
objs_from_dir_p=$(foreach obj, $($(2)-$(3)), $(1)/$(obj))
objs_from_dir=$(call objs_from_dir_p,$(1),$(2),y)

# Get build configuration for host tools
include util/build.mk
include util/lock/build.mk

# Collect all includes.
includes+=$(includes-y)

host-utils := $(call objs_from_dir,$(out)/util,host-util-bin)
host-utils-cxx := $(call objs_from_dir,$(out)/util,host-util-bin-cxx)
build-srcs := $(foreach u,$(build-util-bin-y),$(sort $($(u)-objs:%.o=util/%.c) \
                $(wildcard util/$(u).c)))
host-srcs := $(foreach u,$(host-util-bin-y),$(sort $($(u)-objs:%.o=util/%.c) \
               $(wildcard util/$(u).c)))
host-srcs-cxx := $(foreach u,$(host-util-bin-cxx-y), \
	$(sort $($(u)-objs:%.o=util/%.cc) $(wildcard util/$(u).cc)))

common_dirs=util

host-deps := $(addsuffix .d, $(host-utils) $(host-utils-cxx))

deps := $(host-deps) $(deps-y)

include Makefile.rules
export CROSS_COMPILE CFLAGS CC CPP LD NM AR OBJCOPY OBJDUMP
