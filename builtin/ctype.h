/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef BUILTIN_CTYPE_H_
#define BUILTIN_CTYPE_H_

int isdigit(int c);
int isspace(int c);
int isalpha(int c);
int isupper(int c);
int isprint(int c);
int tolower(int c);

#endif /* BUILTIN_CTYPE_H_ */
