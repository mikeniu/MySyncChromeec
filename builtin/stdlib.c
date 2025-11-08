/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Standard library utility functions for Chrome EC */

#include "common.h"
#include "console.h"
#include "printf.h"
#include "util.h"

#include <stdio.h>

/*
 * The following macros are defined in stdlib.h in the C standard library, which
 * conflict with the definitions in this file.
 */
#undef isspace
#undef isdigit
#undef isalpha
#undef isupper
#undef isprint
#undef tolower

/* Context for snprintf() */
struct snprintf_context {
	char *str;
	int size;
};

/**
 * Add a character to the string context.
 *
 * @param context	Context receiving character
 * @param c		Character to add
 * @return 0 if character added, 1 if character dropped because no space.
 */
static int snprintf_addchar(void *context, int c)
{
	struct snprintf_context *ctx = (struct snprintf_context *)context;

	if (!ctx->size)
		return 1;

	*(ctx->str++) = c;
	ctx->size--;
	return 0;
}

int crec_vsnprintf(char *str, size_t size, const char *format, va_list args)
{
	struct snprintf_context ctx;
	int rv;

	if (!str || !format || size <= 0)
		return -EC_ERROR_INVAL;

	ctx.str = str;
	ctx.size = size - 1; /* Reserve space for terminating '\0' */

	rv = vfnprintf(snprintf_addchar, &ctx, format, args);

	/* Terminate string */
	*ctx.str = '\0';

	return (rv == EC_SUCCESS) ? (ctx.str - str) : -rv;
}

int crec_snprintf(char *str, size_t size, const char *format, ...)
{
	va_list args;
	int rv;

	va_start(args, format);
	rv = crec_vsnprintf(str, size, format, args);
	va_end(args);

	return rv;
}

/*
 * TODO(b/237712836): Zephyr's libc should provide strcasecmp. For now we'll
 * use the EC implementation.
 */
__stdlib_compat int strcasecmp(const char *s1, const char *s2)
{
	int diff;

	do {
		diff = tolower(*s1) - tolower(*s2);
		if (diff)
			return diff;
	} while (*(s1++) && *(s2++));
	return 0;
}
