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
# include <gl/GL.h>
# include <gl/glut.h>
#elif	MAC
# include <OpenGL/gl.h>
# include <OpenGL/glu.h>
# include <GLUT/glut.h>
# include <Carbon/Carbon.h>
#else	/* LIN */
# include <GL/gl.h>
# include <GL/glu.h>
# include <GL/glut.h>
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

static bool_t inited = B_FALSE;
static XPLMDataRef *lat_dr = NULL, *lon_dr = NULL;

static void
draw_line(double x1, double y1, double x2, double y2)
{
	glBegin(GL_LINES);
	glVertex2f(x1, y1);
	glVertex2f(x2, y2);
	glEnd();
}

static int
draw_cb(XPLMDrawingPhase phase, int before, void *refcon)
{
#define	dbgX(coord)	(coord * draw_scale + screen_x / 2)
#define	dbgY(coord)	(coord * draw_scale + screen_y / 2)

	int screen_x, screen_y;
	double draw_scale;
	vect2_t pos_v;
	const airport_t *curarpt;

	UNUSED(phase);
	UNUSED(before);
	UNUSED(refcon);
	ASSERT(inited);

	curarpt = find_nearest_curarpt();
	if (curarpt == NULL)
		return (1);
	ASSERT(curarpt->load_complete);

	pos_v = geo2fpp(GEO_POS2(XPLMGetDatad(lat_dr), XPLMGetDatad(lon_dr)),
	    &curarpt->fpp);
	XPLMGetScreenSize(&screen_x, &screen_y);
	draw_scale = MIN(screen_y / (2.0 * vect2_abs(pos_v)), 1.0);

	/*
	 * Our graphics state:
	 * 1) disables fog
	 * 2) disables multitexturing
	 * 3) disables GL lighting
	 * 4) enables per-pixel alpha testing
	 * 5) enables per-pixel alpha blending
	 * 6) disables per-pixel bit depth testing
	 * 7) disables writeback of depth info to the depth buffer
	 */
	XPLMSetGraphicsState(0, 0, 0, 1, 1, 0, 0);

	/* draw center crosshair - this is the airport reference point */
	glColor4f(1, 0, 0, 1);
	glLineWidth(2);
	draw_line(dbgX(-5), dbgY(0), dbgX(5), dbgY(0));
	draw_line(dbgX(0), dbgY(-5), dbgX(0), dbgY(5));

	return (1);
#undef	dbgX
#undef	dbgY
}

void
dbg_gui_init(void)
{
	dbg_log("dbg_gui", 1, "init");

	ASSERT(!inited);
	inited = B_TRUE;

	lat_dr = XPLMFindDataRef("sim/flightmodel/position/latitude");
	VERIFY(lat_dr != NULL);
	lon_dr = XPLMFindDataRef("sim/flightmodel/position/longitude");
	VERIFY(lon_dr != NULL);

	XPLMRegisterDrawCallback(draw_cb, xplm_Phase_Window, 0, NULL);
}

void
dbg_gui_fini(void)
{
	dbg_log("dbg_gui", 1, "fini");

	ASSERT(inited);
	inited = B_FALSE;

	XPLMUnregisterDrawCallback(draw_cb, xplm_Phase_Window, 0, NULL);
}