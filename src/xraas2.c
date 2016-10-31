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

#include <errno.h>
#include <ctype.h>
#include <stddef.h>
#include <math.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#include <XPLMDataAccess.h>
#include <XPLMNavigation.h>
#include <XPLMPlanes.h>
#include <XPLMProcessing.h>
#include <XPLMUtilities.h>
#include <XPLMPlugin.h>

#include "airportdb.h"
#include "assert.h"
#include "avl.h"
#include "conf.h"
#include "dbg_gui.h"
#include "geom.h"
#include "helpers.h"
#include "list.h"
#include "log.h"
#include "math.h"
#include "nd_alert.h"
#include "perf.h"
#include "rwy_key_tbl.h"
#include "snd_sys.h"
#include "types.h"
#include "wav.h"
#include "xraas2.h"
#include "xraas_cfg.h"

#define	XRAAS2_VERSION			"2.0"
#define	XRAAS2_PLUGIN_NAME		"X-RAAS " XRAAS2_VERSION
#define	XRAAS2_PLUGIN_SIG		"skiselkov.xraas2"
#define	XRAAS2_PLUGIN_DESC		"A simulation of the Runway " \
					"Awareness and Advisory System"

#define	EXEC_INTVAL			0.5		/* seconds */
#define	HDG_ALIGN_THRESH		20		/* degrees */

#define	SPEED_THRESH			20.5		/* m/s, 40 knots */
#define	HIGH_SPEED_THRESH		30.9		/* m/s, 60 knots */
#define	SLOW_ROLL_THRESH		5.15		/* m/s, 10 knots */
#define	STOPPED_THRESH			2.06		/* m/s, 4 knots */

#define	LANDING_ROLLOUT_TIME_FACT	1		/* seconds */
#define	RADALT_GRD_THRESH		5		/* feet */
#define	RADALT_FLARE_THRESH		100		/* feet */
#define	RADALT_DEPART_THRESH		100		/* feet */
#define	STARTUP_DELAY			3		/* seconds */
#define	STARTUP_MSG_TIMEOUT		4		/* seconds */
#define	ARPT_RELOAD_INTVAL		10		/* seconds */
#define	ARPT_LOAD_LIMIT			(8 * 1852)	/* meters, 8nm */
#define	ACCEL_STOP_SPD_THRESH		2.6		/* m/s, 5 knots */
#define	STOP_INIT_DELAY			300		/* meters */
#define	BOGUS_THR_ELEV_LIMIT		500		/* feet */
#define	STD_BARO_REF			29.92		/* in.Hg */
#define	ALTM_SETTING_TIMEOUT		30		/* seconds */
#define	ALTM_SETTING_ALT_CHK_LIMIT	1500		/* feet */
#define	ALTIMETER_SETTING_QNH_ERR_LIMIT	120		/* feet */
#define	ALTM_SETTING_QFE_ERR_LIMIT	120		/* feet */
#define	ALTM_SETTING_BARO_ERR_LIMIT	0.02		/* in.Hg */
#define	IMMEDIATE_STOP_DIST		50		/* meters */
#define	GOAROUND_CLB_RATE_THRESH	300		/* feet per minute */
#define	OFF_RWY_HEIGHT_MAX		250		/* feet */
#define	OFF_RWY_HEIGHT_MIN		100		/* feet */

#define	RWY_APCH_FLAP1_THRESH		950	/* feet */
#define	RWY_APCH_FLAP2_THRESH		600	/* feet */
#define	RWY_APCH_FLAP3_THRESH		450	/* feet */
#define	RWY_APCH_FLAP4_THRESH		300	/* feet */
#define	ARPT_APCH_BLW_ELEV_THRESH	500	/* feet */
#define	RWY_APCH_ALT_MAX		700	/* feet */
#define	RWY_APCH_ALT_MIN		320	/* feet */
#define	SHORT_RWY_APCH_ALT_MAX		390	/* feet */
#define	SHORT_RWY_APCH_ALT_MIN		320	/* feet */
#define	TATL_REMOTE_ARPT_DIST_LIMIT	500000	/* meters */
#define	MIN_BUS_VOLT			11	/* Volts */
#define	BUS_LOAD_AMPS			2	/* Amps */
#define	XRAAS_apt_dat_cache_version	3
#define	UNITS_APPEND_INTVAL		120	/* seconds */

#define	TILE_NAME_FMT			"%+03.0f%+04.0f"

typedef struct {
	double min, max;
	bool_t ann;
} accel_stop_dist_t;

typedef struct {
	double min, max;
	double f1, f2;
} lin_func_seg_t;

typedef struct {
	double min, max;
} range_t;

/* Suppress 'approaching' annunciations in these altitude windows */
#define	NUM_RWY_APCH_SUPP_WINDOWS 2
static range_t RWY_APCH_SUPP_WINDOWS[NUM_RWY_APCH_SUPP_WINDOWS] = {
    { .max = 530, .min = 480},
    { .max = 430, .min = 380},
};

/*
 * Because we examine these ranges in 0.5 second intervals, there is
 * a maximum speed at which we are guaranteed to announce the distance
 * remaining. The ranges are configured so as to allow for a healthy
 * maximum speed margin over anything that could be reasonably attained
 * over that portion of the runway.
 */
static accel_stop_dist_t accel_stop_distances[] = {
    { .max = 2807, .min = 2743, .ann = B_FALSE }, /* 9200-9000 ft, 250 KT */
    { .max = 2503, .min = 2439, .ann = B_FALSE }, /* 8200-8000 ft, 250 KT */
    { .max = 2198, .min = 2134, .ann = B_FALSE }, /* 7200-7000 ft, 250 KT */
    { .max = 1892, .min = 1828, .ann = B_FALSE }, /* 6200-6000 ft, 250 KT */
    { .max = 1588, .min = 1524, .ann = B_FALSE }, /* 5200-5000 ft, 250 KT */
    { .max = 1284, .min = 1220, .ann = B_FALSE }, /* 4200-4000 ft, 250 KT */
    { .max = 1036, .min = 915, .ann = B_FALSE },  /* 3200-3000 ft, 250 KT */
    { .max = 674, .min = 610, .ann = B_FALSE },   /* 2200-2000 ft, 250 KT */
    { .max = 369, .min = 305, .ann = B_FALSE },   /* 1200-1000 ft, 250 KT */
    { .max = 185, .min = 153, .ann = B_FALSE },   /* 600-500 ft, 125 KT */
    { .max = 46, .min = 31, .ann = B_FALSE },     /* 150-100 ft, 60 KT */
    { .max = NAN, .min = NAN, .ann = -1 }         /* list terminator */
};

static bool_t inited = B_FALSE;
static xraas_state_t state;

static char xpdir[512] = { 0 };
static char xpprefsdir[512] = { 0 };
static char plugindir[512] = { 0 };
static char acf_path[512] = { 0 };
static char acf_filename[512] = { 0 };

static const char *FJS737[] = { "B732", NULL };
static const char *IXEG737[] = { "B733", NULL };
static const char *FF757[] = { "B752", "B753", NULL };
static const char *FF777[] = { "B772", "B773", "B77L", "B77W", NULL };
static const char *JAR[] = {
    "A318", "A319", "A320", "A321", "A322", "A333", "A338", "A339", NULL
};

static struct {
	XPLMDataRef baro_alt;
	XPLMDataRef rad_alt;
	XPLMDataRef airspeed;
	XPLMDataRef gs;
	XPLMDataRef lat, lon;
	XPLMDataRef elev;
	XPLMDataRef hdg;
	XPLMDataRef pitch;
	XPLMDataRef nw_offset;
	XPLMDataRef flaprqst;
	XPLMDataRef gear;
	XPLMDataRef gear_type;
	XPLMDataRef baro_set;
	XPLMDataRef baro_sl;
	XPLMDataRef view_is_ext;
	XPLMDataRef bus_volt;
	XPLMDataRef avionics_on;
	XPLMDataRef num_engines;
	XPLMDataRef mtow;
	XPLMDataRef ICAO;
	XPLMDataRef gpws_prio;
	XPLMDataRef gpws_inop;
	XPLMDataRef replay_mode;
	XPLMDataRef plug_bus_load;
} drs;

/*
 * Returns true if `x' is within the numerical ranges in `rngs'.
 * The range check is inclusive.
 */
static bool_t
number_in_rngs(double x, range_t *rngs, size_t num)
{
	for (size_t i = 0; i < num; i++) {
		if (x <= rngs[i].max && x >= rngs[i].min)
			return (B_TRUE);
	}
	return (B_FALSE);
}

static void
append_msglist(msg_type_t **msglist, size_t *len, msg_type_t msg)
{
	VERIFY(msg < NUM_MSGS);
	*msglist = realloc(*msglist, ++(*len) * sizeof (msg_type_t));
	(*msglist)[(*len) - 1] = msg;
}

static XPLMDataRef
dr_get(const char *drname)
{
	XPLMDataRef dr = XPLMFindDataRef(drname);
	VERIFY(dr != NULL);
	return (dr);
}

static void
raas_dr_reset(void)
{
	memset(&drs, 0, sizeof (drs));

	drs.baro_alt = dr_get("sim/flightmodel/misc/h_ind");
	drs.rad_alt = dr_get("sim/cockpit2/gauges/indicators/"
	    "radio_altimeter_height_ft_pilot");
	drs.airspeed = dr_get("sim/flightmodel/position/indicated_airspeed");
	drs.gs = dr_get("sim/flightmodel/position/groundspeed");
	drs.lat = dr_get("sim/flightmodel/position/latitude");
	drs.lon = dr_get("sim/flightmodel/position/longitude");
	drs.elev = dr_get("sim/flightmodel/position/elevation");
	drs.hdg = dr_get("sim/flightmodel/position/true_psi");
	drs.pitch = dr_get("sim/flightmodel/position/true_theta");
	drs.nw_offset = dr_get("sim/flightmodel/parts/tire_z_no_deflection");
	drs.flaprqst = dr_get("sim/flightmodel/controls/flaprqst");
	drs.gear = dr_get("sim/aircraft/parts/acf_gear_deploy");
	drs.gear_type = dr_get("sim/aircraft/parts/acf_gear_type");
	drs.baro_set = dr_get("sim/cockpit/misc/barometer_setting");
	drs.baro_sl = dr_get("sim/weather/barometer_sealevel_inhg");
	drs.view_is_ext = dr_get("sim/graphics/view/view_is_external");
	drs.bus_volt = dr_get("sim/cockpit2/electrical/bus_volts");
	drs.avionics_on = dr_get("sim/cockpit/electrical/avionics_on");
	drs.num_engines = dr_get("sim/aircraft/engine/acf_num_engines");
	drs.mtow = dr_get("sim/aircraft/weight/acf_m_max");
	drs.ICAO = dr_get("sim/aircraft/view/acf_ICAO");

	drs.gpws_prio = dr_get(state.GPWS_priority_dataref);
	drs.gpws_inop = dr_get(state.GPWS_inop_dataref);

	drs.replay_mode = dr_get("sim/operation/prefs/replay_mode");
	/*
	 * Unfortunately at this moment electrical loading is broken,
	 * because X-Plane resets plugin_bus_load_amps when the aircraft
	 * is repositioned, making it impossible for us to track our
	 * electrical load appropriately. So it's better to disable it,
	 * than to have it be broken.
	 * drs.plug_bus_load = dr_get("sim/cockpit2/electrical/" ..
	 *    "plugin_bus_load_amps")
	 */
}

/*
 * Converts an quantity which is calculated per execution cycle of X-RAAS
 * into a per-minute quantity, so the caller need not worry about X-RAAS
 * execution frequency.
 */
