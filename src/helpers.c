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

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <ctype.h>

#if	IBM
#include <windows.h>
#include <strsafe.h>
#else	/* !IBM */
#include <sys/types.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#endif	/* !IBM */

#include "assert.h"
#include "helpers.h"
#include "log.h"

/*
 * How to turn to get from hdg1 to hdg2 with positive being right and negative
 * being left. Always turns the shortest way around (<= 180 degrees).
 */
double
rel_hdg(double hdg1, double hdg2)
{
	ASSERT(is_valid_hdg(hdg1) && is_valid_hdg(hdg2));
	if (hdg1 > hdg2) {
		if (hdg1 > hdg2 + 180)
			return (360 - hdg1 + hdg2);
		else
			return (-(hdg1 - hdg2));
	} else {
		if (hdg2 > hdg1 + 180)
			return (-(360 - hdg2 + hdg1));
		else
			return (hdg2 - hdg1);
	}
}

bool_t
is_valid_vor_freq(double freq_mhz)
{
	unsigned freq_khz = freq_mhz * 1000;

	/* Check correct frequency band */
	if (freq_khz < 108000 || freq_khz > 117950)
		return (0);
	/*
	 * Check the LOC band - freq must be multiple of 200 kHz or
	 * remainder must be 50 kHz.
	 */
	if (freq_khz >= 108000 && freq_khz <= 112000 &&
	    freq_khz % 200 != 0 && freq_khz % 200 != 50)
		return (0);
	/* Above 112 MHz, frequency must be multiple of 50 kHz */
	if (freq_khz % 50 != 0)
		return (0);

	return (1);
}

bool_t
is_valid_loc_freq(double freq_mhz)
{
	unsigned freq_khz = freq_mhz * 1000;

	/* Check correct frequency band */
	if (freq_khz < 108100 || freq_khz > 111950)
		return (0);
	/* Check 200 kHz spacing with 100 kHz or 150 kHz remainder. */
	if (freq_khz % 200 != 100 && freq_khz % 200 != 150)
		return (0);

	return (1);
}

bool_t
is_valid_tacan_freq(double freq_mhz)
{
	unsigned freq_khz = freq_mhz * 1000;

	/* this is quite a guess! */
	if (freq_khz < 133000 || freq_khz > 136000 ||
	    freq_khz % 100 != 0)
		return (0);
	return (1);
}

bool_t
is_valid_ndb_freq(double freq_khz)
{
	unsigned freq_hz = freq_khz * 1000;
	/* 177 kHz for an NDB is the lowest I've ever seen */
	return (freq_hz >= 177000 && freq_hz <= 1750000);
}

/*
 * Checks a runway ID for correct formatting. Runway IDs are
 * 2-, 3- or 4-character strings conforming to the following format:
 *	*) Two-digit runway heading between 01 and 36. Headings less
 *	   than 10 are always prefixed by a '0'. "00" is NOT valid.
 *	*) An optional parallel runway discriminator, one of 'L', 'R' or 'C'.
 */
bool_t
is_valid_rwy_ID(const char *rwy_ID)
{
	int hdg;
	int len = strlen(rwy_ID);

	if (len < 2 || len > 3 || !isdigit(rwy_ID[0]) || !isdigit(rwy_ID[1]))
		return (B_FALSE);
	hdg = atoi(rwy_ID);
	if (hdg == 0 || hdg > 36)
		return (B_FALSE);
	if (len == 3) {
		if (rwy_ID[2] != 'R' && rwy_ID[2] != 'L' && rwy_ID[2] != 'C')
			return (B_FALSE);
	}

	return (B_TRUE);
}

/*
 * Grabs the next non-empty, non-comment line from a file, having stripped
 * away all leading and trailing whitespace.
 *
 * @param fp File from which to retrieve the line.
 * @param linep Line buffer which will hold the new line. If the buffer pointer
 *	is set to NULL, it will be allocated. If it is not long enough, it
 *	will be expanded.
 * @param linecap The capacity of *linep. If set to zero a new buffer is
 *	allocated.
 * @param linenum The current line number. Will be advanced by 1 for each
 *	new line read.
 *
 * @return The number of characters in the line (after stripping whitespace)
 *	without the terminating NUL.
 */
ssize_t
parser_get_next_line(FILE *fp, char **linep, size_t *linecap, size_t *linenum)
{
	for (;;) {
		ssize_t len = getline(linep, linecap, fp);
		if (len == -1)
			return (-1);
		(*linenum)++;
		strip_space(*linep);
		if (**linep != 0 && **linep == '#')
			continue;
		return (strlen(*linep));
	}
}

