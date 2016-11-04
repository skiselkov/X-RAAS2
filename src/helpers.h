/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
 *
 * You can obtain a copy of the license in the file COPYING
 * or http://www.opensource.org/licenses/CDDL-1.0.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file COPYING.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2015 Saso Kiselkov. All rights reserved.
 */

#ifndef	_XRAAS_HELPERS_H_
#define	_XRAAS_HELPERS_H_

#include <stdarg.h>
#include <stdio.h>
#include <sys/types.h>

#include "types.h"

#ifdef	__cplusplus
extern "C" {
#endif

#define	BUILD_DIRSEP	'/'	/* we only build on Unix-like OSes */
#if	IBM
#define	DIRSEP '\\'
#else	/* !IBM */
#define	DIRSEP '/'
#endif	/* !IBM */

#if	defined(__GNUC__) || defined(__clang__)
#define	PRINTF_ATTR(x)	__attribute__ ((format (printf, x, x + 1)))
#ifndef	BSWAP32
#define	BSWAP16(x)	__builtin_bswap16((x))
#define	BSWAP32(x)	__builtin_bswap32((x))
#define	BSWAP64(x)	__builtin_bswap64((x))
#endif	/* BSWAP32 */
#else	/* !__GNUC__ && !__clang__ */
#define	PRINTF_ATTR(x)
#ifndef	BSWAP32
#define	BSWAP16(x)	\
	((((x) & 0xff00) >> 8) | \
	(((x) & 0x00ff) << 8))
#define	BSWAP32(x)	\
	((((x) & 0xff000000) >> 24) | \
	(((x) & 0x00ff0000) >> 8) | \
	(((x) & 0x0000ff00) << 8) | \
	(((x) & 0x000000ff) << 24))
#define	BSWAP64(x)	\
	((((x) & 0x00000000000000ffllu) >> 56) | \
	(((x) & 0x000000000000ff00llu) << 40) | \
	(((x) & 0x0000000000ff0000llu) << 24) | \
	(((x) & 0x00000000ff000000llu) << 8) | \
	(((x) & 0x000000ff00000000llu) >> 8) | \
	(((x) & 0x0000ff0000000000llu) >> 24) | \
	(((x) & 0x00ff000000000000llu) >> 40) | \
	(((x) & 0xff00000000000000llu) << 56))
#endif	/* BSWAP32 */
#endif	/* !__GNUC__ && !__clang__ */

#ifdef	WINDOWS
#define	PATHSEP	"\\"
#else
#define	PATHSEP	"/"
#endif

/* Minimum/Maximum allowable elevation AMSL of anything */
#define	MIN_ELEV	-2000.0
#define	MAX_ELEV	30000.0

/* Minimum/Maximum allowable altitude AMSL of anything */
#define	MIN_ALT		-2000.0
#define	MAX_ALT		100000.0

/* Maximum valid speed of anything */
#define	MAX_SPD		1000.0

/* Minimum/Maximum allowable arc radius on any procedure */
#define	MIN_ARC_RADIUS	0.1
#define	MAX_ARC_RADIUS	100.0

#define	UNUSED_ATTR		__attribute__((unused))
#define	UNUSED(x)		(void)(x)

/*
 * Compile-time assertion. The condition 'x' must be constant.
 */
#define	CTASSERT(x)		_CTASSERT(x, __LINE__)
#define	_CTASSERT(x, y)		__CTASSERT(x, y)
#define	__CTASSERT(x, y)	\
	typedef char __compile_time_assertion__ ## y [(x) ? 1 : -1]

/* generic parser validator helpers */

static inline bool_t
is_valid_lat(double lat)
{
	return (lat <= 90.0 && lat >= -90.0);
}

static inline bool_t
is_valid_lon(double lon)
{
	return (lon <= 180.0 && lon >= -180.0);
}

static inline bool_t
is_valid_elev(double elev)
{
	return (elev >= MIN_ELEV && elev <= MAX_ELEV);
}

static inline bool_t
is_valid_alt(double alt)
{
	return (alt >= MIN_ALT && alt <= MAX_ALT);
}

static inline bool_t
is_valid_spd(double spd)
{
	return (spd >= 0.0 && spd <= MAX_SPD);
}

static inline bool_t
is_valid_hdg(double hdg)
{
	return (hdg >= 0.0 && hdg <= 360.0);
}

double rel_hdg(double hdg1, double hdg2);

static inline bool_t
is_valid_arc_radius(double radius)
{
	return (radius >= MIN_ARC_RADIUS && radius <= MAX_ARC_RADIUS);
}

static inline bool_t
is_valid_bool(bool_t b)
{
	return (b == B_FALSE || b == B_TRUE);
}

bool_t is_valid_vor_freq(double freq_mhz);
bool_t is_valid_loc_freq(double freq_mhz);
bool_t is_valid_ndb_freq(double freq_khz);
bool_t is_valid_tacan_freq(double freq_mhz);
bool_t is_valid_rwy_ID(const char *rwy_ID);

/* CSV file & string processing helpers */
ssize_t parser_get_next_line(FILE *fp, char **linep, size_t *linecap,
    size_t *linenum);
char **strsplit(const char *input, char *sep, bool_t skip_empty, size_t *num);
void free_strlist(char **comps, size_t len);
void strip_space(char *line);
void append_format(char **str, size_t *sz, const char *format, ...)
    PRINTF_ATTR(3);

char *mkpathname(const char *comp, ...);
void fix_pathsep(char *str);

void my_strlcpy(char *restrict dest, const char *restrict src, size_t cap);
#if     IBM
ssize_t getline(char **lineptr, size_t *n, FILE *stream);
#endif  /* IBM */

#if	defined(__GNUC__) || defined(__clang__)
#define	highbit64(x)	(64 - __builtin_clzll(x) - 1)
#define	highbit32(x)	(32 - __builtin_clzll(x) - 1)
#else
#error	"Compiler platform unsupported, please add highbit definition"
#endif

/*
 * return x rounded up to the nearest power-of-2.
 */
#define	P2ROUNDUP(x)	(-(-(x) & -(1 << highbit64(x))))
#if	!defined(MIN) && !defined(MAX) && !defined(AVG)
#define	MIN(x, y)	((x) < (y) ? (x) : (y))
#define	MAX(x, y)	((x) > (y) ? (x) : (y))
#define	AVG(x, y)	(((x) + (y)) / 2)
#endif	/* MIN or MAX */

long long microclock(void);
#define	USEC2SEC(usec)	(usec / 1000000ll)
#define	SEC2USEC(sec)	(sec * 1000000ll)

/* directory manipulation */
bool_t create_directory(const char *dirname);
bool_t remove_directory(const char *dirname);

#ifdef	__cplusplus
}
#endif

#endif	/* _XRAAS_HELPERS_H_ */
