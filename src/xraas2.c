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

#include <math.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#if	IBM
#include <windows.h>
#else	/* !IBM */
#include <sys/time.h>
#endif	/* !IBM */

#include <XPLMDataAccess.h>

#include "types.h"
#include "helpers.h"
#include "log.h"
#include "geom.h"

#define XRAAS_VERSION			"2.0"
#define	EXEC_INTVAL			0.5		/* seconds */
#define	HDG_ALIGN_THRESH		25		/* degrees */

#define	SPEED_THRESH			20.5		/* m/s, 40 knots */
#define	HIGH_SPEED_THRESH		30.9		/* m/s, 60 knots */
#define	SLOW_ROLL_THRESH		5.15		/* m/s, 10 knots */
#define	STOPPED_THRESH			2.06		/* m/s, 4 knots */

#define	RWY_PROXIMITY_LAT_FRACT		3
#define	RWY_PROXIMITY_LON_DISP		609.57		/* meters, 2000 ft */
#define	RWY_PROXIMITY_TIME_FACT		2		/* seconds */
#define	LANDING_ROLLOUT_TIME_FACT	1		/* seconds */
#define	RADALT_GRD_THRESH		5		/* feet */
#define	RADALT_FLARE_THRESH		100		/* feet */
#define	RADALT_DEPART_THRESH		100		/* feet */
#define	STARTUP_DELAY			3		/* seconds */
#define	STARTUP_MSG_TIMEOUT		4		/* seconds */
#define	INIT_ERR_MSG_TIMEOUT		25		/* seconds */
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

#define	RWY_APCH_PROXIMITY_LAT_ANGLE	3.3	/* degrees */
#define	RWY_APCH_PROXIMITY_LON_DISPL	5500	/* meters */
/* precomputed, since it doesn't change */
#define	RWY_APCH_PROXIMITY_LAT_DISPL \
    RWY_APCH_PROXIMITY_LON_DISPL * \
    tan(DEG2RAD(RWY_APCH_PROXIMITY_LAT_ANGLE))
#define	RWY_APCH_FLAP1_THRESH		950	/* feet */
#define	RWY_APCH_FLAP2_THRESH		600	/* feet */
#define	RWY_APCH_FLAP3_THRESH		450	/* feet */
#define	RWY_APCH_FLAP4_THRESH		300	/* feet */
#define	ARPT_APCH_BLW_ELEV_THRESH	500	/* feet */
#define	RWY_APCH_ALT_MAX		700	/* feet */
#define	RWY_APCH_ALT_MIN		320	/* feet */
#define	SHORT_RWY_APCH_ALT_MAX		390	/* feet */
#define	SHORT_RWY_APCH_ALT_MIN		320	/* feet */
#define	RWY_APCH_SUPP_WINDOWS {			/* Suppress 'approaching' */ \
    {["max"] = 530, ["min"] = 480},		/* annunciations in these */ \
    {["max"] = 430, ["min"] = 380},		/* altitude windows (based */ \
}						/* on radio altitude). */
#define	TATL_REMOTE_ARPT_DIST_LIMIT	500000	/* meters */
#define	MIN_BUS_VOLT			11	/* Volts */
#define	BUS_LOAD_AMPS			2	/* Amps */
#define	XRAAS_apt_dat_cache_version	3
#define	UNITS_APPEND_INTVAL		120	/* seconds */

typedef struct {
	double min, max;
} range_t;

/*
 * Because we examine these ranges in 0.5 second intervals, there is
 * a maximum speed at which we are guaranteed to announce the distance
 * remaining. The ranges are configured so as to allow for a healthy
 * maximum speed margin over anything that could be reasonably attained
 * over that portion of the runway.
 */
