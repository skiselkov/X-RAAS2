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

#ifndef	_XRAAS_ND_ALERT_H_
#define	_XRAAS_ND_ALERT_H_

#include "types.h"
#include "xraas2.h"

#ifdef	__cplusplus
extern "C" {
#endif

/* ND alert severity */
typedef enum nd_alert_level {
	ND_ALERT_ROUTINE = 0,
	ND_ALERT_NONROUTINE = 1,
	ND_ALERT_CAUTION = 2
} nd_alert_level_t;

/*
 * This is what we set our ND alert dataref to when we want to communicate
 * to the aircraft's FMS that it should display a message on the ND. Value
 * '0' is reserved for 'nothing'.
 *
 * Since the dataref is an int and we need to annunciate various messages,
 * we split this int into several bitfields:
 * bits 0 - 7  (8 bits):	message ID
 * bits 8 - 13 (6 bits):	numeric runway ID:
 *				'00' means 'taxiway'
 *				'01' through '36' means a runway ID
 *				'37' means 'RWYS' (i.e. multiple runways)
 * bits 14 - 15 (2 bits):<----->'0' means 'no suffix'
 *				'1' means 'RIGHT'
 *				'2' means 'LEFT'
 *				'3' means 'CENTER'
 * bits 16 - 23 (8 bits):<----->Runway length available to the nearest 100
 *				feet or meters. '0' means 'do not display'.
 * Bits 8 through 23 are only used by the ND_ALERT_APP and ND_ALERT_ON messages
 */
typedef enum nd_alert_msg_type {
	ND_ALERT_FLAPS = 1,		/* 'FLAPS' */
	ND_ALERT_TOO_HIGH = 2,		/* 'TOO HIGH' */
	ND_ALERT_TOO_FAST = 3,		/* 'TOO FAST' */
	ND_ALERT_UNSTABLE = 4,		/* 'UNSTABLE' */
	ND_ALERT_TWY = 5,		/* 'TAXIWAY' */
	ND_ALERT_SHORT_RWY = 6,		/* 'SHORT RUNWAY' */
	ND_ALERT_ALTM_SETTING = 7,	/* 'ALTM SETTING' */
	ND_ALERT_APP = 8,		/* 'APP XX' or 'APP XX ZZ' */
					/* 'XX' is rwy ID (bits 8 - 15) */
					/* 'ZZ' is rwy length (bits 16 - 23) */
	ND_ALERT_ON = 9,		/* 'ON XX' or 'ON XX ZZ' */
					/* 'XX' is rwy ID (bits 8 - 15) */
					/* 'ZZ' is rwy length (bits 16 - 23) */
	ND_ALERT_LONG_LAND = 10,	/* 'LONG LANDING' */
	ND_ALERT_DEEP_LAND = 11		/* 'DEEP LANDING' */
} nd_alert_msg_type_t;

extern const char *ND_alert_overlay_default_font;
extern const int ND_alert_overlay_default_font_size;

bool_t ND_alerts_init(void);
void ND_alerts_fini(void);
void ND_alert(nd_alert_msg_type_t msg, nd_alert_level_t level,
    const char *rwy_id, int dist);

#ifdef	__cplusplus
}
#endif

#endif	/* _XRAAS_ND_ALERT_H_ */
