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
 * Copyright 2017 Saso Kiselkov. All rights reserved.
 */

#ifndef	_XRAAS_INIT_MSG_H_
#define	_XRAAS_INIT_MSG_H_

#include <acfutils/helpers.h>
#include <acfutils/types.h>

#ifdef	__cplusplus
extern "C" {
#endif

enum {
	INIT_ERR_MSG_TIMEOUT = 25
};
void log_init_msg(bool_t display, int timeout, const char *man_sect,
    const char *man_sect_name, const char *fmt, ...) PRINTF_ATTR(5);

bool_t init_msg_sys_init(void);
void init_msg_sys_fini(void);

#ifdef	__cplusplus
}
#endif

#endif	/* _XRAAS_INIT_MSG_H_ */
