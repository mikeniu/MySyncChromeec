/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <cstddef>
#include <exception>
#include <typeinfo>

#ifdef CONFIG_RISCV
/*
 * The riscv32-cros-elf libc++abi archive is built with exceptions disabled
 * and omits cxa_noexception.cpp, leaving __cxa_allocate_exception and
 * __cxa_throw undefined when exception_lib.cc is compiled with -fexceptions.
 * Provide the standard no-exceptions stubs that invoke std::terminate().
 */
extern "C" {
void *__cxa_allocate_exception(size_t thrown_size) noexcept
{
	std::terminate();
}

void __cxa_throw(void *thrown_exception, std::type_info *tinfo,
		 void (*dest)(void *))
{
	std::terminate();
}
}
#endif

void exception_lib_throw(void)
{
	throw std::exception();
}
