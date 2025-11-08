/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Handy clever tricks */

#ifndef __CROS_EC_COMPILE_TIME_MACROS_H
#define __CROS_EC_COMPILE_TIME_MACROS_H

#include <zephyr/sys/util.h>

#ifdef __cplusplus
#define _STATIC_ASSERT static_assert
#else
#define _STATIC_ASSERT _Static_assert
#endif

/* Test an important condition at compile time, not run time */
#define _BA1_(cond, file, line, msg) \
	_STATIC_ASSERT(cond, file ":" #line ": " msg)
#define _BA0_(c, f, l, msg) _BA1_(c, f, l, msg)
/* Pass in an option message to display after condition */

/*
 * Test an important condition inside code path at run time, taking advantage of
 * -Werror=div-by-zero.
 */
#define BUILD_CHECK_INLINE(value, cond_true) ((value) / (!!(cond_true)))

/* Check that the value is an array (not a pointer) */
#ifdef __cplusplus
#define _IS_ARRAY(arr) (std::is_array<decltype(arr)>::value)
#else
#define _IS_ARRAY(arr) \
	!__builtin_types_compatible_p(typeof(arr), typeof(&(arr)[0]))
#endif

/* Make for loops that iterate over pointers to array entries more readable */
#define ARRAY_BEGIN(array)                                                   \
	({                                                                   \
		BUILD_ASSERT(_IS_ARRAY(array),                               \
			     "ARRAY_BEGIN is only compatible with arrays."); \
		(array);                                                     \
	})
#define ARRAY_END(array) ((array) + ARRAY_SIZE(array))

/* Just in case - http://gcc.gnu.org/onlinedocs/gcc/Offsetof.html */
#ifndef offsetof
#define offsetof(type, member) __builtin_offsetof(type, member)
#endif

#define member_size(type, member) sizeof(((type *)0)->member)

/*
 * Bit operation macros.
 */
#define BIT_ULL(nr) (1ULL << (nr))

#endif /* __CROS_EC_COMPILE_TIME_MACROS_H */
