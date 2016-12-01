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
 * The single-letter versions of the IDs need to go after the two-letter ones
 * to make sure we pick up the two-letter versions first.
 */
static const char *const icao_country_codes[] = {
	"AN",	"AY",
	"BG",	"BI",	"BK",
	"C",
	"DA",	"DB",	"DF",	"DG",	"DI",	"DN",	"DR",	"DT",	"DX",
	"EB",	"ED",	"EE",	"EF",	"EG",	"EH",	"EI",	"EK",	"EL",
	"EN",	"EP",	"ES",	"ET",	"EV",	"EY",
	"FA",	"FB",	"FC",	"FD",	"FE",	"FG",	"FH",	"FI",	"FJ",
	"FK",	"FL",	"FM",	"FN",	"FO",	"FP",	"FQ",	"FS",	"FT",
	"FV",	"FW",	"FX",	"FY",	"FZ",
	"GA",	"GB",	"GC",	"GE",	"GF",	"GG",	"GL",	"GM",	"GO",
	"GQ",	"GS",	"GU",	"GV",
	"HA",	"HB",	"HC",	"HD",	"HE",	"HH",	"HK",	"HL",	"HR",
	"HS",	"HT",	"HU",
	"K",
	"LA",	"LB",	"LC",	"LD",	"LE",	"LF",	"LG",	"LH",	"LI",
	"LJ",	"LK",	"LL",	"LM",	"LN",	"LO",	"LP",	"LQ",	"LR",
	"LS",	"LT",	"LU",	"LV",	"LW",	"LX",	"LY",	"LZ",
	"MB",	"MD",	"MG",	"MH",	"MK",	"MM",	"MN",	"MP",	"MR",
	"MS",	"MT",	"MU",	"MW",	"MY",	"MZ",
	"NC",	"NF",	"NG",	"NI",	"NL",	"NS",	"NT",	"NV",	"NW",
	"NZ",
	"OA",	"OB",	"OE",	"OI",	"OJ",	"OK",	"OL",	"OM",	"OO",
	"OP",	"OR",	"OS",	"OT",	"OY",
	"PA",	"PB",	"PC",	"PF",	"PG",	"PH",	"PJ",	"PK",	"PL",
	"PM",	"PO",	"PP",	"PT",	"PW",
	"RC",	"RJ",	"RK",	"RO",	"RP",
	"SA",	"SB",	"SC",	"SD",	"SE",	"SF",	"SG",	"SH",	"SI",
	"SJ",	"SK",	"SL",	"SM",	"SN",	"SO",	"SP",	"SS",	"SU",
	"SV",	"SW",	"SY",
	"TA",	"TB",	"TD",	"TF",	"TG",	"TI",	"TJ",	"TK",	"TL",
	"TN",	"TQ",	"TR",	"TT",	"TU",	"TV",	"TX",
	"UA",	"UB",	"UC",	"UD",	"UG",	"UK",	"UM",	"UT",
	"U",
	"VA",	"VC",	"VD",	"VE",	"VG",	"VH",	"VI",	"VL",	"VM",
	"VN",	"VO",	"VQ",	"VR",	"VT",	"VV",	"VY",
	"WA",	"WB",	"WI",	"WM",	"WP",	"WQ",	"WR",	"WS",
	"Y",
	"ZK",	"ZM",
	"Z",
	NULL
};

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

/*
 * Checks if a string is a valid ICAO airport code. ICAO airport codes always:
 * 1) are 4 characters long
 * 2) are all upper case
 * 3) contain only the letters A-Z
 * 4) may not start with I, J, Q or X
 */
bool_t
is_valid_icao_code(const char *icao)
{
	if (strlen(icao) != 4)
		return (B_FALSE);
	for (int i = 0; i < 4; i++)
		if (icao[i] < 'A' || icao[i] > 'Z')
			return (B_FALSE);
	if (icao[0] == 'I' || icao[0] == 'J' || icao[0] == 'Q' ||
	    icao[0] == 'X')
		return (B_FALSE);
	return (B_TRUE);
}

/*
 * Extracts the country code portion from an ICAO airport code. Because the
 * returned string pointer for any given country code is always the same, it
 * is possible to simply compare country code correspondence using a simple
 * pointer value comparison. If the ICAO country code is not known, returns
 * NULL instead.
 * If the passed string isn't a valid ICAO country code, returns NULL as well.
 */
