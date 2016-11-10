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

#include <stddef.h>
#include <string.h>

#include <XPLMDisplay.h>
#include <XPLMGraphics.h>
#include <XPLMProcessing.h>
#include <XPLMMenus.h>
#include <XPWidgets.h>
#include <XPStandardWidgets.h>

#include "assert.h"
#include "dbg_gui.h"
#include "list.h"
#include "xraas2.h"

#include "gui.h"
#include "gui_tooltips.h"

#define	XRAAS_MENU_NAME		"X-RAAS"
#define	CONFIG_GUI_CMD_NAME	"Config GUI..."
#define	DBG_GUI_TOGGLE_CMD_NAME	"Toggle debug overlay"
#define	RAAS_RESET_CMD_NAME	"Reset"

#define	MAIN_WINDOW_WIDTH	800
#define	MAIN_WINDOW_HEIGHT	400

#define	TOOLTIP_INTVAL		0.1
#define	TOOLTIP_DISPLAY_DELAY	SEC2USEC(1)

typedef struct {
	int		x, y, w, h;
	const char	**lines;
	list_node_t	node;
} tooltip_t;

typedef struct {
	XPWidgetID	window;
	list_t		tooltips;
	list_node_t	node;
} tooltip_set_t;

enum {
	CONFIG_GUI_CMD,
	DBG_GUI_TOGGLE_CMD,
	RAAS_RESET_CMD
};

static int plugins_menu_item;
static XPLMMenuID root_menu;
static int dbg_gui_menu_item;

static XPWidgetID main_window;

static struct {
	XPWidgetID	enabled;
	XPWidgetID	allow_helos;
	XPWidgetID	startup_notify;
	XPWidgetID	use_imperial;
	XPWidgetID	us_runway_numbers;
	XPWidgetID	too_high_enabled;
	XPWidgetID	too_fast_enabled;
	XPWidgetID	alt_setting_enabled;
	XPWidgetID	qnh_alt_enabled;
	XPWidgetID	qfe_alt_enabled;
	XPWidgetID	disable_ext_view;
	XPWidgetID	override_electrical;
	XPWidgetID	override_replay;
	XPWidgetID	speak_units;
	XPWidgetID	use_tts;
	XPWidgetID	nd_alerts_enabled;
	XPWidgetID	nd_alert_overlay_enabled;
	XPWidgetID	nd_alert_overlay_force;
	XPWidgetID	openal_shared;
} buttons;

static struct {
	XPWidgetID	min_engines;
	XPWidgetID	min_mtow;
	XPWidgetID	min_takeoff_dist;
	XPWidgetID	min_landing_dist;
	XPWidgetID	min_rotation_dist;
	XPWidgetID	min_rotation_angle;
	XPWidgetID	stop_dist_cutoff;
	XPWidgetID	on_rwy_warn_initial;
	XPWidgetID	on_rwy_warn_repeat;
	XPWidgetID	on_rwy_warn_max_n;
	XPWidgetID	gpa_limit_mult;
	XPWidgetID	gpa_limit_max;
	XPWidgetID	long_land_lim_abs;
	XPWidgetID	long_land_lim_fract;
	XPWidgetID	nd_alerts_enabled_timeout;
} text_fields;

static list_t tooltip_sets;
static tooltip_t *cur_tt = NULL;
static XPWidgetID cur_tt_win = NULL;
static int last_mouse_x, last_mouse_y;
static uint64_t mouse_moved_time;

static XPWidgetID
create_widget_rel(int x, int y, bool_t y_from_bottom, int width, int height,
    int visible, const char *descr, int root, XPWidgetID container,
    XPWidgetClass cls)
{
	int wleft = 0, wtop = 0, wright = 0, wbottom = 0;
	int bottom, right;

	if (container != NULL)
		XPGetWidgetGeometry(container, &wleft, &wtop, &wright,
		    &wbottom);
	else
		XPLMGetScreenSize(&wright, &wtop);

	x += wleft;
	if (!y_from_bottom) {
		y = wtop - y;
		bottom = y - height;
	} else {
		bottom = y;
		y = y + height;
	}
	right = x + width;

	return (XPCreateWidget(x, y, right, bottom, visible, descr, root,
	    container, cls));
}