static double
conv_per_min(double x)
{
	return (x * (60 / EXEC_INTVAL));
}

/*
 * Returns true if state.landing gear is fully retracted, false otherwise.
 */
static bool_t
gear_is_up(void)
{
#define	NUM_GEAR 10
	int gear_type[NUM_GEAR];
	float gear[NUM_GEAR];
	int n;

	(void) XPLMGetDatavi(drs.gear_type, gear_type, 0, NUM_GEAR);
	n = XPLMGetDatavf(drs.gear, gear, 0, NUM_GEAR);
	for (int i = 0; i < n; i++) {
		if (gear_type[i] != 0 && gear[i] > 0.0)
			return (B_FALSE);
	}

	return (B_TRUE);
}

bool_t
view_is_external(void)
{
	return (XPLMGetDatai(drs.view_is_ext) != 0);
}

bool_t
GPWS_has_priority(void)
{
	return (drs.gpws_prio ? XPLMGetDatai(drs.gpws_prio) != 0 : B_FALSE);
}

static bool_t
chk_acf_dr(const char **icaos, const char *drname)
{
	char icao[8];

	memset(icao, 0, sizeof (icao));
	XPLMGetDatab(drs.ICAO, icao, 0, sizeof (icao) - 1);

	for (const char **icaos_i = icaos; *icaos_i != NULL; icaos_i++) {
		if (strcmp(icao, *icaos_i) == 0)
			return (XPLMFindDataRef(drname) != NULL);
	}
	return (B_FALSE);
}

/*
 * Checks if the aircraft has a terrain override mode on the GPWS and if it
 * does, returns true if it GPWS terrain warnings are overridden, otherwise
 * returns false.
 */
static bool_t
gpws_terr_ovrd(void)
{
	if (chk_acf_dr(FF757, "anim/75/button")) {
		return (XPLMGetDatai(XPLMFindDataRef("anim/75/button")) == 1);
	} else if (chk_acf_dr(FF777, "anim/51/button")) {
		return (XPLMGetDatai(XPLMFindDataRef("anim/51/button")) == 1);
	} else if (chk_acf_dr(IXEG737, "ixeg/733/misc/egpws_gear_act")) {
		return (XPLMGetDatai(XPLMFindDataRef(
		    "ixeg/733/misc/egpws_gear_act")) == 1);
	} else if (chk_acf_dr(FJS737, "FJS/732/Annun/GPWS_InhibitSwitch")) {
		return (XPLMGetDatai(XPLMFindDataRef(
		    "FJS/732/Annun/GPWS_InhibitSwitch")) == 1);
	} else if (chk_acf_dr(JAR, "sim/custom/xap/gpws_terr")) {
		return (XPLMGetDatai(XPLMFindDataRef(
		    "sim/custom/xap/gpws_terr")) == 1);
	}
	return (B_FALSE);
}

/*
 * Checks if the aircraft has a flaps override mode on the GPWS and if it
 * does, returns true if it GPWS flaps warnings are overridden, otherwise
 * returns false. If the aircraft doesn't have a flaps override GPWS mode,
 * we attempt to also examine if the aircraft has a terrain override mode.
 */
static bool_t
gpws_flaps_ovrd(void)
{
	if (chk_acf_dr(FF757, "anim/72/button")) {
		return (XPLMGetDatai(XPLMFindDataRef("anim/72/button")) == 1);
	} else if (chk_acf_dr(FF777, "anim/79/button")) {
		return (XPLMGetDatai(XPLMFindDataRef("anim/79/button")) == 1);
	} else if (chk_acf_dr(IXEG737, "ixeg/733/misc/egpws_flap_act")) {
		return (XPLMGetDatai(XPLMFindDataRef(
		    "ixeg/733/misc/egpws_flap_act")) == 1);
	} else if (chk_acf_dr(JAR, "sim/custom/xap/gpws_flap")) {
		return (XPLMGetDatai(XPLMFindDataRef(
		    "sim/custom/xap/gpws_flap")) != 0);
	}
	return (gpws_terr_ovrd());
}

/*
 * Locates any airports within a 8 nm radius of the aircraft and loads
 * their RAAS data from the state.apt_dat database. The function then updates
 * state.cur_arpts with the new information and expunges airports that are no
 * longer in range.
 */
static void
load_nearest_airports(void)
{
	geo_pos2_t my_pos;
	int64_t now = microclock();

	if (now - state.last_airport_reload < SEC2USEC(ARPT_RELOAD_INTVAL))
		return;
	state.last_airport_reload = now;

	/*
	 * Must go ahead of unload_distant_airport_tiles to avoid
	 * tripping an assertion in free_aiport.
	 */
	if (state.cur_arpts != NULL)
		free_nearest_airport_list(state.cur_arpts);

	my_pos = GEO_POS2(XPLMGetDatad(drs.lat), XPLMGetDatad(drs.lon));
	load_nearest_airport_tiles(&state.airportdb, my_pos);
	unload_distant_airport_tiles(&state.airportdb, my_pos);

	state.cur_arpts = find_nearest_airports(&state.airportdb, my_pos);

	/*
	 * Remove outdated keys in case we've quickly shifted away from
	 * from the airports which were in the old cur_arpts list without
	 * properly transitioning through the runway proximity tests.
	 */
	rwy_key_tbl_remove_distant(&state.apch_rwy_ann, state.cur_arpts);
	rwy_key_tbl_remove_distant(&state.air_apch_rwy_ann, state.cur_arpts);
}

/*
 * Computes the aircraft's on-ground velocity vector. The length of the
 * vector is computed as a `time_fact' (in seconds) extra ahead of the
 * actual aircraft's nosewheel position.
 */
vect2_t
acf_vel_vector(double time_fact)
{
	float nw_offset;
	XPLMGetDatavf(drs.nw_offset, &nw_offset, 0, 1);
	return (vect2_set_abs(hdg2dir(XPLMGetDataf(drs.hdg)),
	    time_fact * XPLMGetDataf(drs.gs) - nw_offset));
}

/*
 * Determines which of two ends of a runway is closer to the aircraft's
 * current position.
 */
static int
closest_rwy_end(vect2_t pos, const runway_t *rwy)
{
	if (vect2_abs(vect2_sub(pos, rwy->ends[0].dthr_v)) <
	    vect2_abs(vect2_sub(pos, rwy->ends[1].dthr_v)))
		return (0);
	else
		return (1);
}

/*
 * Translates a runway identifier into a suffix suitable for passing to
 * play_msg for announcing whether the runway is left, center or right.
 * If no suffix is present, returns NULL.
 */
static msg_type_t
rwy_lcr_msg(const char *str)
{
	ASSERT(str != NULL);
	ASSERT(strlen(str) >= 3);

	switch (str[2]) {
	case 'L':
		return (LEFT_MSG);
	case 'R':
		return (RIGHT_MSG);
	default:
		ASSERT(str[2] == 'C');
		return (CENTER_MSG);
	}
}

/*
 * Given a runway ID, appends appropriate messages suitable for play_msg
 * to speak it out loud.
 */
static void
rwy_id_to_msg(const char *rwy_id, msg_type_t **msg, size_t *len)
{
	ASSERT(rwy_id != NULL);
	ASSERT(msg != NULL);
	ASSERT(len != NULL);
	ASSERT(strlen(rwy_id) >= 2);

	char first_digit = rwy_id[0];

	if (!is_valid_rwy_ID(rwy_id)) {
		logMsg("Warning: invalid runway ID encountered (%s)", rwy_id);
		return;
	}

	if (first_digit != '0' || !state.us_runway_numbers)
		append_msglist(msg, len, first_digit - '0');
	append_msglist(msg, len, rwy_id[1] - '0');
	if (strlen(rwy_id) >= 3)
		append_msglist(msg, len, rwy_lcr_msg(rwy_id));
}

/*
 * Converts a thousands value to the proper single-digit pronunciation
 */
static void
thousands_msg(msg_type_t **msg, size_t *len, unsigned thousands)
{
	ASSERT(thousands < 100);
	if (thousands >= 10)
		append_msglist(msg, len, thousands / 10);
	append_msglist(msg, len, thousands % 10);
}

/*
 * Given a distance in meters, converts it into a message suitable for
 * play_msg based on the user's current imperial/metric settings.
 * If div_by_100 is B_TRUE, the distance readout is rounded down to the
 * nearest multiple of 100 meters or 300 feet. Otherwise, it is rounded
 * down to the nearest multiple of 1000 feet or 300 meters. If div_by_100
 * is B_FALSE, the last 1000 feet or 300 meters feature two additional
 * readouts, 500 feet or 100 meters, and 100 feet or 30 meters. Below
 * these values, the distance readout is 0 feet or meters.
 * This function also appends the units to the message if X-RAAS is
 * configured to do so and it hasn't spoken the units used for at least
 * UNITS_APPEND_INTVAL seconds.
 */
static void
dist_to_msg(double dist, msg_type_t **msg, size_t *len, bool_t div_by_100)
{
	uint64_t now;

	ASSERT(msg != NULL);
	ASSERT(len != NULL);

	if (!div_by_100) {
		if (state.use_imperial) {
			double dist_ft = MET2FEET(dist);
			if (dist_ft >= 1000) {
				thousands_msg(msg, len, dist_ft / 1000);
				append_msglist(msg, len, THOUSAND_MSG);
			} else if (dist_ft >= 500) {
				append_msglist(msg, len, FIVE_MSG);
				append_msglist(msg, len, HUNDRED_MSG);
			} else if (dist_ft >= 100) {
				append_msglist(msg, len, ONE_MSG);
				append_msglist(msg, len, HUNDRED_MSG);
			} else {
				append_msglist(msg, len, ZERO_MSG);
			}
		} else {
			int dist_300incr = ((int)(dist / 300)) * 300;
			int dist_thousands = (dist_300incr / 1000);
			int dist_hundreds = dist_300incr % 1000;

			if (dist_thousands > 0 && dist_hundreds > 0) {
				thousands_msg(msg, len, dist_thousands);
				append_msglist(msg, len, THOUSAND_MSG);
				append_msglist(msg, len, dist_hundreds / 100);
				append_msglist(msg, len, HUNDRED_MSG);
			} else if (dist_thousands > 0) {
				thousands_msg(msg, len, dist_thousands);
				append_msglist(msg, len, THOUSAND_MSG);
			} else if (dist >= 100) {
				if (dist_hundreds > 0) {
					append_msglist(msg, len,
					    dist_hundreds / 100);
					append_msglist(msg, len, HUNDRED_MSG);
				} else {
					append_msglist(msg, len, ONE_MSG);
					append_msglist(msg, len, HUNDRED_MSG);
				}
			} else if (dist >= 30) {
				append_msglist(msg, len, THIRTY_MSG);
			} else {
				append_msglist(msg, len, ZERO_MSG);
			}
		}
	} else {
		int thousands, hundreds;

		if (state.use_imperial) {
			int dist_ft = MET2FEET(dist);
			thousands = dist_ft / 1000;
			hundreds = (dist_ft % 1000) / 100;
		} else {
			thousands = dist / 1000;
			hundreds = (((int)dist) % 1000) / 100;
		}
		if (thousands != 0) {
			thousands_msg(msg, len, thousands);
			append_msglist(msg, len, THOUSAND_MSG);
		}
		if (hundreds != 0) {
			append_msglist(msg, len, hundreds);
			append_msglist(msg, len, HUNDRED_MSG);
		}
		if (thousands == 0 && hundreds == 0)
			append_msglist(msg, len, ZERO_MSG);
	}

	/* Optionally append units if it is time to do so */
	now = microclock();
	if (now - state.last_units_call > SEC2USEC(UNITS_APPEND_INTVAL) &&
	    state.speak_units) {
		if (state.use_imperial)
			append_msglist(msg, len, FEET_MSG);
		else
			append_msglist(msg, len, METERS_MSG);
	}
	state.last_units_call = now;
}

