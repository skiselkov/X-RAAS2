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

#ifndef	_XRAAS_ANNUN_H_
#define	_XRAAS_ANNUN_H_

#include <stdlib.h>

#include "types.h"
#include "xraas2.h"

#ifdef	__cplusplus
extern "C" {
#endif

typedef enum msg_prio {
	MSG_PRIO_LOW = 1,
	MSG_PRIO_MED = 2,
	MSG_PRIO_HIGH = 3
} msg_prio_t;

typedef enum {
	ZERO_MSG,
	ONE_MSG,
	TWO_MSG,
	THREE_MSG,
	FOUR_MSG,
	FIVE_MSG,
	SIX_MSG,
	SEVEN_MSG,
	EIGHT_MSG,
	NINE_MSG,
	THIRTY_MSG,
	ALT_SET_MSG,
	APCH_MSG,
	AVAIL_MSG,
	CAUTION_MSG,
	CENTER_MSG,
	DEEP_LAND_MSG,
	FEET_MSG,
	FLAPS_MSG,
	HUNDRED_MSG,
	LEFT_MSG,
	LONG_LAND_MSG,
	METERS_MSG,
	ON_RWY_MSG,
	ON_TWY_MSG,
	RIGHT_MSG,
	RMNG_MSG,
	RWYS_MSG,
	PAUSE_MSG,
	SHORT_RWY_MSG,
	THOUSAND_MSG,
	TOO_FAST_MSG,
	TOO_HIGH_MSG,
	TWY_MSG,
	UNSTABLE_MSG,
	NUM_MSGS
} msg_type_t;

void play_msg(msg_type_t *msg, size_t msg_len, msg_prio_t prio);
bool_t modify_cur_msg(msg_type_t *msg, size_t msg_len, msg_prio_t prio);

bool_t snd_sys_init(const char *plugindir);
void snd_sys_fini(void);

#ifdef	__cplusplus
}
#endif

#endif	/* _XRAAS_ANNUN_H_ */
