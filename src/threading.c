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

#include "threading.h"

#include "assert.h"

#if	IBM

void
mutex_init(mutex_t *mtx)
{
	*mtx = CreateMutex(NULL, FALSE, NULL);
	VERIFY(*mtx != NULL);
}

void
mutex_destroy(mutex_t *mtx)
{
	CloseHandle(*mtx);
	*mtx = NULL;
}

void
mutex_enter(mutex_t *mtx)
{
	VERIFY(WaitForSingleObject(*mtx,INFINITE) == WAIT_OBJECT_0);
}

void
mutex_exit(mutex_t *mtx)
{
	VERIFY(ReleaseMutex(*mtx));
}

#else	/* !IBM */

void
mutex_init(mutex_t *mtx)
{
	VERIFY(pthread_mutex_init(mtx, NULL) == 0);
}

void
mutex_destroy(mutex_t *mtx)
{
	VERIFY(pthread_mutex_destroy(mtx) == 0);
}

void
mutex_enter(mutex_t *mtx)
{
	VERIFY(pthread_mutex_lock(mtx) == 0);
}

void
mutex_exit(mutex_t *mtx)
{
	VERIFY(pthread_mutex_unlock(mtx) == 0);
}

#endif	/* !IBM */
