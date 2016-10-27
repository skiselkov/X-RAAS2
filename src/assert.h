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

#ifndef	_XRAAS_ASSERT_H_
#define	_XRAAS_ASSERT_H_

#include <assert.h>

#include "helpers.h"
#include "log.h"

#ifdef	__cplusplus
extern "C" {
#endif

#ifdef	DEBUG
#define	ASSERT(x)		VERIFY(x)
#define	UNUSED_NODEBUG(x)
#else	/* !DEBUG */
#define	ASSERT(x)
#define	UNUSED_NODEBUG(x)	UNUSED(x)
#endif	/* !DEBUG */

#define	VERIFY(x) \
	do { \
		if (!(x)) { \
			logMsg("assertion \"%s\" failed\n", #x); \
			log_backtrace(); \
			abort(); \
		} \
	} while (0)

#ifdef	__cplusplus
}
#endif

#endif	/* _XRAAS_ASSERT_H_ */
