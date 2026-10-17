/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Empty registers header for emulator */

/*
 * There is no register for emulator, but this file exists to prevent
 * compilation failure if any file includes registers.h
 */

#ifndef __CROS_EC_REGISTERS_H
#define __CROS_EC_REGISTERS_H

#include "common.h"
#include "util.h"

#define GNAME(mname, rname)  "GC_" #mname "_" #rname "_NAME"
#define GREG32(mname, rname)	REG32(get_reg_addr(GNAME(mname, rname)))
#define GWRITE(mname, rname, value) (GREG32(mname, rname) = (value))
#define GREAD(mname, rname)	    GREG32(mname, rname)

#define GFIELD_MASK(mname, rname, fname) \
	GC_ ## mname ## _ ## rname ## _ ## fname ## _MASK

#define GFIELD_LSB(mname, rname, fname)  \
	GC_ ## mname ## _ ## rname ## _ ## fname ## _LSB

#define GREAD_FIELD(mname, rname, fname) \
	((GREG32(mname, rname) & GFIELD_MASK(mname, rname, fname)) \
		>> GFIELD_LSB(mname, rname, fname))

#define GWRITE_FIELD(mname, rname, fname, fval)                                \
	do {                                                                   \
		volatile uint32_t *__reg = &GREG32(mname, rname);              \
		*__reg = (*__reg & (~GFIELD_MASK(mname, rname, fname))) |      \
			 ((((uint32_t)(fval)) <<                               \
			   GFIELD_LSB(mname, rname, fname)) &                  \
			  GFIELD_MASK(mname, rname, fname));                   \
	} while (0)

void *get_reg_addr(const char * const reg_name);

#endif /* __CROS_EC_REGISTERS_H */
