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
    "This is the X-RAAS master ON/OFF switch. It takes effect prior",
    "to any other initialization steps.",
    NULL
};
static const char *allow_helos_tooltip[] = {
    "This setting controls whether X-RAAS will auto-inhibit if it",
    "detects that the currently loaded aircraft is a helicopter.",
    "Turning this ON will permit X-RAAS to start up regardless of",
    "the aircraft type loaded. Please note that this doesn't disable",
    "other compatibility checks, such as the minimum number of",
    "engines allowed or the minimum MTOW limit.",
    NULL
};
static const char *startup_notify_tooltip[] = {
    "At startup, X-RAAS displays a short message at the bottom of the",
    "screen to indicate to users that it is installed and working",
    "correctly and what units it is configured with. Switch this",
    "setting to OFF disables that startup message on the screen.",
    "It doesn't inhibit X-RAAS startup itself.",
    NULL
};
static const char *use_imperial_tooltip[] = {
    "By default X-RAAS calls out runway lengths in feet. Turning this",
    "setting OFF, makes X-RAAS call out runway lengths in meters.",
    NULL
};
static const char *us_runway_numbers_tooltip[] = {
    "In the United States, runways are allowed to have single-digit",
    "numbers, so runway “01” is simply referred to as runway “1”. By",
    "default, X-RAAS uses the ICAO standard and always pronounces",
    "runway numbers as two digits, prepending a “0” if necessary. If",
    "you only fly within the US, you can set this parameter to true",
    "to make X-RAAS pronounce single-digit runways without prepending",
    "a “0”.",
    NULL
};
static const char *too_high_enabled_tooltip[] = {
    "When an aircraft is on approach to land and the database indicates",
    "an optimum approach angle, X-RAAS monitors the approach angle and",
    "if the angle is excessive, it will issue three warnings: 'TOO HIGH!",
    "(pause) TOO HIGH!' descending through 950 to 600 feet AFE, a second",
    "'TOO HIGH! TOO HIGH!' descending through 600 - 450 feet AFE, and",
    "finally 'UNSTABLE! UNSTABLE!' descending through 450 - 300 feet",
    "AFE. Changing this setting to OFF will disable these warnings",
    "Refer to section 4.12 of the user manual for a more complete",
    "description of what contitutes an excessive approach angle.",
    NULL
};
static const char *too_fast_enabled_tooltip[] = {
    "On approach, if the aircraft supports setting a landing speed in",
    "the FMC, X-RAAS will monitor aircraft airspeed and will issue the",
    "following three warnings if the airspeed becomes excessive:",
    "'TOO FAST! (pause) TOO FAST!' descending through 950 to 600 feet",
    "AFE, a second 'TOO FAST! TOO FAST!' descending through 600 - 450",
    "feet AFE, and finally 'UNSTABLE! UNSTABLE!' descending through",
    "450 - 300 feet AFE. Changing this setting to OFF disable these",
    "warnings. Refer to section 4.13 of the user manual for a more",
    "complete description of what contitutes an excessive approach",
    "speed and a list of supported aircraft.",
    NULL
};
static const char *alt_setting_enabled_tooltip[] = {
    "X-RAAS monitors your altimeter setting to prevent forgetting to",
    "reset the barometric altimeter subscale to QNE above transition",
    "altitude and back to QNH or QFE when below transition level.",
    "Forgetting to set the appropriate altimeter setting can result in",
    "incorrect flight level (increasing risk of traffic collisions) or",
    "in a CFIT (Controlled Flight Into Terrain). The real world RAAS",
    "interacts with the FMS of the aircraft to determine origin and",
    "destination aerodromes and sets up its transition altitude and",
    "transition level based on what's set in the FMS. X-RAAS can't",
    "interface with 3rd party FMS addons, so it instead attempts to",
    "guess the transition altitude/level based on database entries",
    "for the airports around the aircraft, as well as GPS elevation",
    "and barometric indications. It doesn't always get this 100% right,",
    "so if you don't want to deal with this indication, you can disable",
    "all altimeter checks.",
    NULL
};
static const char *alt_setting_mode_tooltip[] = {
    "When altimeter checks are enabled, when descending through",
    "transition level, the QNH and QFE altimeter mode settings determine",
    "what type of altimeter setting checks are preformed by X-RAAS.",
    "When QNH mode is enabled, X-RAAS checks that the barometric",
    "altimeter reading is within a pre-computed margin from GPS-computed",
    "elevation above mean sea level. When QFE mode is enabled, X-RAAS",
    "checks that the altimeter reading is within a pre-computed margin",
    "from AFE of the nearest airport. Setting both QNH and QFE checking",
    "to ON allows either altimeter setting, whereas setting both to OFF",
    "disables barometric altimeter checking on descent.",
    NULL
};
static const char *test_tooltip[] = {
    "test",
    NULL
};

#ifdef	__cplusplus
}
#endif

#endif	/* _GUI_TOOLTIPS_H_ */