static void
tooltip_new(tooltip_set_t *tts, int x, int y, int w, int h, const char *lines[])
{
	ASSERT(lines[0] != NULL);

	tooltip_t *tt = malloc(sizeof (*tt));
	tt->x = x;
	tt->y = y;
	tt->w = w;
	tt->h = h;
	tt->lines = lines;
	list_insert_tail(&tts->tooltips, tt);
}

static tooltip_set_t *
tooltip_set_new(XPWidgetID window)
{
	tooltip_set_t *tts = malloc(sizeof (*tts));
	tts->window = window;
	list_create(&tts->tooltips, sizeof (tooltip_t),
	    offsetof(tooltip_t, node));
	list_insert_tail(&tooltip_sets, tts);
	return (tts);
}

static void
destroy_cur_tt(void)
{
	ASSERT(cur_tt_win != NULL);
	ASSERT(cur_tt != NULL);
	XPDestroyWidget(cur_tt_win, 1);
	cur_tt_win = NULL;
	cur_tt = NULL;
}

static void
tooltip_set_destroy(tooltip_set_t *tts)
{
	tooltip_t *tt;
	while ((tt = list_head(&tts->tooltips)) != NULL) {
		if (cur_tt == tt)
			destroy_cur_tt();
		list_remove(&tts->tooltips, tt);
		free(tt);
	}
	list_destroy(&tts->tooltips);
	list_remove(&tooltip_sets, tts);
	free(tts);
}

static void
set_cur_tt(tooltip_t *tt, int mouse_x, int mouse_y)
{
	enum {
		TOOLTIP_LINE_HEIGHT = 13,
		WINDOW_MARGIN = 10,
		WINDOW_OFFSET = 5
	};
	int width = 2 * WINDOW_MARGIN;
	int height = 2 * WINDOW_MARGIN;

	ASSERT(cur_tt == NULL);
	ASSERT(cur_tt_win == NULL);

	for (int i = 0; tt->lines[i] != NULL; i++) {
		const char *line = tt->lines[i];
		width = MAX(XPLMMeasureString(xplmFont_Proportional, line,
		    strlen(line)) + 2 * WINDOW_MARGIN, width);
		height += TOOLTIP_LINE_HEIGHT;
	}

	cur_tt = tt;
	cur_tt_win = create_widget_rel(mouse_x + WINDOW_OFFSET,
	    mouse_y - height - WINDOW_OFFSET, B_TRUE, width, height, 0, "",
	    1, NULL, xpWidgetClass_MainWindow);
	XPSetWidgetProperty(cur_tt_win, xpProperty_MainWindowType,
	    xpMainWindowStyle_Translucent);

	for (int i = 0, y = WINDOW_MARGIN; tt->lines[i] != NULL; i++,
	    y += TOOLTIP_LINE_HEIGHT) {
		const char *line = tt->lines[i];
		XPWidgetID line_caption;

		line_caption = create_widget_rel(WINDOW_MARGIN, y, B_FALSE,
		    width - 2 * WINDOW_MARGIN, TOOLTIP_LINE_HEIGHT, 1,
		    line, 0, cur_tt_win, xpWidgetClass_Caption);
		XPSetWidgetProperty(line_caption, xpProperty_CaptionLit, 1);
	}

	XPShowWidget(cur_tt_win);
}

