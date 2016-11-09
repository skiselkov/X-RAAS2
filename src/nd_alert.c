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

#if	IBM
# include <gl.h>
# include <glut.h>
#elif	APL
# include <OpenGL/gl.h>
# include <GLUT/glut.h>
#else	/* LIN */
# include <GL/gl.h>
# include <GL/glut.h>
#endif	/* LIN */

#include <XPLMDataAccess.h>
#include <XPLMDisplay.h>
#include <XPLMGraphics.h>
#include <XPLMProcessing.h>

#include "assert.h"
#include "helpers.h"
#include "perf.h"
#include "../api/c/XRAAS_ND_msg_decode.h"

#include "nd_alert.h"

#define	DR_NAME			"skiselkov/xraas/ND_alert"
#define	AMBER_FLAG		0x40
#define	ND_SCHED_INTVAL		1.0

#define	ND_OVERLAY_FONT		GLUT_BITMAP_HELVETICA_18

static bool_t			inited = B_FALSE;
static XPLMDataRef		dr = NULL;
static int			alert_status = 0;
static long long		alert_start_time = 0;

static XPLMDataRef		dr_local_x, dr_local_y, dr_local_z;
static XPLMDataRef		dr_pitch, dr_roll, dr_hdg;

static int
read_ND_alert(void *refcon)
{
	UNUSED(refcon);
	return (alert_status);
}

static int
nd_alert_draw_cb(XPLMDrawingPhase phase, int before, void *refcon)
{
	char msg[16];
	int color, screen_x, screen_y;
	int width = 0;
	int val = XPLMGetDatai(dr);

	UNUSED(phase);
	UNUSED(before);
	UNUSED(refcon);

	if (!XRAAS_ND_msg_decode(val, msg, &color) ||
	    !xraas_is_on() || view_is_external())
		return (1);

	XPLMGetScreenSize(&screen_x, &screen_y);

	/*
	 * Graphics state for drawing the ND alert overlay:
	 * 1) disable fog
	 * 2) disable multitexturing
	 * 3) disable GL lighting
	 * 4) enable per-pixel alpha testing
	 * 5) enable per-pixel alpha blending
	 * 6) disable per-pixel bit depth testing
	 * 7) disable writeback of depth info to the depth buffer
	 */
	XPLMSetGraphicsState(0, 0, 0, 1, 1, 0, 0);

	for (int i = 0, n = strlen(msg); i < n; i++)
		width += glutBitmapWidth(ND_OVERLAY_FONT, msg[i]);

	glColor4f(0, 0, 0, 0.67);
	glBegin(GL_POLYGON);
	glVertex2f((screen_x - width) / 2 - 8, screen_y * 0.98 - 27);
	glVertex2f((screen_x - width) / 2 - 8, screen_y * 0.98);
	glVertex2f((screen_x + width) / 2 + 8, screen_y * 0.98);
	glVertex2f((screen_x + width) / 2 + 8, screen_y * 0.98 - 27);
	glEnd();

	if (color == XRAAS_ND_ALERT_GREEN)
		glColor4f(0, 1, 0, 1);
	else
		glColor4f(0.9, 0.9, 0, 1);

	glRasterPos2f((screen_x - width) / 2, screen_y * 0.98 - 20);
	for (int i = 0, n = strlen(msg); i < n; i++)
		glutBitmapCharacter(ND_OVERLAY_FONT, msg[i]);

	return (1);
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
	    SEC2USEC(xraas_state->nd_alert_timeout)))
		alert_status = 0;

	return (ND_SCHED_INTVAL);
}

void
ND_alerts_init(void)
{
	dbg_log(nd_alert, 1, "ND_alerts_init");

	ASSERT(!inited);

	dr = XPLMRegisterDataAccessor(DR_NAME, xplmType_Int, 0, read_ND_alert,
	    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	    NULL, NULL);
	VERIFY(dr != NULL);
	XPLMRegisterFlightLoopCallback(alert_sched_cb, ND_SCHED_INTVAL, NULL);
	XPLMRegisterDrawCallback(nd_alert_draw_cb, xplm_Phase_Window, 0,
	    NULL);

	dr_local_x = XPLMFindDataRef("sim/flightmodel/position/local_x");
	VERIFY(dr_local_x != NULL);
	dr_local_y = XPLMFindDataRef("sim/flightmodel/position/local_y");
	VERIFY(dr_local_y != NULL);
	dr_local_z = XPLMFindDataRef("sim/flightmodel/position/local_z");
	VERIFY(dr_local_z != NULL);
	dr_pitch = XPLMFindDataRef("sim/flightmodel/position/theta");
	VERIFY(dr_pitch != NULL);
	dr_roll = XPLMFindDataRef("sim/flightmodel/position/phi");
	VERIFY(dr_roll != NULL);
	dr_hdg = XPLMFindDataRef("sim/flightmodel/position/psi");
	VERIFY(dr_hdg != NULL);

	inited = B_TRUE;
}

void
ND_alerts_fini()
{
	dbg_log(nd_alert, 1, "ND_alerts_fini");

	if (!inited)
		return;

	XPLMUnregisterDataAccessor(dr);
	dr = NULL;
	XPLMUnregisterFlightLoopCallback(alert_sched_cb, NULL);
	XPLMUnregisterDrawCallback(nd_alert_draw_cb, xplm_Phase_Window, 0,
	    NULL);

	inited = B_FALSE;
}

void
ND_alert(nd_alert_msg_type_t msg, nd_alert_level_t level, const char *rwy_id,
    int dist)
{
	ASSERT(inited);
	ASSERT(msg >= ND_ALERT_FLAPS && msg <= ND_ALERT_LONG_LAND);

	if (!xraas_state->nd_alerts_enabled)
		return;

	dbg_log(nd_alert, 1, "msg: %d level: %d rwy_ID: %s dist: %d",
	    msg, level, rwy_id, dist);

	if (level < (nd_alert_level_t)xraas_state->nd_alert_filter) {
		dbg_log(nd_alert, 2, "suppressed due to filter setting");
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
		if (xraas_state->use_imperial)
			msg |= (((int)MET2FEET(dist) / 100) & 0xff) << 16;
		else
			msg |= ((dist / 100) & 0xff) << 16;
	}

	alert_status = msg;
	alert_start_time = microclock();
}