char **
strsplit(const char *input, char *sep, bool_t skip_empty, size_t *num)
{
	char **result;
	size_t i = 0, n = 0;
	size_t seplen = strlen(sep);

	for (const char *a = input, *b = strstr(a, sep);
	    a != NULL; a = b + seplen, b = strstr(a, sep)) {
		if (b == NULL) {
			b = input + strlen(input);
			if (a != b || !skip_empty)
				n++;
			break;
		}
		if (a == b && skip_empty)
			continue;
		n++;
	}

	result = calloc(n, sizeof (char *));

	for (const char *a = input, *b = strstr(a, sep);
	    a != NULL; a = b + seplen, b = strstr(a, sep)) {
		if (b == NULL) {
			b = input + strlen(input);
			if (a != b || !skip_empty) {
				result[i] = calloc(b - a + 1, sizeof (char));
				memcpy(result[i], a, b - a);
			}
			break;
		}
		if (a == b && skip_empty)
			continue;
		ASSERT(i < n);
		result[i] = calloc(b - a + 1, sizeof (char));
		memcpy(result[i], a, b - a);
		i++;
	}

	if (num != NULL)
		*num = n;

	return (result);
}

/*
 * Frees a string array returned by strsplit.
 */
void
free_strlist(char **comps, size_t len)
{
	if (comps == NULL)
		return;
	for (size_t i = 0; i < len; i++)
		free(comps[i]);
	free(comps);
}

/*
 * Removes all leading & trailing whitespace from a line.
 */
void
strip_space(char *line)
{
	char	*p;
	size_t	len = strlen(line);

	/* strip leading whitespace */
	for (p = line; *p && isspace(*p); p++)
		;
	if (p != line)
		memmove(line, p, (p + len) - p + 1);

	for (p = line + len - 1; p >= line && isspace(*p); p--)
		;
	p[1] = 0;
}

void
append_format(char **str, size_t *sz, const char *format, ...)
{
	va_list ap;
	int needed;

	va_start(ap, format);
	needed = vsnprintf(NULL, 0, format, ap);
	va_end(ap);

	va_start(ap, format);
	*str = realloc(*str, *sz + needed + 1);
	(void) vsnprintf(*str + *sz, *sz + needed + 1, format, ap);
	*sz += needed;
	va_end(ap);
}

void
my_strlcpy(char *restrict dest, const char *restrict src, size_t cap)
{
        dest[cap - 1] = '\0';
        strncpy(dest, src, cap - 1);
}

#if     IBM

/*
 * C getline is a POSIX function, so on Windows, we need to roll our own.
 */
size_t
getline(char **lineptr, size_t *n, FILE *stream)
{
	char *bufptr = NULL;
	char *p = bufptr;
	size_t size;
	int c;

	if (lineptr == NULL)
		return (-1);

	if (stream == NULL)
		return (-1);

	if (n == NULL)
		return (-1);

	bufptr = *lineptr;
	size = *n;

	c = fgetc(stream);
	if (c == EOF)
		return (-1);

	if (bufptr == NULL)
		bufptr = malloc(128);
	if (bufptr == NULL)
		return (-1);

	size = 128;

	p = bufptr;
	while(c != EOF) {
		if ((void *)(p - bufptr) > (void *)(size - 1)) {
			size = size + 128;
			bufptr = realloc(bufptr, size);
			if (bufptr == NULL)
				return (-1);
		}
		*p++ = c;
		if (c == '\n')
			break;
		c = fgetc(stream);
	}

	*p++ = '\0';
	*lineptr = bufptr;
	*n = size;

	return (p - bufptr - 1);
}

#endif  /* IBM */

char *
mkpathname(const char *comp, ...)
{
	size_t n = 0, len = 0;
	char *str;
	va_list ap;

	if (comp == NULL)
		return (strdup(""));

	va_start(ap, comp);
	len = strlen(comp);
	for (const char *c = va_arg(ap, const char *); c != NULL;
	    c = va_arg(ap, const char *)) {
		len += 1 + strlen(c);
	}
	va_end(ap);

	str = malloc(len + 1);
	va_start(ap, comp);
	n += snprintf(str, len + 1, "%s", comp);
	for (const char *c = va_arg(ap, const char *); c != NULL;
	    c = va_arg(ap, const char *)) {
		ASSERT(n < len);
		n += snprintf(&str[n], len - n + 1, "%c%s", DIRSEP, c);
	}
	va_end(ap);

	return (str);
}

long long
microclock(void)
{
#if	IBM
	LARGE_INTEGER val, freq;
	QueryPerformanceFrequency(&freq);
	QueryPerformanceCounter(&val);
	return ((val.QuadPart * 1000000ll) / freq.QuadPart);
#else	/* !IBM */
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return ((tv.tv_sec * 1000000ll) + tv.tv_usec);
#endif	/* !IBM */
}

#if	IBM

static void win_perror(DWORD err, const char *fmt, ...) PRINTF_ATTR(2);

static void
win_perror(DWORD err, const char *fmt, ...)
{
	va_list ap;
	LPSTR win_msg = NULL;
	char *caller_msg;
	int len;

	va_start(ap, fmt);
	len = vsnprintf(NULL, 0, fmt, ap);
	va_end(ap);
	caller_msg = malloc(len + 1);
	va_start(ap, fmt);
	vsnprintf(caller_msg, len + 1, fmt, ap);
	va_end(ap);

	(void) FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER |
	    FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
	    NULL, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
	    (LPSTR)&win_msg, 0, NULL);
	logMsg("%s: %s", caller_msg, win_msg);

	free(caller_msg);
	LocalFree(win_msg);
}