static float
tooltip_floop_cb(float elapsed_since_last_call, float elapsed_since_last_floop,
    int counter, void *refcon)
{
	int mouse_x, mouse_y;
	long long now = microclock();
	tooltip_t *hit_tt = NULL;

	UNUSED(elapsed_since_last_call);
	UNUSED(elapsed_since_last_floop);
	UNUSED(counter);
	UNUSED(refcon);

	XPLMGetMouseLocation(&mouse_x, &mouse_y);

	if (last_mouse_x != mouse_x || last_mouse_y != mouse_y) {
		last_mouse_x = mouse_x;
		last_mouse_y = mouse_y;
		mouse_moved_time = now;
		if (cur_tt != NULL)
			destroy_cur_tt();
		return (TOOLTIP_INTVAL);
	}

	if (now - mouse_moved_time < TOOLTIP_DISPLAY_DELAY || cur_tt != NULL)
		return (TOOLTIP_INTVAL);

	for (tooltip_set_t *tts = list_head(&tooltip_sets); tts != NULL;
	    tts = list_next(&tooltip_sets, tts)) {
		int wleft, wtop, wright, wbottom;

		XPGetWidgetGeometry(tts->window, &wleft, &wtop, &wright,
		    &wbottom);
		if (!XPIsWidgetVisible(tts->window) ||
		    mouse_x < wleft || mouse_x > wright ||
		    mouse_y < wbottom || mouse_y > wtop)
			continue;
		for (tooltip_t *tt = list_head(&tts->tooltips); tt != NULL;
		    tt = list_next(&tts->tooltips, tt)) {
			int x1 = wleft + tt->x, x2 = wleft + tt->x + tt->w,
			    y1 = wtop - tt->y - tt->h, y2 = wtop - tt->y;

			if (mouse_x >= x1 && mouse_x <= x2 &&
			    mouse_y >= y1 && mouse_y <= y2) {
				hit_tt = tt;
				goto out;
			}
		}
	}

out:
	if (hit_tt != NULL)
		set_cur_tt(hit_tt, mouse_x, mouse_y);

	return (TOOLTIP_INTVAL);
}

static void
tooltip_init(void)
{
	list_create(&tooltip_sets, sizeof (tooltip_set_t),
	    offsetof(tooltip_set_t, node));

	XPLMRegisterFlightLoopCallback(tooltip_floop_cb, TOOLTIP_INTVAL, NULL);
}

static void
tooltip_fini(void)
{
	tooltip_set_t *tts;
	while ((tts = list_head(&tooltip_sets)) != NULL)
		tooltip_set_destroy(tts);
	list_destroy(&tooltip_sets);

	XPLMUnregisterFlightLoopCallback(tooltip_floop_cb, NULL);
}

