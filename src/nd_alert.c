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

#include <string.h>

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
#include <XPLMPlanes.h>
#include <XPLMProcessing.h>

#include "assert.h"
#include "dr_intf.h"
#include "helpers.h"
#include "init_msg.h"
#include "perf.h"
#include "text_rendering.h"
#include "../api/c/XRAAS_ND_msg_decode.h"

#include "nd_alert.h"

#define	DR_NAME			"xraas/ND_alert"
#define	OVERLAY_DIS_DR		"xraas/ND_alert_overlay_disabled"
#define	AMBER_FLAG		0x40
#define	ND_SCHED_INTVAL		1.0

#ifdef	DEBUG_PANEL
#define	NUM_DEBUG_PANEL_ROWS	16
#define	NUM_DEBUG_PANEL_COLS	16
#define	NUM_DEBUG_PANELS	(NUM_DEBUG_PANEL_ROWS * NUM_DEBUG_PANEL_COLS)
#define	DEBUG_PANEL_SIZE	64
#define	DEBUG_PANEL_OFF		4
#define	DEBUG_PANEL_PHASE	xplm_Phase_Gauges
#define	DEBUG_PANEL_PHASE_FLAG	0
#define	DEBUG_PANEL_FONT_SIZE	32
typedef struct {
	uint8_t	tex[DEBUG_PANEL_SIZE * DEBUG_PANEL_SIZE * 4];
} debug_panel_t;
static GLuint		*debug_textures = NULL;
static debug_panel_t	*debug_panels = NULL;
#endif	/* DEBUG_PANEL */

const char *ND_alert_overlay_default_font = "ShareTechMono" DIRSEP_S
	"ShareTechMono-Regular.ttf";
const int ND_alert_overlay_default_font_size = 28;

static bool_t			inited = B_FALSE;
static XPLMDataRef		dr = NULL, dr_overlay = NULL;
static int			alert_status = 0;
static long long		alert_start_time = 0;

static int			alert_overlay_dis = 0;

static XPLMDataRef		dr_local_x, dr_local_y, dr_local_z;
static XPLMDataRef		dr_pitch, dr_roll, dr_hdg;

static struct {
	FT_Library		ft;
	FT_Face			face;
	GLuint			texture;
	int			width;
	int			height;
	uint8_t			*buf;
} overlay = { NULL, NULL, 0, 0, 0, NULL };

typedef struct {
	unsigned	x, y;
	unsigned	w, h;
	double		v_offset;
} ND_coords_t;

typedef struct {
	const char	*ICAO;
	const char	*acf_filename;
	const char	*studio;
	int		font_sz;
	double		bg_alpha;
	unsigned	num_NDs;
	ND_coords_t	NDs[2];
} acf_ND_overlay_info_t;

static acf_ND_overlay_info_t overlay_infos[] = {
    {
	.ICAO = "B744", .acf_filename = "747-400.acf",
	.studio = "Laminar Research", .font_sz = 32, .bg_alpha = 0.3,
	.num_NDs = 2, .NDs = {
	    {
		.x = 3, .y = 15, .w = 665, .h = 670, .v_offset = 0.5
	    },
	    {
		.x = 691, .y = 15, .w = 665, .h = 670, .v_offset = 0.5
	    }
	}
    },
    {
	.ICAO = "B738", .acf_filename = "b738.acf",
	.studio = "Laminar Research", .font_sz = 24, .bg_alpha = 0.3,
	.num_NDs = 2, .NDs = {
	    {
		.x = 527, .y = 15, .w = 490, .h = 490, .v_offset = 0.5
	    },
	    {
		.x = 527, .y = 505, .w = 490, .h = 490, .v_offset = 0.5
	    }
	}
    },
    {
	.ICAO = "MD82", .acf_filename = "MD80.acf",
	.studio = "Laminar Research", .font_sz = 17, .bg_alpha = 0.3,
	.num_NDs = 1, .NDs = {
	    {
		.x = 259, .y = 576, .w = 270, .h = 204, .v_offset = 0.5
	    }
	}
    },
    {
	.ICAO = NULL
    }
};