const char *
extract_icao_country_code(const char *icao)
{
	if (!is_valid_icao_code(icao))
		return (NULL);

	for (int i = 0; icao_country_codes[i] != NULL; i++) {
		if (strncmp(icao, icao_country_codes[i],
		    strlen(icao_country_codes[i])) == 0)
			return (icao_country_codes[i]);
	}
	return (NULL);
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
 * Splits up an input string by separator into individual components.
 * The input string is `input' and the separator string is `sep'. The
 * skip_empty flag indicates whether to skip empty components (i.e.
 * two occurences of the separator string are next to each other).
 * Returns an array of pointers to the component strings and the number
 * of components in the `num' return parameter. The array of pointers
 * as well as the strings themselves are malloc'd and should be freed
 * by the caller. Use free_strlist for that.
 */
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

/*
 * Appends a printf-like formatted string to the end of *str, reallocating
 * the buffer as needed to contain it. The value of *str is modified to
 * point to the appropriately reallocated buffer.
 */
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

/*
 * strlcpy is a BSD function not available on Windows, so we roll a simple
 * version of it ourselves.
 */
void
my_strlcpy(char *restrict dest, const char *restrict src, size_t cap)
{
	dest[cap - 1] = '\0';
	strncpy(dest, src, cap - 1);
}

#if	IBM

/*
 * C getline is a POSIX function, so on Windows, we need to roll our own.
 */
ssize_t
getline(char **line_p, size_t *cap_p, FILE *fp)
{
	ASSERT(line_p != NULL);
	ASSERT(cap_p != NULL);
	ASSERT(fp != NULL);

	char *line = *line_p;
	size_t cap = *cap_p, n = 0;

	do {
		if (n + 1 >= cap) {
			cap += 256;
			line = realloc(line, cap);
		}
		ASSERT(n < cap);
		if (fgets(&line[n], cap - n, fp) == NULL) {
			if (n != 0) {
				break;
			} else {
				*line_p = line;
				*cap_p = cap;
				return (-1);
			}
		}
		n = strlen(line);
	} while (n > 0 && line[n - 1] != '\n');

	*line_p = line;
	*cap_p = cap;

	return (n);
}

#endif	/* IBM */

/*
 * Creates a file path string from individual path components. The
 * components are provided as separate filename arguments and the list needs
 * to be terminated with a NULL argument. The returned string can be freed
 * via free().
 */
char *
mkpathname(const char *comp, ...)
{
	char *res;
	va_list ap;

	va_start(ap, comp);
	res = mkpathname_v(comp, ap);
	va_end(ap);

	return (res);
}

char *
mkpathname_v(const char *comp, va_list ap)
{
	size_t n = 0, len = 0;
	char *str;
	va_list ap2;

	ASSERT(ap != NULL);

	va_copy(ap2, ap);
	len = strlen(comp);
	for (const char *c = va_arg(ap2, const char *); c != NULL;
	    c = va_arg(ap2, const char *)) {
		len += 1 + strlen(c);
	}
	va_end(ap2);

	str = malloc(len + 1);
	n += snprintf(str, len + 1, "%s", comp);
	for (const char *c = va_arg(ap, const char *); c != NULL;
	    c = va_arg(ap, const char *)) {
		ASSERT(n < len);
		n += snprintf(&str[n], len - n + 1, "%c%s", DIRSEP, c);
		/* kill a trailing directory separator */
		if (str[n - 1] == DIRSEP) {
			str[n - 1] = 0;
			n--;
		}
	}

	return (str);
}

/*
 * For some inexplicable reason, on Windows X-Plane can return paths via the
 * API which are a mixture of Windows- & Unix-style path separators. This
 * function fixes that by flipping all path separators to the appropriate
 * type used on the host OS.
 */
void
fix_pathsep(char *str)
{
	for (int i = 0, n = strlen(str); i < n; i++) {
#if	IBM
		if (str[i] == '/')
			str[i] = '\\';
#else	/* !IBM */
		if (str[i] == '\\')
			str[i] = '/';
#endif	/* !IBM */
	}
}

char *
file2str(const char *comp, ...)
{
#define	MAX_FILESIZE	1024 * 1024
	va_list ap;
	char *filename;
	char *contents;
	FILE *fp;
	long len;

	va_start(ap, comp);
	filename = mkpathname_v(comp, ap);
	va_end(ap);

	fp = fopen(filename, "rb");
	if (fp == NULL) {
		free(filename);
		return (NULL);
	}
	free(filename);
	filename = NULL;

	fseek(fp, 0, SEEK_END);
	len = ftell(fp);
	if (len < 0 || len > MAX_FILESIZE) {
		fclose(fp);
		return (NULL);
	}
	fseek(fp, 0, SEEK_SET);
	contents = malloc(len);
	if (fread(contents, 1, len, fp) != (size_t)len) {
		fclose(fp);
		free(contents);
		return (NULL);
	}
	fclose(fp);

	return (contents);
}

ssize_t
filesz(const char *filename)
{
	ssize_t s;
	FILE *fp = fopen(filename, "rb");

	if (fp == NULL)
		return (-1);
	fseek(fp, 0, SEEK_END);
	s = ftell(fp);
	fclose(fp);

	return (s);
}

/*
 * Returns the current UNIX time in microseconds.
 */
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

#endif	/* IBM */

/*
 * Returns true if the file exists. If `isdir' is non-NULL, if the file
 * exists, isdir is set to indicate if the file is a directory.
 */
bool_t
file_exists(const char *filename, bool_t *isdir)
{
#if	IBM
	int len = strlen(filename);
	TCHAR filenameT[len + 1];
	DWORD attr;

	MultiByteToWideChar(CP_UTF8, 0, filename, -1, filenameT, len + 1);
	attr = GetFileAttributes(filenameT);
	if (isdir != NULL)
		*isdir = !!(attr & FILE_ATTRIBUTE_DIRECTORY);
	return (attr != INVALID_FILE_ATTRIBUTES);
#else	/* !IBM */
	struct stat st;

	if (isdir != NULL)
		*isdir = B_FALSE;
	if (lstat(filename, &st) < 0) {
		if (errno != ENOENT)
			logMsg("Error checking if file %s exists: %s",
			    filename, strerror(errno));
		return (B_FALSE);
	}
	if (isdir != NULL)
		*isdir = S_ISDIR(st.st_mode);
	return (B_TRUE);
#endif	/* !IBM */
}

/*
 * Creates an empty directory at `dirname' with default permissions.
 */
bool_t
create_directory(const char *dirname)
{
	ASSERT(dirname != NULL);
	dbg_log(fs, 1, "create_directory: %s", dirname);
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

	StringCchPrintf(srchnameT, dirname_len + 4, TEXT("%s\\*"), dirnameT);
	h_find = FindFirstFile(srchnameT, &find_data);
	if (h_find == INVALID_HANDLE_VALUE) {
		int err = GetLastError();
		char dirname[MAX_PATH];
		WideCharToMultiByte(CP_UTF8, 0, dirnameT, -1, dirname,
		    sizeof (dirname), NULL, NULL);
		win_perror(err, "Error listing directory %s", dirname);
		return (B_FALSE);
	}
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
	dbg_log(fs, 1, "remove_directory: %s", dirname);
#if	IBM
	TCHAR dirnameT[strlen(dirname) + 1];

	MultiByteToWideChar(CP_UTF8, 0, dirname, -1, dirnameT,
	    sizeof (dirnameT));
	return (win_rmdir(dirnameT));
#else	/* !IBM */
	DIR *dp;
	struct dirent *de;

	if ((dp = opendir(dirname)) == NULL) {
		logMsg("Error removing directory %s: %s", dirname,
		    strerror(errno));
		return (B_FALSE);
	}
	while ((de = readdir(dp)) != NULL) {
		char filename[FILENAME_MAX];
		int err;
		struct stat st;

		if (strcmp(de->d_name, ".") == 0 ||
		    strcmp(de->d_name, "..") == 0)
			continue;

		if (snprintf(filename, sizeof (filename), "%s/%s", dirname,
		    de->d_name) >= (ssize_t)sizeof (filename)) {
			logMsg("Error removing directory %s: path too long",
			    dirname);
			goto errout;
		}
		if (lstat(filename, &st) < 0) {
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

bool_t
remove_file(const char *filename, bool_t notfound_ok)
{
#if	IBM
	TCHAR filenameT[strlen(filename) + 1];
	DWORD error;

	MultiByteToWideChar(CP_UTF8, 0, filename, -1, filenameT,
	    sizeof (filenameT));
	if (!DeleteFile(filenameT) && ((error = GetLastError()) !=
	    ERROR_FILE_NOT_FOUND || !notfound_ok)) {
		win_perror(error, "Cannot remove file %s", filename);
		return (B_FALSE);
	}
	return (B_TRUE);
#else	/* !IBM */
	if (unlink(filename) < 0 && (errno != ENOENT || !notfound_ok)) {
		logMsg("Cannot remove file %s: %s", filename, strerror(errno));
		return (B_FALSE);
	}
	return (B_TRUE);
#endif	/* !IBM */
}