static range_t RAAS_accel_stop_distances[] = {
    { .max = 2807, .min = 2743 },	/* 9200-9000 ft, 250 KT */
    { .max = 2503, .min = 2439 },	/* 8200-8000 ft, 250 KT */
    { .max = 2198, .min = 2134 },	/* 7200-7000 ft, 250 KT */
    { .max = 1892, .min = 1828 },	/* 6200-6000 ft, 250 KT */
    { .max = 1588, .min = 1524 },	/* 5200-5000 ft, 250 KT */
    { .max = 1284, .min = 1220 },	/* 4200-4000 ft, 250 KT */
    { .max = 1036, .min = 915 },	/* 3200-3000 ft, 250 KT */
    { .max = 674, .min = 610 },		/* 2200-2000 ft, 250 KT */
    { .max = 369, .min = 305 },		/* 1200-1000 ft, 250 KT */
    { .max = 185, .min = 153 },		/* 600-500 ft, 125 KT */
    { .max = 46, .min = 31 },		/* 150-100 ft, 60 KT */
    { .max = NAN, .min = NAN }		/* list terminator */
};

/*
 * This is what we set our ND alert dataref to when we want to communicate
 * to the aircraft's FMS that it should display a message on the ND. Value
 * '0' is reserved for 'nothing'.
 *
 * Since the dataref is an int and we need to annunciate various messages,
 * we split this int into several bitfields:
 * bits 0 - 7  (8 bits):	message ID
 * bits 8 - 13 (6 bits):	numeric runway ID:
 *				'00' means 'taxiway'
 *				'01' through '36' means a runway ID
 *				'37' means 'RWYS' (i.e. multiple runways)
 * bits 14 - 15 (2 bits):	'0' means 'no suffix'
 *				'1' means 'RIGHT'
 *				'2' means 'LEFT'
 *				'3' means 'CENTER'
 * bits 16 - 23 (8 bits):	Runway length available to the nearest 100
 *				feet or meters. '0' means 'do not display'.
 * Bits 8 through 23 are only used by the ND_ALERT_APP and ND_ALERT_ON messages
 */
enum nd_alert_msg_type {
    ND_ALERT_FLAPS = 1,		/* 'FLAPS' message on ND */
    ND_ALERT_TOO_HIGH = 2,	/* 'TOO HIGH' message on ND */
    ND_ALERT_TOO_FAST = 3,	/* 'TOO FAST' message on ND */
    ND_ALERT_UNSTABLE = 4,	/* 'UNSTABLE' message on ND */
    ND_ALERT_TWY = 5,		/* 'TAXIWAY' message on ND */
    ND_ALERT_SHORT_RWY = 6,	/* 'SHORT RUNWAY' message on ND */
    ND_ALERT_ALTM_SETTING = 7,	/* 'ALTM SETTING' message on ND */
    /* These two messages encode what we're apch/on in bits 8 through 15 */
    ND_ALERT_APP = 8,		/* 'APP XX' or 'APP XX ZZ' messages. */
				/* 'XX' means runway ID (bits 8 - 15). */
				/* 'ZZ' means runway length (bits 16 - 23). */
    ND_ALERT_ON = 9,		/* 'ON XX' or 'ON XX ZZ' messages. */
				/* 'XX' means runway ID (bits 8 - 15). */
				/* 'ZZ' means runway length (bits 16 - 23). */
    ND_ALERT_LONG_LAND = 10
};

/* ND alert severity */
enum nd_alert_level {
    ND_ALERT_ROUTINE = 0,
    ND_ALERT_NONROUTINE = 1,
    ND_ALERT_CAUTION = 2
};

enum msg_prio {
    MSG_PRIO_LOW = 1,
    MSG_PRIO_MED = 2,
    MSG_PRIO_HIGH = 3
};

typedef struct airport airport_t;
typedef struct runway runway_t;
typedef struct runway_end runway_end_t;

struct runway_end {
	char id[4];			/* runway ID, nul-terminated */
	geo_pos3_t thrpos;		/* threshold position */
	double displ;			/* threshold displacement in meters */
	double blast;			/* stopway/blastpad length in meters */
	double gpa;			/* glidepath angle in degrees */
	double tch;			/* threshold clearing height in feet */
};

struct runway {
	airport_t	*arpt;
	double		width;
	runway_end_t	ends[2];
	list_node_t	node;
};

struct airport {
	const char	icao[5];	/* 4-letter ID, nul terminated */
	geo_pos3_t	refpt;		/* airport reference point location */
	double		TA;		/* transition altitude in feet */
	double		TL;		/* transition level in feet */
	list_t		rwys;
	list_node_t	tile_node;
};