static int acf_ND_info = -1;

#ifdef	DEBUG_PANEL
static int
debug_panel_draw_cb(XPLMDrawingPhase phase, int before, void *refcon)
{
#define	SPACE_WIDTH	2048
#define	SPACE_HEIGHT	2048
#define	CELL_WIDTH	(SPACE_WIDTH / NUM_DEBUG_PANEL_COLS)
#define	CELL_HEIGHT	(SPACE_HEIGHT / NUM_DEBUG_PANEL_ROWS)
	UNUSED(phase);
	UNUSED(before);
	UNUSED(refcon);

	XPLMSetGraphicsState(0, 0, 0, 0, 1, 0, 0);

	glColor3f(1, 1, 1);
	glLineWidth(1);

	glBegin(GL_LINES);
	for (int x = 0; x < NUM_DEBUG_PANEL_COLS; x ++) {
		for (int y = 0; y < NUM_DEBUG_PANEL_ROWS; y++) {
			float cx = x * CELL_WIDTH + CELL_WIDTH / 2 -
			    DEBUG_PANEL_SIZE / 2;
			float cy = y * CELL_HEIGHT + CELL_HEIGHT / 2 -
			    DEBUG_PANEL_SIZE / 2;

			if (cx > SPACE_WIDTH || cy > SPACE_HEIGHT)
				continue;
			glVertex2f(cx - DEBUG_PANEL_SIZE / 2,
			    cy - DEBUG_PANEL_SIZE / 2);
			glVertex2f(cx - DEBUG_PANEL_SIZE / 2,
			    cy + DEBUG_PANEL_SIZE / 2);
			glVertex2f(cx - DEBUG_PANEL_SIZE / 2,
			    cy + DEBUG_PANEL_SIZE / 2);
			glVertex2f(cx + DEBUG_PANEL_SIZE / 2,
			    cy + DEBUG_PANEL_SIZE / 2);
			glVertex2f(cx + DEBUG_PANEL_SIZE / 2,
			    cy + DEBUG_PANEL_SIZE / 2);
			glVertex2f(cx + DEBUG_PANEL_SIZE / 2,
			    cy - DEBUG_PANEL_SIZE / 2);
			glVertex2f(cx + DEBUG_PANEL_SIZE / 2,
			    cy - DEBUG_PANEL_SIZE / 2);
			glVertex2f(cx - DEBUG_PANEL_SIZE / 2,
			    cy - DEBUG_PANEL_SIZE / 2);
		}
	}
	glEnd();

	XPLMSetGraphicsState(0, 1, 0, 0, 1, 0, 0);
	glDepthFunc(GL_LEQUAL);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	for (int x = 0; x < NUM_DEBUG_PANEL_COLS; x ++) {
		for (int y = 0; y < NUM_DEBUG_PANEL_ROWS; y++) {
			float cx = x * CELL_WIDTH + CELL_WIDTH / 2 -
			    DEBUG_PANEL_SIZE / 2;
			float cy = y * CELL_HEIGHT + CELL_HEIGHT / 2 -
			    DEBUG_PANEL_SIZE / 2;

			if (cx > SPACE_WIDTH || cy > SPACE_HEIGHT)
				continue;

			glBindTexture(GL_TEXTURE_2D,
			    debug_textures[x * NUM_DEBUG_PANEL_COLS + y]);
			glBegin(GL_QUADS);
			glTexCoord2f(0.0, 1.0);
			glVertex2f(cx - DEBUG_PANEL_SIZE / 2,
			    cy - DEBUG_PANEL_SIZE / 2);
			glTexCoord2f(0.0, 0.0);
			glVertex2f(cx - DEBUG_PANEL_SIZE / 2,
			    cy + DEBUG_PANEL_SIZE / 2);
			glTexCoord2f(1.0, 0.0);
			glVertex2f(cx + DEBUG_PANEL_SIZE / 2,
			    cy + DEBUG_PANEL_SIZE / 2);
			glTexCoord2f(1.0, 1.0);
			glVertex2f(cx + DEBUG_PANEL_SIZE / 2,
			    cy - DEBUG_PANEL_SIZE / 2);
			glEnd();
		}
	}

	return (0);
}
#endif	/* DEBUG_PANEL */

