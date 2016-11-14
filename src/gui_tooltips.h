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

#ifndef	_GUI_TOOLTIPS_H_
#define	_GUI_TOOLTIPS_H_

#include <stdlib.h>

#ifdef	__cplusplus
extern "C" {
#endif

static const char *enabled_tooltip[] = {
    "The master ON/OFF switch.",
    "ON: X-RAAS starts up if the current aircraft is compatible.",
    "OFF: X-RAAS startup is completely inhibited.",
    NULL
};
static const char *allow_helos_tooltip[] = {
    "ON: permit startup if the current aircraft is a helicopter.",
    "OFF: inhibit startup if the current aircraft is a helicopter.",
    "(This setting doesn't affect other compatibility checks, such as the",
    "minimum number of engines allowed or the minimum MTOW limit.",
    "Refer to section 3 of the X-RAAS user manual for more information.)",
    NULL
};
static const char *startup_notify_tooltip[] = {
    "ON: on startup, show a brief message at the bottom of the screen to",
    "    indicate that X-RAAS is working correctly and what units of",
    "    measure are used for distance callouts.",
    "OFF: display of the startup message is inhibited.",
    "(This setting doesn't inhibit startup of X-RAAS itself.)",
    NULL
};
static const char *use_imperial_tooltip[] = {
    "ON: use feet as the unit of measure in annunciations.",
    "OFF: use meters as the unit of measure in annunciations.",
    "(This setting doesn't affect the units used on this configuration "
    "screen.)",
    NULL
};
static const char *us_runway_numbers_tooltip[] = {
    "ON: pronounce single-digit runway numbers without a leading '0' "
    "(e.g. '1L').",
    "OFF: pronounce single-digit runway numbers with a leading '0' "
    "(e.g. '01L').",
    NULL
};
static const char *too_high_enabled_tooltip[] = {
    "ON: the 'TOO HIGH' approach monitor is enabled.",
    "OFF: the 'TOO HIGH' approach monitor is disabled.",
    "(Refer to section 4.12 of the X-RAAS user manual for more information.)",
    NULL
};
static const char *too_fast_enabled_tooltip[] = {
    "ON: the 'TOO FAST' approach monitor is enabled.",
    "OFF: the 'TOO FAST' approach monitor is disabled.",
    "(Refer to section 4.13 of the X-RAAS user manual for more information.)",
    NULL
};
static const char *alt_setting_enabled_tooltip[] = {
    "ON: the barometric altimeter setting monitor is enabled.",
    "OFF: the barometric altimeter setting monitor is disabled.",
    "(Refer to sections 4.8 and 4.9 of the X-RAAS user manual for more "
    "information.)",
    NULL
};
static const char *qnh_alt_mode_tooltip[] = {
    "ON: the QNH altimeter setting monitor mode is enabled.",
    "OFF: the QNH altimeter setting monitor mode is disabled.",
    "(Refer to section 4.9 of the X-RAAS user manual for more information.)",
    NULL
};
static const char *qfe_alt_mode_tooltip[] = {
    "ON: the QFE altimeter setting monitor mode is enabled.",
    "OFF: the QFE altimeter setting monitor mode is disabled.",
    "(Refer to section 4.9 of the X-RAAS user manual for more information.)",
    NULL
};
static const char *disable_ext_view_tooltip[] = {
    "ON: while the simulator view is external, audible playback and display "
    "of visual overlay annunciations is inhibited.",
    "OFF: while the simulator view is external, audible playback and display "
    "of visual overlay annunciations is permitted.",
    NULL
};
static const char *override_electrical_tooltip[] = {
    "ON: permit startup even if insufficient power is being applied "
    "to the aircraft's electrical buses.",
    "OFF: inhibit startup unless sufficient power is being applied "
    "to the aircraft's electrical buses.",
    NULL
};
static const char *override_replay_tooltip[] = {
    "ON: permit X-RAAS operation even if the simulator is currently in "
    "replay mode.",
    "OFF: inhibit X-RAAS operation if the simulator is currently in "
    "replay mode.",
    NULL
};
static const char *use_tts_tooltip[] = {
    "ON: use the host operating system's text-to-speech function to "
    "generate aural annunciations.",
    "OFF: use X-RAAS's own audio playback to generate aural annunciations.",
#if	LIN
    "(NOTE: this feature is not available on Linux.)",
#endif	/* LIN */
    NULL
};
static const char *speak_units_tooltip[] = {
    "ON: append the units of measure used to initial distance annunciations.",
    "OFF: don't append the units of measure used to distance annunciations.",
    NULL
};
static const char *nd_alerts_enabled_tooltip[] = {
    "ON: permit issuing visual alerts on the ND or the screen overlay.",
    "OFF: inhibit issuing visual alerts on the ND and the screen overlay.",
    "(Refer to section 3.1.2 of the X-RAAS user manual for more information.)",
    NULL
};
static const char *nd_alert_overlay_enabled_tooltip[] = {
    "ON: permit display of visual alerts using the fallback on-screen overlay.",
    "OFF: inhibit display of visual alerts using the on-screen overlay.",
    "(Refer to section 3.1.2 of the X-RAAS user manual for more information.)",
    NULL
};
static const char *nd_alert_overlay_force_tooltip[] = {
    "ON: always display visual alerts using the fallback on-screen overlay.",
    "OFF: only display visual alerts using the on-screen overlay if the",
    "    aircraft doesn't provide native display of visual alerts on the ND.",
    "(Refer to section 3.1.2 of the X-RAAS user manual for more information.)",
    NULL
};
static const char *openal_shared_tooltip[] = {
    "ON: X-RAAS should use an OpenAL audio driver context shared with the "
    "rest of X-Plane.",
    "OFF: X-RAAS should use its own dedicated OpenAL audio driver context.",
    NULL
};
static const char *voice_female_tooltip[] = {
    "ON: the voice of aural annunciations is female.",
    "OFF: the voice of aural annunciations is male.",
    NULL
};
static const char *min_engines_tooltip[] = {
    "Minimum number of engines the aircraft must have for it to be considered",
    "an 'airliner' and permit X-RAAS startup.",
    "(Refer to section 3 of the X-RAAS user manual for more information.)",
    NULL
};
static const char *min_mtow_tooltip[] = {
    "Lowest value of the aircraft's Maximum TakeOff Weight (MTOW) for it to",
    "be considered an 'airliner' and permit X-RAAS startup.",
    "(Refer to section 3 of the X-RAAS user manual for more information.)",
    NULL
};
static const char *voice_volume_tooltip[] = {
    "The relative audio volume for aural annunciations",
    NULL
};
static const char *save_acf_tooltip[] = {
    "Save the current configuration into the currently loaded aircraft.",
    "The configuration will then only be applied to that specific aircraft.",
    "(Refer to section 5 of the X-RAAS user manual for more information.)",
    NULL
};
static const char *save_glob_tooltip[] = {
    "Save the current configuration as the global configuration. The",
    "configuration will be applied to any aircraft which doesn't have",
    "its own aircraft-specific configuration.",
    "(Refer to section 5 of the X-RAAS user manual for more information.)",
    NULL
};
static const char *reset_acf_tooltip[] = {
    "Reset the aircraft-specific configuration to its default values.",
    "If there is a global configuration, it will be applied. Otherwise,",
    "the default configuration will be applied.",
    "(Refer to section 5 of the X-RAAS user manual for more information.)",
    NULL
};
static const char *reset_glob_tooltip[] = {
    "Reset the global configuration to its default values. If there is an",
    "aircraft-specific configuration, it will be applied when the associated",
    "aircraft is loaded. Otherwise the default configuration will be applied.",
    "(Refer to section 5 of the X-RAAS user manual for more information.)",
    NULL
};
static const char *min_takeoff_dist_tooltip[] = {
    "The minimum runway length remaining that is considered to be safe for",
    "conducting a takeoff. If the runway length remaining is less than this",
    "value, caution annunciations will be issued.",
    "(Refer to section 4.3 of the X-RAAS user manual for more information.)",
    NULL
};
static const char *min_landing_dist_tooltip[] = {
    "The minimum runway length remaining that is considered to be safe for",
    "conduting a landing. If the runway length remaining is less than this",
    "value, caution annunciations will be issued.",
    "(Refer to section 4.10 of the X-RAAS user manual for more information.)",
    NULL
};
static const char *min_rotation_dist_tooltip[] = {
    "The minimum runway length remaining by which if the aircraft hasn't",
    "initiated rotation, X-RAAS will start issuing runway length remaining",
    "annunciations to warn of rapidly approaching the runway end.",
    "(Refer to section 4.6 of the X-RAAS user manual for more information.)",
    NULL
};
static const char *min_rotation_angle_tooltip[] = {
    "The minimum pitch angle relative to the runway slope above which",
    "X-RAAS considers the aircraft to have initiated rotation for takeoff.",
    "(Refer to section 4.6 of the X-RAAS user manual for more information.)",
    NULL
};
static const char *stop_dist_cutoff_tooltip[] = {
    "On landing, do not initiate runway length remaining annunciations",
    "as long as the runway length remaining is above this value.",
    "(Refer to section 4.16 of the X-RAAS user manual for more information.)",
    NULL
};
static const char *on_rwy_warn_initial_tooltip[] = {
    "Issue the first 'ON RUNWAY' annunciation for extended holding on the",
    "runway after this number of seconds have elapsed.",
    "(Refer to section 4.2.1 of the X-RAAS user manual for more information.)",
    NULL
};
static const char *on_rwy_warn_repeat_tooltip[] = {
    "Issue subsequent 'ON RUNWAY' annunciations for extended holding",
    "on the runway after this number of seconds have elapsed.",
    "(Refer to section 4.2.1 of the X-RAAS user manual for more information.)",
    NULL
};
static const char *on_rwy_warn_max_n_tooltip[] = {
    "Maximum number of 'ON RUNWAY' annunciations issued for extended "
    "holding on the runway.",
    "(Refer to section 4.2.1 of the X-RAAS user manual for more information.)",
    NULL
};
static const char *gpa_limit_mult_tooltip[] = {
    "Maximum glidepath angle multiplier for the TOO HIGH approach monitor.",
    "(Refer to section 4.12 of the X-RAAS user manual for more information.)",
    NULL
};
static const char *gpa_limit_max_tooltip[] = {
    "Maximum absolute glidepath angle for the TOO HIGH approach monitor.",
    "(Refer to section 4.12 of the X-RAAS user manual for more information.)",
    NULL
};
static const char *long_land_lim_abs_tooltip[] = {
    "Maximum distance from the approach threshold above which if the aircraft",
    "has not yet touched down, the landing is considered a LONG LANDING.",
    "(Refer to section 4.15 of the X-RAAS user manual for more information.)",
    NULL
};
static const char *long_land_lim_fract_tooltip[] = {
    "Fraction of the runway length from the approach threshold above which if",
    "the aircraft has not yet touched down, the landing is considered a "
    "LONG LANDING.",
    "(Refer to section 4.15 of the X-RAAS user manual for more information.)",
    NULL
};
static const char *nd_alert_timeout_tooltip[] = {
    "Number of seconds for which visual alerts are displayed on the ND.",
    NULL
};
static const char *min_landing_flap_tooltip[] = {
    "Minimum relative flap handle position, including and above which the",
    "flaps setting is considered a valid flaps setting for landing.",
    "(Refer to section 4.11 of the X-RAAS user manual for more information.)",
    NULL
};
static const char *min_takeoff_flap_tooltip[] = {
    "Minimum relative flap handle position, including and above which the",
    "flaps setting is considered a valid flaps setting for takeoff.",
    "(Refer to section 4.2 of the X-RAAS user manual for more information.)",
    NULL
};
static const char *max_takeoff_flap_tooltip[] = {
    "Maximum relative flap handle position, including and below which the",
    "flaps setting is considered a valid flaps setting for takeoff.",
    "(Refer to section 4.2 of the X-RAAS user manual for more information.)",
    NULL
};
static const char *nd_alert_filter_tooltip[] = {
    "A filter which controls what visual alerts are displayed on the ND:",
    "ALL: all visual alerts are displayed (routine, non-routine, caution).",
    "Non-routine: only non-routine and caution alerts are displayed.",
    "Caution: only caution alerts are displayed.",
    NULL
};
static const char *nd_alert_overlay_font_tooltip[] = {
    "Specifies the font file (TTF) to use for the ND alert overlay. To use",
    "a custom font, place the font file into the X-RAAS plugin folder under",
    "\"data" DIRSEP_S "fonts\" and specify its filename here.",
    "To revert to the default font, simply leave this text field empty.",
    NULL
};
static const char *nd_alert_overlay_font_size_tooltip[] = {
    "The pixel size of the font to use for the ND alert overlay.",
    NULL
};

#ifdef	__cplusplus
}
#endif

#endif	/* _GUI_TOOLTIPS_H_ */
