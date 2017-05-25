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
 *
 * Copyright 2017 Saso Kiselkov. All rights reserved.
 */

#ifndef	_XRAAS_THREADING_H_
#define	_XRAAS_THREADING_H_

#if	IBM
#include <windows.h>
#else	/* !IBM */
#include <pthread.h>
#endif	/* !IBM */

#ifdef	__cplusplus
extern "C" {
#endif

#if	IBM

#define	mutex_t	HANDLE

#else	/* !IBM */

typedef pthread_mutex_t mutex_t;

#endif	/* !IBM */

void mutex_init(mutex_t *mtx);
void mutex_destroy(mutex_t *mtx);

void mutex_enter(mutex_t *mtx);
void mutex_exit(mutex_t *mtx);

#ifdef	__cplusplus
}
#endif

#endif	/* _XRAAS_THREADING_H_ */