static void
nd_alert_draw_at(int x, int y, int w, int h, double v_offset)
{
	/* only disable lighting, everything else is on */
	XPLMSetGraphicsState(1, 1, 0, 1, 1, 1, 1);

	glBegin(GL_QUADS);
	glTexCoord2f(0.0, 1.0);
	glVertex2f(x + (w - overlay.width) / 2,
	    y + (h * v_offset) - overlay.height);
	glTexCoord2f(0.0, 0.0);
	glVertex2f(x + (w - overlay.width) / 2, y + h * v_offset);
	glTexCoord2f(1.0, 0.0);
	glVertex2f(x + (w + overlay.width) / 2, y + h * v_offset);
	glTexCoord2f(1.0, 1.0);
	glVertex2f(x + (w + overlay.width) / 2,
	    y + h * v_offset - overlay.height);
	glEnd();
}

static int
nd_alert_draw_cb(XPLMDrawingPhase phase, int before, void *refcon)
{
	UNUSED(phase);
	UNUSED(before);
	UNUSED(refcon);

	if (overlay.buf == NULL || !xraas_is_on() || view_is_external())
		return (1);

	glBindTexture(GL_TEXTURE_2D, overlay.texture);

	if (acf_ND_info != -1) {
		const acf_ND_overlay_info_t *info = &overlay_infos[acf_ND_info];
		ASSERT(info->num_NDs <= 2);
		for (unsigned i = 0; i < info->num_NDs; i++) {
			nd_alert_draw_at(info->NDs[i].x, info->NDs[i].y,
			    info->NDs[i].w, info->NDs[i].h,
			    info->NDs[i].v_offset);
		}
	} else {
		int screen_x, screen_y;
		XPLMGetScreenSize(&screen_x, &screen_y);
		nd_alert_draw_at(0, 0, screen_x, screen_y, 0.98);
	}

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
	    SEC2USEC(xraas_state->config.nd_alert_timeout))) {
		if (overlay.buf != NULL) {
			glDeleteTextures(1, &overlay.texture);
			free(overlay.buf);
			overlay.buf = NULL;
		}
		alert_status = 0;
	}

	return (ND_SCHED_INTVAL);
}

static void
chk_acf_ND_integration(void)
{
	XPLMDataRef ICAO_dr = XPLMFindDataRef("sim/aircraft/view/acf_ICAO");
	XPLMDataRef studio_dr = XPLMFindDataRef("sim/aircraft/view/acf_author");
	char ICAO[8], studio[64], acf_filename[256], acf_path[512];

	ASSERT_MSG(acf_ND_info == -1, "acf_ND_info = %d", acf_ND_info);

	XPLMGetNthAircraftModel(0, acf_filename, acf_path);
	XPLMGetDatab(ICAO_dr, ICAO, 0, sizeof (ICAO) - 1);
	ICAO[sizeof (ICAO) - 1] = 0;
	XPLMGetDatab(studio_dr, studio, 0, sizeof (studio) - 1);
	studio[sizeof (studio) - 1] = 0;

	for (int i = 0; overlay_infos[i].ICAO != NULL; i++) {
		if (strcmp(overlay_infos[i].ICAO, ICAO) == 0 &&
		    (overlay_infos[i].acf_filename != NULL ||
		    strcmp(overlay_infos[i].acf_filename, acf_filename) == 0) &&
		    (overlay_infos[i].studio != NULL ||
		    strcmp(overlay_infos[i].studio, studio) == 0)) {
			acf_ND_info = i;
			break;
		}
	}
}