static void
do_approaching_rwy(const airport_t *arpt, const runway_t *rwy,
    int end, bool_t on_ground)
{
	const runway_end_t *rwy_end;
	const char *rwy_id;

	ASSERT(arpt != NULL);
	ASSERT(rwy != NULL);
	ASSERT(end == 0 || end == 1);

	rwy_end = &rwy->ends[end];
	rwy_id = rwy_end->id;

	if ((on_ground &&
	    (rwy_key_tbl_get(&state.apch_rwy_ann, arpt->icao, rwy->joint_id) ||
	    rwy_key_tbl_get(&state.on_rwy_ann, arpt->icao, rwy->ends[0].id) ||
	    rwy_key_tbl_get(&state.on_rwy_ann, arpt->icao, rwy->ends[1].id))) ||
	    (!on_ground && rwy_key_tbl_get(&state.air_apch_rwy_ann, arpt->icao,
	    rwy_id) != 0))
		return;

	if (!on_ground || XPLMGetDataf(drs.gs) < SPEED_THRESH) {
		msg_type_t *msg = NULL;
		size_t msg_len = 0;
		msg_prio_t msg_prio;

		if ((on_ground && state.apch_rwys_ann) ||
		    (!on_ground && state.air_apch_rwys_ann))
			return;

		/* Multiple runways being approached? */
		if ((on_ground && avl_numnodes(&state.apch_rwy_ann) >
		    avl_numnodes(&state.on_rwy_ann)) ||
		    (!on_ground && avl_numnodes(&state.air_apch_rwy_ann) !=
		    0)) {
			msg_type_t *apch_rwys;

			if (on_ground)
				/*
				 * On the ground we don't want to re-annunciate
				 * "approaching" once the runway is resolved.
				 */
				rwy_key_tbl_set(&state.apch_rwy_ann,
				    arpt->icao, rwy->joint_id, B_TRUE);
			else
				/*
				 * In the air, we DO want to re-annunciate
				 * "approaching" once the runway is resolved
				 */
				rwy_key_tbl_empty(&state.air_apch_rwy_ann);
			/*
			 * If the "approaching ..." annunciation for the
			 * previous runway is still playing, try to modify
			 * it to say "approaching runways".
			 */
			apch_rwys = malloc(2 * sizeof (msg_type_t));
			apch_rwys[0] = APCH_MSG;
			apch_rwys[1] = RWYS_MSG;
			if (modify_cur_msg(apch_rwys, 2, MSG_PRIO_MED)) {
				ND_alert(ND_ALERT_APP, ND_ALERT_ROUTINE, "37",
				    -1);
				if (on_ground)
					state.apch_rwys_ann = B_TRUE;
				else
					state.air_apch_rwys_ann = B_TRUE;
				return;
			} else {
				free(apch_rwys);
			}
		}

		int dist_ND = -1;
		nd_alert_level_t level = ND_ALERT_ROUTINE;

		append_msglist(&msg, &msg_len, APCH_MSG);
		rwy_id_to_msg(rwy_end->id, &msg, &msg_len);
		msg_prio = MSG_PRIO_LOW;

		if (!on_ground && rwy->length < state.min_landing_dist) {
			dist_to_msg(rwy->length, &msg, &msg_len, B_TRUE);
			append_msglist(&msg, &msg_len, AVAIL_MSG);
			msg_prio = MSG_PRIO_HIGH;
			dist_ND = rwy->length;
			level = ND_ALERT_NONROUTINE;
		}

		play_msg(msg, msg_len, msg_prio);
		ND_alert(ND_ALERT_APP, level, rwy_end->id, dist_ND);
	}

	if (on_ground)
		rwy_key_tbl_set(&state.apch_rwy_ann, arpt->icao,
		    rwy->joint_id, B_TRUE);
	else
		rwy_key_tbl_set(&state.air_apch_rwy_ann, arpt->icao,
		    rwy_id, B_TRUE);
}

static bool_t
ground_runway_approach_arpt_rwy(const airport_t *arpt, const runway_t *rwy,
    vect2_t pos_v, vect2_t vel_v)
{
	ASSERT(arpt != NULL);
	ASSERT(rwy != NULL);

	if (vect2_in_poly(pos_v, rwy->prox_bbox) ||
	    vect2poly_isect(vel_v, pos_v, rwy->prox_bbox)) {
		do_approaching_rwy(arpt, rwy, closest_rwy_end(pos_v, rwy),
		    B_TRUE);
		return (B_TRUE);
	} else {
		rwy_key_tbl_remove(&state.apch_rwy_ann, arpt->icao,
		    rwy->joint_id);
		return (B_FALSE);
	}
}

static unsigned
ground_runway_approach_arpt(const airport_t *arpt, vect2_t vel_v)
{
	vect2_t pos_v;
	unsigned in_prox = 0;

	ASSERT(arpt != NULL);
	ASSERT(!IS_NULL_VECT(vel_v));

	ASSERT(arpt->load_complete);
	pos_v = geo2fpp(GEO_POS2(XPLMGetDatad(drs.lat), XPLMGetDatad(drs.lon)),
	    &arpt->fpp);

	for (const runway_t *rwy = avl_first(&arpt->rwys); rwy != NULL;
	    rwy = AVL_NEXT(&arpt->rwys, rwy)) {
		if (ground_runway_approach_arpt_rwy(arpt, rwy, pos_v, vel_v))
			in_prox++;
	}

	return (in_prox);
}

static void
ground_runway_approach(void)
{
	unsigned in_prox = 0;

	if (XPLMGetDataf(drs.rad_alt) < RADALT_FLARE_THRESH) {
		vect2_t vel_v = acf_vel_vector(RWY_PROXIMITY_TIME_FACT);
		ASSERT(state.cur_arpts != NULL);
		for (const airport_t *arpt = list_head(state.cur_arpts);
		    arpt != NULL; arpt = list_next(state.cur_arpts, arpt))
			in_prox += ground_runway_approach_arpt(arpt, vel_v);
	}

	if (in_prox == 0) {
		if (state.landing)
			dbg_log("flt_state", 1, "state.landing = false");
		state.landing = B_FALSE;
	}
	if (in_prox <= 1)
		state.apch_rwys_ann = B_FALSE;
}

static void
perform_on_rwy_ann(const char *rwy_id, vect2_t pos_v, vect2_t thr_v,
    vect2_t opp_thr_v, bool_t no_flap_check, bool_t non_routine, int repeats)
{
	msg_type_t *msg = NULL;
	size_t msg_len = 0;
	double dist = 10000000;
	int dist_ND = -1;
	double flaprqst = XPLMGetDataf(drs.flaprqst);
	bool_t allow_on_rwy_ND_alert = B_TRUE;
	nd_alert_level_t level = (non_routine ? ND_ALERT_NONROUTINE :
	    ND_ALERT_ROUTINE);

	if (!IS_NULL_VECT(pos_v) && !IS_NULL_VECT(thr_v) &&
	    !IS_NULL_VECT(opp_thr_v)) {
		double thr2thr = vect2_abs(vect2_sub(thr_v, opp_thr_v));
		double thr2pos = vect2_abs(vect2_sub(thr_v, pos_v));

		if (thr2thr > thr2pos)
			dist = vect2_abs(vect2_sub(opp_thr_v, pos_v));
		else
			dist = 0;
	}

	ASSERT(rwy_id != NULL);
	for (int i = 0; i < repeats; i++) {
		append_msglist(&msg, &msg_len, ON_RWY_MSG);

		rwy_id_to_msg(rwy_id, &msg, &msg_len);
		if (dist < state.min_takeoff_dist && !state.landing) {
			dist_to_msg(dist, &msg, &msg_len, B_TRUE);
			dist_ND = dist;
			level = ND_ALERT_NONROUTINE;
			append_msglist(&msg, &msg_len, RMNG_MSG);
		}

		if ((flaprqst < state.min_takeoff_flap ||
		    flaprqst > state.max_takeoff_flap) &&
		    !state.landing && !gpws_flaps_ovrd() && !no_flap_check) {
			append_msglist(&msg, &msg_len, FLAPS_MSG);
			append_msglist(&msg, &msg_len, FLAPS_MSG);
			allow_on_rwy_ND_alert = B_FALSE;
			ND_alert(ND_ALERT_FLAPS, ND_ALERT_CAUTION, NULL, -1);
		}
	}

	play_msg(msg, msg_len, MSG_PRIO_HIGH);
	if (allow_on_rwy_ND_alert)
		ND_alert(ND_ALERT_ON, level, rwy_id, dist_ND);
}

static void
on_rwy_check(const char *arpt_id, const char *rwy_id, double hdg,
    double rwy_hdg, vect2_t pos_v, vect2_t thr_v, vect2_t opp_thr_v)
{
	int64_t now = microclock();
	double rhdg = fabs(rel_hdg(hdg, rwy_hdg));

	ASSERT(arpt_id != NULL);
	ASSERT(rwy_id != NULL);

	/*
	 * If we are not at all on the appropriate runway heading, don't
	 * generate any annunciations.
	 */
	if (rhdg >= 90) {
		/* reset the annunciation if the aircraft turns around fully */
		rwy_key_tbl_remove(&state.on_rwy_ann, arpt_id, rwy_id);
		return;
	}

	if (state.on_rwy_timer != -1 && *state.rejected_takeoff == 0 &&
	    ((now - state.on_rwy_timer > SEC2USEC(state.on_rwy_warn_initial) &&
	    state.on_rwy_warnings == 0) ||
	    (now - state.on_rwy_timer - SEC2USEC(state.on_rwy_warn_initial) >
	    state.on_rwy_warnings * SEC2USEC(state.on_rwy_warn_repeat))) &&
	    state.on_rwy_warnings < state.on_rwy_warn_max_n) {
		state.on_rwy_warnings++;
		perform_on_rwy_ann(rwy_id, NULL_VECT2, NULL_VECT2, NULL_VECT2,
		    B_TRUE, B_TRUE, 2);
	}

	if (rhdg > HDG_ALIGN_THRESH)
		return;

	if (rwy_key_tbl_get(&state.on_rwy_ann, arpt_id, rwy_id) == B_FALSE) {
		if (XPLMGetDataf(drs.gs) < SPEED_THRESH)
			perform_on_rwy_ann(rwy_id, pos_v, thr_v, opp_thr_v,
			    strcmp(state.rejected_takeoff, "") != 0, B_FALSE, 1);
		rwy_key_tbl_set(&state.on_rwy_ann, arpt_id, rwy_id, B_TRUE);
	}
}

static void
stop_check_reset(const char *arpt_id, const char *rwy_id)
{
	ASSERT(arpt_id != NULL);
	ASSERT(rwy_id != NULL);

	if (rwy_key_tbl_get(&state.accel_stop_max_spd, arpt_id, rwy_id) != 0) {
		rwy_key_tbl_remove(&state.accel_stop_max_spd, arpt_id, rwy_id);
		state.accel_stop_ann_initial = 0;
		for (int i = 0; !isnan(accel_stop_distances[i].min); i++)
			accel_stop_distances[i].ann = B_FALSE;
	}
}

