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

#include <acfutils/assert.h>
#include <acfutils/avl.h>
#include <acfutils/conf.h>
#include <acfutils/dr.h>
#include <acfutils/geom.h>
#include <acfutils/helpers.h>
#include <acfutils/list.h>
#include <acfutils/math.h>
#include <acfutils/perf.h>
#include <acfutils/time.h>
#include <acfutils/types.h>
#include <acfutils/wav.h>

#include "airdata.h"
#include "dbg_gui.h"
#include "dbg_log.h"
#include "gui.h"
#include "init_msg.h"
#include "nd_alert.h"
#include "rwy_key_tbl.h"
#include "snd_sys.h"
#include "xraas2.h"
#include "xraas_cfg.h"

#define	XRAAS2_STANDALONE_PLUGIN_SIG	"skiselkov.xraas2"

#ifdef	XRAAS_IS_EMBEDDED
#if	ACF_TYPE == FF_A320_ACF_TYPE
#define	XRAAS2_PLUGIN_NAME		"X-RAAS " XRAAS2_VERSION \
					" (FlightFactor Airbus A320)"
#define	XRAAS2_PLUGIN_DESC		"A simulation of the Runway " \
					"Awareness and Advisory System " \
					"(FlightFactor Airbus A320 version)"
#define	XRAAS2_PLUGIN_SIG		XRAAS2_STANDALONE_PLUGIN_SIG "_ff_a320"
#define	XRAAS2_DR_PREFIX		"xraas_ff_a320"
#else	/* ACF_TYPE == NO_ACF_TYPE */
#define	XRAAS2_PLUGIN_NAME		"X-RAAS " XRAAS2_VERSION " (embed)"
#define	XRAAS2_PLUGIN_DESC		"A simulation of the Runway " \
					"Awareness and Advisory System " \
					"(embedded version)"
#define	XRAAS2_PLUGIN_SIG		XRAAS2_STANDALONE_PLUGIN_SIG "_embedded"
#define	XRAAS2_DR_PREFIX		"xraas_embed"
#endif	/* ACF_TYPE == NO_ACF_TYPE */
#else	/* !XRAAS_IS_EMBEDDED */
#define	XRAAS2_PLUGIN_NAME		"X-RAAS " XRAAS2_VERSION
#define	XRAAS2_PLUGIN_DESC		"A simulation of the Runway " \
					"Awareness and Advisory System"
#define	XRAAS2_PLUGIN_SIG		XRAAS2_STANDALONE_PLUGIN_SIG
#define	XRAAS2_DR_PREFIX		"xraas"
#endif	/* !XRAAS_IS_EMBEDDED */

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
#define	GOAROUND_CLB_RATE_THRESH	400		/* feet per minute */
#define	OFF_RWY_HEIGHT_MAX		250		/* feet */
#define	OFF_RWY_HEIGHT_MIN		100		/* feet */
#define	ILS_HDEF_LIMIT			1.0		/* dots */
#define	ILS_VDEF_LIMIT			2.0		/* dots */

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

#define	XRAAS_CACHE_DIR			"X-RAAS.cache"

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

enum {
	OVRD_GPWS_PRIO,
	OVRD_GPWS_PRIO_ACT,
	OVRD_GPWS_INOP,
	OVRD_GPWS_INOP_ACT,
	OVRD_GPWS_FLAPS_OVRD,
	OVRD_GPWS_FLAPS_OVRD_ACT,
	OVRD_GPWS_TERR_OVRD,
	OVRD_GPWS_TERR_OVRD_ACT,
	OVRD_VAPP,
	OVRD_VAPP_ACT,
	OVRD_VREF,
	OVRD_VREF_ACT,
	TAKEOFF_FLAPS,
	TAKEOFF_FLAPS_ACT,
	LANDING_FLAPS,
	LANDING_FLAPS_ACT,
	NUM_OVERRIDES
};

/*
 * These allow aircraft avionics to directly override our behavior
 * without having to modify us to look into aircraft model-specific
 * datarefs. They are exposed via datarefs under "xraas/override".
 */
static struct {
	union {
		int		value_i;
		float		value_f;
	};
	dr_t			dr;
	const XPLMDataTypeID	type;
	const char		*name;
} overrides[NUM_OVERRIDES] = {
	{
		.type = xplmType_Int,
		.name = XRAAS2_DR_PREFIX "/override/GPWS_prio"
	},
	{
		.type = xplmType_Int,
		.name = XRAAS2_DR_PREFIX "/override/GPWS_prio_act"
	},
	{
		.type = xplmType_Int,
		.name = XRAAS2_DR_PREFIX "/override/GPWS_inop"
	},
	{
		.type = xplmType_Int,
		.name = XRAAS2_DR_PREFIX "/override/GPWS_inop_act"
	},
	{
		.type = xplmType_Int,
		.name = XRAAS2_DR_PREFIX "/override/GPWS_flaps_ovrd"
	},
	{
		.type = xplmType_Int,
		.name = XRAAS2_DR_PREFIX "/override/GPWS_flaps_ovrd_act"
	},
	{
		.type = xplmType_Int,
		.name = XRAAS2_DR_PREFIX "/override/GPWS_terr_ovrd"
	},
	{
		.type = xplmType_Int,
		.name = XRAAS2_DR_PREFIX "/override/GPWS_terr_ovrd_act"
	},
	{
		.type = xplmType_Int,
		.name = XRAAS2_DR_PREFIX "/override/Vapp"
	},
	{
		.type = xplmType_Int,
		.name = XRAAS2_DR_PREFIX "/override/Vapp_act"
	},
	{
		.type = xplmType_Int,
		.name = XRAAS2_DR_PREFIX "/override/Vref"
	},
	{
		.type = xplmType_Int,
		.name = XRAAS2_DR_PREFIX "/override/Vref_act"
	},
	{
		.type = xplmType_Int,
		.name = XRAAS2_DR_PREFIX "/override/takeoff_flaps"
	},
	{
		.type = xplmType_Float,
		.name = XRAAS2_DR_PREFIX "/override/takeoff_flaps_act"
	},
	{
		.type = xplmType_Int,
		.name = XRAAS2_DR_PREFIX "/override/landing_flaps"
	},
	{
		.type = xplmType_Float,
		.name = XRAAS2_DR_PREFIX "/override/landing_flaps_act"
	}
};

static dr_t	input_faulted_dr;

static bool_t plugin_conflict = B_FALSE;

bool_t xraas_inited = B_FALSE;
static xraas_state_t state;
const xraas_state_t *xraas_state = &state;

static char xpdir[512] = { 0 };
static char prefsdir[512] = { 0 };
static char plugindir[512] = { 0 };
static char acf_path[512] = { 0 };
static char acf_dirpath[512] = { 0 };
static XPLMDataRef acf_livpath_dr = NULL;
static char acf_livpath[1024] = { 0 };
static char acf_filename[512] = { 0 };

const char *xraas_xpdir = xpdir;
const char *xraas_prefsdir = prefsdir;
const char *xraas_acf_dirpath = acf_dirpath;
const char *xraas_acf_livpath = acf_livpath;
const char *xraas_plugindir = plugindir;

static const char *FJS737[] = { "B732", NULL };
static const char *IXEG737[] = { "B733", NULL };
static const char *FF757_767[] = { "B752", "B753", "B763", NULL };
static const char *FF777[] = { "B772", "B773", "B77L", "B77W", NULL };
static const char *JAR[] = {
	"A318", "A319", "A320", "A321", "A322", "A333", "A338", "A339", NULL
};

static void
overrides_init(void)
{
	for (int i = 0; i < NUM_OVERRIDES; i++) {
		overrides[i].value_i = 0;
		if (overrides[i].type == xplmType_Int) {
			dr_create_i(&overrides[i].dr, &overrides[i].value_i,
			    B_TRUE, "%s", overrides[i].name);
		} else {
			ASSERT3S(overrides[i].type, ==, xplmType_Float);
			dr_create_f(&overrides[i].dr, &overrides[i].value_f,
			    B_TRUE, "%s", overrides[i].name);
		}
	}
}

static void
overrides_fini(void)
{
	for (int i = 0; i < NUM_OVERRIDES; i++) {
		dr_delete(&overrides[i].dr);
		overrides[i].value_i = 0;
	}
}

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

