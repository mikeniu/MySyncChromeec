/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* * Bare-metal C++ stubs for unit testing.
 * GTest and other C++ frameworks occasionally pull in standard library
 * headers like <streambuf> that expect missing symbols during bare-metal
 * linking. We explicitly instantiate them here.
 */

#include <ios>
#include <streambuf>

namespace std {
inline namespace __1 {

    // Explicitly instantiate seekoff to force the compiler to emit
    // the symbols for the linker, without providing a redefinition body.
    template basic_streambuf<char>::pos_type
    basic_streambuf<char>::seekoff(basic_streambuf<char>::off_type, ios_base::seekdir, ios_base::openmode);

    template basic_streambuf<wchar_t>::pos_type
    basic_streambuf<wchar_t>::seekoff(basic_streambuf<wchar_t>::off_type, ios_base::seekdir, ios_base::openmode);

}
}
