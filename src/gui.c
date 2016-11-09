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

#include <XPLMMenus.h>

#include "assert.h"
#include "dbg_gui.h"
#include "xraas2.h"

#include "gui.h"

#define	XRAAS_MENU_NAME		"X-RAAS"
#define	CONFIG_GUI_CMD_NAME	"Config GUI..."
#define	DBG_GUI_TOGGLE_CMD_NAME	"Toggle debug GUI..."
#define	RAAS_RESET_CMD_NAME	"Reset"

enum {
	CONFIG_GUI_CMD,
	DBG_GUI_TOGGLE_CMD,
	RAAS_RESET_CMD
};

static int plugins_menu_item;
static XPLMMenuID root_menu;
static int dbg_gui_menu_item;

static void
menu_cb(void *menu, void *item)
{
	int cmd = (long)item;

	UNUSED(menu);
	switch (cmd) {
	case CONFIG_GUI_CMD:
		break;
	case DBG_GUI_TOGGLE_CMD:
		ASSERT(xraas_inited);
		if (!dbg_gui_inited)
			dbg_gui_init();
		else
			dbg_gui_fini();
		XPLMCheckMenuItem(root_menu, dbg_gui_menu_item,
		    dbg_gui_inited ? xplm_Menu_Checked : xplm_Menu_Unchecked);
		break;
	case RAAS_RESET_CMD:
		xraas_fini();
		xraas_init();
		gui_update();
		break;
	}
}

void
gui_init(void)
{
	plugins_menu_item = XPLMAppendMenuItem(XPLMFindPluginsMenu(),
	    XRAAS_MENU_NAME, NULL, 1);
	root_menu = XPLMCreateMenu(XRAAS_MENU_NAME, XPLMFindPluginsMenu(),
	    plugins_menu_item, menu_cb, NULL);
	XPLMAppendMenuItem(root_menu, CONFIG_GUI_CMD_NAME,
	    (void *)CONFIG_GUI_CMD, 1);
	dbg_gui_menu_item = XPLMAppendMenuItem(root_menu,
	    DBG_GUI_TOGGLE_CMD_NAME, (void *)DBG_GUI_TOGGLE_CMD, 1);
	XPLMAppendMenuItem(root_menu, RAAS_RESET_CMD_NAME,
	    (void *)RAAS_RESET_CMD, 1);
}

void
gui_fini(void)
{
	XPLMDestroyMenu(root_menu);
	XPLMRemoveMenuItem(XPLMFindPluginsMenu(), plugins_menu_item);
}

void
gui_update(void)
{
	XPLMEnableMenuItem(root_menu, dbg_gui_menu_item, xraas_inited);
	XPLMCheckMenuItem(root_menu, dbg_gui_menu_item,
	    xraas_state->debug_graphical ? xplm_Menu_Checked :
	    xplm_Menu_Unchecked);
}
