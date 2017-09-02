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
 * Copyright 2017 Saso Kiselkov. All rights reserved.
 */

#ifndef	_GUI_TOOLTIPS_H_
#define	_GUI_TOOLTIPS_H_

#include <stdlib.h>

#ifdef	__cplusplus
extern "C" {
#endif

#define	MAN_REF(sect) \
	"(Refer to section " sect " of the X-RAAS user manual for more " \
	"information.)"

static const char *enabled_tooltip =
    "The master ON/OFF switch.\n"
    "ON: X-RAAS starts up if the current aircraft is compatible.\n"
    "OFF: X-RAAS startup is completely inhibited.";

#ifndef	XRAAS_IS_EMBEDDED
static const char *allow_helos_tooltip =
    "ON: permit startup if the current aircraft is a helicopter.\n"
    "OFF: inhibit startup if the current aircraft is a helicopter.\n"
    "(This setting doesn't affect other compatibility checks, such as the\n"
    "minimum number of engines allowed or the minimum MTOW limit.\n"
    MAN_REF("3");

static const char *startup_notify_tooltip =
    "ON: on startup, show a brief message at the bottom of the screen to\n"
    "    indicate that X-RAAS is working correctly and what units of\n"
    "    measure are used for distance callouts.\n"
    "OFF: display of the startup message is inhibited.\n"
    "(This setting doesn't inhibit startup of X-RAAS itself.)";
#endif	/* !XRAAS_IS_EMBEDDED */

static const char *use_imperial_tooltip =
    "ON: use feet as the unit of measure in annunciations.\n"
    "OFF: use meters as the unit of measure in annunciations.\n"
    "(This setting doesn't affect the units used on this configuration "
    "screen.)";

static const char *us_runway_numbers_tooltip =
    "ON: pronounce single-digit runway numbers without a leading '0' "
    "(e.g. '1L').\n"
    "OFF: pronounce single-digit runway numbers with a leading '0' "
    "(e.g. '01L').";

static const char *monitor_tooltips[NUM_MONITORS] = {
    /* APCH_RWY_ON_GND_MON */
    "ON: the approaching runway on ground monitor is enabled.\n"
    "OFF: the approaching runway on-ground monitor is disabled.\n"
    MAN_REF("4.1"),
    /* APCH_RWY_IN_AIR_MON */
    "ON: the approaching runway in air monitor is enabled.\n"
    "OFF: the approaching runway in air monitor is disabled.\n"
    MAN_REF("4.10"),
    /* APCH_RWY_IN_AIR_SHORT_MON */
    "ON: the approaching short runway in air monitor is enabled.\n"
    "OFF: the approaching short runway in air monitor is disabled\n"
    MAN_REF("4.10"),
    /* ON_RWY_LINEUP_MON */
    "ON: the on-runway lineup monitor is enabled.\n"
    "OFF: the on-runway lineup monitor is disabled.\n"
    MAN_REF("4.2"),
    /* ON_RWY_LINEUP_SHORT_MON */
    "ON: the on-runway (short runway) lineup monitor is enabled.\n"
    "OFF: the on-runway (short runway) lineup monitor is disabled.\n"
    MAN_REF("4.3"),
    /* ON_RWY_FLAP_MON */
    "ON: the on-runway lineup late flap selection monitor is enabled.\n"
    "OFF: the on-runway lineup late flap selection monitor is disabled.\n"
#if	ACF_TYPE == FF_A320_ACF_TYPE
    "NOTE: this monitor is not available on the Airbus A320.\n"
#endif	/* ACF_TYPE == FF_A320_ACF_TYPE */
    MAN_REF("4.1"),
    /* ON_RWY_TKOFF_SHORT_MON */
    "ON: the short runway takeoff monitor is enabled.\n"
    "OFF: the short runway takeoff monitor is disabled.\n"
    MAN_REF("4.4"),
    /* ON_RWY_HOLDING_MON */
    "ON: the on-runway extended holding monitor is enabled.\n"
    "OFF: the on-runway extended holding monitor is disabled.\n"
    MAN_REF("4.2.1"),
    /* ON_TWY_TKOFF_MON */
    "ON: the taxiway takeoff monitor is enabled.\n"
    "OFF: the taxiway takeoff monitor is disabled.\n"
    MAN_REF("4.5"),
    /* DIST_RMNG_LAND_MON */
    "ON: distance remaining callouts on landing are enabled.\n"
    "OFF: distance remaining callouts on landing are disabled.\n"
    MAN_REF("4.16"),
    /* DIST_RMNG_RTO_MON */
    "ON: distance remaining callouts on rejected takeoff are enabled.\n"
    "OFF: distance remaining callouts on rejected takeoff are disabled.\n"
    MAN_REF("4.7"),
    /* TWY_LAND_MON */
    "ON: the taxiway landing monitor is enabled.\n"
    "OFF: the taxiway landing monitor is disabled.\n"
    MAN_REF("4.14"),
    /* RWY_END_MON */
    "ON: the runway ending distance remaining callout is enabled.\n"
    "OFF: the runway ending distance remaining callout is disabled.\n",
    /* APCH_TOO_HIGH_UPPER_MON */
    "ON: the 'TOO HIGH' approach monitor upper gate  is enabled.\n"
    "OFF: the 'TOO HIGH' approach monitor upper gate  is disabled.\n"
    MAN_REF("4.12"),
    /* APCH_TOO_HIGH_LOWER_MON */
    "ON: the 'TOO HIGH' approach monitor lower gate is enabled.\n"
    "OFF: the 'TOO HIGH' approach monitor lower gate is disabled.\n"
    MAN_REF("4.12"),
    /* APCH_TOO_FAST_UPPER_MON */
    "ON: the 'TOO FAST' approach monitor upper gate is enabled.\n"
    "OFF: the 'TOO FAST' approach monitor upper gate is disabled.\n"
    MAN_REF("4.13"),
    /* APCH_TOO_FAST_LOWER_MON */
    "ON: the 'TOO FAST' approach monitor lower gate is enabled.\n"
    "OFF: the 'TOO FAST' approach monitor lower gate is disabled.\n"
    MAN_REF("4.13"),
    /* APCH_FLAPS_UPPER_MON */
    "ON: the late flap selection approach monitor upper gate is enabled.\n"
    "OFF: the late flap selection approach monitor upper gate is disabled.\n"
    MAN_REF("4.11"),
    /* APCH_FLAPS_LOWER_MON */
    "ON: the late flap selection approach monitor lower gate is enabled.\n"
    "OFF: the late flap selection approach monitor lower gate is disabled.\n"
    MAN_REF("4.11"),
    /* APCH_UNSTABLE_MON */
    "ON: the unstable approach monitor is enabled. The conditions checked "
    "depend\n"
    "    on the lower gate setting of the respective approach monitor.\n"
    "OFF: the unstable approach monitor is disabled.\n"
    MAN_REF("4.11"),
    /* ALTM_QNE_MON */
    "ON: the QNE altimeter setting monitor mode is enabled.\n"
    "OFF: the QNE altimeter setting monitor mode is disabled.\n"
    MAN_REF("4.8"),
    /* ALTM_QNH_MON */
    "ON: the QNH altimeter setting monitor mode is enabled.\n"
    "OFF: the QNH altimeter setting monitor mode is disabled.\n"
    MAN_REF("4.9"),
    /* ALTM_QFE_MON */
    "ON: the QFE altimeter setting monitor mode is enabled.\n"
    "OFF: the QFE altimeter setting monitor mode is disabled.\n"
    MAN_REF("4.9"),
    /* LONG_LAND_MON */
    "ON: the long landing monitor is enabled.\n"
    "OFF: the long landing monitor is disabled.\n"
    MAN_REF("4.15"),
    /* LATE ROTATION_MON */
    "ON: the late rotation on takeoff monitor is enabled.\n"
    "OFF: the late rotation on takeoff monitor is disabled.\n"
    MAN_REF("4.6")
};

static const char *disable_ext_view_tooltip =
    "ON: while the simulator view is external, audible playback and "
    "display of visual overlay annunciations is inhibited.\n"
    "OFF: while the simulator view is external, audible playback and "
    "display of visual overlay annunciations is permitted.\n";

#ifndef	XRAAS_IS_EMBEDDED
static const char *override_electrical_tooltip =
    "ON: permit startup even if insufficient power is being applied "
    "to the aircraft's electrical buses.\n"
    "OFF: inhibit startup unless sufficient power is being applied "
    "to the aircraft's electrical buses.\n";
#endif	/* !XRAAS_IS_EMBEDDED */

static const char *override_replay_tooltip =
    "ON: permit X-RAAS operation even if the simulator is currently in "
    "replay mode.\n"
    "OFF: inhibit X-RAAS operation if the simulator is currently in "
    "replay mode.\n";
static const char *use_tts_tooltip =
    "ON: use the host operating system's text-to-speech function to "
    "generate aural annunciations.\n"
    "OFF: use X-RAAS's own audio playback to generate aural "
    "annunciations.\n"
#if	LIN
    "(NOTE: this feature is not available on Linux.)\n"
#endif	/* LIN */
    ;
static const char *speak_units_tooltip =
    "ON: append the units of measure used to initial distance annunciations.\n"
    "OFF: don't append the units of measure used to distance "
    "annunciations.\n";
static const char *nd_alerts_enabled_tooltip =
    "ON: permit issuing visual alerts on the ND or the screen overlay.\n"
    "OFF: inhibit issuing visual alerts on the ND and the screen overlay.\n"
    MAN_REF("3.1.2");
static const char *nd_alert_overlay_enabled_tooltip =
    "ON: permit display of visual alerts using the fallback on-screen "
    "overlay.\n"
    "OFF: inhibit display of visual alerts using the on-screen overlay.\n"
    MAN_REF("3.1.2");
static const char *nd_alert_overlay_force_tooltip =
    "ON: always display visual alerts using the fallback on-screen overlay.\n"
    "OFF: only display visual alerts using the on-screen overlay if the\n"
    "    aircraft doesn't provide native display of visual alerts on the ND.\n"
    MAN_REF("3.1.2");
static const char *openal_shared_tooltip =
	"ON: X-RAAS should use an OpenAL audio driver context shared with the "
	"rest of X-Plane.\n"
	"OFF: X-RAAS should use its own dedicated OpenAL audio driver "
	"context.\n";
static const char *voice_female_tooltip =
    "ON: the voice of aural annunciations is female.\n"
    "OFF: the voice of aural annunciations is male.\n";
#if	ACF_TYPE == NO_ACF_TYPE
static const char *min_engines_tooltip =
    "Minimum number of engines the aircraft must have for it to be "
    "considered an 'airliner' and permit X-RAAS startup.\n"
    MAN_REF("3");
static const char *min_mtow_tooltip =
    "Lowest value of the aircraft's Maximum TakeOff Weight (MTOW) "
    "for it to be considered an 'airliner' and permit X-RAAS startup.\n"
    MAN_REF("3");
#endif	/* ACF_TYPE == NO_ACF_TYPE */
static const char *voice_volume_tooltip =
    "The relative audio volume for aural annunciations\n";
static const char *save_liv_tooltip =
    "Save the current airline configuration. This configuration will then\n"
    "only be applied if the current livery is loaded into the aircraft.\n"
    "Otherwise the aircraft configuration will be loaded.\n"
    MAN_REF("5");
static const char *reset_liv_tooltip =
    "Reset the airline configuration. If there is an aircraft \n"
    "configuration, it will be applied. Otherwise, the default\n"
    "configuration will be applied.\n"
    MAN_REF("5");
#ifdef	XRAAS_IS_EMBEDDED
static const char *save_acf_tooltip =
    "Save the current aircraft configuration. This configuration\n"
    "will then only be applied if no airline configuration exists\n"
    "for the currently loaded livery.\n"
    MAN_REF("5");
static const char *reset_acf_tooltip =
    "Reset the aircraft configuration to its default values.\n"
    "If there is an airline configuration, it will be applied.\n"
    "Otherwise, the default configuration will be applied.\n"
    MAN_REF("5");
#else	/* !XRAAS_IS_EMBEDDED */
static const char *save_acf_tooltip =
    "Save the current configuration into the currently loaded aircraft.\n"
    "The configuration will then only be applied to that specific aircraft.\n"
    MAN_REF("5");
static const char *reset_acf_tooltip =
    "Reset the aircraft-specific configuration to its default values.\n"
    "If there is a global configuration, it will be applied. Otherwise,\n"
    "the default configuration will be applied.\n"
    MAN_REF("5");
static const char *save_glob_tooltip =
    "Save the current configuration as the global configuration. The\n"
    "configuration will be applied to any aircraft which doesn't have\n"
    "its own aircraft-specific configuration.\n"
    MAN_REF("5");
static const char *reset_glob_tooltip =
    "Reset the global configuration to its default values. If there is an\n"
    "aircraft-specific configuration, it will be applied when the "
    "associated\n"
    "aircraft is loaded. Otherwise the default configuration will be applied.\n"
    MAN_REF("5");
#endif	/* !XRAAS_IS_EMBEDDED */
static const char *min_takeoff_dist_tooltip =
    "The minimum runway length remaining that is considered to be safe\n"
    "for conducting a takeoff. If the runway length remaining is less\n"
    "than this value, caution annunciations will be issued.\n"
    MAN_REF("4.3");
static const char *min_landing_dist_tooltip =
    "The minimum runway length remaining that is considered to be safe\n"
    "for conducting a landing. If the runway length remaining is less\n"
    "than this value, caution annunciations will be issued.\n"
    MAN_REF("4.10");
static const char *min_rotation_dist_tooltip =
    "The minimum runway length remaining by which if the aircraft hasn't\n"
    "initiated rotation, X-RAAS will start issuing runway length remaining\n"
    "annunciations to warn of rapidly approaching the runway end.\n"
    MAN_REF("4.6");
static const char *min_rotation_angle_tooltip =
    "The minimum pitch angle relative to the runway slope above which\n"
    "X-RAAS considers the aircraft to have initiated rotation for takeoff.\n"
    MAN_REF("4.6");
static const char *stop_dist_cutoff_tooltip =
    "On landing, do not initiate runway length remaining annunciations\n"
    "as long as the runway length remaining is above this value.\n"
    MAN_REF("4.16");
static const char *on_rwy_warn_initial_tooltip =
    "Issue the first 'ON RUNWAY' annunciation for extended holding on the\n"
    "runway after this number of seconds have elapsed.\n"
    MAN_REF("4.2.1");
static const char *on_rwy_warn_repeat_tooltip =
    "Issue subsequent 'ON RUNWAY' annunciations for extended holding\n"
    "on the runway after this number of seconds have elapsed.\n"
    MAN_REF("4.2.1");
static const char *on_rwy_warn_max_n_tooltip =
    "Maximum number of 'ON RUNWAY' annunciations issued for extended "
    "holding on the runway.\n"
    MAN_REF("4.2.1");
static const char *gpa_limit_mult_tooltip =
    "Maximum glidepath angle multiplier for the TOO HIGH approach monitor.\n"
    MAN_REF("4.12");
static const char *gpa_limit_max_tooltip =
    "Maximum absolute glidepath angle for the TOO HIGH approach monitor.\n"
    MAN_REF("4.12");
static const char *long_land_lim_abs_tooltip =
    "Maximum distance from the approach threshold above which if the aircraft\n"
    "has not yet touched down, the landing is considered a long/deep landing.\n"
    MAN_REF("4.15");
static const char *long_land_lim_fract_tooltip =
    "Fraction of the runway length from the approach threshold above "
    "which if the aircraft\n"
    "has not yet touched down, the landing is considered a "
    "long/deep landing.\n"
    MAN_REF("4.15");
static const char *nd_alert_timeout_tooltip =
    "Number of seconds for which visual alerts are displayed on the ND.\n";
#if	ACF_TYPE == NO_ACF_TYPE
static const char *min_landing_flap_tooltip =
    "Minimum relative flap handle position, including and above which the\n"
    "flaps setting is considered a valid flaps setting for landing.\n"
    MAN_REF("4.11");
static const char *min_takeoff_flap_tooltip =
    "Minimum relative flap handle position, including and above which the\n"
    "flaps setting is considered a valid flaps setting for takeoff.\n"
    MAN_REF("4.2");
static const char *max_takeoff_flap_tooltip =
    "Maximum relative flap handle position, including and below which the\n"
    "flaps setting is considered a valid flaps setting for takeoff.\n"
    MAN_REF("4.2");
#endif	/* ACF_TYPE == NO_ACF_TYPE */
static const char *nd_alert_filter_tooltip =
    "A filter which controls what visual alerts are displayed on the ND:\n"
    "ALL: all visual alerts are displayed (routine, non-routine, caution).\n"
    "NON-R: only non-routine and caution alerts are displayed.\n"
    "CAUT: only caution alerts are displayed.\n"
    MAN_REF("5.1");
static const char *nd_alert_overlay_font_tooltip =
    "Specifies the font file (TTF) to use for the ND alert overlay.\n"
    "To revert to the default font, simply leave this text field empty.\n"
    MAN_REF("5.1");
static const char *nd_alert_overlay_font_size_tooltip =
	"The pixel size of the font to use for the ND alert overlay.\n";
static const char *say_deep_landing_tooltip =
    "ON: long landing annunciations are 'DEEP LANDING'.\n"
    "OFF: long landing annunciations are 'LONG LANDING'.\n"
    "This setting does not control whether the long landing monitor is "
    "enabled.\n"
    MAN_REF("4.15");
#ifndef	XRAAS_IS_EMBEDDED
static const char *auto_disable_notify_tooltip =
    "When the currently loaded aircraft doesn't meet the minimum\n"
    "requirements for X-RAAS to activate, X-RAAS displays a short message\n"
    "at the bottom of the screen to point out that it is auto-inhibited.\n"
    "This option controls whether this auto-inhibition message is displayed.\n"
    "ON: the display of the auto-inhibition message is enabled.\n"
    "OFF: the display of the auto-inhibition message is disabled.\n"
    MAN_REF("5.1");
#endif	/* !XRAAS_IS_EMBEDDED */

#ifdef	__cplusplus
}
#endif

#endif	/* _GUI_TOOLTIPS_H_ */