static void
takeoff_rwy_dist_check(vect2_t opp_thr_v, vect2_t pos_v)
{
	ASSERT(!IS_NULL_VECT(opp_thr_v));
	ASSERT(!IS_NULL_VECT(pos_v));

	if (state.short_rwy_takeoff_chk)
		return;

	double dist = vect2_abs(vect2_sub(opp_thr_v, pos_v));
	if (dist < state.min_takeoff_dist) {
		msg_type_t *msg = NULL;
		size_t msg_len = 0;
		append_msglist(&msg, &msg_len, CAUTION_MSG);
		append_msglist(&msg, &msg_len, SHORT_RWY_MSG);
		append_msglist(&msg, &msg_len, SHORT_RWY_MSG);
		play_msg(msg, msg_len, MSG_PRIO_HIGH);
		ND_alert(ND_ALERT_SHORT_RWY, ND_ALERT_CAUTION, NULL, -1);
	}
	state.short_rwy_takeoff_chk = B_TRUE;
}

static void
perform_rwy_dist_remaining_callouts(vect2_t opp_thr_v, vect2_t pos_v,
    bool_t try_hard)
{
	ASSERT(!IS_NULL_VECT(opp_thr_v));
	ASSERT(!IS_NULL_VECT(pos_v));

	double dist = vect2_abs(vect2_sub(opp_thr_v, pos_v));
	accel_stop_dist_t *the_asd = NULL;
	double maxdelta = 1000000;
	msg_type_t *msg = NULL;
	size_t msg_len = 0;

	for (int i = 0; !isnan(accel_stop_distances[i].min); i++) {
		accel_stop_dist_t *asd = &accel_stop_distances[i];
		if (dist > asd->min && dist < asd->max) {
			the_asd = asd;
			break;
		}
		if (try_hard && the_asd == NULL && dist > asd->min &&
		    dist - asd->min < maxdelta) {
			the_asd = asd;
			maxdelta = dist - asd->min;
		}
	}

	if (the_asd == NULL || the_asd->ann)
		return;

	the_asd->ann = B_TRUE;
	dist_to_msg(dist, &msg, &msg_len, B_FALSE);
	append_msglist(&msg, &msg_len, RMNG_MSG);
	play_msg(msg, msg_len, MSG_PRIO_MED);
}

/*
 * Returns the relative pitch angle of the aircraft's nose to the average
 * runway slope (in degrees), positive being nose-up. `te' and 'ote' are
 * elevations of the runway thresholds, ordered in the direction of takeoff
 * (`te' where takeoff was initiated, `ote' direction in which takeoff is
 * being conducted). `len' is the runway length. The units of `te', `ote'
 * and `len' don't matter, as long as they are all the same.
 */
static double
acf_rwy_rel_pitch(double te, double ote, double len)
{
	double rwy_angle = RAD2DEG(asin((ote - te) / len));
	return (XPLMGetDataf(drs.pitch) - rwy_angle);
}

/*
 * Checks if at the current rate of deceleration, we are going to come to
 * a complete stop before traveling `dist_rmng' (in meters). Returns B_TRUE
 * if we are going to stop before that, B_FALSE otherwise.
 */
static bool_t
decel_check(double dist_rmng)
{
	double cur_gs = XPLMGetDataf(drs.gs);
	double decel_rate = (cur_gs - state.last_gs) / EXEC_INTVAL;
	if (decel_rate >= 0)
		return (B_FALSE);
	double t = cur_gs / (-decel_rate);
	double d = cur_gs * t + 0.5 * decel_rate * POW2(t);
	return (d < dist_rmng);
}

static void
stop_check(const runway_t *rwy, int end, double hdg, vect2_t pos_v)
{
	ASSERT(rwy != NULL);
	ASSERT(end == 0 || end == 1);
	ASSERT(!IS_NULL_VECT(pos_v));

	const char *arpt_id = rwy->arpt->icao;
	int oend = !end;
	const runway_end_t *rwy_end = &rwy->ends[end];
	const runway_end_t *orwy_end = &rwy->ends[oend];
	vect2_t opp_thr_v = orwy_end->dthr_v;
	long gs = XPLMGetDataf(drs.gs);
	long maxspd;
	double dist = vect2_abs(vect2_sub(opp_thr_v, pos_v));
	double rhdg = fabs(rel_hdg(hdg, rwy_end->hdg));

	if (gs < SPEED_THRESH) {
		/*
		 * If there's very little runway remaining, we always want to
		 * call that fact out.
		 */
		if (dist < IMMEDIATE_STOP_DIST && rhdg < HDG_ALIGN_THRESH &&
		    gs > SLOW_ROLL_THRESH)
			perform_rwy_dist_remaining_callouts(opp_thr_v, pos_v,
			    B_FALSE);
		else
			stop_check_reset(arpt_id, rwy_end->id);
		return;
	}

	if (rhdg > HDG_ALIGN_THRESH)
		return;

	if (XPLMGetDataf(drs.rad_alt) > RADALT_GRD_THRESH) {
		double clb_rate = conv_per_min(MET2FEET(XPLMGetDatad(drs.elev) -
		    state.last_elev));

		stop_check_reset(arpt_id, rwy_end->id);
		if (state.departed &&
		    XPLMGetDataf(drs.rad_alt) <= RADALT_DEPART_THRESH &&
		    clb_rate < GOAROUND_CLB_RATE_THRESH) {
			/*
			 * Our distance limit is the greater of either:
			 * 1) the greater of:
			 *	a) runway length minus 2000 feet
			 *	b) 3/4 the runway length
			 * 2) the lesser of:
			 *	a) minimum safe state.landing distance
			 *	b) full runway length
			 */
			double dist_lim = MAX(MAX(
			    rwy->length - state.long_land_lim_abs,
			    rwy->length * (1 - state.long_land_lim_fract)),
			    MIN(rwy->length, state.min_landing_dist));
			if (dist < dist_lim) {
				if (!state.long_landing_ann) {
					msg_type_t *msg = NULL;
					size_t msg_len = 0;
					append_msglist(&msg, &msg_len,
					    LONG_LAND_MSG);
					append_msglist(&msg, &msg_len,
					    LONG_LAND_MSG);
					play_msg(msg, msg_len, MSG_PRIO_HIGH);
					dbg_log("ann_state", 1,
					    "state.long_landing_ann = true");
					state.long_landing_ann = B_TRUE;
					ND_alert(ND_ALERT_LONG_LAND,
					    ND_ALERT_CAUTION, NULL, -1);
				}
				perform_rwy_dist_remaining_callouts(opp_thr_v,
				    pos_v, B_TRUE);
			}
		}
		return;
	}

	if (!state.arriving)
		takeoff_rwy_dist_check(opp_thr_v, pos_v);

	maxspd = rwy_key_tbl_get(&state.accel_stop_max_spd, arpt_id, rwy_end->id);
	if (gs > maxspd) {
		rwy_key_tbl_set(&state.accel_stop_max_spd, arpt_id, rwy_end->id, gs);
		maxspd = gs;
	}
	if (!state.landing && gs < maxspd - ACCEL_STOP_SPD_THRESH)
		my_strlcpy(state.rejected_takeoff, rwy_end->id,
		    sizeof (state.rejected_takeoff));

	double rpitch = acf_rwy_rel_pitch(rwy_end->thr.elev,
	    orwy_end->thr.elev, rwy->length);
	/*
	 * We want to perform distance remaining callouts if:
	 * 1) we are NOT state.landing and speed has decayed below the rejected
	 *    takeoff threshold, or
	 * 2) we ARE state.landing, distance remaining is below the stop readout
	 *    cutoff and our deceleration is insufficient to stop within the
	 *    remaining distance, or
	 * 3) we are NOT state.landing, distance remaining is below the rotation
	 *    threshold and our pitch angle to the runway indicates that
	 *    rotation has not yet been initiated.
	 */
	if (strcmp(state.rejected_takeoff, rwy_end->id) == 0 ||
	    (state.landing && dist < MAX(rwy->length / 2, state.stop_dist_cutoff) &&
	    !decel_check(dist)) ||
	    (!state.landing && dist < state.min_rotation_dist &&
	    XPLMGetDataf(drs.rad_alt) < RADALT_GRD_THRESH &&
	    rpitch < state.min_rotation_angle))
		perform_rwy_dist_remaining_callouts(opp_thr_v, pos_v, B_FALSE);
}

static bool_t
ground_on_runway_aligned_arpt(const airport_t *arpt)
{
	ASSERT(arpt != NULL);
	ASSERT(arpt->load_complete);

	bool_t on_rwy = B_FALSE;
	vect2_t pos_v = vect2_add(geo2fpp(GEO_POS2(XPLMGetDatad(drs.lat),
	    XPLMGetDatad(drs.lon)), &arpt->fpp),
	    acf_vel_vector(LANDING_ROLLOUT_TIME_FACT));
	double hdg = XPLMGetDataf(drs.hdg);
	bool_t airborne = (XPLMGetDataf(drs.rad_alt) > RADALT_GRD_THRESH);
	const char *arpt_id = arpt->icao;

	for (const runway_t *rwy = avl_first(&arpt->rwys); rwy != NULL;
	    rwy = AVL_NEXT(&arpt->rwys, rwy)) {
		ASSERT(rwy->tora_bbox != NULL);
		if (!airborne && vect2_in_poly(pos_v, rwy->tora_bbox)) {
		        /*
		         * In order to produce on-runway annunciations we need
		         * to be both NOT airborne AND in the TORA bbox.
		         */
			on_rwy = B_TRUE;
			on_rwy_check(arpt_id, rwy->ends[0].id, hdg,
			    rwy->ends[0].hdg, pos_v, rwy->ends[0].dthr_v,
			    rwy->ends[1].thr_v);
			on_rwy_check(arpt_id, rwy->ends[1].id, hdg,
			    rwy->ends[1].hdg, pos_v, rwy->ends[1].dthr_v,
			    rwy->ends[0].thr_v);
		} else if (!vect2_in_poly(pos_v, rwy->prox_bbox)) {
		        /*
		         * To reset the 'on-runway' annunciation state, we must
		         * have left the wider approach bbox. This is to give
		         * us some hysteresis in case we come onto the runway
		         * at a very shallow angle and whether we're on the
		         * runway or not could fluctuate.
		         */
			rwy_key_tbl_remove(&state.on_rwy_ann, arpt_id,
			    rwy->ends[0].id);
			rwy_key_tbl_remove(&state.on_rwy_ann, arpt_id,
			    rwy->ends[1].id);
			if (strcmp(state.rejected_takeoff, rwy->ends[0].id) ==
			    0 ||
			    strcmp(state.rejected_takeoff, rwy->ends[1].id) ==
			    0) {
				dbg_log("ann_state", 1,
				    "state.rejected_takeoff = nil");
				*state.rejected_takeoff = 0;
			}
		}
		if (vect2_in_poly(pos_v, rwy->asda_bbox)) {
			stop_check(rwy, 0, hdg, pos_v);
			stop_check(rwy, 1, hdg, pos_v);
		} else {
			stop_check_reset(arpt_id, rwy->ends[0].id);
			stop_check_reset(arpt_id, rwy->ends[1].id);
		}
	}

	return (on_rwy);
}