static bool_t enabled = B_TRUE;
static int min_engines = 2;		/* count */
static int min_MTOW = 5700;			/* kg */
static bool_t allow_helos = B_FALSE;
static bool_t auto_disable_notify = B_TRUE;
static bool_t startup_notify = B_TRUE;
static bool_t override_electrical = B_FALSE;
static bool_t override_replay = B_FALSE;
static bool_t speak_units = B_TRUE;
static bool_t use_TTS = B_FALSE;

static bool_t use_imperial = B_TRUE;
static int min_takeoff_dist = 1000;		/* meters */
static int min_landing_dist = 800;			/* meters */
static int min_rotation_dist = 400;		/* meters */
static int min_rotation_angle = 3;			/* degrees */
static int stop_dist_cutoff = 1500;		/* meters */
static int voice_female = B_TRUE;
static double voice_volume = 1.0;
static int disable_ext_view = B_TRUE;
static double min_landing_flap = 0.5;		/* ratio, 0-1 */
static double min_takeoff_flap = 0.1;		/* ratio, 0-1 */
static double max_takeoff_flap = 0.75;		/* ratio, 0-1 */

static bool_t ND_alerts_enabled = B_TRUE;
enum nd_alert_level ND_alert_filter = ND_ALERT_ROUTINE;
static bool_t ND_alert_overlay_enabled = B_TRUE;
static bool_t ND_alert_overlay_force = B_FALSE;
static int ND_alert_timeout = 7;		/* seconds */

static int on_rwy_warn_initial = 60;		/* seconds */
static int on_rwy_warn_repeat = 120;		/* seconds */
static int on_rwy_warn_max_n = 3;

static bool_t too_high_enabled = B_TRUE;
static bool_t too_fast_enabled = B_TRUE;
static double gpa_limit_mult = 2;		/* multiplier */
static double gpa_limit_max = 8;		/* degrees */

static const char *GPWS_priority_dataref = "sim/cockpit2/annunciators/GPWS";
static const char *GPWS_inop_dataref = "sim/cockpit/warnings/annunciators/GPWS";

static bool_t alt_setting_enabled = B_TRUE;
static bool_t qnh_alt_enabled = B_TRUE;
static bool_t qfe_alt_enabled = B_FALSE;

static bool_t US_runway_numbers = B_FALSE;

static int long_land_lim_abs = 610;		/* meters, 2000 feet */
static double long_land_lim_fract = 0.25;	/* fraction, 0-1 */

static bool_t debug_graphical = B_FALSE;
static int debug_graphical_bg = 0;

static bool_t departed = B_FALSE;
static bool_t arriving = B_FALSE;
static bool_t landing = B_FALSE;

enum TATL_state_e {
	TATL_STATE_ALT,
	TATL_STATE_FL
};

static int TA = 0;
static int TL = 0;
static int TATL_field_elev = 0;
static enum TATL_state_e TATL_state = TATL_STATE_ALT;
static int TATL_transition = -1;
static int TATL_source = 0;

static bool_t view_is_ext = B_FALSE;
static int bus_loaded = -1;
static int last_elev = 0;
static double last_gs = 0;			/* in m/s */
static uint64_t last_units_call = 0;

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
	XPLMDataRef ext_view;
	XPLMDataRef bus_volt;
	XPLMDataRef avionics_on;
	XPLMDataRef num_engines;
	XPLMDataRef mtow;
	XPLMDataRef ICAO;
	XPLMDataRef gpws_prio;
	XPLMDataRef gpws_inop;
	XPLMDataRef ND_alert;
	XPLMDataRef replay_mode;
	XPLMDataRef plug_bus_load;
} drs;

static char **
strsplit(const char *input, char *sep, bool_t skip_empty, size_t *num)
{
	char **result;
	size_t i = 0, n = 0;
	size_t seplen = strlen(sep);

	for (const char *a = input, *b = strstr(a, sep);
	    a != NULL; a = b + seplen, b = strstr(a, sep)) {
		if (b == NULL)
			b = input + strlen(input);
		if (a == b && skip_empty)
			continue;
		n++;
	}

	result = malloc(sizeof (char *) * n);

	for (const char *a = input, *b = strstr(a, sep);
	    a != NULL; a = b + seplen, b = strstr(a, sep)) {
		if (b == NULL)
			b = input + strlen(input);
		if (a == b && skip_empty)
			continue;
		ASSERT(i < n);
		result[i] = calloc(b - a + 1, sizeof (char));
		memcpy(result[i], a, b - a);
		i++;
	}

	*num = n;
	return (result);
}

