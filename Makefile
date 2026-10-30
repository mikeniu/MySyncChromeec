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

ifneq ($(PREPARE_WORKSPACE),)
ifeq ($(MAKELEVEL),0)
_prep_cmd := python3 zephyr/zmake/zmake/repository_prep.py --allow-dirty \
	$(foreach d,$(PATCHES_DIRS),--patches-dir "$(d)") \
	$(foreach d,$(SRC_OVERRIDE_DIRS),--src-override-dir "$(d)")
_prep_status := $(shell $(_prep_cmd) >&2; echo $$?)
ifneq ($(_prep_status),0)
$(error Codebase preparation failed)
endif
endif
endif

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

# Get build configuration for host tools
include util/build.mk
include util/lock/build.mk

# Collect all includes.
includes+=$(includes-y)

# The following variables are meant to be initialized in the
# board's build.mk. They will later be appended to in util/build.mk with
# utils that should be generated for all boards.
#
# host-util-bin-y  - Utils for the target platform on top of the EC.
#                    For example, the 32-bit x86 Chromebook.
#
# The util targets added to these variable will pickup extra build objects
# from their optional <util_name>-objs make variable.
#
# See commit bc4c1b4 for more context.
host-utils := $(addprefix $(out)/util/,$(host-util-bin-y))
host-utils-cxx := $(addprefix $(out)/util/,$(host-util-bin-cxx-y))
# Use the util_name with an added .c/.cc AND the special <util_name>-objs
# variable to compute per-utility source dependencies.
$(foreach u,$(host-util-bin-y),$(eval $(u)-srcs := \
	$(sort $($(u)-objs:%.o=util/%.c) $(wildcard util/$(u).c))))
$(foreach u,$(host-util-bin-cxx-y),$(eval $(u)-srcs := \
	$(sort $($(u)-objs:%.o=util/%.cc) $(wildcard util/$(u).cc))))

common_dirs=util

host-deps := $(addsuffix .d, $(host-utils) $(host-utils-cxx))

deps := $(host-deps)

include Makefile.rules