static bool_t
ground_on_runway_aligned(void)
{
	bool_t on_rwy = B_FALSE;

	if (XPLMGetDataf(drs.rad_alt) < RADALT_DEPART_THRESH) {
		for (const airport_t *arpt = list_head(state.cur_arpts);
		    arpt != NULL; arpt = list_next(state.cur_arpts, arpt)) {
			if (ground_on_runway_aligned_arpt(arpt))
				on_rwy = B_TRUE;
		}
	}

	if (on_rwy && XPLMGetDataf(drs.gs) < STOPPED_THRESH) {
		if (state.on_rwy_timer == -1)
			state.on_rwy_timer = microclock();
	} else {
		state.on_rwy_timer = -1;
		state.on_rwy_warnings = 0;
	}

	if (!on_rwy) {
		state.short_rwy_takeoff_chk = B_FALSE;
		*state.rejected_takeoff = 0;
	}

	/* Taxiway takeoff check */
	if (!on_rwy && XPLMGetDataf(drs.rad_alt) < RADALT_GRD_THRESH &&
	    ((!state.landing && XPLMGetDataf(drs.gs) >= SPEED_THRESH) ||
	    (state.landing && XPLMGetDataf(drs.gs) >= HIGH_SPEED_THRESH))) {
		if (!state.on_twy_ann) {
			msg_type_t *msg = NULL;
			size_t msg_len = 0;

			state.on_twy_ann = B_TRUE;
			append_msglist(&msg, &msg_len, CAUTION_MSG);
			append_msglist(&msg, &msg_len, ON_TWY_MSG);
			append_msglist(&msg, &msg_len, ON_TWY_MSG);
			play_msg(msg, msg_len, MSG_PRIO_HIGH);
			ND_alert(ND_ALERT_ON, ND_ALERT_CAUTION, NULL, -1);
		}
	} else if (XPLMGetDataf(drs.gs) < SPEED_THRESH ||
	    XPLMGetDataf(drs.rad_alt) >= RADALT_GRD_THRESH) {
		state.on_twy_ann = B_FALSE;
	}

	return (on_rwy);
}

/*
 * Computes the glide path angle limit based on the optimal glide path angle
 * for the runway (rwy_gpa) and aircraft distance from the runway threshold
 * (dist_from_thr in meters). Returns the limiting glide path angle in
 * degrees or 90 if the aircraft is outside of the glide path angle
 * protection envelope.
 */
static double
gpa_limit(double rwy_gpa, double dist_from_thr)
{
	/*
	 * These are the linear GPA multiplier segments:
	 * "min" is the minimum distance from the threshold (in meters)
	 * "max" is the maximum distance from the threshold (in meters)
	 * "f1" is the multiplier applied at the "min" distance point
	 * "f2" is the multiplier applied at the "max" distance point
	 */
#define	NUM_GPA_SEGS	4
	const lin_func_seg_t gpa_segs[NUM_GPA_SEGS] = {
	    { .min = 463, .max = 926, .f1 = 2, .f2 = 1.62 },
	    { .min = 926, .max = 1389, .f1 = 1.62, .f2 = 1.5 },
	    { .min = 1389, .max = 2315, .f1 = 1.5, .f2 = 1.4 },
	    { .min = 2315, .max = 4167, .f1 = 1.4, .f2 = 1.33 }
	};

	/* Select the appropriate range from the gpa_segs table */
	for (int i = 0; i < NUM_GPA_SEGS; i++) {
		if (dist_from_thr >= gpa_segs[i].min &&
		    dist_from_thr < gpa_segs[i].max) {
			/*
			 * Compute the multiplier as a linear interpolation
			 * between f1 and f2 depending on aircraft relative
			 * position along factor.min and factor.max
			 */
			double mult = gpa_segs[i].f1 +
			    ((dist_from_thr - gpa_segs[i].min) /
			    (gpa_segs[i].max - gpa_segs[i].min)) *
			    (gpa_segs[i].f2 - gpa_segs[i].f1);
			return (MIN(MIN(state.gpa_limit_mult, mult) *
			    rwy_gpa, state.gpa_limit_max));
		}
	}

	return (90);
}

/*
 * Gets the state.landing speed selected in the FMC. This function returns two
 * values:
 *	*) The state.landing speed (a number).
 *	*) A boolean indicating if the state.landing speed return is a reference
 *	   speed (wind margin is NOT taken into account) or an approach
 *	   speed (wind margin IS taken into account). Boeing aircraft tend
 *	   to use Vref, whereas Airbus aircraft tend to use Vapp (V_LS).
 * The functionality of this depends on the exact aircraft model loaded
 * and whether it exposes the state.landing speed to us. If the aircraft doesn't
 * support exposing the state.landing speed or the state.landing speed is not yet
 * selected in the FMC, this function returns two nil values instead.
 */
static double
get_land_spd(bool_t *vref)
{
	double val;

	ASSERT(vref != NULL);
	*vref = B_FALSE;

	/* FlightFactor 777 */
	if (chk_acf_dr(FF777, "T7Avionics/fms/vref")) {
		val = XPLMGetDatai(XPLMFindDataRef("T7Avionics/fms/vref"));
		if (val < 100)
			return (NAN);
		*vref = B_FALSE;
		return (val);
	/* JARDesigns A320 & A330 */
	} else if (chk_acf_dr(JAR, "sim/custom/xap/pfd/vappr_knots")) {
		/* First try the Vapp, otherwise fall back to Vref */
		val = XPLMGetDatai(XPLMFindDataRef(
		    "sim/custom/xap/pfd/vappr_knots"));
		if (val < 100) {
			*vref = B_FALSE;
			val = XPLMGetDatai(XPLMFindDataRef(
			    "sim/custom/xap/pfd/vref_knots"));
		} else {
			*vref = B_TRUE;
		}
		if (val < 100)
			return (NAN);
		return (val);
	}
	return (NAN);
}

/*
 * Computes the approach speed limit. The approach speed limit is computed
 * relative to the state.landing speed selected in the FMC with an extra added on
 * top based on our height above the runway threshold:
 * 1) If the aircraft is outside the approach speed limit protection envelope
 *	(below 300 feet or above 950 feet above runway elevation), this
 *	function returns a very high speed value to guarantee that any
 *	comparison with the actual airspeed will indicate "in range".
 * 2) If the FMC doesn't expose state.landing speed information, or the information
 *	has not yet been entered, this function again returns a very high
 *	spped limit value.
 * 3) If the FMC exposes state.landing speed information and the information has
 *	been set, the computed margin value is:
 *	a) if the state.landing speed is based on the reference state.landing speed (Vref),
 *	   +30 knots between 300 and 500 feet, and between 500 and 950
 *	   increasing linearly from +30 knots at 500 feet to +40 knots at
 *	   950 feet.
 *	b) if the state.landing speed is based on the approach state.landing speed (Vapp),
 *	   +15 knots between 300 and 500 feet, and between 500 and 950
 *	   increasing linearly from +15 knots at 500 feet to +40 knots at
 *	   950 feet.
 */
static double
apch_spd_limit(double height_abv_thr)
{
	bool_t spd_is_ref;
	double land_spd = get_land_spd(&spd_is_ref);
	double spd_margin = 10000;

#define	APCH_SPD_SEGS 2
	/*
	 * min and max are feet above threshold elevation
	 * f1 and f2 are indicated airspeed values in knots
	 */
	const lin_func_seg_t vref_segs[APCH_SPD_SEGS] = {
		/*
		 * If the state.landing speed is a reference speed (Vref), we allow
		 * up to 30 knots above Vref for approach speed margin when low.
		 */
		{ .min = 300, .max = 500, .f1 = 30, .f2 = 30 },
		{ .min = 500, .max = 950, .f1 = 30, .f2 = 40 }
	};
	const lin_func_seg_t vapp_segs[APCH_SPD_SEGS] = {
		/*
		 * If the state.landing speed is an approach speed (Vapp), we use
		 * a more restrictive speed margin value of 15 knots when low
		 */
		{ .min = 300, .max = 500, .f1 = 15, .f2 = 15 },
		{ .min = 500, .max = 950, .f1 = 15, .f2 = 40 }
	};
	const lin_func_seg_t *segs;

	/*
	 * If the state.landing speed is unknown, just return a huge number so
	 * we will always be under this speed.
	 */
	if (isnan(land_spd))
		return (10000);

	if (spd_is_ref)
		segs = vref_segs;
	else
		segs = vapp_segs;

	/* Select the appropriate range from the spd_factors table */
	for (int i = 0; i < APCH_SPD_SEGS; i++) {
		if (height_abv_thr >= segs[i].min &&
		    height_abv_thr < segs[i].max) {
			/*
			 * Compute the speed limit as a linear interpolation
			 * between f1 and f2 depending on aircraft relative
			 * position along seg.min and seg.max
			 */
			spd_margin = segs[i].f1 +
			    ((height_abv_thr - segs[i].min) /
			    (segs[i].max - segs[i].min)) *
			    (segs[i].f2 - segs[i].f1);
			break;
		}
	}

	/*
	 * If no segment above matched, we don't perform approach speed
	 * checks, so spd_margin is set to a high value to guarantee we'll
	 * always fit within limits
	 */
	return (land_spd + spd_margin);
}

static bool_t
apch_config_chk(const char *arpt_id, const char *rwy_id, double height_abv_thr,
    double gpa_act, double rwy_gpa, double win_ceil, double win_floor,
    msg_type_t **msg, size_t *msg_len, avl_tree_t *flap_ann_table,
    avl_tree_t *gpa_ann_table, avl_tree_t *spd_ann_table, bool_t critical,
    bool_t add_pause, double dist_from_thr, bool_t check_gear)
{
	ASSERT(arpt_id != NULL);
	ASSERT(rwy_id != NULL);

	double clb_rate = conv_per_min(MET2FEET(XPLMGetDatad(drs.elev) -
	    state.last_elev));

	if (height_abv_thr < win_ceil && height_abv_thr > win_floor &&
	    (!gear_is_up() || !check_gear) &&
	    clb_rate < GOAROUND_CLB_RATE_THRESH) {
		dbg_log("apch_config_chk", 2, "check at %.0f/%.0f",
		    win_ceil, win_floor);
		dbg_log("apch_config_chk", 2, "gpa_act = %.02f rwy_gpa = %.02f",
		    gpa_act, rwy_gpa);
		if (rwy_key_tbl_get(flap_ann_table, arpt_id, rwy_id) == 0 &&
		    XPLMGetDataf(drs.flaprqst) < state.min_landing_flap) {
			dbg_log("apch_config_chk", 1, "FLAPS: flaprqst = %f "
			    "min_flap = %f", XPLMGetDataf(drs.flaprqst),
			    state.min_landing_flap);
			if (gpws_flaps_ovrd()) {
				dbg_log("apch_config_chk", 1,
				    "FLAPS: flaps ovrd active");
			} else {
				if (!critical) {
					append_msglist(msg, msg_len, FLAPS_MSG);
					if (add_pause)
						append_msglist(msg, msg_len,
						    PAUSE_MSG);
					append_msglist(msg, msg_len, FLAPS_MSG);
					ND_alert(ND_ALERT_FLAPS,
					    ND_ALERT_CAUTION, NULL, -1);
				} else {
					append_msglist(msg, msg_len,
					    UNSTABLE_MSG);
					append_msglist(msg, msg_len,
					    UNSTABLE_MSG);
					ND_alert(ND_ALERT_UNSTABLE,
					    ND_ALERT_CAUTION, NULL, -1);
				}
			}
			rwy_key_tbl_set(flap_ann_table, arpt_id, rwy_id,
			    B_TRUE);
			return (B_TRUE);
		} else if (rwy_key_tbl_get(gpa_ann_table, arpt_id, rwy_id) ==
		    0 && rwy_gpa != 0 &&
		    gpa_act > gpa_limit(rwy_gpa, dist_from_thr)) {
			dbg_log("apch_config_chk", 1, "TOO HIGH: "
			    "gpa_act = %.02f gpa_limit = %.02f",
			    gpa_act, gpa_limit(rwy_gpa, dist_from_thr));
			if (gpws_terr_ovrd()) {
				dbg_log("apch_config_chk", 1,
				    "TOO HIGH: terr ovrd active");
			} else {
				if (!critical) {
					append_msglist(msg, msg_len,
					    TOO_HIGH_MSG);
					if (add_pause)
						append_msglist(msg, msg_len,
						    PAUSE_MSG);
					append_msglist(msg, msg_len,
					    TOO_HIGH_MSG);
					ND_alert(ND_ALERT_TOO_HIGH,
					    ND_ALERT_CAUTION, NULL, -1);
				} else {
					append_msglist(msg, msg_len,
					    UNSTABLE_MSG);
					append_msglist(msg, msg_len,
					    UNSTABLE_MSG);
					ND_alert(ND_ALERT_UNSTABLE,
					    ND_ALERT_CAUTION, NULL, -1);
				}
			}
			rwy_key_tbl_set(gpa_ann_table, arpt_id, rwy_id, B_TRUE);
			return (B_TRUE);
		} else if (rwy_key_tbl_get(spd_ann_table, arpt_id, rwy_id) ==
		    0 && state.too_fast_enabled && XPLMGetDataf(drs.airspeed) >
		    apch_spd_limit(height_abv_thr)) {
			dbg_log("apch_config_chk", 1, "TOO FAST: "
			    "airspeed = %.0f apch_spd_limit = %.0f",
			    XPLMGetDataf(drs.airspeed), apch_spd_limit(
			    height_abv_thr));
			if (gpws_terr_ovrd()) {
				dbg_log("apch_config_chk", 1,
				    "TOO FAST: terr ovrd active");
			} else if (gpws_flaps_ovrd()) {
				dbg_log("apch_config_chk", 1,
				    "TOO FAST: flaps ovrd active");
			} else {
				if (!critical) {
					append_msglist(msg, msg_len,
					    TOO_FAST_MSG);
					if (add_pause)
						append_msglist(msg, msg_len,
						    PAUSE_MSG);
					append_msglist(msg, msg_len,
					    TOO_FAST_MSG);
					ND_alert(ND_ALERT_TOO_FAST,
					    ND_ALERT_CAUTION, NULL, -1);
				} else {
					append_msglist(msg, msg_len,
					    UNSTABLE_MSG);
					append_msglist(msg, msg_len,
					    UNSTABLE_MSG);
					ND_alert(ND_ALERT_UNSTABLE,
					    ND_ALERT_CAUTION, NULL, -1);
				}
			}
			rwy_key_tbl_set(spd_ann_table, arpt_id, rwy_id, B_TRUE);
			return (B_TRUE);
		}
	}

	return (B_FALSE);
}

