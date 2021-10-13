# -*- makefile -*-
# Copyright 2020 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Intel ADL-P-RVP-ITE board-specific configuration
#

CHIP:=it83xx
CHIP_FAMILY:=it8xxx2
CHIP_VARIANT:=it81302bx_1024
BASEBOARD:=intelrvp

board-y=board.o
