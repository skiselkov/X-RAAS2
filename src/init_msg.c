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
 * Copyright 2016 Saso Kiselkov. All rights reserved.
 */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <XPLMDisplay.h>
#include <XPLMGraphics.h>
#include <XPLMProcessing.h>

#if	IBM
#include <gl.h>
#elif	APL
#include <OpenGL/gl.h>
#else	/* LIN */
#include <GL/gl.h>
#endif	/* LIN */

#include "assert.h"
#include "text_rendering.h"
#include "xraas2.h"

#include "init_msg.h"

#define	INIT_MSG_SCHED_INTVAL	1.0	/* seconds */
#if	IBM
#define	INIT_MSG_FONT		"Aileron\\Aileron-Regular.otf"
#else	/* !IBM */
#define	INIT_MSG_FONT		"Aileron/Aileron-Regular.otf"
#endif	/* !IBM */
#define	INIT_MSG_FONT_SIZE	21

enum { MARGIN_SIZE = 10 };

static bool_t inited = B_FALSE;

static struct {
	char		*msg;
	int		timeout;
	long long	end;
	int		width;
	int		height;
	GLuint		texture;
	uint8_t		*bytes;
} init_msg = { NULL, 0, 0, 0, 0, 0, NULL };

static FT_Library ft;
static FT_Face face;

static int draw_init_msg_cb(XPLMDrawingPhase phase, int before, void *refcon);

static void
man_ref(char **str, size_t *cap, const char *section, const char *section_name)
{
	append_format(str, cap,
	    "\nFor more information, please refer to the X-RAAS "
	    "user manual in docs%cmanual.pdf, section %s \"%s\".", DIRSEP,
	    section, section_name);
}

static void
clear_init_msg(void)
{
	if (init_msg.msg != NULL) {
		glDeleteTextures(1, &init_msg.texture);
		free(init_msg.msg);
		free(init_msg.bytes);
		memset(&init_msg, 0, sizeof (init_msg));
		XPLMUnregisterDrawCallback(draw_init_msg_cb, xplm_Phase_Window,
		    0, NULL);
	}
}

/*
 * Draw callback for the init message mechanism. Paints the actual black
 * square with the text in it. Once the timeout to show the message expires,
 * this callback unregisters itself automatically and clears all state.
 */
static int
draw_init_msg_cb(XPLMDrawingPhase phase, int before, void *refcon)
{
	int screen_x, screen_y;

	UNUSED(phase);
	UNUSED(before);
	UNUSED(refcon);

	ASSERT(init_msg.msg != NULL);

	if (init_msg.end == 0)
		init_msg.end = microclock() + SEC2USEC(init_msg.timeout);

	XPLMGetScreenSize(&screen_x, &screen_y);
	XPLMSetGraphicsState(1, 1, 0, 1, 1, 1, 1);

	glBindTexture(GL_TEXTURE_2D, init_msg.texture);

	glBegin(GL_QUADS);
	glTexCoord2f(0.0, 1.0);
	glVertex2f((screen_x - init_msg.width) / 2 - MARGIN_SIZE, 0);
	glTexCoord2f(0.0, 0.0);
	glVertex2f((screen_x - init_msg.width) / 2 - MARGIN_SIZE,
	    init_msg.height + 2 * MARGIN_SIZE);
	glTexCoord2f(1.0, 0.0);
	glVertex2f((screen_x + init_msg.width) / 2 + MARGIN_SIZE,
	    init_msg.height + 2 * MARGIN_SIZE);
	glTexCoord2f(1.0, 1.0);
	glVertex2f((screen_x + init_msg.width) / 2 + MARGIN_SIZE, 0);
	glEnd();

	return (1);
}

/*
 * Logs an initialization error message to the Log.txt file and optionally
 * also displays it for a given duration on the screen. This can be called
 * as many times as is necessary, but only the last invocation will actually
 * show the message on screen. It can also be called after init is complete.
 *
 * @param display Flag indicating whether to also show the message on screen
 *	or only log it to the Log.txt file.
 * @param timeout How many seconds to display the message.
 * @param man_sect If not NULL, appends a `For more information...'
 *	reference to the specified manual section.
 * @param man_sect_name If not NULL, appends a `For more information...'
 *	reference to the specified manual section name.
 * @param fmt printf-like format string for the message. Also supply any
 *	extra arguments as required by the format string.
 */
