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

#if	IBM
#include <gl.h>
#elif	APL
#include <OpenGL/gl.h>
#else	/* LIN */
#include <GL/gl.h>
#endif	/* LIN */

#include <XPLMDataAccess.h>
#include <XPLMDisplay.h>
#include <XPLMGraphics.h>

#include "airportdb.h"
#include "assert.h"
#include "geom.h"
#include "helpers.h"
#include "log.h"
#include "xraas2.h"

#include "dbg_gui.h"

bool_t dbg_gui_inited = B_FALSE;
static XPLMDataRef *lat_dr = NULL, *lon_dr = NULL;
static int screen_x, screen_y;
static double scale;

#define	DBG_X(coord)	(coord * scale + screen_x / 2)
#define	DBG_Y(coord)	(coord * scale + screen_y / 2)

static void
draw_line(double x1, double y1, double x2, double y2)
{
	glBegin(GL_LINES);
	glVertex2f(x1, y1);
	glVertex2f(x2, y2);
	glEnd();
}

static void
draw_bbox(const vect2_t *bbox)
{
	glBegin(GL_LINES);
	for (int i = 0; !IS_NULL_VECT(bbox[i]); i++) {
		glVertex2f(DBG_X(bbox[i].x), DBG_Y(bbox[i].y));
		if (!IS_NULL_VECT(bbox[i + 1]))
			glVertex2f(DBG_X(bbox[i + 1].x), DBG_Y(bbox[i + 1].y));
		else
			glVertex2f(DBG_X(bbox[0].x), DBG_Y(bbox[0].y));
	}
	glEnd();
}

static int
draw_cb(XPLMDrawingPhase phase, int before, void *refcon)
{
	vect2_t pos_v, vel_v, tgt_v;
	const airport_t *arpt;

	UNUSED(phase);
	UNUSED(before);
	UNUSED(refcon);
	ASSERT(dbg_gui_inited);

	if ((arpt = find_nearest_curarpt()) == NULL)
		return (1);
	ASSERT(arpt->load_complete);

	pos_v = geo2fpp(GEO_POS2(XPLMGetDatad(lat_dr), XPLMGetDatad(lon_dr)),
	    &arpt->fpp);
	vel_v = acf_vel_vector(RWY_PROXIMITY_TIME_FACT);
	tgt_v = vect2_add(pos_v, vel_v);

	XPLMGetScreenSize(&screen_x, &screen_y);
	scale = MIN(screen_y / (2.0 * vect2_abs(pos_v)), 1.0);

	/*
	 * Graphics state for drawing the debug overlay:
	 * 1) disable fog
	 * 2) disable multitexturing
	 * 3) disable GL lighting
	 * 4) enable per-pixel alpha testing
	 * 5) enable per-pixel alpha blending
	 * 6) disable per-pixel bit depth testing
	 * 7) disable writeback of depth info to the depth buffer
	 */
	XPLMSetGraphicsState(0, 0, 0, 1, 1, 0, 0);

	/* draw center crosshair - this is the airport reference point */
	glColor4f(1, 0, 0, 1);
	glLineWidth(2);
	draw_line(DBG_X(0) - 5, DBG_Y(0), DBG_X(0) + 5, DBG_Y(0));
	draw_line(DBG_X(0), DBG_Y(0) - 5, DBG_X(0), DBG_Y(0) + 5);

	for (runway_t *rwy = avl_first(&arpt->rwys); rwy != NULL;
	    rwy = AVL_NEXT(&arpt->rwys, rwy)) {
		glColor4f(1, 1, 1, 0.5);
		draw_bbox(rwy->ends[0].apch_bbox);
		draw_bbox(rwy->ends[1].apch_bbox);
		glColor4f(0, 0, 1, 0.67);
		draw_bbox(rwy->prox_bbox);
		glColor4f(1, 0, 0, 1);
		draw_bbox(rwy->asda_bbox);
		glColor4f(1, 1, 0, 1);
		draw_bbox(rwy->tora_bbox);
		glColor4f(0, 1, 0, 1);
		draw_bbox(rwy->rwy_bbox);
	}

	glColor4f(1, 1, 1, 1);
	draw_line(DBG_X(pos_v.x) - 5, DBG_Y(pos_v.y),
	    DBG_X(pos_v.x) + 5, DBG_Y(pos_v.y));
	draw_line(DBG_X(pos_v.x), DBG_Y(pos_v.y) - 5,
	    DBG_X(pos_v.x), DBG_Y(pos_v.y) + 5);
	glColor4f(0, 1, 1, 1);
	draw_line(DBG_X(pos_v.x), DBG_Y(pos_v.y),
	    DBG_X(tgt_v.x), DBG_Y(tgt_v.y));

	return (1);
}

void
dbg_gui_init(void)
{
	dbg_log(dbg_gui, 1, "init");

	ASSERT(!dbg_gui_inited);

	lat_dr = XPLMFindDataRef("sim/flightmodel/position/latitude");
	VERIFY(lat_dr != NULL);
	lon_dr = XPLMFindDataRef("sim/flightmodel/position/longitude");
	VERIFY(lon_dr != NULL);

	XPLMRegisterDrawCallback(draw_cb, xplm_Phase_Window, 0, NULL);

	dbg_gui_inited = B_TRUE;
}

void
dbg_gui_fini(void)
{
	dbg_log(dbg_gui, 1, "fini");

	if (!dbg_gui_inited)
		return;

	XPLMUnregisterDrawCallback(draw_cb, xplm_Phase_Window, 0, NULL);

	dbg_gui_inited = B_FALSE;
}
