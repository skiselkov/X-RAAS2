/*
 * CDDL HEADER START
 *
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 *
 * CDDL HEADER END
*/
/*
 * Copyright 2016 Saso Kiselkov. All rights reserved.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#if	IBM
#include <windows.h>
#include <dbghelp.h>
#else
#include <execinfo.h>   /* used for stack tracing */
#endif	/* !IBM */

#include "XPLMUtilities.h"

#include "assert.h"
#include "helpers.h"
#include "log.h"

#define	PREFIX		"X-RAAS"
#if	IBM
#define	PREFIX_FMT	"%s[%s:%d]: ", PREFIX, strrchr(filename, '\\') ? \
	strrchr(filename, '\\') + 1 : filename, line
#else	/* !IBM */
#define	PREFIX_FMT	"%s[%s:%d]: ", PREFIX, filename, line
#endif	/* !IBM */

int xraas_debug = 0;

void
log_impl(const char *filename, int line, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	log_impl_v(filename, line, fmt, ap);
	va_end(ap);
}

void
log_impl_v(const char *filename, int line, const char *fmt, va_list ap)
{
	va_list ap_copy;
	char *buf;
	size_t prefix_len, len;

	prefix_len = snprintf(NULL, 0, PREFIX_FMT);
	va_copy(ap_copy, ap);
	len = vsnprintf(NULL, 0, fmt, ap_copy);

	buf = (char *)malloc(prefix_len + len + 2);

	(void) snprintf(buf, prefix_len + 1, PREFIX_FMT);
	(void) vsnprintf(&buf[prefix_len], len + 1, fmt, ap);
	(void) sprintf(&buf[strlen(buf)], "\n");

	XPLMDebugString(buf);

	free(buf);
}

#define	MAX_STACK_FRAMES	128
#define	BACKTRACE_STR		"Backtrace is:\n"
#if	defined(__GNUC__) || defined(__clang__)
#define	BACKTRACE_STRLEN	__builtin_strlen(BACKTRACE_STR)
#else	/* !__GNUC__ && !__clang__ */
#define	BACKTRACE_STRLEN	strlen(BACKTRACE_STR)
#endif	/* !__GNUC__ && !__clang__ */

void
log_backtrace(void)
{
#if	IBM
#define	MAX_SYM_NAME_LEN	256
#define	FRAME_FMT		"%u: %s - 0x%x\n"
	unsigned	 frames;
	void *stack[MAX_STACK_FRAMES];
	SYMBOL_INFO *symbol;
	HANDLE process;
	char *msg = NULL;
	size_t msg_len = BACKTRACE_STRLEN;

	process = GetCurrentProcess();

	SymInitialize(process, NULL, TRUE);

	frames = CaptureStackBackTrace(0, MAX_STACK_FRAMES, stack, NULL);
	symbol = malloc(sizeof(SYMBOL_INFO) + MAX_SYM_NAME_LEN);
	symbol->MaxNameLen = MAX_SYM_NAME_LEN - 1;
	symbol->SizeOfStruct = sizeof(SYMBOL_INFO);

	msg = malloc(msg_len + 1);
	my_strlcpy(msg, BACKTRACE_STR, msg_len);

	for(unsigned i = 0; i < frames; i++) {
		int line_len;

		memset(symbol, 0, sizeof(SYMBOL_INFO) + MAX_SYM_NAME_LEN);
		/*
		 * This is needed because some dunce at Microsoft thought
		 * it'd be a swell idea to design the SymFromAddr function to
		 * always take a DWORD64 rather than a native pointer size.
		 */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpointer-to-int-cast"
		SymFromAddr(process, (DWORD64)(stack[i]), 0, symbol);
#pragma GCC diagnostic pop
		line_len = snprintf(NULL, 0, FRAME_FMT, i, symbol->Name,
		    symbol->Address);
		msg = realloc(msg, msg_len + line_len + 1);
		VERIFY(snprintf(&msg[msg_len], line_len + 1, FRAME_FMT, i,
		    symbol->Name, symbol->Address) == line_len);
		msg_len += line_len;
	}

	XPLMDebugString(msg);
	fputs(msg, stderr);

	free(symbol);
#else	/* !IBM */
	char *msg;
	size_t msg_len;
	void *trace[MAX_STACK_FRAMES];
	size_t i, j, sz;
	char **fnames;

	sz = backtrace(trace, MAX_STACK_FRAMES);
	fnames = backtrace_symbols(trace, sz);

	for (i = 1, msg_len = BACKTRACE_STRLEN; i < sz; i++)
		msg_len += snprintf(NULL, 0, "%s\n", fnames[i]);

	msg = (char *)malloc(msg_len + 1);
	strcpy(msg, BACKTRACE_STR);
	for (i = 1, j = BACKTRACE_STRLEN; i < sz; i++)
		j += sprintf(&msg[j], "%s\n", fnames[i]);

	XPLMDebugString(msg);
	fputs(msg, stderr);

	free(msg);
	free(fnames);
#endif	/* !IBM */
}

void
dbg_log_impl(const char *filename, int line, const char *name, int level,
    const char *fmt, ...)
{
	va_list ap;
	UNUSED(name);
	va_start(ap, fmt);
	if (level < xraas_debug)
		log_impl_v(filename, line, fmt, ap);
	va_end(ap);
}