static bool_t
air_runway_approach_arpt_rwy(const airport_t *arpt, const runway_t *rwy,
    int endpt, vect2_t pos_v, double hdg, double alt)
{
	ASSERT(arpt != NULL);
	ASSERT(rwy != NULL);
	ASSERT(endpt == 0 || endpt == 1);

	const runway_end_t *rwy_end = &rwy->ends[endpt];
	const char *rwy_id = rwy_end->id;
	const char * arpt_id = arpt->icao;
	double elev = arpt->refpt.elev;
	double rwy_hdg = rwy_end->hdg;
	bool_t in_prox_bbox = vect2_in_poly(pos_v, rwy_end->apch_bbox);

	if (in_prox_bbox && fabs(rel_hdg(hdg, rwy_hdg)) < HDG_ALIGN_THRESH) {
		msg_type_t *msg = NULL;
		size_t msg_len = 0;
		msg_prio_t msg_prio = MSG_PRIO_MED;
		vect2_t thr_v = rwy_end->thr_v;
		double dist = vect2_abs(vect2_sub(pos_v, thr_v));

		double gpa_act;
		double rwy_gpa = rwy_end->gpa;
		double tch = rwy_end->tch;
		double telev = rwy_end->thr.elev;
		double above_tch = FEET2MET(MET2FEET(XPLMGetDatad(drs.elev)) -
		    (telev + tch));

		if (state.too_high_enabled && tch != 0 && rwy_gpa != 0 &&
		    fabs(elev - telev) < BOGUS_THR_ELEV_LIMIT)
			gpa_act = RAD2DEG(atan(above_tch / dist));
		else
			gpa_act = 0;

		if (apch_config_chk(arpt_id, rwy_id, alt - telev,
		    gpa_act, rwy_gpa, RWY_APCH_FLAP1_THRESH,
		    RWY_APCH_FLAP2_THRESH, &msg, &msg_len,
		    &state.air_apch_flap1_ann, &state.air_apch_gpa1_ann,
		    &state.air_apch_spd1_ann, B_FALSE, B_TRUE, dist, B_TRUE) ||
		    apch_config_chk(arpt_id, rwy_id, alt - telev,
		    gpa_act, rwy_gpa, RWY_APCH_FLAP2_THRESH,
		    RWY_APCH_FLAP3_THRESH, &msg, &msg_len,
		    &state.air_apch_flap2_ann, &state.air_apch_gpa2_ann,
		    &state.air_apch_spd2_ann, B_FALSE, B_FALSE, dist, B_FALSE) ||
		    apch_config_chk(arpt_id, rwy_id, alt - telev,
		    gpa_act, rwy_gpa, RWY_APCH_FLAP3_THRESH,
		    RWY_APCH_FLAP4_THRESH, &msg, &msg_len,
		    &state.air_apch_flap3_ann, &state.air_apch_gpa3_ann,
		    &state.air_apch_spd3_ann, B_TRUE, B_FALSE, dist, B_FALSE))
			msg_prio = MSG_PRIO_HIGH;

		if (alt - telev < RWY_APCH_ALT_MAX &&
		    alt - telev > RWY_APCH_ALT_MIN &&
		    !number_in_rngs(XPLMGetDataf(drs.rad_alt),
		    RWY_APCH_SUPP_WINDOWS, NUM_RWY_APCH_SUPP_WINDOWS))
			do_approaching_rwy(arpt, rwy, endpt, B_FALSE);

		if (alt - telev < SHORT_RWY_APCH_ALT_MAX &&
		    alt - telev > SHORT_RWY_APCH_ALT_MIN &&
		    rwy_end->land_len < state.min_landing_dist &&
		    !state.air_apch_short_rwy_ann) {
			append_msglist(&msg, &msg_len, CAUTION_MSG);
			append_msglist(&msg, &msg_len, SHORT_RWY_MSG);
			append_msglist(&msg, &msg_len, SHORT_RWY_MSG);
			msg_prio = MSG_PRIO_HIGH;
			state.air_apch_short_rwy_ann = B_TRUE;
			ND_alert(ND_ALERT_SHORT_RWY, ND_ALERT_CAUTION,
			    NULL, -1);
		}

		if (msg_len > 0)
			play_msg(msg, msg_len, msg_prio);

		return (B_TRUE);
	} else if (!in_prox_bbox) {
		rwy_key_tbl_remove(&state.air_apch_rwy_ann, arpt_id, rwy_id);
	}

	return (B_FALSE);
}

static void
reset_airport_approach_table(avl_tree_t *tbl, const airport_t *arpt)
{
	ASSERT(tbl != NULL);
	ASSERT(arpt != NULL);

	if (avl_numnodes(tbl) == 0)
		return;

	for (const runway_t *rwy = avl_first(&arpt->rwys); rwy != NULL;
	    rwy = AVL_NEXT(&arpt->rwys, rwy)) {
		rwy_key_tbl_remove(tbl, arpt->icao, rwy->ends[0].id);
		rwy_key_tbl_remove(tbl, arpt->icao, rwy->ends[1].id);
		rwy_key_tbl_remove(tbl, arpt->icao, rwy->joint_id);
	}
}

static unsigned
air_runway_approach_arpt(const airport_t *arpt)
{
	ASSERT(arpt != NULL);

	unsigned in_apch_bbox = 0;
	double alt = MET2FEET(XPLMGetDatad(drs.elev));
	double hdg = XPLMGetDataf(drs.hdg);
	double elev = arpt->refpt.elev;

	if (alt > elev + 2 * RWY_APCH_FLAP1_THRESH ||
	    alt < elev - ARPT_APCH_BLW_ELEV_THRESH) {
		reset_airport_approach_table(&state.air_apch_flap1_ann, arpt);
		reset_airport_approach_table(&state.air_apch_flap2_ann, arpt);
		reset_airport_approach_table(&state.air_apch_flap3_ann, arpt);
		reset_airport_approach_table(&state.air_apch_gpa1_ann, arpt);
		reset_airport_approach_table(&state.air_apch_gpa2_ann, arpt);
		reset_airport_approach_table(&state.air_apch_gpa3_ann, arpt);
		reset_airport_approach_table(&state.air_apch_spd1_ann, arpt);
		reset_airport_approach_table(&state.air_apch_spd2_ann, arpt);
		reset_airport_approach_table(&state.air_apch_spd3_ann, arpt);
		reset_airport_approach_table(&state.air_apch_rwy_ann, arpt);
		reset_airport_approach_table(&state.apch_rwy_ann, arpt);
		return (0);
	}

	vect2_t pos_v = geo2fpp(GEO_POS2(XPLMGetDatad(drs.lat),
	    XPLMGetDatad(drs.lon)), &arpt->fpp);

	for (const runway_t *rwy = avl_first(&arpt->rwys); rwy != NULL;
	    rwy = AVL_NEXT(&arpt->rwys, rwy)) {
		if (air_runway_approach_arpt_rwy(arpt, rwy, 0, pos_v,
		    hdg, alt) ||
		    air_runway_approach_arpt_rwy(arpt, rwy, 1, pos_v,
		    hdg, alt) ||
		    vect2_in_poly(pos_v, rwy->rwy_bbox))
			in_apch_bbox++;
	}

	return (in_apch_bbox);
}

static void
air_runway_approach(void)
{
	if (XPLMGetDataf(drs.rad_alt) < RADALT_GRD_THRESH)
		return;

	unsigned in_apch_bbox = 0;
	double clb_rate = conv_per_min(MET2FEET(XPLMGetDatad(drs.elev) -
	    state.last_elev));

	for (const airport_t *arpt = list_head(state.cur_arpts); arpt != NULL;
	    arpt = list_next(state.cur_arpts, arpt))
		in_apch_bbox += air_runway_approach_arpt(arpt);

	/*
	 * If we are neither over an approach bbox nor a runway, and we're
	 * not climbing and we're in a state.landing configuration, we're most
	 * likely trying to land onto something that's not a runway.
	 */
	if (in_apch_bbox == 0 && clb_rate < 0 && !gear_is_up() &&
	    XPLMGetDataf(drs.flaprqst) >= state.min_landing_flap) {
		if (XPLMGetDataf(drs.rad_alt) <= OFF_RWY_HEIGHT_MAX) {
			/* only annunciate if we're above the minimum height */
			if (XPLMGetDataf(drs.rad_alt) >= OFF_RWY_HEIGHT_MIN &&
			    !state.off_rwy_ann && !gpws_terr_ovrd()) {
				msg_type_t *msg = NULL;
				size_t msg_len = 0;

				append_msglist(&msg, &msg_len, CAUTION_MSG);
				append_msglist(&msg, &msg_len, TWY_MSG);
				append_msglist(&msg, &msg_len, CAUTION_MSG);
				append_msglist(&msg, &msg_len, TWY_MSG);
				play_msg(msg, msg_len, MSG_PRIO_HIGH);
				ND_alert(ND_ALERT_TWY, ND_ALERT_CAUTION,
				    NULL, -1);
			}
			state.off_rwy_ann = B_TRUE;
		} else {
			/*
			 * Annunciation gets reset once we climb through
			 * the maximum annunciation altitude.
			 */
			state.off_rwy_ann = B_FALSE;
		}
	}
	if (in_apch_bbox == 0)
		state.air_apch_short_rwy_ann = B_FALSE;
	if (in_apch_bbox <= 1 && state.air_apch_rwys_ann) {
		rwy_key_tbl_empty(&state.air_apch_rwy_ann);
		state.air_apch_rwys_ann = B_FALSE;
	}
}

