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

#include <XPLMDisplay.h>
#include <XPLMGraphics.h>

#include "assert.h"

#include "init_msg.h"

#define	INIT_MSG_FONT		GLUT_BITMAP_HELVETICA_18
#define	INIT_MSG_FONT_HEIGHT	21

static struct {
	char		*msg;
	int		timeout;
	long long	end;
	int		width;
	int		height;
} init_msg = { NULL, 0, 0, 0, 0 };

static void
man_ref(char **str, size_t *cap, const char *section, const char *section_name)
{
	append_format(str, cap,
	    "\nFor more information, please refer to the X-RAAS "
	    "user manual in docs%cmanual.pdf, section %s \"%s\".", DIRSEP,
	    section, section_name);
}

/*
 * Draw callback for the init message mechanism. Paints the actual black
 * square with the text in it. Once the timeout to show the message expires,
 * this callback unregisters itself automatically and clears all state.
 */
static int
draw_init_msg(XPLMDrawingPhase phase, int before, void *refcon)
{
	int screen_x, screen_y, x, y;
	enum { MARGIN_SIZE = 10 };

	ASSERT(init_msg.msg != NULL);
	UNUSED(phase);
	UNUSED(before);
	UNUSED(refcon);

	if (init_msg.end == 0)
		init_msg.end = microclock() + SEC2USEC(init_msg.timeout);

	if (microclock() > init_msg.end) {
		init_msg_sys_fini();
		return (1);
	}

	XPLMGetScreenSize(&screen_x, &screen_y);
	glColor4f(0, 0, 0, 0.67);
	glBegin(GL_POLYGON);
	glVertex2f((screen_x - init_msg.width) / 2 - MARGIN_SIZE, 0);
	glVertex2f((screen_x - init_msg.width) / 2 - MARGIN_SIZE,
	    init_msg.height + 2 * MARGIN_SIZE);
	glVertex2f((screen_x + init_msg.width) / 2 + MARGIN_SIZE,
	    init_msg.height + 2 * MARGIN_SIZE);
	glVertex2f((screen_x + init_msg.width) / 2 + MARGIN_SIZE, 0);
	glEnd();

	glColor4f(1, 1, 1, 1);
	x = (screen_x - init_msg.width) / 2;
	y = init_msg.height + MARGIN_SIZE - INIT_MSG_FONT_HEIGHT;
	glRasterPos2f(x, y);
	for (int i = 0, n = strlen(init_msg.msg); i < n; i++) {
		if (init_msg.msg[i] != '\n') {
			glutBitmapCharacter(INIT_MSG_FONT, init_msg.msg[i]);
		} else {
			y -= INIT_MSG_FONT_HEIGHT;
			glRasterPos2f(x, y);
		}
	}

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
		int line_width = 0;

		init_msg_sys_fini();

		XPLMRegisterDrawCallback(draw_init_msg, xplm_Phase_Window,
		    0, NULL);

		init_msg.msg = msg;
		init_msg.timeout = timeout;
		init_msg.width = 0;
		init_msg.height = INIT_MSG_FONT_HEIGHT;

		for (int i = 0, n = strlen(msg); i < n; i++) {
			if (msg[i] == '\n') {
				init_msg.height += INIT_MSG_FONT_HEIGHT;
				init_msg.width = MAX(init_msg.width,
				    line_width);
				line_width = 0;
			} else {
				line_width += glutBitmapWidth(INIT_MSG_FONT,
				    msg[i]);
			}
		}
		init_msg.width = MAX(line_width, init_msg.width);
		line_width = 0;
	} else {
		free(msg);
	}
}

void
init_msg_sys_fini(void)
{
	if (init_msg.msg != NULL) {
		free(init_msg.msg);
		memset(&init_msg, 0, sizeof (init_msg));
		XPLMUnregisterDrawCallback(draw_init_msg, xplm_Phase_Window,
		    0, NULL);
	}
}
