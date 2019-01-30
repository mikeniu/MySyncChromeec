# -*- makefile -*-
# Copyright 2018 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Board specific files build
#
CHIP:=mt_scp
CHIP_VARIANT:=mt8183

board-y=board.o
board-y+=dram_test.o

# TODO: Find a better way to do this, if this is what we want.
# (it'd be easier to do if we just create an archive, for example)
$(out)/RW/board/kukui_scp/dram_test.o: board/kukui_scp/dram_test.c
	$(call quiet,c_to_o,CC     )
	$(OBJCOPY) --prefix-alloc-sections=.dram \
		$(out)/RW/board/kukui_scp/dram_test.o \
		$(out)/RW/board/kukui_scp/dram_test.o.new
	mv $(out)/RW/board/kukui_scp/dram_test.o.new \
		$(out)/RW/board/kukui_scp/dram_test.o