const airport_t *
find_nearest_curarpt(void)
{
	double min_dist = ARPT_LOAD_LIMIT;
	vect3_t pos_ecef = sph2ecef(GEO_POS3(XPLMGetDatad(drs.lat),
	    XPLMGetDatad(drs.lon), XPLMGetDatad(drs.elev)));
	const airport_t *cur_arpt = NULL;

	for (const airport_t *arpt = list_head(state.cur_arpts); arpt != NULL;
	    arpt = list_next(state.cur_arpts, arpt)) {
		double dist = vect3_abs(vect3_sub(arpt->ecef, pos_ecef));
		if (dist < min_dist) {
			min_dist = dist;
			cur_arpt = arpt;
		}
	}

	return (cur_arpt);
}

static void
altimeter_setting(void)
{
	if (!state.alt_setting_enabled ||
	    XPLMGetDataf(drs.rad_alt) < RADALT_GRD_THRESH)
		return;

	const airport_t *cur_arpt = find_nearest_curarpt();
	bool_t field_changed = B_FALSE;

	if (cur_arpt != NULL) {
		const char *arpt_id = cur_arpt->icao;
		dbg_log("altimeter", 2, "find_nearest_curarpt() = %s", arpt_id);
		state.TA = cur_arpt->TA;
		state.TL = cur_arpt->TL;
		state.TATL_field_elev = cur_arpt->refpt.elev;
		if (strcmp(arpt_id, state.TATL_source) != 0) {
			my_strlcpy(state.TATL_source, arpt_id,
			    sizeof (state.TATL_source));
			field_changed = B_TRUE;
			dbg_log("altimeter", 1, "TATL_source: %s "
			    "TA: %d TL: %d field_elev: %d", arpt_id,
			    state.TA, state.TL, state.TATL_field_elev);
		}
	} else {
		float lat = XPLMGetDataf(drs.lat), lon = XPLMGetDataf(drs.lon);
		XPLMNavRef arpt_ref = XPLMFindNavAid(NULL, NULL, &lat, &lon,
		    NULL, xplm_Nav_Airport);
		char outID[256] = { 0 };
		float outLat, outLon;
		vect3_t pos_ecef = NULL_VECT3, arpt_ecef = NULL_VECT3;

		dbg_log("altimeter", 2, "XPLMFindNavAid() = %d", arpt_ref);
		if (arpt_ref != 0) {
			XPLMGetNavAidInfo(arpt_ref, NULL, &outLat, &outLon,
			    NULL, NULL, NULL, outID, NULL, NULL);
			arpt_ecef = sph2ecef(GEO_POS3(outLat, outLon, 0));
			pos_ecef = sph2ecef(GEO_POS3(XPLMGetDatad(drs.lat),
			    XPLMGetDatad(drs.lon), 0));
		}

		if (!IS_NULL_VECT(arpt_ecef) &&
		    strcmp(state.TATL_source, outID) != 0 &&
		    vect3_abs(vect3_sub(pos_ecef, arpt_ecef)) <
		    TATL_REMOTE_ARPT_DIST_LIMIT) {
			geo_pos2_t p = GEO_POS2(outLat, outLon);

			cur_arpt = airport_lookup(&state.airportdb, outID, p);
			if (cur_arpt == NULL)
				cur_arpt = any_airport_at_coords(
				    &state.airportdb, p);
			if (cur_arpt != NULL) {
				dbg_log("altimeter", 2, "fallback airport = %s",
				    cur_arpt->icao);
			}
		}

		if (cur_arpt != NULL) {
			state.TA = cur_arpt->TA;
			state.TL = cur_arpt->TL;
			state.TATL_field_elev = cur_arpt->refpt.elev;
			my_strlcpy(state.TATL_source, cur_arpt->icao,
			    sizeof (state.TATL_source));
			field_changed = B_TRUE;
			dbg_log("altimeter", 1, "TATL_source: %s "
			    "TA: %d TA: %d field_elev: %d", cur_arpt->icao,
			    state.TA, state.TL, state.TATL_field_elev);
		}
	}

	if (state.TL == 0) {
		if (field_changed)
			dbg_log("altimeter", 1, "TL = 0");
		if (state.TA != 0) {
			if (XPLMGetDataf(drs.baro_sl) > STD_BARO_REF) {
				state.TL = state.TA;
			} else {
				double qnh = XPLMGetDataf(drs.baro_sl) * 33.85;
				state.TL = state.TA + 28 * (1013 - qnh);
			}
			if (field_changed)
				dbg_log("altimeter", 1, "TL(auto) = %d",
				    state.TL);
		}
	}
	if (state.TA == 0) {
		if (field_changed)
			dbg_log("altimeter", 1, "TA(auto) = %d", state.TA);
		state.TA = state.TL;
	}

	double elev = MET2FEET(XPLMGetDatad(drs.elev));

	if (state.TA != 0 && elev > state.TA && state.TATL_state == TATL_STATE_ALT) {
		state.TATL_transition = microclock();
		state.TATL_state = TATL_STATE_FL;
		dbg_log("altimeter", 1, "elev > TA (%d) transitioning "
		    "state.TATL_state = fl", state.TA);
	}

	if (state.TL != 0 && elev < state.TA &&
	    XPLMGetDataf(drs.baro_alt) < state.TL &&
	    /*
	     * If there's a gap between the altitudes and flight levels, don't
	     * transition until we're below the state.TA
	     */
	    (state.TA == 0 || elev < state.TA) && state.TATL_state == TATL_STATE_FL) {
		state.TATL_transition = microclock();
		state.TATL_state = TATL_STATE_ALT;
		dbg_log("altimeter", 1, "baro_alt < TL (%d) "
		    "transitioning state.TATL_state = alt", state.TL);
	}

	int64_t now = microclock();
	if (state.TATL_transition != -1) {
		if (/* We have transitioned into ALT mode */
		    state.TATL_state == TATL_STATE_ALT &&
		    /* The fixed timeout has passed, OR */
		    (now - state.TATL_transition > ALTM_SETTING_TIMEOUT ||
		    /*
		     * The field has a known elevation and we are within
		     * 1500 feet of it
		     */
		    (state.TATL_field_elev != TATL_FIELD_ELEV_UNSET &&
		    (elev < state.TATL_field_elev +
		    ALTM_SETTING_ALT_CHK_LIMIT)))) {
			double d_qnh = 0, d_qfe = 0;

			if (state.qnh_alt_enabled)
				d_qnh = fabs(elev - XPLMGetDataf(drs.baro_alt));
			if (state.TATL_field_elev != TATL_FIELD_ELEV_UNSET &&
			    state.qfe_alt_enabled)
				d_qfe = fabs(XPLMGetDataf(drs.baro_alt) -
				    (elev - state.TATL_field_elev));
			dbg_log("altimeter", 1, "alt check; d_qnh: %.1f "
			    " d_qfe: %.1f", d_qnh, d_qfe);
			if (/* The set baro is out of bounds for QNH, OR */
			    d_qnh > ALTIMETER_SETTING_QNH_ERR_LIMIT ||
			    /* Set baro is out of bounds for QFE */
			    d_qfe > ALTM_SETTING_QFE_ERR_LIMIT) {
				msg_type_t *msg = NULL;
				size_t msg_len = 0;
				append_msglist(&msg, &msg_len, ALT_SET_MSG);
				play_msg(msg, msg_len, MSG_PRIO_LOW);
				ND_alert(ND_ALERT_ALTM_SETTING,
				    ND_ALERT_CAUTION, NULL, -1);
			}
			state.TATL_transition = -1;
		} else if (state.TATL_state == TATL_STATE_FL &&
		    now - state.TATL_transition > ALTM_SETTING_TIMEOUT) {
			double d_ref = fabs(XPLMGetDataf(drs.baro_set) -
			    STD_BARO_REF);
			dbg_log("altimeter", 1, "fl check; d_ref: %.1f", d_ref);
			if (d_ref > ALTM_SETTING_BARO_ERR_LIMIT) {
				msg_type_t *msg = NULL;
				size_t msg_len = 0;
				append_msglist(&msg, &msg_len, ALT_SET_MSG);
				play_msg(msg, msg_len, MSG_PRIO_LOW);
				ND_alert(ND_ALERT_ALTM_SETTING,
				    ND_ALERT_CAUTION, NULL, -1);
			}
			state.TATL_transition = -1;
		}
	}
}

/*
 * Transfers our electrical load to bus number `busnr' (numbered from 0).
 */
static void
xfer_elec_bus(int busnr)
{
	int xbusnr = (busnr + 1) % 2;
	if (state.bus_loaded == xbusnr) {
		float val;
		XPLMGetDatavf(drs.plug_bus_load, &val, xbusnr, 1);
		val -= BUS_LOAD_AMPS;
		XPLMSetDatavf(drs.plug_bus_load, &val, xbusnr, 1);
		state.bus_loaded = -1;
	}
	if (state.bus_loaded == -1) {
		float val;
		XPLMGetDatavf(drs.plug_bus_load, &val, busnr, 1);
		val += BUS_LOAD_AMPS;
		XPLMSetDatavf(drs.plug_bus_load, &val, busnr, 1);
		state.bus_loaded = busnr;
	}
}

/*
 * Returns true if X-RAAS has electrical power from the aircraft.
 */
bool_t
xraas_is_on(void)
{
	float bus_volts[2];
	bool_t turned_on;

	XPLMGetDatavf(drs.bus_volt, bus_volts, 0, 2);

	turned_on = ((bus_volts[0] > MIN_BUS_VOLT ||
	    bus_volts[1] > MIN_BUS_VOLT) &&
	    XPLMGetDatai(drs.avionics_on) == 1 &&
	    XPLMGetDatai(drs.gpws_inop) != 1);

	if (turned_on) {
		float bus_volt;
		XPLMGetDatavf(drs.bus_volt, &bus_volt, 0, 1);
		if (bus_volt < MIN_BUS_VOLT)
			xfer_elec_bus(1);
		else
			xfer_elec_bus(0);
	} else if (state.bus_loaded != -1) {
		float bus_load;
		XPLMGetDatavf(drs.plug_bus_load, &bus_load, state.bus_loaded, 1);
		bus_load -= BUS_LOAD_AMPS;
		XPLMSetDatavf(drs.plug_bus_load, &bus_load, state.bus_loaded, 1);
		state.bus_loaded = -1;
	}

	return ((turned_on || state.override_electrical) &&
	    (XPLMGetDatai(drs.replay_mode) == 0 || state.override_replay));
}

static void
raas_exec(void)
{
	if (!xraas_is_on()) {
		dbg_log("power_state", 1, "is_on = false");
		return;
	}

	load_nearest_airports();

	if (XPLMGetDataf(drs.rad_alt) > RADALT_FLARE_THRESH) {
		if (!state.departed) {
			state.departed = B_TRUE;
			dbg_log("flt_state", 1, "state.departed = true");
		}
		if (!state.arriving) {
			state.arriving = B_TRUE;
			dbg_log("flt_state", 1, "state.arriving = true");
		}
		if (state.long_landing_ann) {
			dbg_log("ann_state", 1,
			    "state.long_landing_ann = false");
			state.long_landing_ann = B_FALSE;
		}
	} else if (XPLMGetDataf(drs.rad_alt) < RADALT_GRD_THRESH) {
		if (state.departed) {
			dbg_log("flt_state", 1, "state.landing = true");
			dbg_log("flt_state", 1, "state.departed = false");
			state.landing = B_TRUE;
		}
		state.departed = B_FALSE;
		if (XPLMGetDataf(drs.gs) < SPEED_THRESH)
			state.arriving = B_FALSE;
	}
	if (XPLMGetDataf(drs.gs) < SPEED_THRESH && state.long_landing_ann) {
		dbg_log("ann_state", 1, "state.long_landing_ann = false");
		state.long_landing_ann = B_FALSE;
	}

	ground_runway_approach();
	ground_on_runway_aligned();
	air_runway_approach();
	altimeter_setting();

	if (XPLMGetDataf(drs.rad_alt) > RADALT_DEPART_THRESH) {
		for (int i = 0; !isnan(accel_stop_distances[i].min); i++)
			accel_stop_distances[i].ann = B_FALSE;
	}

	state.last_elev = XPLMGetDatad(drs.elev);
	state.last_gs = XPLMGetDataf(drs.gs);
}