void
log_init_msg(bool_t display, int timeout, const char *man_sect,
    const char *man_sect_name, const char *fmt, ...)
{
	va_list ap;
	int len;
	char *msg;

	va_start(ap, fmt);
	len = vsnprintf(NULL, 0, fmt, ap);
	va_end(ap);

	msg = calloc(1, len + 1);

	va_start(ap, fmt);
	vsnprintf(msg, len + 1, fmt, ap);
	va_end(ap);

	if (man_sect != NULL) {
		size_t sz = len + 1;
		man_ref(&msg, &sz, man_sect, man_sect_name);
	}

	logMsg("%s", msg);
	if (display) {
		int tex_w, tex_h;
		uint8_t *tex_bytes;

		clear_init_msg();

		if (!get_text_block_size(msg, face, INIT_MSG_FONT_SIZE,
		    &init_msg.width, &init_msg.height)) {
			free(msg);
			return;
		}

		tex_w = init_msg.width + 2 * MARGIN_SIZE;
		tex_h = init_msg.height + 2 * MARGIN_SIZE;
		tex_bytes = calloc(tex_w * tex_h * 4, 1);

		/* fill with a black, semi-transparent background */
		for (int i = 0; i < tex_w * tex_h; i++)
			tex_bytes[i * 4 + 3] = (uint8_t)(255 * 0.67);

		if (!render_text_block(msg, face, INIT_MSG_FONT_SIZE,
		    MARGIN_SIZE, MARGIN_SIZE + INIT_MSG_FONT_SIZE,
		    255, 255, 255, tex_bytes, tex_w, tex_h)) {
			free(msg);
			free(tex_bytes);
			return;
		}

		init_msg.msg = msg;
		init_msg.timeout = timeout;
		init_msg.bytes = tex_bytes;

		XPLMRegisterDrawCallback(draw_init_msg_cb, xplm_Phase_Window,
		    0, NULL);

		glGenTextures(1, &init_msg.texture);
		glBindTexture(GL_TEXTURE_2D, init_msg.texture);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
		    GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
		    GL_NEAREST);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, tex_w, tex_h, 0,
		    GL_RGBA, GL_UNSIGNED_BYTE, init_msg.bytes);
	} else {
		free(msg);
	}
}

static float
init_msg_sched_cb(float elapsed_since_last_call, float elapsed_since_last_floop,
    int counter, void *refcon)
{
	UNUSED(elapsed_since_last_call);
	UNUSED(elapsed_since_last_floop);
	UNUSED(counter);
	UNUSED(refcon);

	if (init_msg.end != 0 && microclock() > init_msg.end) {
		clear_init_msg();
		return (1);
	}

	return (INIT_MSG_SCHED_INTVAL);
}

bool_t
init_msg_sys_init(void)
{
	FT_Error err;
	char *filename;

	ASSERT(!inited);

	XPLMRegisterFlightLoopCallback(init_msg_sched_cb,
	    INIT_MSG_SCHED_INTVAL, NULL);
	if ((err = FT_Init_FreeType(&ft)) != 0) {
		logMsg("Error initializing FreeType library: %s",
		    ft_err2str(err));
		return (B_FALSE);
	}

	filename = mkpathname(xraas_plugindir, "data", "fonts", INIT_MSG_FONT,
	    NULL);
	if ((err = FT_New_Face(ft, filename, 0, &face)) != 0) {
		logMsg("Error loading init_msg font %s: %s", filename,
		    ft_err2str(err));
		VERIFY(FT_Done_FreeType(ft) == 0);
		free(filename);
		return (B_FALSE);
	}
	free(filename);

	inited = B_TRUE;

	return (B_TRUE);
}

void
init_msg_sys_fini(void)
{
	if (!inited)
		return;

	VERIFY(FT_Done_Face(face) == 0);
	VERIFY(FT_Done_FreeType(ft) == 0);
	clear_init_msg();
	XPLMUnregisterFlightLoopCallback(init_msg_sched_cb, NULL);
}
