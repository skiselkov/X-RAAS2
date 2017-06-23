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

#define	NUM_DEBUG_PANEL_ROWS	32
#define	NUM_DEBUG_PANEL_COLS	32
#define	NUM_DEBUG_PANELS	(NUM_DEBUG_PANEL_ROWS * NUM_DEBUG_PANEL_COLS)
#define	DEBUG_PANEL_SIZE	64
#define	DEBUG_PANEL_OFF		4
#define	DEBUG_PANEL_PHASE	xplm_Phase_Gauges
#define	DEBUG_PANEL_PHASE_FLAG	0
#define	DEBUG_PANEL_FONT_SIZE	24
typedef struct {
	uint8_t	tex[DEBUG_PANEL_SIZE * DEBUG_PANEL_SIZE * 4];
} debug_panel_t;
static GLuint		*debug_textures = NULL;
static debug_panel_t	*debug_panels = NULL;

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
	double		hoff, voff;
	list_node_t	node;
} ND_coords_t;

typedef struct {
	const char	ICAO[8];
	const char	acf_filename[64];
	const char	studio[64];
	int		font_sz;
	double		bg_alpha;
	list_t		NDs;
} acf_ND_overlay_info_t;

static acf_ND_overlay_info_t *overlay_info = NULL;

static int
debug_panel_draw_cb(XPLMDrawingPhase phase, int before, void *refcon)
{
	enum {
	    SPACE_WIDTH = 2048,
	    SPACE_HEIGHT = 2048,
	    CELL_WIDTH = (SPACE_WIDTH / NUM_DEBUG_PANEL_COLS),
	    CELL_HEIGHT = (SPACE_HEIGHT / NUM_DEBUG_PANEL_ROWS)
	};

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

static void
nd_alert_draw_at(int x, int y, int w, int h, double hoff, double voff)
{
	/* only disable lighting, everything else is on */
	XPLMSetGraphicsState(1, 1, 0, 1, 1, 1, 1);

	glBegin(GL_QUADS);
	glTexCoord2f(0.0, 1.0);
	glVertex2f(x + (w - overlay.width) * hoff,
	    y + (h * voff) - overlay.height);
	glTexCoord2f(0.0, 0.0);
	glVertex2f(x + (w - overlay.width) * hoff, y + h * voff);
	glTexCoord2f(1.0, 0.0);
	glVertex2f(x + (w + overlay.width) * hoff, y + h * voff);
	glTexCoord2f(1.0, 1.0);
	glVertex2f(x + (w + overlay.width) * hoff,
	    y + h * voff - overlay.height);
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

	if (overlay_info != NULL) {
		for (ND_coords_t *nd = list_head(&overlay_info->NDs);
		    nd != NULL; nd = list_next(&overlay_info->NDs, nd)) {
			nd_alert_draw_at(nd->x, nd->y, nd->w, nd->h,
			    nd->hoff, nd->voff);
		}
	} else {
		int screen_x, screen_y;
		XPLMGetScreenSize(&screen_x, &screen_y);
		nd_alert_draw_at(0, 0, screen_x, screen_y, 0.5, 0.98);
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
ND_integ_debug_init(void)
{
	int w, h;

	dbg_log(nd_alert, 1, "ND_integ_debug_init %dx%d (%dx%d)",
	    NUM_DEBUG_PANEL_COLS, NUM_DEBUG_PANEL_ROWS, DEBUG_PANEL_SIZE,
	    DEBUG_PANEL_SIZE);

	get_text_block_size("0000", overlay.face, DEBUG_PANEL_FONT_SIZE,
	    &w, &h);
	debug_textures = calloc(NUM_DEBUG_PANELS, sizeof (*debug_textures));
	debug_panels = calloc(NUM_DEBUG_PANELS, sizeof (*debug_panels));
	for (int i = 0; i < NUM_DEBUG_PANELS; i++) {
		char text[8];
		snprintf(text, sizeof (text), "%02x%02x",
		    i / NUM_DEBUG_PANEL_COLS, i % NUM_DEBUG_PANEL_COLS);
		int x = (DEBUG_PANEL_SIZE - w) / 2;
		int y = (DEBUG_PANEL_SIZE + h) / 2;
		VERIFY(render_text_block(text, overlay.face,
		    DEBUG_PANEL_FONT_SIZE,  x, y, 255, 255, 255,
		    debug_panels[i].tex, DEBUG_PANEL_SIZE, DEBUG_PANEL_SIZE));
	}
	glGenTextures(NUM_DEBUG_PANELS, debug_textures);
	VERIFY(glGetError() == GL_NO_ERROR);
	for (int i = 0; i < NUM_DEBUG_PANELS; i++) {
		glBindTexture(GL_TEXTURE_2D, debug_textures[i]);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
		    GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
		    GL_NEAREST);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, DEBUG_PANEL_SIZE,
		    DEBUG_PANEL_SIZE, 0, GL_RGBA, GL_UNSIGNED_BYTE,
		    debug_panels[i].tex);
		VERIFY(glGetError() == GL_NO_ERROR);
	}

	XPLMRegisterDrawCallback(debug_panel_draw_cb, DEBUG_PANEL_PHASE,
	    DEBUG_PANEL_PHASE_FLAG, NULL);
}

/*
 * Initializes the ND alert display integration engine. On certain aircraft
 * models, rather than drawing the ND alert overlay on the screen, we draw
 * it into the aircraft's navigation display directly. We do so by exploting
 * xplm_Draw_Gauges and manually figuring out where the ND's texture is
 * located.
 *
 * This function attempts to match the currently loaded aircraft with our
 * ND position configuration file in data/ND_overlays.cfg. The file is
 * parsed here. It consists of a set of whitespace-separated keywords with
 * optional arguments. String arguments allow for "%XY" escape sequences.
 * See unescape_percent in helpers.c.
 * A typical config file will consists from one or more blocks like this:
 *	icao	ABCD
 *	studio	Foo%20Bar%20Studios
 *	author	Bob%20The%20Aircraft%20Builder
 *	acf	WrightFlyer3000.acf
 *	fontsz	25
 *	bgalpha	0.5
 *	nd	x 0	y 5	w 300	h 250	hoff 0.2	voff 0.6
 * These keywords have the following meanings:
 *	icao (required): Denotes the start of an aircraft block and must be
 *		followed by a 4-letter ICAO aircraft type identifier (e.g.
 *		"B752"). This must be matched by the ICAO identifier of the
 *		currently loaded aircraft.
 *	studio (optional): When specified, checks if the currently loaded
 *		aircraft's studio (as defined in Plane Maker) matches the
 *		string argument.
 *	author (optional): When specified, checks if the currently loaded
 *		aircraft's author (as defined in Plane Maker) matches the
 *		string argument.
 *	acf	(optional): When specified, checks if the currently loaded
 *		aircraft's ACF filename matches the string argument.
 *	fontsz (required): Specifies the pixel height of the font to be
 *		used when rendering the ND alert.
 *	bgalpha (optional): Specifies how opaque the black background around
 *		the ND alert text should be (0.0 for fully transparent and
 *		1.0 for fully opaque). The default is 0.5.
 *	nd :	Starts an ND panel configuration block. You may provide
 *		multiple ND blocks in case the aircraft has multiple
 *		independent ND screens.
 *	x, y, w, h (required): specifies X & Y pixel coordinates of the lower
 *		left edge of the ND screen + the screen's width & height.
 *	hoff, voff (optional): specifies the horizontal and vertical offset
 *		of the center of the ND alert from the top left of the ND
 *		screen as a fraction of the screen's width & height.
 *		"hoff 0 voff 0" means the ND alert will be centered on the
 *		top left screen edge, whereas "hoff 1 voff 1" will center
 *		the ND alert on the bottom right edge of the screen. The
 *		default for both parameters is 0.5, meaning, the ND alert
 *		will appear in the center of the screen.
 */
static void
ND_integ_init(void)
{
	char		buf[128] = { 0 };
	bool_t		debug_on = B_FALSE;
	FILE		*fp;
	char		*filename;
	ND_coords_t	*nd = NULL;
	char		my_icao[8] = { 0 }, my_author[256] = { 0 };
	char		my_studio[256] = { 0 }, my_acf[256] = { 0 };
	char		acf_path[512] = { 0 };
	char		*line = NULL;
	size_t		line_len = 0;
	XPLMDataRef icao_dr = XPLMFindDataRef("sim/aircraft/view/acf_ICAO");
	XPLMDataRef auth_dr = XPLMFindDataRef("sim/aircraft/view/acf_author");
	bool_t		skip = B_FALSE;

	XPLMGetNthAircraftModel(0, my_acf, acf_path);
	XPLMGetDatab(icao_dr, my_icao, 0, sizeof (my_icao) - 1);
	my_icao[sizeof (my_icao) - 1] = 0;

	XPLMGetDatab(auth_dr, my_author, 0, sizeof (my_author) - 1);
	my_author[sizeof (my_author) - 1] = 0;

	/*
	 * Unfortunately the studio isn't available via datarefs, so parse
	 * our acf file instead.
	 */
	fp = fopen(acf_path, "r");
	if (fp == NULL)
		return;
	while (getline(&line, &line_len, fp) != 0) {
		if (strstr(line, "P acf/_studio ") == line) {
			strip_space(line);
			my_strlcpy(my_studio, &line[14], sizeof (my_studio));
			break;
		}
	}
	fclose(fp);

	dbg_log(nd_alert, 3, "attempting ND_overlays.cfg match, %s/%s/%s/%s",
	    my_icao, my_acf, my_studio, my_author);

	filename = mkpathname(xraas_plugindir, "data", "ND_overlays.cfg", NULL);
	fp = fopen(filename, "r");
	free(filename);
	if (fp == NULL)
		return;

#define	FILTER_PARAM(param) \
	do { \
		char param[256]; \
		int res; \
		if (overlay_info == NULL) \
			continue; \
		if (fscanf(fp, "%255s", param) != 1) { \
			logMsg("Error parsing ND_overlays.cfg: expected " \
			    "string following \"" #param "\"."); \
			goto errout; \
		} \
		unescape_percent(param); \
		res = strcmp(param, my_ ## param); \
		dbg_log(nd_alert, 3, #param "(\"%s\") %s my_" #param, \
		    param, (res == 0 ? "==" : "!=")); \
		if (res != 0) { \
			list_destroy(&overlay_info->NDs); \
			free(overlay_info); \
			overlay_info = NULL; \
			skip = B_TRUE; \
		} \
	} while (0)

#define	PARSE_ND_PARAM(param, fmt, typename) \
	do { \
		if (overlay_info == NULL) \
			continue; \
		if (nd == NULL) { \
			logMsg("Error parsing ND_overlays.cfg: " \
			    "\"" #param "\" must be preceded by \"nd\"."); \
			goto errout; \
		} \
		if (fscanf(fp, fmt, &nd->param) != 1) { \
			logMsg("Error parsing ND_overlays.cfg: expected " \
			    typename " following \"" #param "\"."); \
			goto errout; \
		} \
		dbg_log(nd_alert, 3, "ND->" #param " = " fmt, nd->param); \
	} while (0)

	while (!feof(fp) && fscanf(fp, "%127s", buf) == 1) {
		if (buf[0] == '#') {
			logMsg("found comment, skipping");
			while (fgetc(fp) != '\n' && !feof(fp))
				;
			continue;
		}
		if (!skip)
			dbg_log(nd_alert, 4, "found keyword \"%s\"", buf);
		if (strcmp(buf, "debug") == 0) {
			debug_on = B_TRUE;
			dbg_log(nd_alert, 3, "ND debug on");
		} else if (strcmp(buf, "icao") == 0) {
			char icao[8];
			int res;
			if (overlay_info != NULL) {
				/* We're done parsing the entry we wanted */
				break;
			}
			if (fscanf(fp, "%7s", icao) != 1) {
				logMsg("Error parsing ND_overlays.cfg: "
				    "expected string following \"icao\".");
				goto errout;
			}
			unescape_percent(icao);
			res = strcmp(icao, my_icao);
			dbg_log(nd_alert, 3, "icao(\"%s\") %s my_icao", icao,
			    (res == 0 ? "==" : "!="));
			if (res == 0) {
				overlay_info = calloc(1,
				    sizeof (*overlay_info));
				list_create(&overlay_info->NDs,
				    sizeof (ND_coords_t),
				    offsetof(ND_coords_t, node));
				overlay_info->bg_alpha = 0.5;
				skip = B_FALSE;
			} else {
				skip = B_TRUE;
			}
		} else if (strcmp(buf, "studio") == 0) {
			FILTER_PARAM(studio);
		} else if (strcmp(buf, "acf") == 0) {
			FILTER_PARAM(acf);
		} else if (strcmp(buf, "author") == 0) {
			FILTER_PARAM(author);
		} else if (strcmp(buf, "fontsz") == 0) {
			if (overlay_info == NULL)
				continue;
			if (fscanf(fp, "%d", &overlay_info->font_sz) != 1) {
				logMsg("Error parsing ND_overlays.cfg: "
				    "expected integer following \"fontsz\".");
				goto errout;
			}
			dbg_log(nd_alert, 3, "fontsz: %d",
			    overlay_info->font_sz);
		} else if (strcmp(buf, "bgalpha") == 0) {
			if (overlay_info == NULL)
				continue;
			if (fscanf(fp, "%lf", &overlay_info->bg_alpha) != 1) {
				logMsg("Error parsing ND_overlays.cfg: "
				    "expected float following \"bgalpha\".");
				goto errout;
			}
			dbg_log(nd_alert, 3, "bgalpha: %.3f",
			    overlay_info->bg_alpha);
		} else if (strcmp(buf, "nd") == 0) {
			if (overlay_info == NULL)
				continue;
			nd = calloc(1, sizeof (*nd));
			list_insert_tail(&overlay_info->NDs, nd);
			nd->hoff = 0.5;
			nd->voff = 0.5;
			dbg_log(nd_alert, 3, "new ND");
		} else if (strcmp(buf, "x") == 0) {
			PARSE_ND_PARAM(x, "%d", "integer");
		} else if (strcmp(buf, "y") == 0) {
			PARSE_ND_PARAM(y, "%d", "integer");
		} else if (strcmp(buf, "w") == 0) {
			PARSE_ND_PARAM(w, "%d", "integer");
		} else if (strcmp(buf, "h") == 0) {
			PARSE_ND_PARAM(h, "%d", "integer");
		} else if (strcmp(buf, "hoff") == 0) {
			PARSE_ND_PARAM(hoff, "%lf", "float");
		} else if (strcmp(buf, "voff") == 0) {
			PARSE_ND_PARAM(voff, "%lf", "float");
		} else if (!skip) {
			logMsg("Error parsing ND_overlays.cfg: "
			    "unknown keyword \"%s\".", buf);
			goto errout;
		}
	}
#undef	FILTER_PARAM
#undef	PARSE_ND_PARAM

	if (overlay_info != NULL) {
		dbg_log(nd_alert, 1, "ND_integ_init: match %s/%s/%s/%s",
		    my_icao, my_acf, my_studio, my_author);
	}

	if (debug_on)
		ND_integ_debug_init();

	fclose(fp);
	return;

errout:
	if (overlay_info != NULL) {
		ND_coords_t *nd;
		while ((nd = list_head(&overlay_info->NDs)) != NULL) {
			list_remove_head(&overlay_info->NDs);
			free(nd);
		}
		free(overlay_info);
		overlay_info = NULL;
	}
	fclose(fp);
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

	ND_integ_init();

	dr = dr_intf_add_i(DR_NAME, &alert_status, B_FALSE);
	VERIFY(dr != NULL);
	dr_overlay = dr_intf_add_i(OVERLAY_DIS_DR, &alert_overlay_dis, B_TRUE);
	VERIFY(dr_overlay != NULL);
	XPLMRegisterFlightLoopCallback(alert_sched_cb, ND_SCHED_INTVAL, NULL);

	XPLMRegisterDrawCallback(nd_alert_draw_cb,
	    overlay_info != NULL ? xplm_Phase_Gauges : xplm_Phase_Window,
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

/*
 * Grabs the current setting of the ND alert, decodes it into text and
 * renders a texture suitable for display either on the ND alert overlay,
 * or on the ND in the VC of a supported aircraft (see ND_integ_init).
 */
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

	if (overlay_info != NULL) {
		font_size = overlay_info->font_sz;
		bg_alpha = overlay_info->bg_alpha;
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

	if (debug_textures != NULL) {
		glDeleteTextures(NUM_DEBUG_PANELS, debug_textures);
		free(debug_textures);
		free(debug_panels);
		debug_textures = NULL;
		debug_panels = NULL;
		XPLMUnregisterDrawCallback(debug_panel_draw_cb,
		    DEBUG_PANEL_PHASE, DEBUG_PANEL_PHASE_FLAG, NULL);
	}

	VERIFY(FT_Done_Face(overlay.face) == 0);
	VERIFY(FT_Done_FreeType(overlay.ft) == 0);

	memset(&overlay, 0, sizeof (overlay));

	dr_intf_remove(dr);
	dr_intf_remove(dr_overlay);
	dr = NULL;
	XPLMUnregisterFlightLoopCallback(alert_sched_cb, NULL);
	XPLMUnregisterDrawCallback(nd_alert_draw_cb,
	    overlay_info != NULL ? xplm_Phase_Gauges : xplm_Phase_Window,
	    0, NULL);

	if (overlay_info != NULL) {
		ND_coords_t *nd;
		while ((nd = list_head(&overlay_info->NDs)) != NULL) {
			list_remove_head(&overlay_info->NDs);
			free(nd);
		}
		list_destroy(&overlay_info->NDs);
		free(overlay_info);
		overlay_info = NULL;
	}

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

	if ((xraas_state->config.nd_alert_overlay_enabled &&
	    alert_overlay_dis == 0) || overlay_info != NULL)
		render_alert_texture();
}

void
ND_alert_overlay_enable(void)
{
	alert_overlay_dis = 0;
}
