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
 * Copyright 2016 Saso Kiselkov. All rights reserved.
 */

#ifndef	_XRAAS_CFG_H_
#define	_XRAAS_CFG_H_

#include "xraas2.h"
#include "conf.h"

#ifdef	__cplusplus
extern "C" {
#endif

bool_t load_configs(xraas_state_t *state, const char *plugindir,
    const char *acf_path);

#ifdef	__cplusplus
}
#endif

#endif	/* _XRAAS_CFG_H_ */