bool_t
ND_alerts_init(void)
{
	FT_Error err;
	char *filename;

	dbg_log(nd_alert, 1, "ND_alerts_init");

	ASSERT(!inited);

	if ((err = FT_Init_FreeType(&overlay.ft)) != 0) {
		log_init_msg(B_TRUE, INIT_ERR_MSG_TIMEOUT, NULL, NULL,
		    "ND alert overlay initialization error: cannot "
		    "initialize FreeType library: %s", ft_err2str(err));
		return (B_FALSE);
	}
	filename = mkpathname(xraas_plugindir, "data", "fonts",
	    xraas_state->config.nd_alert_overlay_font, NULL);
	if ((err = FT_New_Face(overlay.ft, filename, 0, &overlay.face)) != 0) {
		log_init_msg(B_TRUE, INIT_ERR_MSG_TIMEOUT, NULL, NULL,
		    "ND alert overlay initialization error: cannot "
		    "load font file %s: %s", filename, ft_err2str(err));
		VERIFY(FT_Done_FreeType(overlay.ft) == 0);
		free(filename);
		return (B_FALSE);
	}
	free(filename);

#ifdef	DEBUG_PANEL
	int w, h;
	get_text_block_size("00", overlay.face, DEBUG_PANEL_FONT_SIZE, &w, &h);
	debug_textures = calloc(NUM_DEBUG_PANELS, sizeof (*debug_textures));
	debug_panels = calloc(NUM_DEBUG_PANELS, sizeof (*debug_panels));
	for (int i = 0; i < NUM_DEBUG_PANELS; i++) {
		char text[8];
		snprintf(text, sizeof (text), "%02x", i);
		int x = (DEBUG_PANEL_SIZE - w) / 2;
		int y = (DEBUG_PANEL_SIZE + h) / 2;
		if (!render_text_block(text, overlay.face,
		    DEBUG_PANEL_FONT_SIZE,  x, y, 255, 255, 255,
		    debug_panels[i].tex, DEBUG_PANEL_SIZE, DEBUG_PANEL_SIZE)) {
			free(debug_textures);
			free(debug_panels);
			debug_textures = NULL;
			debug_panels = NULL;
			return (B_FALSE);
		}
	}
	glGenTextures(NUM_DEBUG_PANELS, debug_textures);
	for (int i = 0; i < NUM_DEBUG_PANELS; i++) {
		glBindTexture(GL_TEXTURE_2D, debug_textures[i]);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
		    GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
		    GL_NEAREST);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, DEBUG_PANEL_SIZE,
		    DEBUG_PANEL_SIZE, 0, GL_RGBA, GL_UNSIGNED_BYTE,
		    debug_panels[i].tex);
	}

	XPLMRegisterDrawCallback(debug_panel_draw_cb, DEBUG_PANEL_PHASE,
	    DEBUG_PANEL_PHASE_FLAG, NULL);
#endif	/* DEBUG_PANEL */

	dr = dr_intf_add_i(DR_NAME, &alert_status, B_FALSE);
	VERIFY(dr != NULL);
	dr_overlay = dr_intf_add_i(OVERLAY_DIS_DR, &alert_overlay_dis, B_TRUE);
	VERIFY(dr_overlay != NULL);
	XPLMRegisterFlightLoopCallback(alert_sched_cb, ND_SCHED_INTVAL, NULL);

	chk_acf_ND_integration();
	XPLMRegisterDrawCallback(nd_alert_draw_cb,
	    acf_ND_info != -1 ? xplm_Phase_Gauges : xplm_Phase_Window,
	    0, NULL);

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

	return (B_TRUE);
}