static void
reset_airport_rwy_table(rwy_key_tbl_t *tbl, const airport_t *arpt)
{
	ASSERT(tbl != NULL);
	ASSERT(arpt != NULL);

	if (avl_numnodes(&tbl->tree) == 0)
		return;

	for (const runway_t *rwy = avl_first(&arpt->rwys); rwy != NULL;
	    rwy = AVL_NEXT(&arpt->rwys, rwy)) {
		rwy_key_tbl_remove(tbl, arpt->icao, rwy->ends[0].id);
		rwy_key_tbl_remove(tbl, arpt->icao, rwy->ends[1].id);
		rwy_key_tbl_remove(tbl, arpt->icao, rwy->joint_id);
	}
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
 * Returns true if landing gear is fully retracted, false otherwise.
 */
static bool_t
gear_is_up(void)
{
	for (size_t i = 0; i < adc->n_gear; i++) {
		if (adc->gear_type[i] != 0 && adc->gear[i] > 0.0)
			return (B_FALSE);
	}

	return (B_TRUE);
}

bool_t
view_is_external(void)
{
	return (XPLMGetDatai(drs->view_is_ext) != 0);
}

/*
 * Returns true if the GPWS computer is inoperative.
 */
bool_t
GPWS_is_inop(void)
{
	if (overrides[OVRD_GPWS_INOP].value_i != 0)
		return (overrides[OVRD_GPWS_INOP_ACT].value_i != 0);
	return (drs->gpws_inop != NULL ?XPLMGetDatai(drs->gpws_inop) != 0 :
	    B_FALSE);
}

/*
 * Returns true if the GPWS has requested priority for its annunciations.
 * Our output is suppressed.
 */
bool_t
GPWS_has_priority(void)
{
	if (overrides[OVRD_GPWS_PRIO].value_i != 0)
		return (overrides[OVRD_GPWS_PRIO_ACT].value_i != 0);
	if (ff_a320_is_loaded())
		return (ff_a320_suppressed() || ff_a320_alerting());
	return (drs->gpws_prio != NULL ? XPLMGetDatai(drs->gpws_prio) != 0 :
	    B_FALSE);
}

static bool_t
chk_acf_dr(const char **icaos, const char *author, const char *drname)
{
	char icao[8];

	memset(icao, 0, sizeof (icao));
	XPLMGetDatab(drs->ICAO, icao, 0, sizeof (icao) - 1);

	if (author != NULL) {
		char acf_author[64];
		memset(acf_author, 0, sizeof (acf_author));
		XPLMGetDatab(drs->author, acf_author, 0,
		    sizeof (acf_author) - 1);
		if (strcmp(acf_author, author) != 0)
			return (B_FALSE);
	}

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
	if (overrides[OVRD_GPWS_TERR_OVRD].value_i != 0) {
		return (overrides[OVRD_GPWS_TERR_OVRD_ACT].value_i != 0);
	} else if (chk_acf_dr(FF757_767, NULL, "anim/75/button")) {
		return (XPLMGetDatai(XPLMFindDataRef("anim/75/button")) == 1);
	} else if (chk_acf_dr(FF777, NULL, "anim/51/button")) {
		return (XPLMGetDatai(XPLMFindDataRef("anim/51/button")) == 1);
	} else if (chk_acf_dr(IXEG737, NULL, "ixeg/733/misc/egpws_gear_act")) {
		return (XPLMGetDatai(XPLMFindDataRef(
		    "ixeg/733/misc/egpws_gear_act")) == 1);
	} else if (chk_acf_dr(FJS737, "FlyJsim",
	    "FJS/732/Annun/GPWS_InhibitSwitch")) {
		return (XPLMGetDatai(XPLMFindDataRef(
		    "FJS/732/Annun/GPWS_InhibitSwitch")) == 1);
	} else if (chk_acf_dr(JAR, NULL, "sim/custom/xap/gpws_terr")) {
		return (XPLMGetDatai(XPLMFindDataRef(
		    "sim/custom/xap/gpws_terr")) == 1);
	} else if (ff_a320_is_loaded()) {
		return (ff_a320_inhibit() || ff_a320_inhibit_ex());
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
	if (overrides[OVRD_GPWS_FLAPS_OVRD].value_i != 0) {
		return (overrides[OVRD_GPWS_FLAPS_OVRD_ACT].value_i != 0);
	} else if (chk_acf_dr(FF757_767, NULL, "anim/72/button")) {
		return (XPLMGetDatai(XPLMFindDataRef("anim/72/button")) == 1);
	} else if (chk_acf_dr(FF777, NULL, "anim/79/button")) {
		return (XPLMGetDatai(XPLMFindDataRef("anim/79/button")) == 1);
	} else if (chk_acf_dr(IXEG737, NULL, "ixeg/733/misc/egpws_flap_act")) {
		return (XPLMGetDatai(XPLMFindDataRef(
		    "ixeg/733/misc/egpws_flap_act")) == 1);
	} else if (chk_acf_dr(JAR, NULL, "sim/custom/xap/gpws_flap")) {
		return (XPLMGetDatai(XPLMFindDataRef(
		    "sim/custom/xap/gpws_flap")) != 0);
	} else if (ff_a320_is_loaded()) {
		return (ff_a320_inhibit() || ff_a320_inhibit_flaps());
	}
	return (gpws_terr_ovrd());
}

/*
 * Sets the current rejected-takeoff (RTO) state. Must be supplied by an
 * airport and runway identifier to set on which runway we are rejecting the
 * takeoff. Alternatively, supply NULL for both arguments to reset the RTO
 * state to the initial non-rejected-takeoff state.
 */
static void
set_rto(const char *icao, const char *rwy_id)
{
	if (icao != NULL && rwy_id != NULL) {
		if (strcmp(state.rejected_takeoff.icao, icao) == 0 &&
		    strcmp(state.rejected_takeoff.rwy_id, rwy_id) == 0)
			return;
		dbg_log(flt_state, 1, "rejected_takeoff = %s/%s", icao, rwy_id);
		strlcpy(state.rejected_takeoff.icao, icao,
		    sizeof (state.rejected_takeoff.icao));
		strlcpy(state.rejected_takeoff.rwy_id, rwy_id,
		    sizeof (state.rejected_takeoff.rwy_id));
	} else {
		ASSERT(icao == NULL && rwy_id == NULL);
		if (*state.rejected_takeoff.icao == 0 &&
		    *state.rejected_takeoff.rwy_id == 0)
			return;
		dbg_log(flt_state, 1, "rejected_takeoff = nil");
		*state.rejected_takeoff.icao = 0;
		*state.rejected_takeoff.rwy_id = 0;
	}
}

/*
 * Checks if we are currently in a rejected-takeoff (RTO) state. If supplied
 * with an airport and runway identifier, checks if we are rejecting a takeoff
 * on that specific runway.
 */
static bool_t
check_rto(const char *icao, const char *rwy_id)
{
	if (icao != NULL && rwy_id != NULL) {
		return (strcmp(state.rejected_takeoff.icao, icao) == 0 &&
		    strcmp(state.rejected_takeoff.rwy_id, rwy_id) == 0);
	} else if (icao != NULL) {
		return (strcmp(state.rejected_takeoff.icao, icao) == 0);
	} else {
		ASSERT(icao == NULL && rwy_id == NULL);
		return (*state.rejected_takeoff.rwy_id != 0);
	}
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

	if (now - state.last_airport_reload < SEC2USEC(ARPT_RELOAD_INTVAL) &&
	    state.cur_arpts != NULL)
		return;
	state.last_airport_reload = now;

	/*
	 * Must go ahead of unload_distant_airport_tiles to avoid
	 * tripping an assertion in free_aiport.
	 */
	if (state.cur_arpts != NULL)
		free_nearest_airport_list(state.cur_arpts);

	my_pos = GEO_POS2(adc->lat, adc->lon);
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

#ifdef	XRAAS_IS_EMBEDDED
	if (ff_a320_is_loaded())
		ff_a320_find_nearest_rwy();
#endif	/* XRAAS_IS_EMBEDDED */
}

/*
 * Computes the aircraft's on-ground velocity vector. The length of the
 * vector is computed as a `time_fact' (in seconds) extra ahead of the
 * actual aircraft's nosewheel position.
 */
vect2_t
acf_vel_vector(double time_fact)
{
	return (vect2_set_abs(hdg2dir(adc->hdg),
	    time_fact * adc->gs - adc->nw_offset));
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
	ASSERT(is_valid_rwy_ID(rwy_id));

	char first_digit = rwy_id[0];

	if (first_digit != '0' || !state.config.us_runway_numbers)
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
dist_to_msg(double dist, msg_type_t **msg, size_t *len, bool_t div_by_100,
    bool_t allow_units)
{
	uint64_t now;

	ASSERT(msg != NULL);
	ASSERT(len != NULL);

	if (!div_by_100) {
		if (state.config.use_imperial) {
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

		if (state.config.use_imperial) {
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
	    state.config.speak_units && allow_units) {
		if (state.config.use_imperial)
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

	if (!on_ground || adc->gs < SPEED_THRESH) {
		msg_type_t *msg = NULL;
		size_t msg_len = 0;
		msg_prio_t msg_prio;

		if ((on_ground && state.apch_rwys_ann) ||
		    (!on_ground && state.air_apch_rwys_ann))
			return;

		/* Multiple runways being approached? */
		if ((on_ground && avl_numnodes(&state.apch_rwy_ann.tree) >
		    avl_numnodes(&state.on_rwy_ann.tree)) ||
		    (!on_ground && avl_numnodes(&state.air_apch_rwy_ann.tree) !=
		    0)) {
			msg_type_t *apch_rwys;

			if ((on_ground &&
			    !state.config.monitors[APCH_RWY_ON_GND_MON]) ||
			    (!on_ground &&
			    !state.config.monitors[APCH_RWY_IN_AIR_MON]))
				return;

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

		/*
		 * For short rwys, use a different enabling check. If OFF,
		 * we still want to try and annunciate as normal.
		 */
		if (!on_ground && rwy->length < state.config.min_landing_dist &&
		    state.config.monitors[APCH_RWY_IN_AIR_SHORT_MON]) {
			dist_to_msg(rwy->length, &msg, &msg_len, B_TRUE,
			    B_TRUE);
			append_msglist(&msg, &msg_len, AVAIL_MSG);
			msg_prio = MSG_PRIO_HIGH;
			dist_ND = rwy->length;
			level = ND_ALERT_NONROUTINE;
		} else {
			if ((on_ground &&
			    !state.config.monitors[APCH_RWY_ON_GND_MON]) ||
			    (!on_ground &&
			    !state.config.monitors[APCH_RWY_IN_AIR_MON])) {
				free(msg);
				return;
			}
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

	if (point_in_poly(pos_v, rwy->prox_bbox) ||
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
	pos_v = geo2fpp(GEO_POS2(adc->lat, adc->lon), &arpt->fpp);

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

	if (adc->rad_alt < RADALT_FLARE_THRESH) {
		vect2_t vel_v = acf_vel_vector(RWY_PROXIMITY_TIME_FACT);
		ASSERT(state.cur_arpts != NULL);
		for (const airport_t *arpt = list_head(state.cur_arpts);
		    arpt != NULL; arpt = list_next(state.cur_arpts, arpt))
			in_prox += ground_runway_approach_arpt(arpt, vel_v);
	} else {
		for (const airport_t *arpt = list_head(state.cur_arpts);
		    arpt != NULL; arpt = list_next(state.cur_arpts, arpt)) {
			reset_airport_rwy_table(&state.apch_rwy_ann, arpt);
			reset_airport_rwy_table(&state.on_rwy_ann, arpt);
		}
	}

	if (in_prox == 0) {
		if (state.landing)
			dbg_log(flt_state, 1, "state.landing = false");
		state.landing = B_FALSE;
	}
	if (in_prox <= 1)
		state.apch_rwys_ann = B_FALSE;
}

/*
 * Returns true if the current flaps setting is valid for takeoff or landing
 * (depending on the takeoff parameter).
 * If the FMS provides an override, we check the flaps setting exactly.
 * Otherwise we use the min_takeoff_flap/max_takeoff_flap or min_landing_flap
 * limits.
 */
static bool_t
flaps_chk(bool_t takeoff)
{
	double lower_gate, upper_gate;

	if (takeoff) {
		if (!isnan(adc->takeoff_flaps)) {
			lower_gate = upper_gate = adc->takeoff_flaps;
		} else if (overrides[TAKEOFF_FLAPS].value_i != 0) {
			lower_gate = upper_gate =
			    overrides[TAKEOFF_FLAPS_ACT].value_f;
		} else {
			lower_gate = state.config.min_takeoff_flap;
			upper_gate = state.config.max_takeoff_flap;
		}
	} else {
		if (!isnan(adc->landing_flaps)) {
			lower_gate = upper_gate = adc->landing_flaps;
		} else if (overrides[LANDING_FLAPS].value_i != 0) {
			lower_gate = upper_gate =
			    overrides[LANDING_FLAPS_ACT].value_f;
		} else {
			lower_gate = state.config.min_landing_flap;
			upper_gate = 1.0;
		}
	}
	dbg_log(config, 2, "flaps_chk (%s) %.02f <= %.02f <= %.02f",
	    takeoff ? "takeoff" : "landing", lower_gate, adc->flaprqst,
	    upper_gate);
	return (lower_gate <= adc->flaprqst && adc->flaprqst <= upper_gate);
}

static void
perform_on_rwy_ann(const char *rwy_id, vect2_t pos_v, vect2_t thr_v,
    vect2_t opp_thr_v, bool_t length_check, bool_t flap_check,
    bool_t non_routine, int repeats, int monitor)
{
	msg_type_t *msg = NULL;
	size_t msg_len = 0;
	double dist = 10000000;
	int dist_ND = -1;
	bool_t allow_on_rwy_ND_alert = B_TRUE;
	bool_t monitor_override = B_FALSE;
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
	}

	if (dist < state.config.min_takeoff_dist && !state.landing &&
	    length_check) {
		dist_to_msg(dist, &msg, &msg_len, B_TRUE, B_TRUE);
		dist_ND = dist;
		level = ND_ALERT_NONROUTINE;
		append_msglist(&msg, &msg_len, RMNG_MSG);
		monitor_override = B_TRUE;
	}

	if (!flaps_chk(B_TRUE) && !state.landing &&
	    !gpws_flaps_ovrd() && flap_check) {
		dbg_log(apch_cfg_chk, 1, "FLAPS: flaprqst = %g "
		    "min_flap = %g (adc: %g ovrd: %g)", adc->flaprqst,
		    state.config.min_landing_flap, adc->landing_flaps,
		    overrides[LANDING_FLAPS_ACT].value_f);
		append_msglist(&msg, &msg_len, FLAPS_MSG);
		append_msglist(&msg, &msg_len, FLAPS_MSG);
		allow_on_rwy_ND_alert = B_FALSE;
		ND_alert(ND_ALERT_FLAPS, ND_ALERT_NONROUTINE, NULL, -1);
		monitor_override = B_TRUE;
	}

	if (!state.config.monitors[monitor] && !monitor_override) {
		free(msg);
		return;
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

	if (state.on_rwy_timer != -1 && !check_rto(arpt_id, NULL) &&
	    ((now - state.on_rwy_timer >
	    SEC2USEC(state.config.on_rwy_warn_initial) &&
	    state.on_rwy_warnings == 0) ||
	    (now - state.on_rwy_timer -
	    SEC2USEC(state.config.on_rwy_warn_initial) >
	    state.on_rwy_warnings *
	    SEC2USEC(state.config.on_rwy_warn_repeat))) &&
	    state.on_rwy_warnings < state.config.on_rwy_warn_max_n) {
		state.on_rwy_warnings++;
		perform_on_rwy_ann(rwy_id, NULL_VECT2, NULL_VECT2, NULL_VECT2,
		    B_FALSE, B_FALSE, B_TRUE, 2, ON_RWY_HOLDING_MON);
	}

	if (rhdg > HDG_ALIGN_THRESH)
		return;

	if (rwy_key_tbl_get(&state.on_rwy_ann, arpt_id, rwy_id) == B_FALSE) {
		if (adc->gs < SPEED_THRESH) {
			perform_on_rwy_ann(rwy_id, pos_v, thr_v, opp_thr_v,
			    state.config.monitors[ON_RWY_LINEUP_SHORT_MON] &&
			    !check_rto(arpt_id, NULL),
			    state.config.monitors[ON_RWY_FLAP_MON] &&
			    !check_rto(arpt_id, NULL), B_FALSE, 1,
			    ON_RWY_LINEUP_MON);
		}
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
	if (dist < state.config.min_takeoff_dist &&
	    state.config.monitors[ON_RWY_TKOFF_SHORT_MON]) {
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
perform_rwy_dist_remaining_callouts_extended(vect2_t opp_thr_v, vect2_t pos_v,
    bool_t try_hard, bool_t allow_last_dist, msg_type_t **msg, size_t *msg_len)
{
	ASSERT(!IS_NULL_VECT(opp_thr_v));
	ASSERT(!IS_NULL_VECT(pos_v));

	double dist = vect2_abs(vect2_sub(opp_thr_v, pos_v));
	accel_stop_dist_t *the_asd = NULL;
	double maxdelta = 1000000;
	bool_t allow_units = B_TRUE;

	for (int i = 0; !isnan(accel_stop_distances[i].min); i++) {
		accel_stop_dist_t *asd = &accel_stop_distances[i];
		if (isnan(accel_stop_distances[i + 1].min)) {
			/* Only allow the last 100 foot callout if requested */
			if (!allow_last_dist)
				break;
			/*
			 * The 100 foot callout never has units - not enough
			 * time left to say them.
			 */
			allow_units = B_FALSE;
		}
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
	dist_to_msg(dist, msg, msg_len, B_FALSE, allow_units);
	append_msglist(msg, msg_len, RMNG_MSG);
}

static void
perform_rwy_dist_remaining_callouts(vect2_t opp_thr_v, vect2_t pos_v,
    bool_t try_hard, bool_t allow_last_dist)
{
	msg_type_t *msg = NULL;
	size_t msg_len = 0;
	perform_rwy_dist_remaining_callouts_extended(opp_thr_v, pos_v,
	    try_hard, allow_last_dist, &msg, &msg_len);
	if (msg_len != 0)
		play_msg(msg, msg_len, MSG_PRIO_HIGH);
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
	return (adc->pitch - rwy_angle);
}

/*
 * Checks if at the current rate of deceleration, we are going to come to
 * a complete stop before traveling `dist_rmng' (in meters). Returns B_TRUE
 * if we are going to stop before that, B_FALSE otherwise.
 */
static bool_t
decel_check(double dist_rmng)
{
	double cur_gs = adc->gs;
	double decel_rate = (cur_gs - state.last_gs) / EXEC_INTVAL;
	if (decel_rate >= 0)
		return (B_FALSE);
	double t = cur_gs / (-decel_rate);
	double d = cur_gs * t + 0.5 * decel_rate * POW2(t);
	return (d < dist_rmng);
}

static void
long_landing_check(const runway_t *rwy, double dist, vect2_t opp_thr_v,
    vect2_t pos_v)
{
	/*
	 * Our distance limit is the greater of either:
	 * 1) the greater of:
	 *	a) runway length minus 2000 feet
	 *	b) 3/4 the runway length
	 * 2) the lesser of:
	 *	a) minimum safe landing distance
	 *	b) full runway length
	 */
	double dist_lim = MAX(MAX(rwy->length - state.config.long_land_lim_abs,
	    rwy->length * (1 - state.config.long_land_lim_fract)),
	    MIN(rwy->length, state.config.min_landing_dist));
	if (dist < dist_lim && state.config.monitors[LONG_LAND_MON]) {
		if (!state.long_landing_ann) {
			msg_type_t m = state.config.say_deep_landing ?
			    DEEP_LAND_MSG : LONG_LAND_MSG;
			msg_type_t *msg = NULL;
			size_t msg_len = 0;
			dbg_log(ann_state, 1, "state.long_landing_ann = true");
			append_msglist(&msg, &msg_len, m);
			append_msglist(&msg, &msg_len, m);
			perform_rwy_dist_remaining_callouts_extended(opp_thr_v,
			    pos_v, B_TRUE, B_FALSE, &msg, &msg_len);
			play_msg(msg, msg_len, MSG_PRIO_HIGH);

			state.long_landing_ann = B_TRUE;
			ND_alert(state.config.say_deep_landing ?
			    ND_ALERT_DEEP_LAND : ND_ALERT_LONG_LAND,
			    ND_ALERT_CAUTION, NULL, -1);
		} else {
			perform_rwy_dist_remaining_callouts(opp_thr_v, pos_v,
			    B_FALSE, B_FALSE);
		}
	}
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
	long gs = adc->gs;
	long maxspd;
	double dist = vect2_abs(vect2_sub(opp_thr_v, pos_v));
	double rhdg = fabs(rel_hdg(hdg, rwy_end->hdg));

	if (gs < SPEED_THRESH) {
		/*
		 * If there's very little runway remaining, we always want to
		 * call that fact out.
		 */
		if (dist < IMMEDIATE_STOP_DIST && rhdg < HDG_ALIGN_THRESH &&
		    gs > SLOW_ROLL_THRESH && state.config.monitors[RWY_END_MON])
			perform_rwy_dist_remaining_callouts(opp_thr_v, pos_v,
			    B_FALSE, B_TRUE);
		else
			stop_check_reset(arpt_id, rwy_end->id);
		return;
	}

	if (rhdg > HDG_ALIGN_THRESH)
		return;

	if (adc->rad_alt > RADALT_GRD_THRESH) {
		double clb_rate = conv_per_min(MET2FEET(adc->elev -
		    state.last_elev));
		stop_check_reset(arpt_id, rwy_end->id);
		if (state.departed && adc->rad_alt <= RADALT_DEPART_THRESH &&
		    clb_rate < GOAROUND_CLB_RATE_THRESH)
			long_landing_check(rwy, dist, opp_thr_v, pos_v);
		return;
	}

	if (!state.arriving)
		takeoff_rwy_dist_check(opp_thr_v, pos_v);

	maxspd = rwy_key_tbl_get(&state.accel_stop_max_spd, arpt_id,
	    rwy_end->id);
	if (gs > maxspd) {
		rwy_key_tbl_set(&state.accel_stop_max_spd, arpt_id,
		    rwy_end->id, gs);
		maxspd = gs;
	}
	if (!state.landing && gs < maxspd - ACCEL_STOP_SPD_THRESH)
		set_rto(arpt_id, rwy_end->id);

	double rpitch = acf_rwy_rel_pitch(rwy_end->thr.elev,
	    orwy_end->thr.elev, rwy->length);
	/*
	 * We want to perform distance remaining callouts if:
	 * 1) we are NOT landing and speed has decayed below the rejected
	 *    takeoff threshold, or
	 * 2) we ARE landing, distance remaining is below the stop readout
	 *    cutoff and our deceleration is insufficient to stop within the
	 *    remaining distance, or
	 * 3) we are NOT landing, distance remaining is below the rotation
	 *    threshold and our pitch angle to the runway indicates that
	 *    rotation has not yet been initiated.
	 */
	if ((check_rto(arpt_id, rwy_end->id) &&
	    state.config.monitors[DIST_RMNG_RTO_MON]) ||
	    (state.landing && dist < MAX(rwy->length / 2,
	    state.config.stop_dist_cutoff) && !decel_check(dist) &&
	    state.config.monitors[DIST_RMNG_LAND_MON]) ||
	    (!state.landing && dist < state.config.min_rotation_dist &&
	    adc->rad_alt < RADALT_GRD_THRESH &&
	    rpitch < state.config.min_rotation_angle &&
	    state.config.monitors[LATE_ROTATION_MON]))
		perform_rwy_dist_remaining_callouts(opp_thr_v, pos_v, B_FALSE,
		    B_TRUE);
}

static bool_t
ground_on_runway_aligned_arpt(const airport_t *arpt)
{
	ASSERT(arpt != NULL);
	ASSERT(arpt->load_complete);

	bool_t on_rwy = B_FALSE;
	vect2_t pos_v = vect2_add(geo2fpp(GEO_POS2(adc->lat, adc->lon),
	    &arpt->fpp), acf_vel_vector(LANDING_ROLLOUT_TIME_FACT));
	double hdg = adc->hdg;
	bool_t airborne = (adc->rad_alt > RADALT_GRD_THRESH);
	const char *arpt_id = arpt->icao;

	for (const runway_t *rwy = avl_first(&arpt->rwys); rwy != NULL;
	    rwy = AVL_NEXT(&arpt->rwys, rwy)) {
		ASSERT(rwy->tora_bbox != NULL);
		if (!airborne && point_in_poly(pos_v, rwy->tora_bbox)) {
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
		} else if (!point_in_poly(pos_v, rwy->prox_bbox)) {
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
			if (check_rto(arpt->icao, rwy->ends[0].id) ||
			    check_rto(arpt->icao, rwy->ends[1].id))
				set_rto(NULL, NULL);
		}
		if (point_in_poly(pos_v, rwy->asda_bbox)) {
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

	if (adc->rad_alt < RADALT_DEPART_THRESH) {
		for (const airport_t *arpt = list_head(state.cur_arpts);
		    arpt != NULL; arpt = list_next(state.cur_arpts, arpt)) {
			if (ground_on_runway_aligned_arpt(arpt))
				on_rwy = B_TRUE;
		}
	}

	if (on_rwy && adc->gs < STOPPED_THRESH) {
		if (state.on_rwy_timer == -1)
			state.on_rwy_timer = microclock();
	} else {
		state.on_rwy_timer = -1;
		state.on_rwy_warnings = 0;
	}

	if (!on_rwy)
		state.short_rwy_takeoff_chk = B_FALSE;

	/* Taxiway takeoff check */
	if (!on_rwy && adc->rad_alt < RADALT_GRD_THRESH &&
	    ((!state.landing && adc->gs >= SPEED_THRESH) ||
	    (state.landing && adc->gs >= HIGH_SPEED_THRESH))) {
		if (!state.on_twy_ann && state.config.monitors[TWY_TKOFF_MON]) {
			msg_type_t *msg = NULL;
			size_t msg_len = 0;

			state.on_twy_ann = B_TRUE;
			append_msglist(&msg, &msg_len, CAUTION_MSG);
			append_msglist(&msg, &msg_len, ON_TWY_MSG);
			append_msglist(&msg, &msg_len, ON_TWY_MSG);
			play_msg(msg, msg_len, MSG_PRIO_HIGH);
			ND_alert(ND_ALERT_ON, ND_ALERT_CAUTION, NULL, -1);
		}
	} else if (adc->gs < SPEED_THRESH ||
	    adc->rad_alt >= RADALT_GRD_THRESH) {
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
			return (MIN(MIN(state.config.gpa_limit_mult, mult) *
			    rwy_gpa, state.config.gpa_limit_max));
		}
	}

	return (90);
}

/*
 * Gets the landing speed selected in the FMC. This function returns two
 * values:
 *	*) The landing speed (a number).
 *	*) A boolean indicating if the landing speed return is a reference
 *	   speed (wind margin is NOT taken into account) or an approach
 *	   speed (wind margin IS taken into account). Boeing aircraft tend
 *	   to use Vref, whereas Airbus aircraft tend to use Vapp (V_LS).
 * The functionality of this depends on the exact aircraft model loaded
 * and whether it exposes the landing speed to us. If the aircraft doesn't
 * support exposing the landing speed or the landing speed is not yet
 * selected in the FMC, this function returns two nil values instead.
 */
static double
get_land_spd(bool_t *vref)
{
	double val;
	enum { MIN_APPCH_SPD = 60 };

	ASSERT(vref != NULL);
	*vref = B_FALSE;

	/* first try the overrides */
	if (!isnan(adc->vapp) && adc->vapp > MIN_APPCH_SPD) {
		*vref = B_FALSE;
		return (adc->vapp);
	}
	if (!isnan(adc->vref) && adc->vref > MIN_APPCH_SPD) {
		*vref = B_TRUE;
		return (adc->vref);
	}
	if (overrides[OVRD_VAPP].value_i != 0) {
		*vref = B_FALSE;
		if (overrides[OVRD_VAPP_ACT].value_i > MIN_APPCH_SPD)
			return (overrides[OVRD_VAPP_ACT].value_i);
		else
			return (NAN);
	}
	if (overrides[OVRD_VREF].value_i != 0) {
		*vref = B_TRUE;
		if (overrides[OVRD_VREF_ACT].value_i > MIN_APPCH_SPD)
			return (overrides[OVRD_VREF].value_i);
		else
			return (NAN);
	}

	/* FlightFactor 777 */
	if (chk_acf_dr(FF777, NULL, "T7Avionics/fms/vref")) {
		val = XPLMGetDatai(XPLMFindDataRef("T7Avionics/fms/vref"));
		if (val < MIN_APPCH_SPD)
			return (NAN);
		*vref = B_FALSE;
		return (val);
	/* JARDesigns A320 & A330 */
	} else if (chk_acf_dr(JAR, NULL, "sim/custom/xap/pfd/vappr_knots")) {
		/* First try the Vapp, otherwise fall back to Vref */
		val = XPLMGetDatai(XPLMFindDataRef(
		    "sim/custom/xap/pfd/vappr_knots"));
		if (val < MIN_APPCH_SPD) {
			*vref = B_FALSE;
			val = XPLMGetDatai(XPLMFindDataRef(
			    "sim/custom/xap/pfd/vref_knots"));
		} else {
			*vref = B_TRUE;
		}
		if (val < MIN_APPCH_SPD)
			return (NAN);
		return (val);
	}
	return (NAN);
}

/*
 * Computes the approach speed limit. The approach speed limit is computed
 * relative to the landing speed selected in the FMC with an extra added
 * on top based on our height above the runway threshold:
 * 1) If the aircraft is outside the approach speed limit protection envelope
 *	(below 300 feet or above 950 feet above runway elevation), this
 *	function returns a very high speed value to guarantee that any
 *	comparison with the actual airspeed will indicate "in range".
 * 2) If the FMC doesn't expose landing speed information, or the
 *	information has not yet been entered, this function again returns
 *	a very high speed limit value.
 * 3) If the FMC exposes landing speed information and the information
 *	has been set, the computed margin value is:
 *	a) if the landing speed is based on the reference landing speed
 *	   (Vref), +30 knots between 300 and 500 feet, and between 500 and 950
 *	   increasing linearly from +30 knots at 500 feet to +40 knots at
 *	   950 feet.
 *	b) if the landing speed is based on the approach landing speed
 *	   (Vapp), +15 knots between 300 and 500 feet, and between 500 and 950
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
		 * If the landing speed is a reference speed (Vref), we allow
		 * up to 30 knots above Vref for approach speed margin when low.
		 */
		{ .min = 300, .max = 500, .f1 = 30, .f2 = 30 },
		{ .min = 500, .max = 950, .f1 = 30, .f2 = 40 }
	};
	const lin_func_seg_t vapp_segs[APCH_SPD_SEGS] = {
		/*
		 * If the landing speed is an approach speed (Vapp), we use
		 * a more restrictive speed margin value of 15 knots when low
		 */
		{ .min = 300, .max = 500, .f1 = 15, .f2 = 15 },
		{ .min = 500, .max = 950, .f1 = 15, .f2 = 40 }
	};
	const lin_func_seg_t *segs;

	/*
	 * If the landing speed is unknown, just return a huge number so
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

static void
ann_apch_cfg(msg_type_t **msg, size_t *msg_len, bool_t add_pause,
    msg_type_t msg_type, nd_alert_msg_type_t nd_alert)
{
	append_msglist(msg, msg_len, msg_type);
	if (add_pause)
		append_msglist(msg, msg_len, PAUSE_MSG);
	append_msglist(msg, msg_len, msg_type);
	ND_alert(nd_alert, ND_ALERT_CAUTION, NULL, -1);
}

static void
ann_unstable_apch(msg_type_t **msg, size_t *msg_len)
{
	if (!state.config.monitors[APCH_UNSTABLE_MON] || state.unstable_ann)
		return;
	append_msglist(msg, msg_len, UNSTABLE_MSG);
	append_msglist(msg, msg_len, UNSTABLE_MSG);
	ND_alert(ND_ALERT_UNSTABLE, ND_ALERT_CAUTION, NULL, -1);
	state.unstable_ann = B_TRUE;
}

static bool_t
apch_cfg_chk(const char *arpt_id, const char *rwy_id, double height_abv_thr,
    double gpa_act, double rwy_gpa, double win_ceil, double win_floor,
    msg_type_t **msg, size_t *msg_len, rwy_key_tbl_t *flap_ann_table,
    rwy_key_tbl_t *gpa_ann_table, rwy_key_tbl_t *spd_ann_table,
    bool_t critical, bool_t upper_gate, bool_t add_pause, double dist_from_thr,
    bool_t check_gear)
{
	ASSERT(arpt_id != NULL);
	ASSERT(rwy_id != NULL);

	double clb_rate = conv_per_min(MET2FEET(adc->elev - state.last_elev));
	int too_fast_mon, too_high_mon, flaps_mon;

	if (upper_gate) {
		too_fast_mon = APCH_TOO_FAST_UPPER_MON;
		too_high_mon = APCH_TOO_HIGH_UPPER_MON;
		flaps_mon = APCH_FLAPS_UPPER_MON;
	} else {
		too_fast_mon = APCH_TOO_FAST_LOWER_MON;
		too_high_mon = APCH_TOO_HIGH_LOWER_MON;
		flaps_mon = APCH_FLAPS_LOWER_MON;
	}

	if (height_abv_thr < win_ceil && height_abv_thr > win_floor &&
	    (!gear_is_up() || !check_gear) &&
	    clb_rate < GOAROUND_CLB_RATE_THRESH) {
		dbg_log(apch_cfg_chk, 2, "check at %.0f/%.0f",
		    win_ceil, win_floor);
		dbg_log(apch_cfg_chk, 2, "gpa_act = %.02f rwy_gpa = %.02f",
		    gpa_act, rwy_gpa);
		if (rwy_key_tbl_get(flap_ann_table, arpt_id, rwy_id) == 0 &&
		    !flaps_chk(B_FALSE) && !gpws_flaps_ovrd() &&
		    state.config.monitors[flaps_mon]) {
			dbg_log(apch_cfg_chk, 1, "FLAPS: flaprqst = %g "
			    "min_flap = %g (adc: %g ovrd: %g)", adc->flaprqst,
			    state.config.min_landing_flap, adc->landing_flaps,
			    overrides[LANDING_FLAPS_ACT].value_f);
			if (!critical)
				ann_apch_cfg(msg, msg_len, add_pause,
				    FLAPS_MSG, ND_ALERT_FLAPS);
			else
				ann_unstable_apch(msg, msg_len);
			rwy_key_tbl_set(flap_ann_table, arpt_id, rwy_id,
			    B_TRUE);
			return (B_TRUE);
		/*
		 * To annunciate TOO HIGH, all of the following conditions
		 * must be met:
		 * 1) we have NOT yet annunciated for this runway
		 * 2) we are NOT approaching mutliple runways
		 * 3) we know the GPA for this runway
		 * 4) GPWS terrain override is NOT active
		 * 5) actual GPA is above computed GPA limit for this runway
		 * 6) IF we have an ILS tuned & receiving, either:
		 *	6a) horizontal deflection is above limit (1 dot), OR
		 *	6b) vertical deflection is above limit (2 dots)
		 * 7) the too high approach monitor is enabled
		 */
		} else if (!rwy_key_tbl_get(gpa_ann_table, arpt_id, rwy_id) &&
		    !state.apch_rwys_ann && rwy_gpa != 0 && !gpws_terr_ovrd() &&
		    gpa_act > gpa_limit(rwy_gpa, dist_from_thr) &&
		    (!adc->ils_info.active ||
		    fabs(adc->ils_info.hdef) > ILS_HDEF_LIMIT ||
		    adc->ils_info.vdef > ILS_VDEF_LIMIT) &&
		    state.config.monitors[too_high_mon]) {
			dbg_log(apch_cfg_chk, 1, "TOO HIGH: "
			    "gpa_act = %.02f gpa_limit = %.02f",
			    gpa_act, gpa_limit(rwy_gpa, dist_from_thr));
			if (!critical)
				ann_apch_cfg(msg, msg_len, add_pause,
				    TOO_HIGH_MSG, ND_ALERT_TOO_HIGH);
			else
				ann_unstable_apch(msg, msg_len);
			rwy_key_tbl_set(gpa_ann_table, arpt_id, rwy_id, B_TRUE);
			return (B_TRUE);
		} else if (rwy_key_tbl_get(spd_ann_table, arpt_id, rwy_id) ==
		    0 && state.config.monitors[too_fast_mon] &&
		    !gpws_terr_ovrd() && !gpws_flaps_ovrd() &&
		    adc->cas > apch_spd_limit(height_abv_thr) &&
		    state.config.monitors[too_fast_mon]) {
			dbg_log(apch_cfg_chk, 1, "TOO FAST: "
			    "airspeed = %.0f apch_spd_limit = %.0f",
			    adc->cas, apch_spd_limit(height_abv_thr));
			if (!critical)
				ann_apch_cfg(msg, msg_len, add_pause,
				    TOO_FAST_MSG, ND_ALERT_TOO_FAST);
			else
				ann_unstable_apch(msg, msg_len);
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
	const char *arpt_id = arpt->icao;
	double elev = arpt->refpt.elev;
	double rwy_hdg = rwy_end->hdg;
	bool_t in_prox_bbox = point_in_poly(pos_v, rwy_end->apch_bbox);

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
		double above_tch = FEET2MET(MET2FEET(adc->elev) - (telev + tch));

		if (tch != 0 && rwy_gpa != 0 &&
		    fabs(elev - telev) < BOGUS_THR_ELEV_LIMIT)
			gpa_act = RAD2DEG(atan(above_tch / dist));
		else
			gpa_act = 0;

		if (apch_cfg_chk(arpt_id, rwy_id, alt - telev,
		    gpa_act, rwy_gpa, RWY_APCH_FLAP1_THRESH,
		    RWY_APCH_FLAP2_THRESH, &msg, &msg_len,
		    &state.air_apch_flap1_ann, &state.air_apch_gpa1_ann,
		    &state.air_apch_spd1_ann, B_FALSE, B_TRUE, B_TRUE, dist,
		    B_TRUE) ||
		    apch_cfg_chk(arpt_id, rwy_id, alt - telev, gpa_act,
		    rwy_gpa, RWY_APCH_FLAP2_THRESH, RWY_APCH_FLAP3_THRESH,
		    &msg, &msg_len,
		    &state.air_apch_flap2_ann, &state.air_apch_gpa2_ann,
		    &state.air_apch_spd2_ann, B_FALSE, B_FALSE, B_FALSE, dist,
		    B_FALSE) ||
		    apch_cfg_chk(arpt_id, rwy_id, alt - telev, gpa_act,
		    rwy_gpa, RWY_APCH_FLAP3_THRESH, RWY_APCH_FLAP4_THRESH,
		    &msg, &msg_len,
		    &state.air_apch_flap3_ann, &state.air_apch_gpa3_ann,
		    &state.air_apch_spd3_ann, B_TRUE, B_FALSE, B_FALSE, dist,
		    B_FALSE))
			msg_prio = MSG_PRIO_HIGH;

		if (alt - telev < RWY_APCH_ALT_MAX &&
		    alt - telev > RWY_APCH_ALT_MIN &&
		    !number_in_rngs(adc->rad_alt,
		    RWY_APCH_SUPP_WINDOWS, NUM_RWY_APCH_SUPP_WINDOWS))
			do_approaching_rwy(arpt, rwy, endpt, B_FALSE);

		if (alt - telev < SHORT_RWY_APCH_ALT_MAX &&
		    alt - telev > SHORT_RWY_APCH_ALT_MIN &&
		    rwy_end->land_len < state.config.min_landing_dist &&
		    !state.air_apch_short_rwy_ann &&
		    state.config.monitors[APCH_RWY_IN_AIR_SHORT_MON]) {
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

static unsigned
air_runway_approach_arpt(const airport_t *arpt)
{
	ASSERT(arpt != NULL);

	unsigned in_apch_bbox = 0;
	double alt = MET2FEET(adc->elev);
	double hdg = adc->hdg;
	double elev = arpt->refpt.elev;

	if (alt > elev + 2 * RWY_APCH_FLAP1_THRESH ||
	    alt < elev - ARPT_APCH_BLW_ELEV_THRESH) {
		reset_airport_rwy_table(&state.air_apch_flap1_ann, arpt);
		reset_airport_rwy_table(&state.air_apch_flap2_ann, arpt);
		reset_airport_rwy_table(&state.air_apch_flap3_ann, arpt);
		reset_airport_rwy_table(&state.air_apch_gpa1_ann, arpt);
		reset_airport_rwy_table(&state.air_apch_gpa2_ann, arpt);
		reset_airport_rwy_table(&state.air_apch_gpa3_ann, arpt);
		reset_airport_rwy_table(&state.air_apch_spd1_ann, arpt);
		reset_airport_rwy_table(&state.air_apch_spd2_ann, arpt);
		reset_airport_rwy_table(&state.air_apch_spd3_ann, arpt);
		reset_airport_rwy_table(&state.air_apch_rwy_ann, arpt);
		return (0);
	}

	vect2_t pos_v = geo2fpp(GEO_POS2(adc->lat, adc->lon), &arpt->fpp);

	for (const runway_t *rwy = avl_first(&arpt->rwys); rwy != NULL;
	    rwy = AVL_NEXT(&arpt->rwys, rwy)) {
		if (air_runway_approach_arpt_rwy(arpt, rwy, 0, pos_v,
		    hdg, alt) ||
		    air_runway_approach_arpt_rwy(arpt, rwy, 1, pos_v,
		    hdg, alt) ||
		    point_in_poly(pos_v, rwy->rwy_bbox))
			in_apch_bbox++;
	}

	return (in_apch_bbox);
}

static void
air_runway_approach(void)
{
	if (adc->rad_alt < RADALT_GRD_THRESH)
		return;

	unsigned in_apch_bbox = 0;
	double clb_rate = conv_per_min(MET2FEET(adc->elev - state.last_elev));

	for (const airport_t *arpt = list_head(state.cur_arpts); arpt != NULL;
	    arpt = list_next(state.cur_arpts, arpt))
		in_apch_bbox += air_runway_approach_arpt(arpt);

	/*
	 * If we are neither over an approach bbox nor a runway, and we're
	 * not climbing and we're in a landing configuration, we're most
	 * likely trying to land onto something that's not a runway.
	 */
	if (in_apch_bbox == 0 && clb_rate < 0 && !gear_is_up() &&
	    flaps_chk(B_FALSE)) {
		if (adc->rad_alt <= OFF_RWY_HEIGHT_MAX) {
			/* only annunciate if we're above the minimum height */
			if (adc->rad_alt >= OFF_RWY_HEIGHT_MIN &&
			    !state.off_rwy_ann && !gpws_terr_ovrd() &&
			    state.config.monitors[TWY_LAND_MON]) {
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
	if (in_apch_bbox == 0) {
		state.air_apch_short_rwy_ann = B_FALSE;
		state.unstable_ann = B_FALSE;
	}
	if (in_apch_bbox <= 1 && state.air_apch_rwys_ann) {
		rwy_key_tbl_empty(&state.air_apch_rwy_ann);
		state.air_apch_rwys_ann = B_FALSE;
	}
}

const airport_t *
find_nearest_curarpt(void)
{
	double min_dist = ARPT_LOAD_LIMIT;
	vect3_t pos_ecef = sph2ecef(GEO_POS3(adc->lat, adc->lon, adc->elev));
	const airport_t *cur_arpt = NULL;

	if (state.cur_arpts == NULL)
		return (NULL);

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
guess_TATL_from_airport(int *TA, int *TL, bool_t *field_changed)
{
	const airport_t *cur_arpt = find_nearest_curarpt();

	if (cur_arpt != NULL) {
		const char *arpt_id = cur_arpt->icao;
		dbg_log(altimeter, 2, "find_nearest_curarpt() = %s", arpt_id);
		*TA = cur_arpt->TA;
		*TL = cur_arpt->TL;
		state.TATL_field_elev = cur_arpt->refpt.elev;
		if (strcmp(arpt_id, state.TATL_source) != 0) {
			strlcpy(state.TATL_source, arpt_id,
			    sizeof (state.TATL_source));
			*field_changed = B_TRUE;
			dbg_log(altimeter, 1, "TATL_source: %s "
			    "TA: %d TL: %d field_elev: %d", arpt_id,
			    *TA, *TL, state.TATL_field_elev);
		}
	} else {
		float lat = adc->lat, lon = adc->lon;
		XPLMNavRef arpt_ref = XPLMFindNavAid(NULL, NULL, &lat, &lon,
		    NULL, xplm_Nav_Airport);
		char outID[256] = { 0 };
		float outLat, outLon;
		vect3_t pos_ecef = NULL_VECT3, arpt_ecef = NULL_VECT3;

		dbg_log(altimeter, 2, "XPLMFindNavAid() = %d", arpt_ref);
		if (arpt_ref != 0) {
			XPLMGetNavAidInfo(arpt_ref, NULL, &outLat, &outLon,
			    NULL, NULL, NULL, outID, NULL, NULL);
			arpt_ecef = sph2ecef(GEO_POS3(outLat, outLon, 0));
			pos_ecef = sph2ecef(GEO_POS3(adc->lat, adc->lon, 0));
		}

		if (!IS_NULL_VECT(arpt_ecef) &&
		    strcmp(state.TATL_source, outID) != 0 &&
		    vect3_abs(vect3_sub(pos_ecef, arpt_ecef)) <
		    TATL_REMOTE_ARPT_DIST_LIMIT) {
			geo_pos2_t p = GEO_POS2(outLat, outLon);

			cur_arpt = airport_lookup(&state.airportdb, outID, p);
			if (cur_arpt == NULL || (cur_arpt->TA == 0 &&
			    cur_arpt->TL == 0))
				cur_arpt = matching_airport_in_tile_with_TATL(
				    &state.airportdb, p, outID);
			if (cur_arpt != NULL) {
				dbg_log(altimeter, 2, "fallback airport = %s",
				    cur_arpt->icao);
			}
		}

		if (cur_arpt != NULL) {
			*TA = cur_arpt->TA;
			*TL = cur_arpt->TL;
			state.TATL_field_elev = cur_arpt->refpt.elev;
			strlcpy(state.TATL_source, cur_arpt->icao,
			    sizeof (state.TATL_source));
			*field_changed = B_TRUE;
			dbg_log(altimeter, 1, "TATL_source: %s "
			    "TA: %d TA: %d field_elev: %d", cur_arpt->icao,
			    *TA, *TL, state.TATL_field_elev);
		}
	}

}

/*
 * Altimeter setting check processing. We establish the current TA & TL
 * values and compare it with our barometric altimeter reading, GPS elevation
 * and try to determine if the aircraft should be in the ALT or FL regime.
 * Depending on the regime in effect, we call "ALTIMETER SETTING" either
 * when the baro subscale setting isn't 1013 (FL regime) or when the actual
 * altimeter reading differs from true GPS elevation by a certain margin
 * (ALT regime).
 */
static void
altimeter_setting(void)
{
	int TA = 0, TL = 0;
	int elev = MET2FEET(adc->elev), baro_alt = adc->baro_alt;
	int64_t now;
	bool_t field_changed = B_FALSE;

	/* Don't do anything if radalt indicates we're on the ground */
	if (adc->rad_alt < RADALT_GRD_THRESH)
		return;

	now = microclock();

	/*
	 * We use two approaches for guessing at the TA & TL:
	 * 1) We can receive the values directly from the FMS via the ADC
	 *	bridge. This is the preferred method.
	 * 2) We can pull the values from the navdb. Since we do not know
	 *	the exact departure & destination aerodromes, this approach
	 *	takes a bit more guessing.
	 */
	if (adc->trans_alt != 0 || adc->trans_lvl != 0) {
		TA = adc->trans_alt;
		TL = adc->trans_lvl;
	} else {
		guess_TATL_from_airport(&TA, &TL, &field_changed);
	}

	/*
	 * In the above case when TA & TL were auto-determined from the
	 * navdb, sometimes the TA or TL aren't published. Provided we
	 * have at least one value, we can synthesize the other parameter.
	 * 1) If we have TA but not TL, we compute a TL that is equal in
	 *	absolute vertical position to the TA value at the current
	 *	sea-level pressure (from adc->baro_sl). Obviously this is
	 *	a bit of a cheat that we could only do in a simulator.
	 * 2) If we have TL but not TA, we can't really tell where the
	 *	actual TA is supposed to go. Therefore we simply set it
	 *	to the same value as the published TL to avoid excessive
	 *	TATL state transitions.
	 */
	if (TL == 0 && TA != 0) {
		if (adc->baro_sl > STD_BARO_REF) {
			TL = TA;
		} else {
			double qnh = adc->baro_sl * 33.85;
			TL = TA + 28 * (1013 - qnh);
		}
		if (field_changed)
			dbg_log(altimeter, 1, "TL(auto) = %d", TL);
	}
	if (TA == 0) {
		if (field_changed)
			dbg_log(altimeter, 1, "TA(auto) = %d", TA);
		TA = TL;
	}

	/*
	 * If we have a TA and our present baro altitude is above it,
	 * and we were transition.
	 */
	if (TA != 0 && baro_alt > TA && state.TATL_state == TATL_STATE_ALT) {
		state.TATL_transition = microclock();
		state.TATL_state = TATL_STATE_FL;
		dbg_log(altimeter, 1, "baro_alt (%d) > TA (%d) transitioning "
		    "state.TATL_state = fl", baro_alt, TA);
	}

	if (TL != 0 && baro_alt < TL && state.TATL_state == TATL_STATE_FL) {
		state.TATL_transition = microclock();
		state.TATL_state = TATL_STATE_ALT;
		dbg_log(altimeter, 1, "baro_alt (%d) < TL (%d) "
		    "transitioning state.TATL_state = alt", baro_alt, TL);
	}

	if (state.TATL_transition != -1) {
		if (/* We have transitioned into ALT mode */
		    state.TATL_state == TATL_STATE_ALT &&
		    /* The fixed timeout has passed, OR */
		    (now - state.TATL_transition >
		    SEC2USEC(ALTM_SETTING_TIMEOUT) ||
		    /*
		     * The field has a known elevation and we are within
		     * 1500 feet of it
		     */
		    (state.TATL_field_elev != TATL_FIELD_ELEV_UNSET &&
		    (elev < state.TATL_field_elev +
		    ALTM_SETTING_ALT_CHK_LIMIT)))) {
			double d_qnh = 0, d_qfe = 0;

			if (state.config.monitors[ALTM_QNH_MON])
				d_qnh = fabs(elev - adc->baro_alt);
			if (state.TATL_field_elev != TATL_FIELD_ELEV_UNSET &&
			    state.config.monitors[ALTM_QFE_MON])
				d_qfe = fabs(adc->baro_alt -
				    (elev - state.TATL_field_elev));
			dbg_log(altimeter, 1, "alt check; d_qnh: %.1f "
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
		    now - state.TATL_transition >
		    SEC2USEC(ALTM_SETTING_TIMEOUT)) {
			double d_ref = fabs(adc->baro_set - STD_BARO_REF);
			dbg_log(altimeter, 1, "fl check; d_ref: %.03f", d_ref);
			if (d_ref > ALTM_SETTING_BARO_ERR_LIMIT &&
			    state.config.monitors[ALTM_QNE_MON]) {
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
 * Returns true if X-RAAS has electrical power from the aircraft.
 */
bool_t
xraas_is_on(void)
{
	float bus_volts[2];
	bool_t turned_on;

	if (ff_a320_is_loaded() && !GPWS_is_inop())
		return (ff_a320_powered());

	XPLMGetDatavf(drs->bus_volt, bus_volts, 0, 2);

	turned_on = ((bus_volts[0] > MIN_BUS_VOLT ||
	    bus_volts[1] > MIN_BUS_VOLT) &&
	    XPLMGetDatai(drs->avionics_on) == 1 &&
	    !GPWS_is_inop());

	return ((turned_on || state.config.override_electrical) &&
	    (XPLMGetDatai(drs->replay_mode) == 0 ||
	    state.config.override_replay));
}

static void
raas_exec(void)
{
	dbg_log(pwr_state, 3, "raas_exec");

	/*
	 * Ahead of the enabling check so that we can provide sensible runway
	 * info in the embedded FF A320 case.
	 */
	state.input_faulted = !adc_collect();
	if (state.input_faulted) {
		dbg_log(pwr_state, 1, "input_fault = true");
		return;
	}
	load_nearest_airports();

#ifdef	XRAAS_IS_EMBEDDED
	if (plugin_conflict) {
		/*
		 * There appears to be a stand-alone version of the plugin
		 * installed. The embedded versions tend to be more tightly
		 * bound to their aircraft model, so we want to disable a
		 * global stand-alone version in this case.
		 */
		XPLMDataRef dr_inop = XPLMFindDataRef(
		    "xraas/override/GPWS_inop");
		XPLMDataRef dr_inop_act = XPLMFindDataRef(
		    "xraas/override/GPWS_inop_act");
		if (dr_inop != NULL && dr_inop_act != NULL) {
			XPLMSetDatai(dr_inop, 1);
			XPLMSetDatai(dr_inop_act, 1);
		} else {
			logMsg("CAUTION: there appears to be a global X-RAAS "
			    "installation in this simulator, but I seem to be "
			    "unable to override it. Please disable the global "
			    "X-RAAS plugin or else the RAAS functionality "
			    "won't work reliably.");
			return;
		}
	}
#endif	/* XRAAS_IS_EMBEDDED */


#ifdef	XRAAS_IS_EMBEDDED
	if (!state.config.enabled)
		return;
#else	/* !XRAAS_IS_EMBEDDED */
	VERIFY(state.config.enabled);
#endif	/* !XRAAS_IS_EMBEDDED */

	if (!xraas_is_on()) {
		dbg_log(pwr_state, 1, "is_on = false");
		return;
	}

	if (adc->rad_alt > RADALT_FLARE_THRESH) {
		if (!state.departed) {
			state.departed = B_TRUE;
			dbg_log(flt_state, 1, "state.departed = true");
		}
		if (!state.arriving) {
			state.arriving = B_TRUE;
			dbg_log(flt_state, 1, "state.arriving = true");
		}
		if (state.long_landing_ann) {
			dbg_log(ann_state, 1,
			    "state.long_landing_ann = false");
			state.long_landing_ann = B_FALSE;
		}
	} else if (adc->rad_alt < RADALT_GRD_THRESH) {
		if (state.departed) {
			dbg_log(flt_state, 1, "state.landing = true");
			dbg_log(flt_state, 1, "state.departed = false");
			state.landing = B_TRUE;
		}
		state.departed = B_FALSE;
		if (adc->gs < SPEED_THRESH)
			state.arriving = B_FALSE;
	}
	if (adc->gs < SPEED_THRESH && state.long_landing_ann) {
		dbg_log(ann_state, 1, "state.long_landing_ann = false");
		state.long_landing_ann = B_FALSE;
	}

	ground_runway_approach();
	ground_on_runway_aligned();
	air_runway_approach();
	altimeter_setting();

	if (adc->rad_alt > RADALT_DEPART_THRESH) {
		for (int i = 0; !isnan(accel_stop_distances[i].min); i++)
			accel_stop_distances[i].ann = B_FALSE;
	}

	state.last_elev = adc->elev;
	state.last_gs = adc->gs;
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

#if	ACF_TYPE == NO_ACF_TYPE
/*
 * Check if the aircraft is a helicopter (or at least says it flies like one).
 * This isn't used on embedded type-specific builds, because there we *know*
 * about aircraft compatibility for certain.
 */
static bool_t
chk_acf_is_helo(void)
{
	FILE *fp = fopen(acf_path, "r");
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
}
#endif	/* ACF_TYPE == NO_ACF_TYPE */

static void
startup_complete(void)
{
#ifndef	XRAAS_IS_EMBEDDED
	log_init_msg(state.config.startup_notify, STARTUP_MSG_TIMEOUT, NULL,
	    NULL, "X-RAAS(%s): Runway Awareness OK; %s.", XRAAS2_VERSION,
	    state.config.use_imperial ? "Feet" : "Meters");
#endif	/* !XRAAS_IS_EMBEDDED */
}

void
xraas_init(void)
{
	bool_t airportdb_created = B_FALSE;
	char *sep;
	char livpath[1024];
	char *cachedir;

	ASSERT(!xraas_inited);

	dbg_log(startup, 1, "xraas_init");

	/* these must go ahead of config parsing */
	XPLMGetNthAircraftModel(0, acf_filename, acf_path);
	if (strlen(acf_filename) == 0)
		/* no aircraft loaded yet */
		return;
#if	IBM
	fix_pathsep(acf_filename);
	fix_pathsep(acf_path);
#endif
	if ((sep = strrchr(acf_path, DIRSEP)) != NULL) {
		memset(acf_dirpath, 0, sizeof (acf_dirpath));
		memcpy(acf_dirpath, acf_path, sep - acf_path);
	} else {
		/* aircraft's dirpath is unknown */
		logMsg("WARNING: can't determine your aircraft's directory "
		    "path based on its .acf file path (%s). Some functions "
		    "of X-RAAS will be limited (e.g. aircraft-specific "
		    "configuration loading).", acf_path);
		acf_dirpath[0] = 0;
	}

	memset(livpath, 0, sizeof (livpath));
	XPLMGetDatab(acf_livpath_dr, livpath, 0, sizeof (livpath));
#if	IBM
	fix_pathsep(livpath);
#endif
	snprintf(acf_livpath, sizeof (acf_livpath), "%s%c%s", xpdir,
	    DIRSEP, livpath);

	if (!load_configs(&state))
		return;

	if (!state.config.enabled) {
		logMsg("X-RAAS: DISABLED");
		/*
		 * When running in embedded mode, we never really get disabled.
		 * Instead, we keep on running the flight loop, but only the
		 * nearest airport fetch. This allows us to provide the host
		 * avionics with the nearest runway location information (e.g.
		 * the FF A320 needs this).
		 */
#ifndef	XRAAS_IS_EMBEDDED
		return;
#endif
	}

	if (!snd_sys_init(plugindir) || !ND_alerts_init() || !adc_init())
		goto errout;

	if (state.config.debug_graphical)
		dbg_gui_init();

#ifdef	XRAAS_IS_EMBEDDED
	cachedir = mkpathname(xraas_plugindir, XRAAS_CACHE_DIR, NULL);
#else	/* !XRAAS_IS_EMBEDDED */
	cachedir = mkpathname(xpdir, "Output", "caches", XRAAS_CACHE_DIR,
	    NULL);
#endif	/* !XRAAS_IS_EMBEDDED */
	airportdb_create(&state.airportdb, xpdir, cachedir);
	airportdb_created = B_TRUE;
	free(cachedir);

	if (!recreate_cache(&state.airportdb))
		goto errout;

#if	ACF_TYPE == NO_ACF_TYPE
	/* Type-specific builds aren't bound by these */

	if (chk_acf_is_helo() && !state.config.allow_helos) {
		log_init_msg(state.config.auto_disable_notify,
		    INIT_ERR_MSG_TIMEOUT,
		    "3", "Activating X-RAAS in the aircraft",
		    "X-RAAS: auto-disabled: aircraft is a helicopter.");
		goto errout;
	}
	if (XPLMGetDatai(drs->num_engines) < state.config.min_engines ||
	    XPLMGetDataf(drs->mtow) < state.config.min_mtow) {
		char icao[8];
		memset(icao, 0, sizeof (icao));
		XPLMGetDatab(drs->ICAO, icao, 0, sizeof (icao) - 1);
		log_init_msg(state.config.auto_disable_notify,
		    INIT_ERR_MSG_TIMEOUT,
		    "3", "Activating X-RAAS in the aircraft",
		    "X-RAAS: auto-disabled: aircraft below X-RAAS limits:\n"
		    "X-RAAS configuration: minimum number of engines: %d; "
		    "minimum MTOW: %d kg\n"
		    "Your aircraft: (%s) number of engines: %d; "
		    "MTOW: %.0f kg", state.config.min_engines,
		    state.config.min_mtow, icao, XPLMGetDatai(drs->num_engines),
		    XPLMGetDataf(drs->mtow));
		goto errout;
	}
#endif	/* ACF_TYPE == NO_ACF_TYPE */

	rwy_key_tbl_create(&state.accel_stop_max_spd, "accel_stop_max_spd");
	rwy_key_tbl_create(&state.on_rwy_ann, "on_rwy_ann");
	rwy_key_tbl_create(&state.apch_rwy_ann, "apch_rwy_ann");
	rwy_key_tbl_create(&state.air_apch_rwy_ann, "air_apch_rwy_ann");
	rwy_key_tbl_create(&state.air_apch_flap1_ann, "air_apch_flap1_ann");
	rwy_key_tbl_create(&state.air_apch_flap2_ann, "air_apch_flap2_ann");
	rwy_key_tbl_create(&state.air_apch_flap3_ann, "air_apch_flap3_ann");
	rwy_key_tbl_create(&state.air_apch_gpa1_ann, "air_apch_gpa1_ann");
	rwy_key_tbl_create(&state.air_apch_gpa2_ann, "air_apch_gpa2_ann");
	rwy_key_tbl_create(&state.air_apch_gpa3_ann, "air_apch_gpa3_ann");
	rwy_key_tbl_create(&state.air_apch_spd1_ann, "air_apch_spd1_ann");
	rwy_key_tbl_create(&state.air_apch_spd2_ann, "air_apch_spd2_ann");
	rwy_key_tbl_create(&state.air_apch_spd3_ann, "air_apch_spd3_ann");

	XPLMRegisterFlightLoopCallback(raas_exec_cb, EXEC_INTVAL, NULL);
	dr_create_i(&input_faulted_dr, (int *)&state.input_faulted, B_FALSE,
	    "xraas/state/input_faulted");

	xraas_inited = B_TRUE;
	startup_complete();

	return;

errout:
	snd_sys_fini();
	dbg_gui_fini();
	if (airportdb_created)
		airportdb_destroy(&state.airportdb);
	ND_alerts_fini();
}

void
xraas_fini(void)
{
	if (!xraas_inited)
		return;

#ifndef	XRAAS_IS_EMBEDDED
	if (!state.config.enabled)
		return;
#endif	/* XRAAS_IS_EMBEDDED */

	dbg_log(startup, 1, "xraas_fini");

	snd_sys_fini();

	if (state.cur_arpts != NULL) {
		free_nearest_airport_list(state.cur_arpts);
		state.cur_arpts = NULL;
	}

	airportdb_destroy(&state.airportdb);

	ND_alerts_fini();
	adc_fini();

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

	XPLMUnregisterFlightLoopCallback(raas_exec_cb, NULL);

	if (state.config.debug_graphical)
		dbg_gui_fini();

	dr_delete(&input_faulted_dr);

	xraas_inited = B_FALSE;
}

PLUGIN_API int
XPluginStart(char *outName, char *outSig, char *outDesc)
{
	char *p;

	log_init(XPLMDebugString, XRAAS2_PLUGIN_NAME);

	/* Always use Unix-native paths on the Mac! */
	XPLMEnableFeature("XPLM_USE_NATIVE_PATHS", 1);

	XPLMGetSystemPath(xpdir);
	XPLMGetPrefsPath(prefsdir);
	XPLMGetPluginInfo(XPLMGetMyID(), NULL, plugindir, NULL, NULL);

#if	IBM
	/*
	 * For some absolutely inexplicable reason, X-Plane returns these
	 * paths as a hybrid mix of '/' and '\' separators on Windows. So
	 * before we start cutting path names, we'll flip all '/'s into
	 * '\'s to keep consistent with the native path separator scheme.
	 */
	fix_pathsep(xpdir);
	fix_pathsep(prefsdir);
	fix_pathsep(plugindir);
#endif	/* IBM */

	strcpy(outName, XRAAS2_PLUGIN_NAME);
	strcpy(outSig, XRAAS2_PLUGIN_SIG);
	strcpy(outDesc, XRAAS2_PLUGIN_DESC);

	/* Inhibit startup if another X-RAAS instance was found. */
	if (XPLMFindPluginBySignature(XRAAS2_STANDALONE_PLUGIN_SIG) !=
	    XPLM_NO_PLUGIN_ID) {
		plugin_conflict = B_TRUE;
#ifndef	XRAAS_IS_EMBEDDED
		/*
		 * This is a rather unexpected situation if we are stand-alone
		 * and indicates an installation problem. Warn the user.
		 */
		logMsg("CAUTION: it seems your simulator is loading X-RAAS "
		    "twice. Please check your installation to make sure "
		    "there aren't any duplicate copies of the plugin!");
		return (0);
#endif	/* !XRAAS_IS_EMBEDDED */
	}

	if (strlen(xpdir) > 0 && xpdir[strlen(xpdir) - 1] == DIRSEP) {
		/*
		 * Strip the last path separator from the end to avoid
		 * double '//' in paths generated with mkpathname. Not
		 * necessary, just a niceness thing.
		 */
		xpdir[strlen(xpdir) - 1] = '\0';
	}

	/* cut off X-Plane.prf from prefs path */
	if ((p = strrchr(prefsdir, DIRSEP)) != NULL)
		*p = '\0';

	/* cut off the trailing path component (our filename) */
	if ((p = strrchr(plugindir, DIRSEP)) != NULL)
		*p = '\0';
	/* cut off an optional '64' trailing component */
	if ((p = strrchr(plugindir, DIRSEP)) != NULL) {
		if (strcmp(p + 1, "64") == 0)
			*p = '\0';
	}

	/*
	 * This has to go very early in the initialization stage since the
	 * rest of X-RAAS depends on log_init_msg being available, but it
	 * needs to go after system path determination, since we need to
	 * load fonts from our plugin directory here.
	 */
	if (!init_msg_sys_init())
		return (0);

#ifdef	XRAAS_IS_EMBEDDED
	p = &plugindir[strlen(xpdir)];
	if (strlen(plugindir) > strlen(xpdir) &&
	    (strstr(p, DIRSEP_S "Resources" DIRSEP_S "plugins") == p ||
	    strstr(p, DIRSEP_S "resources" DIRSEP_S "plugins") == p)) {
		/*
		 * This means somebody has installed the embeddable version
		 * into the global plugins directory. Warn the user.
		 */
		log_init_msg(B_TRUE, 10 * INIT_ERR_MSG_TIMEOUT, NULL, NULL,
		    "X-RAAS(%s) CAUTION: it seems you have installed an "
		    "embeddable version of X-RAAS into X-Plane's global "
		    "Resources" DIRSEP_S "plugins folder.\n"
		    "The embeddable version is meant to be embedded into an "
		    "aircraft model by its developer.\n"
		    "If you would like to use X-RAAS as a stand-alone plugin, "
		    "download the stand-alone version.\n"
		    "If you are an aircraft developer, please move the "
		    "X-RAAS plugin into your aircraft's \"plugins\" folder.",
		    XRAAS2_VERSION);
		return (0);
	}
#endif	/* XRAAS_IS_EMBEDDED */

	acf_livpath_dr = XPLMFindDataRef("sim/aircraft/view/acf_livery_path");
	VERIFY(acf_livpath_dr != NULL);

	/*
	 * Override initialization has to happen during startup to allow an
	 * aircraft-specific plugin to override us in its load routine.
	 */
	overrides_init();

	return (1);
}

PLUGIN_API void
XPluginStop(void)
{
	overrides_fini();
	init_msg_sys_fini();
}

PLUGIN_API int
XPluginEnable(void)
{
	xraas_init();
	gui_init();
	return (1);
}

PLUGIN_API void
XPluginDisable(void)
{
	gui_fini();
	xraas_fini();
}

PLUGIN_API void
XPluginReceiveMessage(XPLMPluginID src, int msg, void *param)
{
	UNUSED(src);
	UNUSED(param);

	switch (msg) {
	case XPLM_MSG_LIVERY_LOADED:
	case XPLM_MSG_AIRPORT_LOADED:
		xraas_fini();
		xraas_init();
		gui_update();
		break;
	case XPLM_MSG_PLANE_UNLOADED:
		/* only respond to user aircraft unloads */
		if (param != 0)
			break;
		/*
		 * Reenable the ND alert overlay in case a previously loaded
		 * aircraft asked us to disable it, but the new aircraft
		 * doesn't want to disable it (or isn't even aware of us).
		 */
		ND_alert_overlay_enable();
		break;
	}
}
