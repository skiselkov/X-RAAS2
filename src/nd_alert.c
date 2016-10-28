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

#include <string.h>

#include <XPLMDataAccess.h>
#include <XPLMProcessing.h>

#include "assert.h"
#include "helpers.h"
#include "perf.h"

#include "nd_alert.h"

#define	DR_NAME			"skiselkov/xraas/ND_alert"
#define	AMBER_FLAG		0x40
#define	ND_SCHED_INTVAL		1.0

static bool_t			inited = B_FALSE;
static XPLMDataRef		dr = NULL;
static int			alert_status = 0;
static long long		alert_start_time = 0;
static const xraas_state_t	*state = NULL;

static int
read_ND_alert(void *refcon)
{
	UNUSED(refcon);
	return (alert_status);
}

static float
alert_sched_cb(float elapsed_since_last_call, float elapsed_since_last_floop,
    int counter, void *refcon)
{
	UNUSED(elapsed_since_last_call);
	UNUSED(elapsed_since_last_floop);
	UNUSED(counter);
	UNUSED(refcon);

	if (alert_status != 0 && (microclock() - alert_start_time >
	    state->nd_alert_timeout))
		alert_status = 0;

	return (ND_SCHED_INTVAL);
}

void
ND_alerts_init(const xraas_state_t *conf_state)
{
	ASSERT(!inited);

	state = conf_state;
	dr = XPLMRegisterDataAccessor(DR_NAME, xplmType_Int, 0, read_ND_alert,
	    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	    NULL, NULL);
	VERIFY(dr != NULL);
	XPLMRegisterFlightLoopCallback(alert_sched_cb, ND_SCHED_INTVAL, NULL);

	inited = B_TRUE;
}

void
ND_alerts_fini()
{
	ASSERT(inited);
	inited = B_FALSE;

	XPLMUnregisterDataAccessor(dr);
	dr = NULL;
	XPLMUnregisterFlightLoopCallback(alert_sched_cb, NULL);
}

void
ND_alert(nd_alert_msg_type_t msg, nd_alert_level_t level, const char *rwy_id,
    int dist)
{
	ASSERT(inited);
	ASSERT(msg >= ND_ALERT_FLAPS && msg <= ND_ALERT_LONG_LAND);

	if (!state->nd_alerts_enabled)
		return;

	dbg_log("ND_alert", 1, "msg: %d level: %d rwy_ID: %s dist: %d",
	    msg, level, rwy_id, dist);

	if (level < (nd_alert_level_t)state->nd_alert_filter) {
		dbg_log("ND_alert", 2, "suppressed due to filter setting");
		return;
	}

	/* encode any non-routine alerts as amber */
	if (level > ND_ALERT_ROUTINE)
		msg |= AMBER_FLAG;

	/* encode the optional runway ID field */
	if (rwy_id != NULL && strcmp(rwy_id, "") != 0) {
		int num = atoi(rwy_id);
		char suffix = strlen(rwy_id) == 3 ? rwy_id[2] : 0;

		msg |= num << 8;
		switch (suffix) {
		case 'R':
			msg |= 1 << 14;
			break;
		case 'L':
			msg |= 2 << 14;
			break;
		case 'C':
			msg |= 3 << 14;
			break;
		}
	}

	/* encode the optional distance field */
	if (dist >= 0) {
		if (state->use_imperial)
			msg |= (((int)MET2FEET(dist) / 100) & 0xff) << 16;
		else
			msg |= ((dist / 100) & 0xff) << 16;
	}

	alert_status = msg;
	alert_start_time = microclock();
}
