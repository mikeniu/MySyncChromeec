/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Atomic operations for ARMv7 */

#ifndef __CROS_EC_ATOMIC_H
#define __CROS_EC_ATOMIC_H

#include "common.h"

/**
 * Implements atomic arithmetic operations on 32-bit integers.
 *
 * It used load/store exclusive.
 * If you write directly the integer used as an atomic variable,
 * you must either clear explicitly the exclusive monitor (using clrex)
 * or do it in exception context (which clears the monitor).
 */
#define ATOMIC_OP(asm_op, a, v) do {				\
	uint32_t reg0, reg1;                                    \
								\
	__asm__ __volatile__("1: ldrex   %0, [%2]\n"            \
			     #asm_op" %0, %0, %3\n"		\
			     "   strex   %1, %0, [%2]\n"        \
			     "   teq     %1, #0\n"              \
			     "   bne     1b"                    \
			     : "=&r" (reg0), "=&r" (reg1)       \
			     : "r" (a), "r" (v) : "cc");        \
} while (0)

static inline void atomic_clear(uint32_t volatile *addr, uint32_t bits)
{
	ATOMIC_OP(bic, addr, bits);
}

static inline void atomic_or(uint32_t volatile *addr, uint32_t bits)
{
	ATOMIC_OP(orr, addr, bits);
}

static inline void atomic_add(uint32_t volatile *addr, uint32_t value)
{
	ATOMIC_OP(add, addr, value);
}

static inline void atomic_sub(uint32_t volatile *addr, uint32_t value)
{
	ATOMIC_OP(sub, addr, value);
}

static inline uint32_t atomic_read_clear(uint32_t volatile *addr)
{
	uint32_t ret, tmp;

	__asm__ __volatile__("   mov     %3, #0\n"
			     "1: ldrex   %0, [%2]\n"
			     "   strex   %1, %3, [%2]\n"
			     "   teq     %1, #0\n"
			     "   bne     1b"
			     : "=&r" (ret), "=&r" (tmp)
			     : "r" (addr), "r" (0) : "cc");

	return ret;
}

/**
 * Performs an atomic operation on the specified address. The mask to use
 * depends on whether the value stored at the address matches the value
 * at the address specified in if_value.
 *
 * Returns 1 if the value matched the condition and true_mask was used;
 * 0 otherwise.
 */
#define ATOMIC_CONDITIONAL_OP(asm_op) {				\
	uint32_t value;     /* scratch */			\
	uint32_t matched;					\
	uint32_t strex_ret;					\
								\
	do {							\
		__asm__ __volatile__(				\
		    "   ldrex   %[val], [%[addr]]\n"		\
		    "   teq     %[val], %[if_v]\n"		\
		    "   ittee   eq\n"				\
		    "   moveq   %[mtch], #1\n"			\
		    #asm_op"eq  %[val], %[t_m]\n"		\
		    "   movne   %[mtch], #0\n"			\
		    #asm_op"ne  %[val], %[f_m]\n"		\
		    "   strex   %[ret], %[val], [%[addr]]\n"	\
		    : [val]  "+&r" (value),			\
		      [mtch] "=&r" (matched),			\
		      [ret]  "=r"  (strex_ret)			\
		    : [addr] "r"   (addr),			\
		      [if_v] "r"   (if_value),			\
		      [t_m]  "r"   (true_mask),			\
		      [f_m]  "r"   (false_mask)			\
		    : "cc");					\
	} while (strex_ret);					\
								\
	return matched;						\
}

/**
 * Reads the value at addr and compares with if_value. Performs an
 * OR of the value at addr with either true_mask (if the original
 * value at addr matches if_value) or false_mask (if it did not).
 *
 * Returns 1 if true_mask was used, 0 if false_mask was used.
 */
static inline int atomic_cond_or(uint32_t volatile *addr,
				 uint32_t if_value,
				 uint32_t true_mask,
				 uint32_t false_mask) {
	ATOMIC_CONDITIONAL_OP(orr);
}

/**
 * Reads the value at addr and compares with if_value. Performs an
 * AND of the value at addr with either the complement of true_mask
 * (if the original value at addr matches if_value) or the complement
 * of false_mask (if it did not).
 *
 * Returns 1 if true_mask was used, 0 if false_mask was used.
 */
static inline int atomic_cond_clear(uint32_t volatile *addr,
				    uint32_t if_value,
				    uint32_t true_mask,
				    uint32_t false_mask) {
	ATOMIC_CONDITIONAL_OP(bic);
}

#endif  /* __CROS_EC_ATOMIC_H */