static void
menu_cb(void *menu, void *item)
{
	int cmd = (long)item;

	UNUSED(menu);
	switch (cmd) {
	case CONFIG_GUI_CMD:
		XPShowWidget(main_window);
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

static void
create_menu(void)
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

static void
destroy_menu(void)
{
	XPLMDestroyMenu(root_menu);
	XPLMRemoveMenuItem(XPLMFindPluginsMenu(), plugins_menu_item);
}

static int
main_window_cb(XPWidgetMessage msg, XPWidgetID widget, intptr_t param1,
    intptr_t param2)
{
	UNUSED(param1);
	UNUSED(param2);

	if (msg == xpMessage_CloseButtonPushed && widget == main_window) {
		XPHideWidget(main_window);
		return (1);
	}

	return (0);
}

static void
create_main_window(void)
{
	int x, y;
	tooltip_set_t *main_tts;

	main_window = create_widget_rel(100, 100, B_FALSE, MAIN_WINDOW_WIDTH,
	    MAIN_WINDOW_HEIGHT, 0, XRAAS_MENU_NAME, 1, NULL,
	    xpWidgetClass_MainWindow);
	XPSetWidgetProperty(main_window, xpProperty_MainWindowHasCloseBoxes, 1);
	XPAddWidgetCallback(main_window, main_window_cb);

	main_tts = tooltip_set_new(main_window);

#define	COLUMN_X	220

#define	LAYOUT_START_X	10
#define	LAYOUT_START_Y	30

#define BUTTON_HEIGHT		20
#define	BUTTON_WIDTH		200

#define TEXT_FIELD_WIDTH	200
#define TEXT_FIELD_HEIGHT	20

#define	LAYOUT_BUTTON(var, text, type, behavior, tooltip) \
	do { \
		buttons.var = create_widget_rel(x, y, B_FALSE, 20, \
		    BUTTON_HEIGHT - 5, 1, "", 0, main_window, \
		    xpWidgetClass_Button); \
		XPSetWidgetProperty(buttons.var, xpProperty_ButtonType, \
		    xpRadioButton); \
		XPSetWidgetProperty(buttons.var, xpProperty_ButtonBehavior, \
		    xpButtonBehavior ## behavior); \
		(void) create_widget_rel(x + 20, y, B_FALSE, \
		    BUTTON_WIDTH - 20, BUTTON_HEIGHT - 5, 1, text, 0, \
		    main_window, xpWidgetClass_Caption); \
		tooltip_new(main_tts, x, y, BUTTON_WIDTH, BUTTON_HEIGHT, \
		    tooltip); \
		y += BUTTON_HEIGHT; \
	} while (0)

	x = LAYOUT_START_X;
	y = LAYOUT_START_Y;
	LAYOUT_BUTTON(enabled, "Enabled",
	    PushButton, CheckBox, enabled_tooltip);
	LAYOUT_BUTTON(allow_helos, "Allow helicopters",
	    PushButton, CheckBox, allow_helos_tooltip);
	LAYOUT_BUTTON(startup_notify, "Notify on startup",
	    PushButton, CheckBox, startup_notify_tooltip);
	LAYOUT_BUTTON(use_imperial, "Call out distances in feet",
	    PushButton, CheckBox, use_imperial_tooltip);
	LAYOUT_BUTTON(us_runway_numbers, "US runway numbers",
	    PushButton, CheckBox, us_runway_numbers_tooltip);
	LAYOUT_BUTTON(too_high_enabled, "Too high approach monitor",
	    PushButton, CheckBox, too_high_enabled_tooltip);
	LAYOUT_BUTTON(too_fast_enabled, "Too fast approach monitor",
	    PushButton, CheckBox, too_fast_enabled_tooltip);
	LAYOUT_BUTTON(alt_setting_enabled, "Altimeter setting monitor",
	    PushButton, CheckBox, alt_setting_enabled_tooltip);
	LAYOUT_BUTTON(qnh_alt_enabled, "QNH altimeter setting mode",
	    PushButton, CheckBox, alt_setting_mode_tooltip);
	LAYOUT_BUTTON(qfe_alt_enabled, "QFE altimeter setting mode",
	    PushButton, CheckBox, alt_setting_mode_tooltip);
	LAYOUT_BUTTON(disable_ext_view, "Silence in external view",
	    PushButton, CheckBox, test_tooltip);
	LAYOUT_BUTTON(override_electrical, "Override electrical check",
	    PushButton, CheckBox, test_tooltip);
	LAYOUT_BUTTON(override_replay, "Show in replay mode",
	    PushButton, CheckBox, test_tooltip);
	LAYOUT_BUTTON(use_tts, "Use Text-To-Speech",
	    PushButton, CheckBox, test_tooltip);
	LAYOUT_BUTTON(speak_units, "Speak units",
	    PushButton, CheckBox, test_tooltip);
	LAYOUT_BUTTON(nd_alerts_enabled, "Alerts on ND enabled",
	    PushButton, CheckBox, test_tooltip);
	LAYOUT_BUTTON(nd_alert_overlay_enabled, "ND alert overlay enabled",
	    PushButton, CheckBox, test_tooltip);
	LAYOUT_BUTTON(nd_alert_overlay_force, "Always show ND alert overlay",
	    PushButton, CheckBox, test_tooltip);
	LAYOUT_BUTTON(openal_shared, "Shared OpenAL context",
	    PushButton, CheckBox, test_tooltip);

#undef	LAYOUT_BUTTON

#define	LAYOUT_TEXT_FIELD(var, label, max_chars, tooltip) \
	do {\
		(void) create_widget_rel(x, y, B_FALSE, \
		    TEXT_FIELD_WIDTH * 0.8, TEXT_FIELD_HEIGHT - 5, 1, label, \
		    0, main_window, xpWidgetClass_Caption); \
		text_fields.var = create_widget_rel( \
		    x + TEXT_FIELD_WIDTH * 0.8, y, B_FALSE, \
		    TEXT_FIELD_WIDTH * 0.25, TEXT_FIELD_HEIGHT - 5, 1, "", 0, \
		    main_window, xpWidgetClass_TextField); \
		XPSetWidgetProperty(text_fields.var, xpProperty_TextFieldType, \
		    xpTextEntryField); \
		XPSetWidgetProperty(text_fields.var, xpProperty_Enabled, 1); \
		XPSetWidgetProperty(text_fields.var, xpProperty_MaxCharacters, \
		    max_chars); \
		tooltip_new(main_tts, x, y, TEXT_FIELD_WIDTH, \
		    TEXT_FIELD_HEIGHT, tooltip); \
		y += TEXT_FIELD_HEIGHT; \
	} while (0)

	x = LAYOUT_START_X + COLUMN_X;
	y = LAYOUT_START_Y;
	LAYOUT_TEXT_FIELD(min_engines, "Minimum number of engines",
	    1, test_tooltip);
	LAYOUT_TEXT_FIELD(min_mtow, "Minimum MTOW", 6, test_tooltip);
	LAYOUT_TEXT_FIELD(min_takeoff_dist, "Minimum takeoff distance",
	    4, test_tooltip);
	LAYOUT_TEXT_FIELD(min_landing_dist, "Minimum landing distance",
	    4, test_tooltip);
	LAYOUT_TEXT_FIELD(min_rotation_dist, "Minimum rotation distance",
	    4, test_tooltip);
	LAYOUT_TEXT_FIELD(min_rotation_angle, "Minimum rotation angle",
	    4, test_tooltip);
	LAYOUT_TEXT_FIELD(stop_dist_cutoff, "Stop distance cutoff",
	    4, test_tooltip);
	LAYOUT_TEXT_FIELD(on_rwy_warn_initial, "On runway warning (initial)",
	    3, test_tooltip);
	LAYOUT_TEXT_FIELD(on_rwy_warn_repeat, "On runway warning (repeat)",
	    3, test_tooltip);
	LAYOUT_TEXT_FIELD(on_rwy_warn_max_n, "On runway maximum number",
	    2, test_tooltip);
	LAYOUT_TEXT_FIELD(gpa_limit_mult, "GPA limit multiplier",
	    4, test_tooltip);
	LAYOUT_TEXT_FIELD(gpa_limit_max, "GPA limit maximum",
	    4, test_tooltip);
	LAYOUT_TEXT_FIELD(long_land_lim_abs, "Long landing absolute limit",
	    4, test_tooltip);
	LAYOUT_TEXT_FIELD(long_land_lim_fract, "Long landing limit fraction",
	    4, test_tooltip);
	LAYOUT_TEXT_FIELD(nd_alerts_enabled_timeout, "ND alert timeout",
	    3, test_tooltip);

#undef	LAYOUT_TEXT_FIELD
}

static void
destroy_main_window(void)
{
	XPDestroyWidget(main_window, 1);
}

static void
update_main_window(void)
{
#define	UPDATE_BUTTON_STATE(button) \
	XPSetWidgetProperty(buttons.button, xpProperty_ButtonState, \
	    xraas_state->button)
	UPDATE_BUTTON_STATE(enabled);
	UPDATE_BUTTON_STATE(allow_helos);
	UPDATE_BUTTON_STATE(startup_notify);
	UPDATE_BUTTON_STATE(use_imperial);
	UPDATE_BUTTON_STATE(us_runway_numbers);
	UPDATE_BUTTON_STATE(too_high_enabled);
	UPDATE_BUTTON_STATE(too_fast_enabled);
	UPDATE_BUTTON_STATE(alt_setting_enabled);
	UPDATE_BUTTON_STATE(qnh_alt_enabled);
	UPDATE_BUTTON_STATE(qfe_alt_enabled);
	UPDATE_BUTTON_STATE(disable_ext_view);
	UPDATE_BUTTON_STATE(override_electrical);
	UPDATE_BUTTON_STATE(override_replay);
	UPDATE_BUTTON_STATE(speak_units);
	UPDATE_BUTTON_STATE(nd_alerts_enabled);
	UPDATE_BUTTON_STATE(nd_alert_overlay_enabled);
	UPDATE_BUTTON_STATE(nd_alert_overlay_force);
	UPDATE_BUTTON_STATE(openal_shared);
#undef	UPDATE_BUTTON_STATE
}

void
gui_init(void)
{
	tooltip_init();
	create_menu();
	create_main_window();
}

void
gui_fini(void)
{
	destroy_menu();
	destroy_main_window();
	tooltip_fini();
}

void
gui_update(void)
{
	XPLMEnableMenuItem(root_menu, dbg_gui_menu_item, xraas_inited);
	XPLMCheckMenuItem(root_menu, dbg_gui_menu_item,
	    xraas_state->debug_graphical ? xplm_Menu_Checked :
	    xplm_Menu_Unchecked);
	update_main_window();
}