/*
 * Returns true if `x' is within the numerical ranges in `rngs'.
 * The range check is inclusive.
 */
static bool_t
number_in_rngs(double x, const range_t *rngs)
{
	for (; rngs->max != NAN; rngs++) {
		if (x <= rngs->max && x >= rngs->min)
			return (B_TRUE);
	}
	return (B_FALSE);
}

static uint64_t
microclock(void)
{
#if	IBM
	LARGE_INTEGER val, freq;
	QueryPerformanceFrequency(&freq);
	QueryPerformanceCounter(&val);
	return ((val.QuadPart * 1000000llu) / freq.QuadPart);
#else	/* !IBM */
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return ((tv.tv_sec * 1000000llu) + tv.tv_usec);
#endif	/* !IBM */
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
	drs.ext_view = dr_get("sim/graphics/view/view_is_external");
	drs.bus_volt = dr_get("sim/cockpit2/electrical/bus_volts");
	drs.avionics_on = dr_get("sim/cockpit/electrical/avionics_on");
	drs.num_engines = dr_get("sim/aircraft/engine/acf_num_engines");
	drs.mtow = dr_get("sim/aircraft/weight/acf_m_max");
	drs.ICAO = dr_get("sim/aircraft/view/acf_ICAO");

/*	if RAAS_GPWS_priority_dataref ~= nil then
		drs.gpws_prio = dr_get(RAAS_GPWS_priority_dataref)
	else
		drs.gpws_prio = {[0] = 0}
	end
	if RAAS_GPWS_inop_dataref ~= nil then
		drs.gpws_inop = dr_get(RAAS_GPWS_inop_dataref)
	else
		drs.gpws_inop = {[0] = 0}
	end*/

//	drs.ND_alert = dr_get(
//	    "sim/multiplayer/position/plane19_taxi_light_on")

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
 * Returns true if landing gear is fully retracted, false otherwise.
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

static bool_t
GPWS_has_priority(void)
{
	return (drs.gpws_prio ? XPLMGetDatai(drs.gpws_prio) != 0 : B_FALSE);
}

/*
 * Given a runway ID, returns the reciprocal runway ID.
 */
static void
recip_rwy_id(const char *rwy_id, char out_rwy_id[4])
{
	int recip_num = atoi(rwy_id) + 18;

	if (recip_num > 36)
		recip_num -= 36;
	ASSERT(recip_num >= 1 && recip_num <= 36);

	if (strlen(rwy_id) > 2) {
		char recip_suffix;

		switch (rwy_id[2]) {
		case 'L':
			recip_suffix = 'R';
			break;
		case 'R':
			recip_suffix = 'L';
			break;
		default:
			ASSERT(rwy_id[2] == 'C');
			recip_suffix = rwy_id[2];
			break;
		}
		snprintf(out_rwy_id, 4, "%02d%c", recip_num, recip_suffix);
	} else {
		snprintf(out_rwy_id, 4, "%02d", recip_num);
	}
}

/*
 * Given a runway threshold vector, direction vector, width, length and
 * threshold longitudinal displacement, prepares a bounding box which
 * encompasses that runway.
 */
static vect2_t *
make_rwy_bbox(vect2_t thresh_v, vect2_t dir_v, double width, double len,
    double long_displ)
{
	vect2_t *bbox;
	vect2_t len_displ_v;

	bbox = malloc(sizeof (*bbox) * 5);

	/*
	 * Displace the 'a' point from the runway threshold laterally
	 * by 1/2 width to the right.
	 */
	bbox[0] = vect2_add(thresh_v, vect2_set_abs(vect2_norm(dir_v, B_TRUE),
	    width / 2));
	/* pull it back by `long_displ' */
	bbox[0] = vect2_add(bbox[0], vect2_set_abs(vect2_neg(dir_v),
	    long_displ));

	/* do the same for the `d' point, but displace to the left */
	bbox[3] = vect2_add(thresh_v, vect2_set_abs(vect2_norm(dir_v, B_FALSE),
	    width / 2));
	/* pull it back by `long_displ' */
	bbox[3] = vect2_add(bbox[3], vect2_set_abs(vect2_neg(dir_v),
	    long_displ));

	/*
	 * points `b' and `c' are along the runway simply as runway len +
	 * long_displ
	 */
	len_displ_v = vect2_set_abs(dir_v, len + long_displ);
	bbox[1] = vect2_add(bbox[0], len_displ_v);
	bbox[2] = vect2_add(bbox[3], len_displ_v);

	bbox[4] = NULL_VECT2;

	return (bbox);
}

/*
 * Checks if the numerical runway type `t' is a hard-surface runway. From
 * the X-Plane v850 apt.dat spec:
 *	t=1: asphalt
 *	t=2: concrete
 *	t=15: unspecified hard surface (transparent)
 */
static bool_t
rwy_is_hard(int t)
{
	return (t == 1 || t == 2 || t == 15);
}

/*
 * Parses an apt.dat (or X-RAAS_apt_dat.cache) file, parses its contents
 * and reconstructs our raas.apt_dat table. This is called at the start of
 * X-RAAS to populate the airport and runway database.
 */
static size_t
map_apt_dat(const char *apt_dat_fname)
{
	FILE *apt_dat_f;
	size_t arpt_cnt = 0;
	airport_t *arpt;
	runway_t *rwy;
	char *line = NULL;
	size_t linecap = 0;

	dbg_log("tile", 2, "raas.map_apt_dat(\"%s\")", apt_dat_fname);

	apt_dat_f = fopen(apt_dat_fname, "r");
	if (apt_dat_f == NULL)
		return (0);

	while (!feof(apt_dat_f)) {
		if (getline(&line, &linecap, apt_dat_f) <= 0)
			continue;
		if (strnstr(line, "1 ") == line) {
			size_t ncomps;
			char **comps = strsplit(line, " ", B_TRUE, &ncomps);
			const char *new_icao;
			double TA = 0, TL = 0;
			geo_pos3_t pos = NULL_GEO_POS3;

			if (ncomps < 5) {
				dbg_log("tile", 0, "apt.dat \"%s\" contains "
				    "an invalid '1' line, skipping. Offending "
				    "line follows:\n%s", apt_dat_fname, line);
				free(comps);
				continue;
			}

			new_icao = comps[4];
			pos.elev = atof(comps[1]);
			arpt = htbl_lookup(apt_dat, new_icao);
			if (ncomps >= 9 &&
			    strstr(comps[5], "TA:") == comps[5] &&
			    strstr(comps[6], "TL:") == comps[6] &&
			    strstr(comps[7], "LAT:") == comps[7] &&
			    strstr(comps[8], "LON:") == comps[8]) {
				TA = atof(&comps[5][3]);
				TL = atof(&comps[6][3]);
				pos.lat = atof(&comps[7][4]);
				pos.lon = atof(&comps[8][4]);
			} else if (arpt != NULL) {
				pos = arpt->pos;
			}

			if (arpt == NULL) {
				arpt_cnt++;
				arpt = calloc(1, sizeof (*arpt));
				list_create(&arpt->rwys, sizeof(runway_t),
				    offsetof(runway_t, node));
				strcpy(arpt->icao, new_icao);
				arpt->pos = pos;
				arpt->TL = TL;
				arpt->TA = TA;
				htbl_set(apt_dat, new_icao, arpt);
				if (!IS_NULL_GEO_POS(pos)) {
					list_t *tile = geo_table_get_tile(
					    GEO3_TO_GEO2(pos), B_FALSE);
					ASSERT(tile != NULL);
					list_insert_tail(&tile->arpts, arpt);
					dbg_log("tile", 2, "geo_xref\t%s\t%f"
					    "\t%f", new_icao, pos.lat, pos.lon);
				}
			}
			free(comps);
		} else if (strstr(line, "100 ") == line && arpt != NULL) {
			size_t ncomps;
			char **comps = strsplit(line, " ", B_TRUE, &ncomps);
			runway_t *rwy;

			if (ncomps < 8 + 9 + 5) {
				dbg_log("tile", 0, "apt.dat \"%s\" contains "
				    "an invalid '100' line, skipping. "
				    "Offending line follows:\n%s",
				    apt_dat_fname line);
				free(comps);
				continue;
			}

			if (arpt == NULL) {
				dbg_log("tile", 0, "apt.dat \"%s\" contains "
				    "an invalid '100' line, which is not "
				    "preceded by a '1' line. Skipping.");
				free(comps);
				continue;
			}

			if (rwy_is_hard(atoi(comps[2]))) then
				rwy = calloc(1, sizeof (*rwy));

				rwy->width = atof(comps[1]);

				rwy->ends[0].id = atof(comps[8 + 0]);
				rwy->ends[0].pos = GEO_POS3(atof(comps[8 + 1]),
				    atof(comps[8 + 2]), 0);
				rwy->ends[0].displ = atof(comps[8 + 3]);
				rwy->ends[0].blast = atof(comps[8 + 4]);

				rwy->ends[1].id = atof(comps[8 + 9 + 0]);
				rwy->ends[1].pos = GEO_POS3(
				    atof(comps[8 + 9 + 1]),
				    atof(comps[8 + 9 + 2]), 0);
				rwy->ends[1].displ = atof(comps[8 + 9 + 3]);
				rwy->ends[1].blast = atof(comps[8 + 9 + 4]);


				if (ncomps >= 28 &&
				    strstr(comps[22], "GPA1:") == comps[22] &&
				    strstr(comps[23], "GPA2:") == comps[23] &&
				    strstr(comps[24], "TCH1:") == comps[24] &&
				    strstr(comps[25], "TCH2:") == comps[25] &&
				    strstr(comps[26], "TELEV1:") == comps[26] &&
				    strstr(comps[27], "TELEV2:") == comps[27]) {
					rwy->ends[0].gpa = atof(&comps[22][5]);
					rwy->ends[1].gpa = atof(&comps[23][5]);
					rwy->ends[0].tch = atof(&comps[24][5]);
					rwy->ends[1].tch = atof(&comps[25][5]);
					rwy->ends[0].pos.elev =
					    atof(&comps[26][7]);
					rwy->ends[1].pos.elev =
					    atof(&comps[27][7]);
				}

				list_insert_tail(&arpt->rwys, rwy);
			}
		}
	}

	free(line);
	fclose(apt_dat_f);

	return (arpt_cnt);
}