static void
render_alert_texture(void)
{
	char msg[16];
	int color, r, g, b, text_w, text_h, font_size;
	double bg_alpha;
	enum {
	    MARGIN_SIZE = 10
	};

	VERIFY(XRAAS_ND_msg_decode(alert_status, msg, &color) != 0);

	if (acf_ND_info != -1) {
		font_size = overlay_infos[acf_ND_info].font_sz;
		bg_alpha = overlay_infos[acf_ND_info].bg_alpha;
	} else {
		font_size = xraas_state->config.nd_alert_overlay_font_size;
		bg_alpha = 0.67;
	}

	if (!get_text_block_size(msg, overlay.face, font_size, &text_w,
	    &text_h))
		return;

	/* If the old alert is still being displayed, avoid leaking it. */
	if (overlay.buf != NULL) {
		glDeleteTextures(1, &overlay.texture);
		free(overlay.buf);
		overlay.buf = NULL;
	}

	overlay.width = text_w + 2 * MARGIN_SIZE;
	overlay.height = text_h + 2 * MARGIN_SIZE;
	overlay.buf = calloc(overlay.width * overlay.height * 4, 1);

	/* fill with a black, semi-transparent background */
	if (bg_alpha != 0) {
		for (int i = 0; i < overlay.width * overlay.height; i++)
			overlay.buf[i * 4 + 3] = (uint8_t)(255 * bg_alpha);
	}

	if (color == XRAAS_ND_ALERT_GREEN) {
		r = 0;
		g = 255;
		b = 0;
	} else {
		r = 255;
		g = 255;
		b = 0;
	}

	if (!render_text_block(msg, overlay.face, font_size, MARGIN_SIZE,
	    MARGIN_SIZE + font_size, r, g, b, overlay.buf, overlay.width,
	    overlay.height)) {
		free(overlay.buf);
		overlay.buf = NULL;
		return;
	}

	glGenTextures(1, &overlay.texture);
	glBindTexture(GL_TEXTURE_2D, overlay.texture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, overlay.width, overlay.height,
	    0, GL_RGBA, GL_UNSIGNED_BYTE, overlay.buf);
}

void
ND_alerts_fini()
{
	dbg_log(nd_alert, 1, "ND_alerts_fini");

	if (!inited)
		return;

	if (overlay.buf != NULL) {
		glDeleteTextures(1, &overlay.texture);
		free(overlay.buf);
		overlay.buf = NULL;
	}

#ifdef	DEBUG_PANEL
	if (debug_textures != NULL)
		glDeleteTextures(NUM_DEBUG_PANELS, debug_textures);
	free(debug_textures);
	free(debug_panels);
	XPLMUnregisterDrawCallback(debug_panel_draw_cb, DEBUG_PANEL_PHASE,
	    DEBUG_PANEL_PHASE_FLAG, NULL);
#endif

	VERIFY(FT_Done_Face(overlay.face) == 0);
	VERIFY(FT_Done_FreeType(overlay.ft) == 0);

	memset(&overlay, 0, sizeof (overlay));

	dr_intf_remove(dr);
	dr_intf_remove(dr_overlay);
	dr = NULL;
	XPLMUnregisterFlightLoopCallback(alert_sched_cb, NULL);
	XPLMUnregisterDrawCallback(nd_alert_draw_cb,
	    acf_ND_info != -1 ? xplm_Phase_Gauges : xplm_Phase_Window,
	    0, NULL);

	acf_ND_info = -1;

	inited = B_FALSE;
}

void
ND_alert(nd_alert_msg_type_t msg, nd_alert_level_t level, const char *rwy_id,
    int dist)
{
	if (!inited)
		return;

	ASSERT(msg >= ND_ALERT_FLAPS && msg <= ND_ALERT_DEEP_LAND);

	if (!xraas_state->config.nd_alerts_enabled)
		return;

	dbg_log(nd_alert, 1, "msg: %d level: %d rwy_ID: %s dist: %d",
	    msg, level, rwy_id, dist);

	if (level < (nd_alert_level_t)xraas_state->config.nd_alert_filter) {
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
		if (xraas_state->config.use_imperial)
			msg |= (((int)MET2FEET(dist) / 100) & 0xff) << 16;
		else
			msg |= ((dist / 100) & 0xff) << 16;
	}

	alert_status = msg;
	alert_start_time = microclock();

	if (xraas_state->config.nd_alert_overlay_enabled &&
	    alert_overlay_dis == 0)
		render_alert_texture();
}

void
ND_alert_overlay_enable(void)
{
	alert_overlay_dis = 0;
}
