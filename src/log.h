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

#ifndef _XRAAS_LOG_H_
#define _XRAAS_LOG_H_

#include <stdarg.h>

#include "helpers.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * This lets us chop out the basename (last path component) from __FILE__
 * at compile time. This works on GCC and Clang. The fallback mechanism
 * below just chops it out at compile time.
 */
#if	defined(__GNUC__) || defined(__clang__)
#define	xraas_log_basename(f) (__builtin_strrchr(f, DIRSEP) ? \
	__builtin_strrchr(f, DIRSEP) + 1 : f)
#else	/* !__GNUC__ && !__clang__ */
const char *xraas_log_basename(const char *filename);
#endif	/* !__GNUC__ && !__clang__ */

#define	logMsg(...) \
	xraas_log(xraas_log_basename(__FILE__), __LINE__, __VA_ARGS__)
#define	dbg_log(...) \
	xraas_dbg_log(xraas_log_basename(__FILE__), __LINE__, __VA_ARGS__)

void xraas_log(const char *filename, int line, const char *fmt, ...)
    PRINTF_ATTR(3);
void xraas_log_v(const char *filename, int line, const char *fmt, va_list ap);
void xraas_log_backtrace(void);

extern int xraas_debug;
void xraas_dbg_log(const char *filename, int line, const char *name, int level,
    const char *fmt, ...) PRINTF_ATTR(5);

#ifdef __cplusplus
}
#endif

#endif /* _XRAAS_LOG_H_ */