/*
 * Locates all apt.dat files used by X-Plane to display scenery. It consults
 * scenery_packs.ini to determine which scenery packs are currently enabled
 * and together with the default apt.dat returns them in a list sorted
 * numerically in preference order (lowest index for highest priority).
 * If the as_keys argument is true, the returned list is instead indexed
 * by the apt.dat file name and the values are the preference order of that
 * apt.dat (starting from 1 for highest priority and increasing with lowering
 * priority).
 * The apt.dat filenames relative to the X-Plane main folder
 * (raas.const.xpdir), not full filesystem paths.
 */
static char **
find_all_apt_dats(size_t *num)
	size_t n = 0;
	local i = 1

	local scenery_packs_ini = fopen(xpdir ..
	    "Custom Scenery" .. DIRSEP .. "scenery_packs.ini")

	if scenery_packs_ini ~= nil then
		for line in scenery_packs_ini:lines() do
			if line:find("SCENERY_PACK ", 1, true) == 1 then
				local scn_name = line:sub(14)
				local filename = scn_name ..
				    "Earth nav data" .. DIRSEP .. "apt.dat"
				local fp = io.open(raas.const.xpdir ..
				    filename)
				if fp ~= nil then
					fp:close()
					if as_keys then
						apt_dats[filename] = i
						i = i + 1
					else
						apt_dats[#apt_dats + 1] =
						    filename
					end
				end
			end
		end
		io.close(scenery_packs_ini)
	end

	local default_apt_dat_filename = "Resources" .. DIRSEP ..
	    "default scenery" .. DIRSEP .. "default apt dat" ..
	    DIRSEP .. "Earth nav data" .. DIRSEP .. "apt.dat"
	if as_keys then
		apt_dats[default_apt_dat_filename] = i
	else
		apt_dats[#apt_dats + 1] = default_apt_dat_filename
	end

	return apt_dats
end