static float
raas_exec_cb(float elapsed_since_last_call, float elapsed_since_last_floop,
    int counter, void *refcon)
{
	UNUSED(elapsed_since_last_call);
	UNUSED(elapsed_since_last_floop);
	UNUSED(counter);
	UNUSED(refcon);

	raas_exec();

	return (EXEC_INTVAL);
}

static void
man_ref(char **str, size_t *cap, int section_number, const char *section_name)
{
	append_format(str, cap,
	    "For more information, please refer to the X-RAAS "
	    "user manual in docs%cmanual.pdf, section %d \"%s\".", DIRSEP,
	    section_number, section_name);
}

/*
 * This is to be called ONCE per X-RAAS startup to log an initial startup
 * message and then exit.
 */
void
log_init_msg(bool_t display, int timeout, int man_sect_number,
    const char *man_sect_name, const char *fmt, ...)
{
	va_list ap;
	int len;
	char *msg;

	va_start(ap, fmt);
	len = vsnprintf(NULL, 0, fmt, ap);
	va_end(ap);

	/* +2 for optional newline and terminating nul */
	msg = calloc(1, len + 2);

	va_start(ap, fmt);
	vsnprintf(msg, len + 1, fmt, ap);
	va_end(ap);

	if (man_sect_number != -1) {
		size_t sz = len + 2;
		man_ref(&msg, &sz, man_sect_number, man_sect_name);
	}

	logMsg("%s", msg);
	if (display) {
		state.init_msg = msg;
		state.init_msg_end = microclock() + SEC2USEC(timeout);
	} else {
		free(msg);
	}
}

/*
 * Check if the aircraft is a helicopter (or at least says it flies like one).
 */
static bool_t
chk_acf_is_helo(void)
{
#if 0
	/* TODO: implement AIRCRAFT_FILENAME */
	FILE *fp = fopen(AIRCRAFT_FILENAME, "r");
	char *line = NULL;
	size_t linecap = 0;
	bool_t result = B_FALSE;

	if (fp != NULL) {
		while (!feof(fp)) {
			if (getline(&line, &linecap, fp) <= 0)
				continue;
			if (strstr(line, "P acf/_fly_like_a_helo") == line) {
				result = (strstr(line,
				    "P acf/_fly_like_a_helo 1") == line);
				break;
			}
		}
	}
	free(line);
	return (result);
#else	/* !0 */
	return (B_FALSE);
#endif	/* !0 */
}

static void
xraas_init(void)
{
	bool_t snd_inited = B_FALSE, dbg_gui_inited = B_FALSE,
	    airportdb_created = B_FALSE;

	ASSERT(!inited);

	/* these must go ahead of config parsing */
	XPLMGetNthAircraftModel(0, acf_filename, acf_path);

	if (!load_configs(&state, plugindir, acf_path))
		return;

	if (!state.enabled)
		return;

	dbg_log("startup", 1, "xraas_init");

	if (!(snd_inited = snd_sys_init(plugindir, &state)))
		goto errout;

	if (state.debug_graphical) {
		dbg_gui_init();
		dbg_gui_inited = B_TRUE;
	}

	raas_dr_reset();

	airportdb_create(&state.airportdb, xpdir, xpprefsdir);
	airportdb_created = B_TRUE;

	if (!recreate_apt_dat_cache(&state.airportdb))
		goto errout;

	if (chk_acf_is_helo() && !state.allow_helos)
		goto errout;

	if (XPLMGetDatai(drs.num_engines) < state.min_engines ||
	    XPLMGetDataf(drs.mtow) < state.min_mtow) {
		char icao[8];
		memset(icao, 0, sizeof (icao));
		XPLMGetDatab(drs.ICAO, icao, 0, sizeof (icao) - 1);
		log_init_msg(state.auto_disable_notify, INIT_ERR_MSG_TIMEOUT,
		    3, "Activating X-RAAS in the aircraft",
		    "X-RAAS: auto-disabled: aircraft below X-RAAS limits:\n"
		    "X-RAAS configuration: minimum number of engines: %d; "
		    "minimum MTOW: %d kg\n"
		    "Your aircraft: (%s) number of engines: %d; "
		    "MTOW: %.0f kg\n", state.min_engines, state.min_mtow, icao,
		    XPLMGetDatai(drs.num_engines), XPLMGetDataf(drs.mtow));
		goto errout;
	}

	ND_alerts_init(&state);

	rwy_key_tbl_create(&state.accel_stop_max_spd);
	rwy_key_tbl_create(&state.on_rwy_ann);
	rwy_key_tbl_create(&state.apch_rwy_ann);
	rwy_key_tbl_create(&state.air_apch_rwy_ann);
	rwy_key_tbl_create(&state.air_apch_flap1_ann);
	rwy_key_tbl_create(&state.air_apch_flap2_ann);
	rwy_key_tbl_create(&state.air_apch_flap3_ann);
	rwy_key_tbl_create(&state.air_apch_gpa1_ann);
	rwy_key_tbl_create(&state.air_apch_gpa2_ann);
	rwy_key_tbl_create(&state.air_apch_gpa3_ann);
	rwy_key_tbl_create(&state.air_apch_spd1_ann);
	rwy_key_tbl_create(&state.air_apch_spd2_ann);
	rwy_key_tbl_create(&state.air_apch_spd3_ann);

	XPLMRegisterFlightLoopCallback(raas_exec_cb, EXEC_INTVAL, NULL);

	inited = B_TRUE;

	return;

errout:
	if (snd_inited)
		snd_sys_fini();
	if (dbg_gui_inited)
		dbg_gui_fini();
	if (airportdb_created)
		airportdb_destroy(&state.airportdb);
	ND_alerts_fini();

	return;
}

static void
xraas_fini(void)
{
	if (!inited)
		return;

	if (!state.enabled)
		return;

	dbg_log("startup", 1, "xraas_fini");

	snd_sys_fini();

	if (state.bus_loaded != -1) {
		float bus_load;
		XPLMGetDatavf(drs.plug_bus_load, &bus_load, state.bus_loaded,
		    1);
		bus_load -= BUS_LOAD_AMPS;
		XPLMSetDatavf(drs.plug_bus_load, &bus_load, state.bus_loaded,
		    1);
		state.bus_loaded = -1;
	}

	if (state.cur_arpts != NULL) {
		free_nearest_airport_list(state.cur_arpts);
		state.cur_arpts = NULL;
	}

	airportdb_destroy(&state.airportdb);

	ND_alerts_fini();

	rwy_key_tbl_destroy(&state.accel_stop_max_spd);
	rwy_key_tbl_destroy(&state.on_rwy_ann);
	rwy_key_tbl_destroy(&state.apch_rwy_ann);
	rwy_key_tbl_destroy(&state.air_apch_rwy_ann);
	rwy_key_tbl_destroy(&state.air_apch_flap1_ann);
	rwy_key_tbl_destroy(&state.air_apch_flap2_ann);
	rwy_key_tbl_destroy(&state.air_apch_flap3_ann);
	rwy_key_tbl_destroy(&state.air_apch_gpa1_ann);
	rwy_key_tbl_destroy(&state.air_apch_gpa2_ann);
	rwy_key_tbl_destroy(&state.air_apch_gpa3_ann);
	rwy_key_tbl_destroy(&state.air_apch_spd1_ann);
	rwy_key_tbl_destroy(&state.air_apch_spd2_ann);
	rwy_key_tbl_destroy(&state.air_apch_spd3_ann);

	if (state.init_msg != NULL) {
		free(state.init_msg);
		state.init_msg = NULL;
	}

	XPLMUnregisterFlightLoopCallback(raas_exec_cb, NULL);

	if (state.debug_graphical)
		dbg_gui_fini();

	inited = B_FALSE;
}

PLUGIN_API int
XPluginStart(char *outName, char *outSig, char *outDesc)
{
	char *p;

	/* Always use Unix-native paths on the Mac! */
	XPLMEnableFeature("XPLM_USE_NATIVE_PATHS", 1);

	strcpy(outName, XRAAS2_PLUGIN_NAME);
	strcpy(outSig, XRAAS2_PLUGIN_SIG);
	strcpy(outDesc, XRAAS2_PLUGIN_DESC);

	XPLMGetSystemPath(xpdir);
	XPLMGetPrefsPath(xpprefsdir);
	XPLMGetPluginInfo(XPLMGetMyID(), NULL, plugindir, NULL, NULL);

#if	IBM
	/*
	 * For some absolutely inexplicable reason, X-Plane returns these
	 * paths as a hybrid mix of '/' and '\' separators on Windows. So
	 * before we start cutting path names, we'll flip all '/'s into
	 * '\'s to keep consistent with the native path separator scheme.
	 */
	for (int i = 0, n = strlen(xpdir); i < n; i++) {
		if (xpdir[i] == '/')
			xpdir[i] = '\\';
	}
	for (int i = 0, n = strlen(xpprefsdir); i < n; i++) {
		if (xpprefsdir[i] == '/')
			xpprefsdir[i] = '\\';
	}
	for (int i = 0, n = strlen(plugindir); i < n; i++) {
		if (plugindir[i] == '/')
			plugindir[i] = '\\';
	}
#endif	/* IBM */

	if (strlen(xpdir) > 0 && xpdir[strlen(xpdir) - 1] == DIRSEP) {
		/*
		 * Strip the last path separator from the end to avoid
		 * double '//' in paths generated with mkpathname. Not
		 * necessary, just a niceness thing.
		 */
		xpdir[strlen(xpdir) - 1] = '\0';
	}

	/* cut off X-Plane.prf from prefs path */
	if ((p = strrchr(xpprefsdir, DIRSEP)) != NULL)
		*p = '\0';

	/* cut off the trailing path component (our filename) */
	if ((p = strrchr(plugindir, DIRSEP)) != NULL)
		*p = '\0';
	/* cut off an optional '64' trailing component */
	if ((p = strrchr(plugindir, DIRSEP)) != NULL) {
		if (strcmp(p + 1, "64") == 0)
			*p = '\0';
	}

	logMsg("xpdir: %s", xpdir);
	logMsg("prefsdir: %s", xpprefsdir);
	logMsg("plugindir: %s", plugindir);

	return (1);
}

PLUGIN_API void
XPluginStop(void)
{
}

PLUGIN_API int
XPluginEnable(void)
{
	xraas_init();
	return (1);
}

PLUGIN_API void
XPluginDisable(void)
{
	xraas_fini();
}

PLUGIN_API void
XPluginReceiveMessage(XPLMPluginID src, int msg, void *param)
{
	UNUSED(src);
	UNUSED(param);

	switch(msg) {
	case XPLM_MSG_PLANE_LOADED:
		/* only respond to user aircraft reloads */
		if (param != 0)
			break;
		xraas_fini();
		xraas_init();
		break;
	}
}