#endif

/*
 * Creates an empty directory at `dirname' with default permissions.
 */
bool_t
create_directory(const char *dirname)
{
	ASSERT(dirname != NULL);
#if	IBM
	DWORD err;
	int len = strlen(dirname);
	WCHAR dirnameW[len + 1];

	MultiByteToWideChar(CP_UTF8, 0, dirname, -1, dirnameW, len + 1);
	if (!CreateDirectory(dirnameW, NULL) &&
	    (err = GetLastError()) != ERROR_ALREADY_EXISTS) {
		win_perror(err, "Error creating directory %s", dirname);
		return (B_FALSE);
	}
#else	/* !IBM */
	if (mkdir(dirname, 0777) != 0 && errno != EEXIST) {
		logMsg("Error creating directory %s: %s", dirname,
		    strerror(errno));
		return (B_FALSE);
	}
#endif	/* !IBM */
	return (B_TRUE);
}

#if	IBM

static bool_t
win_rmdir(const LPTSTR dirnameT)
{
	WIN32_FIND_DATA find_data;
	HANDLE h_find;
	int dirname_len = wcslen(dirnameT);
	TCHAR srchnameT[dirname_len + 4];

	StringCchPrintf(srchnameT, dirname_len, TEXT("%s\\*"), dirnameT);
	h_find = FindFirstFile(srchnameT, &find_data);
	do {
		TCHAR filepathT[MAX_PATH];
		DWORD attrs;

		if (wcscmp(find_data.cFileName, TEXT(".")) == 0 ||
		    wcscmp(find_data.cFileName, TEXT("..")) == 0)
			continue;

		StringCchPrintf(filepathT, MAX_PATH, TEXT("%s\\%s"), dirnameT,
		    find_data.cFileName);
		attrs = GetFileAttributes(filepathT);

		if (attrs & FILE_ATTRIBUTE_DIRECTORY) {
			if (!win_rmdir(filepathT))
				goto errout;
		} else {
			if (!DeleteFile(filepathT)) {
				char filepath[MAX_PATH];
				WideCharToMultiByte(CP_UTF8, 0, filepathT, -1,
				    filepath, sizeof (filepath), NULL, NULL);
				win_perror(GetLastError(), "Error removing "
				    "file %s", filepath);
				goto errout;
			}
		}
	} while (FindNextFile(h_find, &find_data));
	FindClose(h_find);

	if (!RemoveDirectory(dirnameT)) {
		char dirname[MAX_PATH];
		WideCharToMultiByte(CP_UTF8, 0, dirnameT, -1,
		    dirname, sizeof (dirname), NULL, NULL);
		win_perror(GetLastError(), "Error removing directory %s",
		    dirname);
		return (B_FALSE);
	}

	return (B_TRUE);
errout:
	FindClose(h_find);
	return (B_FALSE);
}

#endif	/* IBM */

/*
 * Recursive directory removal, including all its contents.
 */
bool_t
remove_directory(const char *dirname)
{
#if	IBM
	TCHAR dirnameT[strlen(dirname) + 1];

	MultiByteToWideChar(CP_UTF8, 0, dirname, -1, dirnameT,
	    sizeof (dirnameT));
	return (win_rmdir(dirnameT));
#else	/* !IBM */
	DIR *dp;
	struct dirent *de;

	if ((dp = opendir(dirname)) == NULL) {
		if (errno == ENOENT)
			/* ignore if the directory doesn't exist anyway */
			return (B_TRUE);
		logMsg("Error removing directory %s: %s", dirname,
		    strerror(errno));
		return (B_FALSE);
	}
	while ((de = readdir(dp)) != NULL) {
		char filename[FILENAME_MAX];
		int err;
		struct stat64 st;

		if (strcmp(de->d_name, ".") == 0 ||
		    strcmp(de->d_name, "..") == 0)
			continue;

		if (snprintf(filename, sizeof (filename), "%s/%s", dirname,
		    de->d_name) >= (ssize_t)sizeof (filename)) {
			logMsg("Error removing directory %s: path too long",
			    dirname);
			goto errout;
		}
		if (lstat64(filename, &st) < 0) {
			logMsg("Error removing directory %s: cannot stat "
			    "file %s: %s", dirname, de->d_name,
			    strerror(errno));
			goto errout;
		}
		if (S_ISDIR(st.st_mode)) {
			if (!remove_directory(filename))
				goto errout;
			err = rmdir(filename);
		} else {
			err = unlink(filename);
		}
		if (err != 0) {
			logMsg("Error removing %s: %s", filename,
			    strerror(errno));
			goto errout;
		}
	}
	closedir(dp);
	return (B_TRUE);
errout:
	closedir(dp);
	return (B_FALSE);
#endif	/* !IBM */
}
