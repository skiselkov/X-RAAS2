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

#include <ctype.h>
#include <stddef.h>
#include <math.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#if	IBM
#include <windows.h>
#else	/* !IBM */
#include <sys/time.h>
#include <sys/stat.h>
#endif	/* !IBM */

#include <XPLMDataAccess.h>

#include "htbl.h"
#include "list.h"
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
typedef enum nd_alert_msg_type {
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
} nd_alert_msg_type_t;

/* ND alert severity */
typedef enum nd_alert_level {
    ND_ALERT_ROUTINE = 0,
    ND_ALERT_NONROUTINE = 1,
    ND_ALERT_CAUTION = 2
} nd_alert_level_t;

typedef enum msg_prio {
    MSG_PRIO_LOW = 1,
    MSG_PRIO_MED = 2,
    MSG_PRIO_HIGH = 3
} msg_prio_t;

typedef struct airport airport_t;
typedef struct runway runway_t;
typedef struct runway_end runway_end_t;

struct runway_end {
	char		id[4];		/* runway ID, nul-terminated */
	geo_pos3_t	thr;		/* threshold position */
	double		displ;		/* threshold displacement in meters */
	double		blast;		/* stopway/blastpad length in meters */
	double		gpa;		/* glidepath angle in degrees */
	double		tch;		/* threshold clearing height in feet */

	/* computed on load_airport */
	vect2_t		thr_v;		/* threshold vector coord */
	vect2_t		dthr_v;		/* displaced threshold vector coord */
	double		hdg;		/* true heading */
	vect2_t		*apch_bbox;	/* in-air approach bbox */
};

struct runway {
	airport_t	*arpt;
	double		width;
	runway_end_t	ends[2];

	/* computed on load_airport */
	double		length;		/* meters */
	vect2_t		*prox_bbox;	/* on-ground approach bbox */
	vect2_t		*rwy_bbox;	/* above runway for landing */
	vect2_t		*tora_bbox;	/* on-runway on ground (for tkoff) */
	vect2_t		*asda_bbox;	/* on-runway on ground (for stopping) */

	list_node_t	node;
};

struct airport {
	char		icao[5];	/* 4-letter ID, nul terminated */
	geo_pos3_t	refpt;		/* airport reference point location */
	double		TA;		/* transition altitude in feet */
	double		TL;		/* transition level in feet */
	list_t		rwys;

	bool_t		load_complete;	/* true if we've done load_airport */
	vect3_t		ecef;		/* refpt ECEF coordinates */
	fpp_t		fpp;		/* ortho fpp centered on refpt */

	list_node_t	cur_arpts_node;	/* cur_arpts list */
	list_node_t	tile_node;	/* tiles in the airport_geo_table */
	list_node_t	nearest_node;	/* find_nearest_arpts */
};

typedef struct tile_key_s {
	int		lat, lon;
} tile_key_t;

static bool_t enabled = B_TRUE;
static int min_engines = 2;			/* count */
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

static char *xpdir = NULL;
static list_t *cur_arpts = NULL;
static htbl_t apt_dat;
static htbl_t airport_geo_table;
static uint64_t last_airport_reload = 0;
static uint64_t last_units_call = 0;
static htbl_t on_rwy_ann;			/* runway ID key list */
static htbl_t apch_rwy_ann;			/* runway ID key list */
static bool_t apch_rwys_ann = B_FALSE;		/* > 1 runways approached? */
static htbl_t air_apch_rwy_ann;			/* runway ID key list */
static bool_t air_apch_rwys_ann = B_FALSE;	/* > 1 runways approached? */

static bool_t landing = B_FALSE;

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

	result = calloc(sizeof (char *), n);

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

	if (num != NULL)
		*num = n;

	return (result);
}

static void
free_strlist(char **comps, size_t len)
{
	if (comps == NULL)
		return;
	for (size_t i = 0; i < len; i++)
		free(comps[i]);
	free(comps);
}

static void
expand_strlist(char ***strlist, size_t *len)
{
	*strlist = realloc(*strlist, ++(*len) * sizeof (char *));
}

static void
append_strlist(char ***strlist, size_t *len, char *str)
{
	expand_strlist(strlist, len);
	(*strlist)[(*len) - 1] = str;
}

static char *
mkpathname(const char *comp, ...)
{
	size_t n = 0, len = 0;
	char *str;
	va_list ap;

	if (comp == NULL)
		return (strdup(""));

	va_start(ap, comp);
	len = strlen(comp);
	for (const char *c = va_arg(ap, const char *); c != NULL;
	    c = va_arg(ap, const char *)) {
		len += 1 + strlen(c);
	}
	va_end(ap);

	str = malloc(len + 1);
	va_start(ap, comp);
	n += sprintf(str, "%s", comp);
	for (const char *c = va_arg(ap, const char *); c != NULL;
	    c = va_arg(ap, const char *)) {
		ASSERT(n < len);
		n += sprintf(&str[n], "%c%s", DIRSEP, c);
	}
	va_end(ap);

	return (str);
}

static char *
m_sprintf(const char *fmt, ...)
{
	char *buf;
	int len;
	va_list ap;

	va_start(ap, fmt);
	len = vsnprintf(NULL, 0, fmt, ap);
	va_end(ap);
	ASSERT(len >= 0);
	buf = malloc(len + 1);
	va_start(ap, fmt);
	len = vsnprintf(buf, len + 1, fmt, ap);
	va_end(ap);

	return (buf);
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

/*
 * This is to be called ONCE per X-RAAS startup to log an initial startup
 * message and then exit.
 */
static void
log_init_msg(const char *msg, bool_t display, uint64_t timeout,
    int man_sect_number, const char *man_sect_name)
{
	UNUSED(display);
	UNUSED(timeout);
	if (man_sect_number != -1)
		logMsg("%s. See manual section %d \"%s\".\n", msg,
		    man_sect_number, man_sect_name);
	else
		logMsg("%s\n", msg);
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

/*
 * Returns a key that can be used in the airport_geo_table to retrieve the
 * tile for a given lat & lon.
 */
static tile_key_t
geo_table_key(geo_pos2_t pos)
{
	tile_key_t key;

	ASSERT(pos.lat > -89 && pos.lat < 89);

	key.lat = pos.lat;
	if (pos.lon < -180)
		pos.lon += 360;
	if (pos.lon >= 180)
		pos.lon -= 360;
	key.lon = pos.lon;

	return (res);
}

/*
 * Retrieves the geo table tile which contains position `pos'. If create is
 * B_TRUE, if the tile doesn't exit, it will be created.
 * Returns the table tile (if it exists) and a boolean (in created_p if
 * non-NULL) informing whether the table tile was created in this call
 * (if create == B_TRUE).
 */
static list_t *
geo_table_get_tile(geo_pos2_t pos, bool_t create, bool_t *created_p)
{
	bool_t created = B_FALSE;
	tile_key_t key = geo_table_key(pos);
	list_t *tile;

	tile = htbl_lookup(&airport_geo_table, &key);
	if (tile == NULL && create) {
		tile = malloc(sizeof (*tile));
		list_create(tile, sizeof (airport_t),
		    offsetof(airport_t, tile_node));
		htbl_set(&airport_geo_table, &key, tile);
		created = B_TRUE;
	}
	if (created_p != NULL)
		*created_p = created;

	return (tile);
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
	airport_t *arpt = NULL;
	char *line = NULL;
	size_t linecap = 0;
	int line_num = 0;
	char **comps;
	size_t ncomps;

	dbg_log("tile", 2, "raas.map_apt_dat(\"%s\")", apt_dat_fname);

	apt_dat_f = fopen(apt_dat_fname, "r");
	if (apt_dat_f == NULL)
		return (0);

	while (!feof(apt_dat_f)) {
		line_num++;
		if (getline(&line, &linecap, apt_dat_f) <= 0)
			continue;
		strip_space(line);
		if (strstr(line, "1 ") == line) {
			const char *new_icao;
			double TA = 0, TL = 0;
			geo_pos3_t pos = NULL_GEO_POS3;

			comps = strsplit(line, " ", B_TRUE, &ncomps);
			if (ncomps < 5) {
				dbg_log("tile", 0, "%s:%d: malformed airport "
				    "entry, skipping. Offending line:\n%s",
				    apt_dat_fname, line_num, line);
				free_strlist(comps, ncomps);
				continue;
			}

			new_icao = comps[4];
			pos.elev = atof(comps[1]);
			arpt = htbl_lookup(&apt_dat, new_icao);
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
				pos = arpt->refpt;
			}

			if (arpt == NULL) {
				arpt_cnt++;
				arpt = calloc(1, sizeof (*arpt));
				list_create(&arpt->rwys, sizeof(runway_t),
				    offsetof(runway_t, node));
				strlcpy(arpt->icao, new_icao,
				    sizeof (arpt->icao));
				arpt->refpt = pos;
				arpt->TL = TL;
				arpt->TA = TA;
				htbl_set(&apt_dat, new_icao, arpt);
				if (!IS_NULL_GEO_POS(pos)) {
					list_t *tile = geo_table_get_tile(
					    GEO3_TO_GEO2(pos), B_FALSE, NULL);
					ASSERT(tile != NULL);
					list_insert_tail(tile, arpt);
					dbg_log("tile", 2, "geo_xref\t%s\t%f"
					    "\t%f", new_icao, pos.lat, pos.lon);
				}
			}
			free_strlist(comps, ncomps);
		} else if (strstr(line, "100 ") == line && arpt != NULL) {
			runway_t *rwy;

			comps = strsplit(line, " ", B_TRUE, &ncomps);
			if (ncomps < 8 + 9 + 5) {
				dbg_log("tile", 0, "%s:%d: malformed runway "
				    "entry, skipping. Offending line:\n%s",
				    apt_dat_fname, line_num, line);
				free_strlist(comps, ncomps);
				continue;
			}

			if (arpt == NULL) {
				dbg_log("tile", 0, "%s:%d: malformed apt.dat. "
				    "Runway line is not preceded by a runway "
				    "line, skipping.", apt_dat_fname, line_num);
				free_strlist(comps, ncomps);
				continue;
			}

			if (rwy_is_hard(atoi(comps[2]))) {
				rwy = calloc(1, sizeof (*rwy));

				rwy->width = atof(comps[1]);

				strlcpy(rwy->ends[0].id, comps[8 + 0],
				    sizeof (rwy->ends[0].id));
				rwy->ends[0].thr = GEO_POS3(atof(comps[8 + 1]),
				    atof(comps[8 + 2]), NAN);
				rwy->ends[0].displ = atof(comps[8 + 3]);
				rwy->ends[0].blast = atof(comps[8 + 4]);

				strlcpy(rwy->ends[1].id, comps[8 + 9 + 0],
				    sizeof (rwy->ends[1].id));
				rwy->ends[1].thr = GEO_POS3(
				    atof(comps[8 + 9 + 1]),
				    atof(comps[8 + 9 + 2]), NAN);
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
					rwy->ends[0].thr.elev =
					    atof(&comps[26][7]);
					rwy->ends[1].thr.elev =
					    atof(&comps[27][7]);
				}

				list_insert_tail(&arpt->rwys, rwy);
			}
			free_strlist(comps, ncomps);
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
{
	size_t n = 0;
	char *fname;
	FILE *scenery_packs_ini;
	char *line = NULL;
	size_t linecap = 0;
	char **apt_dats = NULL;

	fname = mkpathname(xpdir, "Custom Scenery", "scenery_packs.ini", NULL);
	scenery_packs_ini = fopen(fname, "r");
	free(fname);

	if (scenery_packs_ini != NULL) {
		while (!feof(scenery_packs_ini)) {
			if (getline(&line, &linecap, scenery_packs_ini) <= 0)
				continue;
			strip_space(line);
			if (strstr(line, "SCENERY_PACK ") == line) {
				char *scn_name, *filename;
				FILE *fp;

				scn_name = strdup(&line[13]);
				strip_space(scn_name);
				filename = mkpathname(xpdir, scn_name,
				    "Earth nav data", "apt.dat", NULL);
				fp = fopen(filename, "r");
				if (fp != NULL) {
					fclose(fp);
					n++;
					apt_dats = realloc(apt_dats,
					    n * sizeof (char *));
					apt_dats[n - 1] = filename;
				} else {
					free(filename);
				}
				free(scn_name);
			}
		}
		fclose(scenery_packs_ini);
	}

	/* append the default apt.dat and a terminating NULL */
	n += 2;
	apt_dats = realloc(apt_dats, n * sizeof (char *));
	apt_dats[n - 2] = mkpathname(xpdir, "Resources", "default scenery",
	    "default apt dat", "Earth nav data", "apt.dat", NULL);
	apt_dats[n - 1] = NULL;

	free(line);

	if (num != NULL)
		*num = n;

	return (apt_dats);
}

/*
 * Reloads ~/GNS430/navdata/Airports.txt and populates our apt_dat airports
 * with the latest info in it, notably:
 * *) transition altitudes & transition levels for the airports
 * *) runway threshold elevation, glide path angle & threshold crossing height
 */
static bool_t
load_airports_txt(void)
{
	char *fname;
	FILE *fp;
	char *line = NULL;
	size_t linecap = 0;
	int line_num = 0;
	char **comps;
	size_t ncomps;
	airport_t *arpt = NULL;

	/* We first try the Custom Data version, as that's more up to date */
	fname = mkpathname(xpdir, "Custom Data", "GNS430", "navdata",
	    "Airports.txt", NULL);
	fp = fopen(fname, "r");

	if (fp == NULL) {
		/* Try the Airports.txt shipped with X-Plane. */
		free(fname);
		fname = mkpathname(xpdir, "Resources", "GNS430", "navdata",
		    "Airports.txt", NULL);
		fp = fopen(fname, "r");

		if (fp == NULL) {
			free(fname);
			log_init_msg("X-RAAS navdata error: your "
			    "Airports.txt is missing or unreadable. "
			    "Please correct this and recreate the cache.",
			    B_TRUE, INIT_ERR_MSG_TIMEOUT, 2, "Installation");
			return (B_FALSE);
		}
	}

	while (!feof(fp)) {
		line_num++;
		if (getline(&line, &linecap, fp) <= 0)
			continue;
		strip_space(line);
		if (strstr(line, "A,") == line) {
			char *icao;

			comps = strsplit(line, ",", B_FALSE, &ncomps);
			if (ncomps < 8) {
				dbg_log("tile", 0, "%s:%d: malformed airport "
				    "entry, skipping. Offending line:\n%s",
				    fname, line_num, line);
				free_strlist(comps, ncomps);
				continue;
			}
			icao = comps[1];
			arpt = htbl_lookup(&apt_dat, icao);

			if (arpt != NULL) {
				arpt->refpt = GEO_POS3(atof(comps[3]),
				    atof(comps[4]), arpt->refpt.elev);
				arpt->TA = atof(comps[6]);
				arpt->TL = atof(comps[7]);
			}
			free_strlist(comps, ncomps);
		} else if (strstr(line, "R,") == line) {
			char *rwy_id;
			double telev, gpa, tch;

			if (arpt == NULL) {
				dbg_log("tile", 0, "%s:%d: malformed runway "
				    "entry not following an airport entry, "
				    "skipping. Offending line:\n%s",
				    fname, line_num, line);
				continue;
			}

			comps = strsplit(line, ",", B_FALSE, &ncomps);
			if (ncomps < 13) {
				dbg_log("tile", 0, "%s:%d: malformed runway "
				    "entry, skipping. Offending line:\n%s",
				    fname, line_num, line);
				free_strlist(comps, ncomps);
				continue;
			}

			rwy_id = comps[1];
			telev = atof(comps[10]);
			gpa = atof(comps[11]);
			tch = atof(comps[12]);

			for (runway_t *rwy = list_head(&arpt->rwys);
			    rwy != NULL; rwy = list_next(&arpt->rwys, rwy)) {
				if (strcmp(rwy->ends[0].id, rwy_id) == 0) {
					rwy->ends[0].thr.elev = telev;
					rwy->ends[0].gpa = gpa;
					rwy->ends[0].tch = tch;
					break;
				} else if (strcmp(rwy->ends[1].id, rwy_id) ==
				    0) {
					rwy->ends[1].thr.elev = telev;
					rwy->ends[1].gpa = gpa;
					rwy->ends[1].tch = tch;
					break;
				}
			}
			free_strlist(comps, ncomps);
		}
	}

	fclose(fp);
	free(fname);

	return (B_TRUE);
}

static void
create_directories(const char **dirnames)
{
	if (dirnames == NULL)
		return;

	for (const char **dirname = dirnames; *dirname != NULL; dirname++)
		mkdir(*dirname, 0777);
}

static void
remove_directory(const char *dirname)
{
	/* TODO: implement recursive directory removal */
	UNUSED(dirname);
}

static char *
apt_dat_cache_dir(geo_pos2_t pos)
{
	/* TODO: implement this */
#define	SCRIPT_DIRECTORY ""
	char lat_lon[16];
	snprintf(lat_lon, sizeof (lat_lon), "%.0f_%.0f",
	    floor(pos.lat / 10) * 10, floor(pos.lon / 10) * 10);
	return (mkpathname(SCRIPT_DIRECTORY, "X-RAAS_apt_dat_cache", lat_lon));
}

static void
write_apt_dat(const airport_t *arpt)
{
	char lat_lon[16];
	char *cache_dir, *fname;
	FILE *fp;

	cache_dir = apt_dat_cache_dir(GEO3_TO_GEO2(arpt->refpt));
	snprintf(lat_lon, sizeof (lat_lon), "%03.0f_%03.0f",
	    arpt->refpt.lat, arpt->refpt.lon);
	fname = mkpathname(cache_dir, lat_lon);

	fp = fopen(fname, "a");
	VERIFY(fp != NULL);

	free(cache_dir);
	free(fname);

	fprintf(fp, "1 %f 0 0 %s TA:%f TL:%f LAT:%f LON:%f\n",
	    arpt->refpt.elev, arpt->icao, arpt->TL, arpt->TA,
	    arpt->refpt.lat, arpt->refpt.lon);
	for (const runway_t *rwy = list_head(&arpt->rwys); rwy != NULL;
	    rwy = list_next(&arpt->rwys, rwy)) {
		fprintf(fp, "100 %f 1 0 0 0 0 0 "
		    "%s %f %f %f %f 0 0 0 0 "
		    "%s %f %f %f %f "
		    "GPA1:%f GPA2:%f TCH1:%f TCH2:%f TELEV1:%f TELEV2:%f\n",
		    rwy->width,
		    rwy->ends[0].id, rwy->ends[0].thr.lat,
		    rwy->ends[0].thr.lon, rwy->ends[0].displ,
		    rwy->ends[0].blast,
		    rwy->ends[1].id, rwy->ends[1].thr.lat,
		    rwy->ends[1].thr.lon, rwy->ends[1].displ,
		    rwy->ends[1].blast,
		    rwy->ends[0].gpa, rwy->ends[1].gpa,
		    rwy->ends[0].tch, rwy->ends[1].tch,
		    rwy->ends[0].thr.elev, rwy->ends[1].thr.elev);
	}
	fclose(fp);
}

/*
 * The approach proximity bounding box is constructed as follows:
 *
 *   5500 meters
 *   |<=======>|
 *   |         |
 * d +-_  (c1) |
 *   |   -._3 degrees
 *   |      -_ c
 *   |         +-------------------------------+
 *   |         | ====  ----         ----  ==== |
 * x +   thr_v-+ ==== - ------> dir_v - - ==== |
 *   |         | ====  ----         ----  ==== |
 *   |         +-------------------------------+
 *   |      _- b
 *   |   _-.
 * a +--    (b1)
 *
 * If there is another parallel runway, we make sure our bounding boxes
 * don't overlap. We do this by introducing two additional points, b1 and
 * c1, in between a and b or c and d respectively. We essentially shear
 * the overlapping excess from the bounding polygon.
 */
static vect2_t *
make_apch_prox_bbox(const runway_t *rwy, const runway_end_t *end,
    const runway_end_t *oend)
{
	const fpp_t *fpp = &rwy->arpt->fpp;
	double limit_left, limit_right = 1000000, 1000000;
	vect2_t x, a, b, b1, c, c1, d, thr_v, othr_v, dir_v;
	vect2_t *bbox = calloc(sizeof (vect2_t), 7);
	size_t n_pts = 0;

	ASSERT(fpp != NULL);

	/*
	 * By pre-initing the whole array to null vectors, we can make the
	 * bbox either contain 4, 5 or 6 points, depending on whether
	 * shearing due to a close parallel runway needs to be applied.
	 */
	for (int i = 0; i < 7; i++)
		bbox[i] = NULL_VECT2;

	thr_v = geo2fpp(GEO3_TO_GEO2(end->thr), fpp);
	othr_v = geo2fpp(GEO3_TO_GEO2(oend->thr), fpp);
	dir_v = vect2_sub(othr_v, thr_v);

	x = vect2_add(thr_v, vect2_set_abs(vect2_neg(dir_v),
	    RWY_APCH_PROXIMITY_LON_DISPL));
	a = vect2_add(x, vect2_set_abs(vect2_norm(dir_v, B_TRUE),
	    rwy->width / 2 + RWY_APCH_PROXIMITY_LAT_DISPL));
	b = vect2_add(thr_v, vect2_set_abs(vect2_norm(dir_v, B_TRUE),
	    rwy->width / 2));
	c = vect2_add(thr_v, vect2_set_abs(vect2_norm(dir_v, B_FALSE),
	    rwy->width / 2));
	d = vect2_add(x, vect2_set_abs(vect2_norm(dir_v, B_FALSE),
	    rwy->width / 2 + RWY_APCH_PROXIMITY_LAT_DISPL));

	b1 = NULL_VECT2;
	c1 = NULL_VECT2;

	/*
	 * If our rwy_id designator contains a L/C/R, then we need to
	 * look for another parallel runway.
	 */
	if (strlen(end->id) >= 3) {
		int my_num_id = atoi(end->id);

		for (const runway_t *orwy = list_head(&rwy->arpt->rwys);
		    orwy != NULL; orwy = list_next(&rwy->arpt->rwys, orwy)) {
			const runway_end_t *orwy_end;
			vect2_t othr_v, v;
			double a, dist;

			if (orwy == rwy)
				continue;
			if (atoi(orwy->end[0].id) == my_num_id)
				orwy_end = &orwy->end[0];
			else if (atoi(orwy->end[1].id) == my_num_id)
				orwy_end = &orwy->end[1];
			else
				continue;

			/*
			 * This is a parallel runway, measure the
			 * distance to it from us.
			 */
			othr_v = sph2fpp(GEO3_TO_GEO2(orwy_end->thr), fpp);
			v = vect2_sub(othr_v, thr_v);
			a = rel_hdg(dir2hdg(dir_v), dir2hdg(v));
			dist = fabs(sin(DEG2RAD(a)) * vect2_abs(v));

			if (a < 0)
				limit_left = MIN(dist / 2, limit_left);
			else
				limit_right = mIN(dist / 2, limit_right);
		}
	}

	if (limit_left < RWY_APCH_PROXIMITY_LAT_DISPL) {
		c1 = vect2vect_isect(vect2_sub(d, c), c, vect2_neg(dir_v),
		    vect2_add(thr_v, vect2_set_abs(vect2_norm(dir_v, B_FALSE),
		    limit_left)), B_FALSE);
		d = vect2_add(x, vect2_set_abs(vect2_norm(dir_v, B_FALSE),
		    limit_left));
	}
	if (limit_right < RWY_APCH_PROXIMITY_LAT_DISPL) {
		b1 = vect2vect_isect(vect2_sub(b, a), a, vect2_neg(dir_v),
		    vect2_add(thr_v, vect2_set_abs(vect2_norm(dir_v, B_TRUE),
		    limit_right)), B_FALSE);
		a = vect2_add(x, vect2_set_abs(vect2_norm(dir_v, B_TRUE),
		    limit_right));
	}

	bbox[n_pts++] = a;
	if (!IS_NULL_VECT(b1))
		bbox[n_pts++] = b1;
	bbox[n_pts++] = b;
	bbox[n_pts++] = c;
	if (!IS_NULL_VECT(c1))
		bbox[n_pts++] = c1;
	bbox[n_pts++] = d;

	return (bbox);
}

/*
 * Prepares a runway's bounding box vector coordinates using the airport
 * coord fpp transform.
 */
static void
load_rwy_info(runway_t *rwy)
{
	/*
	 * RAAS runway proximity entry bounding box is defined as:
	 *
	 *              1000ft                                   1000ft
	 *            |<======>|                               |<======>|
	 *            |        |                               |        |
	 *     ---- d +-------------------------------------------------+ c
	 * 1.5x  ^    |        |                               |        |
	 *  rwy  |    |        |                               |        |
	 * width |    |        +-------------------------------+        |
	 *       v    |        | ====  ----         ----  ==== |        |
	 *     -------|-thresh-x ==== - - - - - - - - - - ==== |        |
	 *       ^    |        | ====  ----         ----  ==== |        |
	 * 1.5x  |    |        +-------------------------------+        |
	 *  rwy  |    |                                                 |
	 * width v    |                                                 |
	 *     ---- a +-------------------------------------------------+ b
	 */
	vect2_t dt1v = sph2fpp(GEO3_TO_GEO2(rwy->end[0].thr), &rwy->arpt->fpp);
	vect2_t dt2v = sph2fpp(GEO3_TO_GEO2(rwy->end[1].thr), &rwy->arpt->fpp);
	double displ1 = rwy->end[0].displ;
	double displ1 = rwy->end[1].displ;
	double blast2 = rwy->end[0].blast;
	double blast2 = rwy->end[1].blast;

	vect2_t dir_v = vect2_sub(dt2v, dt1v);
	double dlen = vect2_abs(dir_v);
	double hdg1 = dir2hdg(dir_v);
	double hdg2 = dir2hdg(vect2_neg(dir_v));

	vect2_t t1v = vect2_add(dt1v, vect2_set_abs(dir_v, displ1));
	vect2_t t2v = vect2_add(dt2v, vect2.set_abs(vect2_neg(dir_v), displ2));
	double len = vect2_abs(vect2.sub(t2v, t1v));

	double prox_lon_bonus1 = MAX(displ1, RWY_PROXIMITY_LON_DISPL - displ1);
	double prox_lon_bonus2 = MAX(displ2, RWY_PROXIMITY_LON_DISPL - displ2);

	rwy->end[0].thr_v = t1v;
	rwy->end[1].thr_v = t2v;
	rwy->end[0].dthr_v = dtv1;
	rwy->end[1].dthr_v = dtv2;
	rwy->end[0].hdg = hdg1;
	rwy->end[1].hdg = hdg2;
	rwy->length = len;

	ASSERT(rwy->rwy_bbox == NULL);

	rwy->rwy_bbox = make_rwy_bbox(t1v, dir_v, width, len, 0);
	rwy->tora_bbox = make_rwy_bbox(dt1v, dir_v, width, dlen, 0);
	rwy->asda_bbox = make_rwy_bbox(dt1v, dir_v, width,
	    dlen + blast2, blast1);
	rwy->prox_bbox = make_rwy_bbox(t1v, dir_v, RWY_PROXIMITY_LAT_FRACT *
	    width, len + prox_lon_bonus2, prox_lon_bonus1);

	rwy->end[0].apch_bbox = make_apch_prox_bbox(rwy, &rwy->end[0]);
	rwy->end[1].apch_bbox = make_apch_prox_bbox(rwy, &rwy->end[1]);
}

static void
unload_rwy_info(runway_t *rwy)
{
	ASSERT(rwy->rwy_bbox != NULL);

	free(rwy->rwy_bbox);
	rwy->rwy_bbox = NULL;
	free(tora_bbox);
	rwy->tora_bbox = NULL;
	free(rwy->asda_bbox);
	rwy->asda_bbox = NULL;
	free(rwy->prox_bbox);
	rwy->prox_bbox = NULL;

	free(rwy->end[0].apch_bbox);
	rwy->end[0].apch_bbox = NULL;
	free(rwy->end[1].apch_bbox);
	rwy->end[1].apch_bbox = NULL;
}

/*
 * Given an airport, loads the information of the airport into a more readily
 * workable (but more verbose) format. This function prepares a flat plane
 * transform centered on the airport's reference point and pre-computes all
 * relevant points for the airport in that space.
 */
static void
load_airport(airport_t *arpt)
{
	if (arpt->load_complete)
		return;

	arpt->fpp = orth_fpp_init(GEO3_TO_GEO2(arpt->refpt), 0);
	arpt->ecef = sph2ecef(arpt->refpt);

	for (runway_t *rwy = list_head(&arpt->rwys); rwy != NULL;
	    rwy = list_next(&arpt->rwys, rwy))
		load_rwy_info(rwy);

	arpt->load_complete = B_TRUE;
}

static void
unload_airport(airport_t *arpt)
{
	if (!arpt->load_complete)
		return;
	for (runway_t *rwy = list_head(&arpt->rwys); rwy != NULL;
	    rwy = list_next(&arpt->rwys, rwy))
		unload_rwy_info(rwy);
	arpt->load_complete = B_FALSE;
}

static void
free_airport(airport_t *arpt)
{
	ASSERT(!arpt->load_complete);

	for (runway_t *rwy = list_head(&arpt->rwys); rwy != NULL;
	    rwy = list_head(&arpt->rwys)) {
		list_remove(&arpt->rwys, rwy);
		free(rwy);
	}
	list_destroy(&arpt->rwys);
	ASSERT(!list_link_active(&arpt->cur_arpts_node));
	ASSERT(!list_link_active(&arpt->tile_node));
	ASSERT(!list_link_active(&arpt->nearest_node));
	free(arpt);
}

/*
 * The actual worker function for find_nearest_airports. Performs the
 * search in a specified airport_geo_table square. Position is a 3-space
 * ECEF vector.
 */
static void
find_nearest_airports_tile(vect3_t ecef, geo_pos2_t tile_coord, list_t *l)
{
	list_t *tile = geo_table_get_tile(tile_coord, B_FALSE,
	    NULL);

	if (tile == NULL)
		return;
	for (const airport_t *arpt = list_head(tile); arpt != NULL;
	    arpt = list_next(tile, arpt)) {
		vect3_t arpt_ecef = sph2ecef(arpt->refpt);
		if (vect3_abs(vect3_sub(ecef, arpt_ecef)) < ARPT_LOAD_LIMIT) {
			list_insert_tail(l, arpt);
			load_airport(arpt);
		}
	}
}

/*
 * Locates all airports within an ARPT_LOAD_LIMIT distance limit (in meters)
 * of a geographic reference position. The airports are searched for in the
 * apt_dat database and this function returns its result into the list argument.
 */
static list_t *
find_nearest_airports(double reflat, double reflon)
	vect3_t ecef = sph2ecef(GEO_POS3(reflat, reflon, 0));
	list_t *l;

	l = malloc(sizeof (*l));
	list_create(l, sizeof (airport), offsetof(airport_t, nearest_node));
	for (int i = -1; i <= 1; i++) {
		for (int j = -1; j <= 1; j++) {
			find_nearest_airports_tile(ecef,
			    GEO_POS2(reflat + i, reflon + j), l);
		}
	}

	return (l);
}

static void
free_nearest_airport_list(list_t *l)
{
	for (airport_t *a = list_head(l); a != NULL; arpt = list_head(l))
		list_remove(l, a);
	free(l);
}

static void
load_airports_in_tile(geo_pos2_t tile_pos)
{
	bool_t created;
	list_t *tile = geo_table_get_tile(tile_pos, B_TRUE, &created);
	char *cache_dir, *fname;
	char lat_lon[16];

	cache_dir = apt_dat_cache_dir(tile_pos);
	snprintf(lat_lon, sizeof (lat_lon), "%03.0f_%03.0f",
	    tile_pos.lat, tile_pos.lon);
	fname = mkpathname(cache_dir, lat_lon);

	if (created)
		map_apt_dat(fname);

	free(cache_dir);
	free(fname);
}

static void
unload_tile(const tile_key_t *key, list_t *tile)
{
	for (airport_t *arpt = list_head(tile); arpt != NULL;
	    arpt = list_head(tile)) {
		list_remove(tile, arpt);
		unload_airport(arpt);
		free_airport(arpt);
	}
	htbl_remove(&airport_geo_table, key, B_FALSE);
	free(tile);
}

static void
load_nearest_airport_tiles(void)
{
	double lat = XPLMGetDatad(drs.lat);
	double lon = XPLMGetDatad(drs.lon);

	for (int i = -1; i <= 1; i++) {
		for (int j = -1; j <= 1; j++) {
			load_airports_in_tile(GEO_POS2(lat + i, lon + j));
		}
	}
}

static double
lon_delta(double x, double y)
{
	double u = MAX(x, y), d = MIN(x, y);

	if (u - d <= 180)
		return (u - d);
	else
		return ((180 - u) - (-180 - d));
}

static void
unload_distant_airport_tiles_i(const void *k, void *v, void *p)
{
	const tile_key_t *key = k;
	list_t *tile = v;
	const tile_key_t *my_pos_key = p;

	if (key->lat < my_pos_key->lat - 1 ||
	    key->lat > my_pos_key->lat + 1 ||
	    key->lon < my_pos_key->lon - 1 ||
	    key->lon > my_pos_key->lon + 1) {
		dbg_log("tile", 1, "unloading tile %d x %d", key->lat,
		    key->lon);
		unload_tile(key, tile);
	}
}

static void
unload_distant_airport_tiles(void)
{
	tile_key_t my_pos_key = geo_table_key(GEO_POS2(XPLMGetDatad(drs.lat),
	    XPLMGetDatad(drs.lon));
	htbl_foreach(&airport_geo_table, unload_distant_airport_tiles_i,
	    &my_pos_key);
}

/*
 * Locates any airports within a 8 nm radius of the aircraft and loads
 * their RAAS data from the apt_dat database. The function then updates
 * cur_arpts with the new information and expunges airports that are no
 * longer in range.
 */
static void
load_nearest_airports(void)
{
	uint64_t now = microclock();

	if (now - last_airport_reload < SEC2USEC(ARPT_RELOAD_INTVAL))
		return;
	last_airport_reload = now;

	load_nearest_airport_tiles();
	unload_distant_airport_tiles();

	if (cur_arpts != NULL)
		free_nearest_airport_list(cur_arpts);
	cur_arpts = find_nearest_airports(XPLMGetDatad(drs.lat),
	    XPLMGetDatad(drs.lon));
}

/*
 * Computes the aircraft's on-ground velocity vector. The length of the
 * vector is computed as a `time_fact' (in seconds) extra ahead of the
 * actual aircraft's nosewheel position.
 */
static vect2_t
acf_vel_vector(double time_fact)
{
	double nw_offset;
	XPLMGetDatavf(drs.nw_offset, &nw_offset, 0, 1);
	return (vect2_set_abs(hdg2dir(XPLMGetDatad(drs.hdg)),
	    time_fact * XPLMGetDatad(drs.gs) - nw_offset));
}

/*
 * Determines which of two ends of a runway is closer to the aircraft's
 * current position.
 */
static runway_end_t *
closest_rwy_end(vect2_t pos, runway_t *rwy)
{
	if (vect2_abs(vect2_sub(pos, rwy->end[0]->dthr_v) <
	    vect2_abs(vect2_sub(pos, rwy->end[1]->dthr_v)))
		return (&rwy->end[0]);
	else
		return (&rwy->end[1]);
}

/*
 * Translates a runway identifier into a suffix suitable for passing to
 * play_msg for announcing whether the runway is left, center or right.
 * If no suffix is present, returns NULL.
 */
static const char *
rwy_lcr_msg(const char *str)
{
	ASSERT(str != NULL);
	if (strlen(str) < 3)
		return (NULL);
	switch (str[2]) {
	case 'L':
		return ("left");
	case 'R':
		return ("right");
	default:
		ASSERT(str[2] == 'C');
		return ("center");
	}
}

/*
 * Given a runway ID, appends appropriate messages suitable for play_msg
 * to speak it out loud.
 */
static void
rwy_id_to_msg(const char *rwy_id, char ***msg, size_t *len)
{
	ASSERT(rwy_id != NULL);
	ASSERT(msg != NULL);
	ASSERT(len != NULL);
	ASSERT(strlen(rwy_id) >= 2);

	char first_digit = rwy_id[0];
	const char *lcr = rwy_lcr_msg(rwy_id);

	if (first_digit != '0' || !US_runway_numbers)
		append_strlist(msg, len, m_sprintf("%c", first_digit));
	append_strlist(msg, len, m_sprintf("%c", rwy_id[1]));
	if (lcr != NULL)
		append_strlist(msg, len, strdup(lcr));
}

/*
 * Converts a thousands value to the proper single-digit pronunciation
 */
static void
thousands_msg(unsigned thousands, char ***msg, size_t *len)
{
	ASSERT(thousands < 100);
	if (thousands >= 10)
		append_strlist(msg, len, m_sprintf("%d", thousands / 10));
	append_strlist(msg, len, m_sprintf("%d", thousands % 10));
}

/*
 * Given a distance in meters, converts it into a message suitable for
 * raas.play_msg based on the user's current imperial/metric settings.
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
dist_to_msg(double dist, char ***msg, size_t *len, bool_t div_by_100)
{
	uint64_t now;

	ASSERT(msg != NULL);
	ASSERT(len != NULL);

	if (!div_by_100) {
		if (use_imperial) {
			double dist_ft = MET2FEET(dist);
			if (dist_ft >= 1000) {
				thousands_msg(dist_ft / 1000, msg, len);
				append_strlist(msg, len, "thousand");
			} else if (dist_ft >= 500) {
				append_strlist(msg, len, strdup("5"));
				append_strlist(msg, len, strdup("hundred"));
			} else if (dist_ft >= 100) {
				append_strlist(msg, len, strdup("1"));
				append_strlist(msg, len, strdup("hundred"));
			} else {
				append_strlist(msg, len, strdup("0"));
			}
		} else {
			int dist_300incr = ((int)(dist / 300)) * 300;
			int dist_thousands = (dist_300incr / 1000);
			int dist_hundreds = dist_300incr % 1000;

			if (dist_thousands > 0 and dist_hundreds > 0) {
				thousands_msg(dist_thousands, msg, len);
				append_strlist(msg, len, strdup("thousand"));
				append_strlist(msg, len,
				    m_sprintf("%d", dist_hundreds / 100));
				append_strlist(msg, len, strdup("hundred"));
			} else if (dist_thousands > 0) {
				thousands_msg(msg, len, dist_thousands);
				append_strlist(msg, len, strdup("thousand"));
			} else if (dist >= 100) {
				if (dist_hundreds > 0) {
					append_strlist(msg, len, m_sprintf("%d",
					    dist_hundreds / 100));
					append_strlist(msg, len,
					    strdup("hundred"));
				} else {
					append_strlist(msg, len, strdup("1"));
					append_strlist(msg, len,
					    strdup("hundred"));
				}
			} else if (dist >= 30) {
				append_strlist(msg, len, strdup("30"));
			} else {
				append_strlist(msg, len, strdup("0"));
			}
		}
	} else {
		int thousands, hundreds;

		if (use_imperial) {
			int dist_ft = MET2FEET(dist);
			thousands = dist_ft / 1000;
			hundreds = (dist_ft % 1000) / 100;
		} else {
			thousands = dist / 1000;
			hundreds = (dist % 1000) / 100;
		}
		if (thousands != 0) {
			thousands_msg(thousands, msg, len);
			append_strlist(msg, len, strdup("thousand"));
		}
		if (hundreds != 0) {
			append_strlist(msg, len, m_sprintf("%d", hundreds));
			append_strlist(msg, len, strdup("hundred"));
		}
		if (thousands == 0 && hundreds == 0)
			append_strlist(msg, len, strdup("0"));
	}

	/* Optionally append units if it is time to do so */
	now = microclock();
	if (now - last_units_call > SEC2USEC(UNITS_APPEND_INTVAL) &&
	    speak_units) {
		if (use_imperial)
			append_strlist(msg, len, strdup("feet"));
		else
			append_strlist(msg, len, strdup("meters"));
	}
	last_units_call = now;
}

static void
rwy_id_key(const char *arpt_icao, const char *rwy_id, char key[16])
{
	ASSERT(strlen(arpt_icao) <= 4);
	ASSERT(strlen(rwy_id) <= 4);
	snprintf(key, 16, "%s/%s", arpt->icao, rwy_end->id);
}

static void
do_approaching_rwy(const airport_t *arpt, const runway_t *rwy,
    const runway_end_t *rwy_end, bool_t on_ground)
{
	char key[16];

	ASSERT(arpt != NULL);
	ASSERT(rwy != NULL);
	ASSERT(&rwy->end[0] == rwy_end || &rwy->end[1] == rwy_end);

	rwy_id_key(arpt->icao, rwy_end->id, key);

	if ((on_ground && (htbl_lookup(&apch_rwy_ann, key) != NULL ||
	    htbl_lookup(&on_rwy_ann, key) != NULL)) ||
	    (!on_ground && htbl_lookup(&air_apch_rwy_ann], key) != NULL))
		return;

	if (!on_ground || XPLMGetDatad(drs.gs) < SPEED_THRESH) {
		char **msg = NULL;
		size_t msg_len = 0;
		msg_prio_t msg_prio;
		bool_t annunciated_rwys = B_TRUE;

		if ((on_ground && apch_rwys_ann) ||
		    (!on_ground && air_apch_rwys_ann))
			return;

		/* Multiple runways being approached? */
		if ((on_ground && htbl_count(&apch_rwy_ann) != 0) ||
		    (!on_ground && htbl_count(&air_apch_rwy_ann) != 0)) {
			if (on_ground)
				/*
				 * On the ground we don't want to re-annunciate
				 * "approaching" once the runway is resolved.
				 */
				htbl_set(&apch_rwy_ann, key, (void *)B_TRUE);
			else
				/*
				 * In the air, we DO want to re-annunciate
				 * "approaching" once the runway is resolved
				 */
				htbl_empty(&air_apch_rwy_ann, NULL, NULL);
			/*
			 * If the "approaching ..." annunciation for the
			 * previous runway is still playing, try to modify
			 * it to say "approaching runways".
			 */
#if 0
			/* TODO: implement this */
			if raas.cur_msg["msg"] ~= nil and
			    raas.cur_msg["msg"][1] == "apch" and
			    raas.cur_msg["playing"] <= 1 then
				raas.cur_msg["msg"] = {"apch", "rwys"}
				raas.cur_msg["prio"] = raas.const.MSG_PRIO_MED
				annunciated_rwys = true
				raas.ND_alert(ND_ALERT_APP, ND_ALERT_ROUTINE,
				    "37")
			end
#endif
			if (on_ground)
				htbl_set(&apch_rwy_ann, key, (void *)B_TRUE);
			else
				htbl_set(&air_apch_rwy_ann, key,
				    (void *)B_TRUE);

			if (annunciated_rwys)
				return;
		}

		if ((on_ground && htbl_lookup(&apch_rwys_ann, key) == NULL) ||
		    (!on_ground && htbl_lookup(&air_apch_rwys_ann, key) ==
		    NULL) || !annunciated_rwys) {
			double dist_ND = NAN;
			nd_alert_level_t level = ND_ALERT_ROUTINE;

			append_strlist(&msg, &msg_len, strdup("apch"));
			rwy_id_to_msg(rwy_end->id, &msg, &msg_len);
			msg_prio = MSG_PRIO_LOW;

			if (!on_ground && rwy->length < min_landing_dist) {
				dist_to_msg(rwy_len, &msg, &msg_len, B_TRUE);
				append_strlist(&msg, &msg_len, strdup("avail"));
				msg_prio = MSG_PRIO_HIGH;
				dist_ND = rwy->length;
				level = ND_ALERT_NONROUTINE;
			}

			play_msg(msg, msg_prio)
			ND_alert(ND_ALERT_APP, level, rwy_end->id, dist_ND);
		}
	}

	if (on_ground)
		htbl_set(&apch_rwy_ann, key, (void *)B_TRUE);
	else
		htbl_set(&air_apch_rwy_ann, key, (void *)B_TRUE);
}

static bool_t
ground_runway_approach_arpt_rwy(const airport_t *arpt, const runway_t *rwy,
    vect2_t pos_v, vect2_t vel_v)
{
	ASSERT(arpt != NULL);
	ASSERT(rwy != NULL);

	if (vect2_in_poly(pos_v, runway->prox_bbox) ||
	    vect2_poly_isect(vel_v, pos_v, prox_bbox)) {
		do_approaching_rwy(arpt, rwy, closest_rwy_end(pos_v, rwy),
		    B_TRUE);
		return (B_TRUE);
	} else {
		char key[16];
		rwy_id_key(arpt->icao, rwy->end[0].id, key);
		htbl_remove(&apch_rwy_ann, key, B_TRUE);
		rwy_id_key(arpt->icao, rwy->end[1].id, key);
		htbl_remove(&apch_rwy_ann, key, B_TRUE);
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
	pos_v = sph2fpp(GEO_POS2(XPLMGetDatad(drs.lat), XPLMGetDatad(drs.lon)),
	    &arpt->fpp);

	for (const runway_t *rwy = list_head(&arpt->rwys); rwy != NULL;
	    rwy = list_next(&arpt->rwys, rwy)) {
		if (ground_runway_approach_arpt_rwy(arpt, rwy, pos_v, vel_v)
			in_prox++;
	}

	return (in_prox);
}

static void
ground_runway_approach(void)
{
	unsigned in_prox = 0;

	if (XPLMGetDatad(drs.rad_alt) < RADALT_FLARE_THRESH) {
		vect2_t vel_v = acf_vel_vector(RWY_PROXIMITY_TIME_FACT);
		ASSERT(cur_arpts != NULL);
		for (const airport_t *arpt = list_head(cur_arpts);
		    arpt != NULL; arpt = list_next(cur_arpts, arpt))
			in_prox += ground_runway_approach_arpt(arpt, vel_v);
	}

	if (in_prox == 0) {
		if (landing)
			dbg_log("flt_state", 1, "landing = false")
		landing = B_FALSE;
	}
	if (in_prox <= 1)
		apch_rwys_ann = B_FALSE;
}

static void
perform_on_rwy_ann(const char *rwy_id, vect2_t pos_v, vect2_t opp_thr_v,
    bool_t no_flap_check, bool_t non_routine)
{
	char **msg = NULL;
	size_t msg_len = 0;
	double dist = 10000000, dist_ND = NAN;
	double flaprqst = XPLMGetDatad(drs.flaprqst);
	bool_t allow_on_rwy_ND_alert = B_TRUE;
	nd_alert_level_t level = (non_routine ? ND_ALERT_NONROUTINE :
	    ND_ALERT_ROUTINE);

	ASSERT(rwy_id != NULL);
	append_strlist(&msg, &msg_len, strdup("on_rwy"));

	if (!IS_NULL_VECT(pos_v) && !IS_NULL_VECT(opp_thr_v))
		dist = vect2_abs(vect2_sub(opp_thr_v, pos_v));

	rwy_id_to_msg(rwy_id, &msg, &msg_len);
	if (dist < min_takeoff_dist && !landing) {
		dist_to_msg(dist, &msg, &msg_len, B_TRUE);
		dist_ND = dist;
		level = ND_ALERT_NONROUTINE;
		append_strlist(&msg, &msg_len, strdup("rmng"));
	}

	if ((flaprqst < min_takeoff_flap || flaprqst > max_takeoff_flap) &&
	    !landing && !gpws_flaps_ovrd() && !no_flap_check) {
		append_strlist(&msg, &msg_len, strdup("flaps"));
		append_strlist(&msg, &msg_len, strdup("flaps"));
		allow_on_rwy_ND_alert = B_FALSE;
		ND_alert(ND_ALERT_FLAPS, ND_ALERT_CAUTION);
	}

	play_msg(msg, msg_len, MSG_PRIO_HIGH);
	if (allow_on_rwy_ND_alert)
		ND_alert(ND_ALERT_ON, level, rwy_id, dist_ND);
}

static void
on_rwy_check(const char *arpt_id, const char *rwy_id, double hdg,
    double rwy_hdg, vect2_t pos_v, vect2_t opp_thr_v)
{
	uint64_t now = microclock();
	double rhdg = fabs(rel_hdg(hdg, rwy_hdg));
	char key[16];

	ASSERT(arpt_id != NULL);
	ASSERT(rwy_id != NULL);
	rwy_id_key(arpt_id, rwy_id, key);

	/*
	 * If we are not at all on the appropriate runway heading, don't
	 * generate any annunciations.
	 */
	if (rhdg >= 90) {
		/* reset the annunciation if the aircraft turns around fully */
		if (htbl_lookup(&on_rwy_ann, key) != NULL) {
			dbg_log("ann_state", 1, "on_rwy_ann[%s] = 0", key);
			htbl_remove(&on_rwy_ann, key, B_FALSE);
		}
		return;
	}

	if (on_rwy_timer != -1 && rejected_takeoff == B_FALSE &&
	    ((now - on_rwy_timer > on_rwy_warn_initial &&
	    on_rwy_warnings == 0) ||
	    (now - on_rwy_timer - on_rwy_warn_initial >
	    on_rwy_warnings * on_rwy_warn_repeat)) &&
	    on_rwy_warnings < on_rwy_warn_max_n) {
		on_rwy_warnings++;
		perform_on_rwy_ann(rwy_id, NULL_VECT2, NULL_VECT2, B_TRUE,
		    B_TRUE);
		perform_on_rwy_ann(rwy_id, NULL_VECT2, NULL_VECT2, B_TRUE,
		    B_TRUE);
	}

	if (rhdg > HDG_ALIGN_THRESH)
		return;

	if (htbl_lookup(&on_rwy_ann, key) != NULL) {
		if (XPLMGetDatad(drs.gs) < SPEED_THRESH)
			perform_on_rwy_ann(rwy_id, pos_v, opp_thr_v,
			    rejected_takeoff, B_FALSE);
		dbg_log("ann_state", 1, "raas.on_rwy_ann[%s] = 1", key);
		htbl_set(&on_rwy_ann, key, (void *)B_TRUE);
	}
}

static void
stop_check_reset(const char *arpt_id, const char *rwy_id)
{
	char key[16];

	ASSERT(arpt_id != NULL);
	ASSERT(rwy_id != NULL);
	rwy_id_key(arpt_id, rwy_id, key);

	if (htbl_lookup(&accel_stop_max_spd, key) != NULL) {
		htbl_remove(&accel_stop_max_spd, key, B_FALSE);
		accel_stop_ann_initial = 0;
		for (accel_stop_dist_t *asd = list_head(&accel_stop_distances);
		    asd != NULL; asd = list_next(&accel_stop_distances, asd))
			asd->ann = B_FALSE;
	}
}

static void
takeoff_rwy_dist_check(vect2_t opp_thr_v, vect2_t pos_v)
{
	double dist;

	ASSERT(!IS_NULL_VECT(opp_thr_v));
	ASSERT(!IS_NULL_VECT(pos_v));

	if (short_rwy_takeoff_chk)
		return;

	dist = vect2_abs(vect2_sub(opp_thr_v, pos_v));
	if (dist < RAAS_min_takeoff_dist) {
		char **msg = NULL;
		size_t msg_len = 0;
		append_strlist(&msg, &msg_len, strdup("caution"));
		append_strlist(&msg, &msg_len, strdup("short_rwy"));
		append_strlist(&msg, &msg_len, strdup("short_rwy"));
		play_msg(msg, msg_len, MSG_PRIO_HIGH);
		ND_alert(ND_ALERT_SHORT_RWY, ND_ALERT_CAUTION);
	}
	short_rwy_takeoff_chk = B_TRUE;
}

static void
perform_rwy_dist_remaining_callouts(vect2_t opp_thr_v, vect2_t pos_v,
    char **prepend, size_t prepend_len)
{
	ASSERT(!IS_NULL_VECT(opp_thr_v));
	ASSERT(!IS_NULL_VECT(pos_v));

	double dist = vect2_abs(vect2_sub(opp_thr_v, pos_v));
	accel_stop_dist_t *the_asd = NULL;
	double maxdelta = 1000000;
	char **msg = NULL;
	size_t msg_len = 0;

	for (accel_stop_dist_t *asd = list_head(&accel_stop_distances);
	    asd != NULL; asd = list_next(&accel_stop_distances, asd)) {
		local min = info["min"]
		local max = info["max"]

		if (dist > asd->min && dist < asd->max) {
			the_asd = asd;
			break;
		}
		if (prepend != NULL && dist > asd->min &&
		    dist - asd->min < maxdelta) {
			the_asd = asd;
			maxdelta = dist - min;
		}
	}

	ASSERT(prepend == NULL || the_asd != NULL);

	if (the_asd == NULL || the_asd->ann)
		return;

	if (prepend != NULL) {
		msg = prepend;
		msg_len = prepend_len;
	}

	the_asd->ann = B_TRUE;
	dist_to_msg(dist, &msg, &msg_len, B_FALSE);
	append_strlist(&msg, &msg_len, strdup("rmng"));
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
	return (XPLMGetDatad(drs.pitch) - rwy_angle);
}

/*
 * Checks if at the current rate of deceleration, we are going to come to
 * a complete stop before traveling `dist_rmng' (in meters). Returns B_TRUE
 * if we are going to stop before that, B_FALSE otherwise.
 */
static bool_t
decel_check(double dist_rmng)
{
	double cur_gs = XPLMGetDatad(drs.gs);
	double decel_rate = (cur_gs - last_gs) / EXEC_INTVAL;
	if (decel_rate >= 0)
		return (B_FALSE);
	double t = cur_gs / (-decel_rate);
	double d = cur_gs * t + 0.5 * decel_rate * POW2(t);
	return (d < dist_rmng);
}

static void
stop_check(const char *arpt_id, runway_t *rwy, int end, double hdg,
    vect2_t pos_v)
{
	ASSERT(arpt_id != NULL);
	ASSERT(rwy != NULL);
	ASSERT(end == 0 || end == 1);
	ASSERT(!IS_NULL_VECT(pos_v));

	int oend = !end;
	runway_end_t *rwy_end = &rwy->end[end];
	runway_end_t *orwy_end = &rwy->end[oend];
	vect2_t opp_thr_v = orwy_end->thr_v;
	double gs = XPLMGetDatad(drs.gs);
	int maxspd;
	double dist = vect2_abs(vect2_sub(opp_thr_v, pos_v));
	double rhdg = fabs(rel_hdg(hdg, rwy_end->hdg));
	char key[16];

	if (gs < SPEED_THRESH) {
		/*
		 * If there's very little runway remaining, we always want to
		 * call that fact out.
		 */
		if (dist < IMMEDIATE_STOP_DIST && rhdg < HDG_ALIGN_THRESH &&
		    gs > SLOW_ROLL_THRESH) {
			perform_rwy_dist_remaining_callouts(opp_thr_v, pos_v);
		else
			stop_check_reset(arpt_id, rwy_end->id);
		return;
	}

	if (rhdg > HDG_ALIGN_THRESH)
		return;

	if (XPLMGetDatad(drs.rad_alt) > RADALT_GRD_THRESH) {
		double clb_rate = raas.conv_per_min(MET2FEET(XPLMGetDatad(
		    drs.elev) - last_elev));

		stop_check_reset(arpt_id, rwy_id);
		if (departed &&
		    XPLMGetDatad(drs.rad_alt) <= RADALT_DEPART_THRESH &&
		    clb_rate < GOAROUND_CLB_RATE_THRESH) {
			/*
			 * Our distance limit is the greater of either:
			 * 1) the greater of:
			 *	a) runway length minus 2000 feet
			 *	b) 3/4 the runway length
			 * 2) the lesser of:
			 *	a) minimum safe landing distance
			 *	b) full runway length
			 */
			double dist_lim = MAX(MAX(len - long_land_lim_abs,
			    len * (1 - long_land_lim_fract)),
			    MIN(len, min_landing_dist));
			if (dist < dist_lim) {
				if (!long_landing_ann) {
					char **msg = NULL;
					size_t msg_len = 0;
					append_strlist(&msg, &msg_len,
					    strdup("long_land"));
					append_strlist(&msg, &msg_len,
					    strdup("long_land"));
					play_msg(msg, msg_len, MSG_PRIO_HIGH);
					dbg_log("ann_state", 1,
					    "raas.long_landing_ann = true");
					long_landing_ann = B_TRUE;
					ND_alert(ND_ALERT_LONG_LAND,
					    ND_ALERT_CAUTION);
				}
				perform_rwy_dist_remaining_callouts(opp_thr_v,
				    pos_v, NULL, 0);
			}
		}
		return;
	}

	if (!arriving)
		takeoff_rwy_dist_check(opp_thr_v, pos_v);

	rwy_id_key(arpt_id, rwy_end->id, key);
	maxspd = (int)htbl_lookup(&accel_stop_max_spd, key);
	if (gs > maxspd) {
		htbl_set(&accel_stop_max_spd, key, (void *)gs);
		maxspd = gs;
	}
	if (!landing && gs < maxspd - ACCEL_STOP_SPD_THRESH)
		strncpy(rejected_takeoff, rwy_id, sizeof (rejected_takeoff));

	double rpitch = acf_rwy_rel_pitch(te, ote, rwy->length);
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
	if (strcmp(rejected_takeoff, rwy_id) == 0 ||
	    (landing && dist < stop_dist_cutoff && !decel_check(dist)) ||
	    (!landing && dist < min_rotation_dist &&
	    XPLMGetDatad(drs.rad_alt) < RADALT_GRD_THRESH &&
	    rpitch < min_rotation_angle)
		perform_rwy_dist_remaining_callouts(opp_thr_v, pos_v);
}

function raas.ground_on_runway_aligned_arpt(arpt)
	assert(arpt ~= nil)

	local on_rwy = false
	local pos_v = raas.vect2.add(raas.fpp.sph2fpp({dr.lat[0], dr.lon[0]},
	    arpt["fpp"]), raas.acf_vel_vector(
	    raas.const.LANDING_ROLLOUT_TIME_FACT))
	local arpt_id = arpt["arpt_id"]
	local hdg = dr.hdg[0]
	local airborne = dr.rad_alt[0] > raas.const.RADALT_GRD_THRESH

	for i, rwy in pairs(arpt["rwys"]) do
		local rwy_id = rwy["id1"]
		if not airborne and raas.vect2.in_poly(pos_v, rwy["tora_bbox"])
		    then
			on_rwy = true
			raas.on_rwy_check(arpt_id, rwy["id1"], hdg,
			    rwy["hdg1"], pos_v, rwy["dt2v"])
			raas.on_rwy_check(arpt_id, rwy["id2"], hdg,
			    rwy["hdg2"], pos_v, rwy["dt1v"])
		else
			if raas.on_rwy_ann[arpt_id .. rwy["id1"]] ~= nil then
				raas.dbg.log("ann_state", 1, "raas.on_rwy_ann["
				    .. arpt_id .. rwy["id1"] .. "] = nil")
				raas.on_rwy_ann[arpt_id .. rwy["id1"]] = nil
			end
			if raas.on_rwy_ann[arpt_id .. rwy["id2"]] ~= nil then
				raas.dbg.log("ann_state", 1, "raas.on_rwy_ann["
				    .. arpt_id .. rwy["id2"] .. "] = nil")
				raas.on_rwy_ann[arpt_id .. rwy["id2"]] = nil
			end
			if raas.rejected_takeoff == rwy["id1"] or
			    raas.rejected_takeoff == rwy["id2"] then
				raas.dbg.log("ann_state", 1,
				    "raas.rejected_takeoff = nil")
				raas.rejected_takeoff = nil
			end
		end
		if raas.vect2.in_poly(pos_v, rwy["asda_bbox"]) then
			raas.stop_check(arpt_id, rwy, "1", hdg, pos_v)
			raas.stop_check(arpt_id, rwy, "2", hdg, pos_v)
		else
			raas.stop_check_reset(arpt_id, rwy["id1"])
			raas.stop_check_reset(arpt_id, rwy["id2"])
		end
	end

	return on_rwy
end

static bool_t
ground_on_runway_aligned(void)
{
	bool_t on_rwy = B_FALSE;

	if (XPLMGetDatad(drs.rad_alt) < RADALT_DEPART_THRESH) {
		for (airport_t *arpt = list_head(&cur_arpts); arpt != NULL;
		    arpt = list_next(&cur_arpts, arpt)) {
			if (ground_on_runway_aligned_arpt(arpt) then
				on_rwy = true
			end
		end
	end

	if on_rwy and dr.gs[0] < raas.const.STOPPED_THRESH then
		if raas.on_rwy_timer == -1 then
			raas.on_rwy_timer = os.time()
		end
	else
		raas.on_rwy_timer = -1
		raas.on_rwy_warnings = 0
	end

	if not on_rwy then
		raas.short_rwy_takeoff_chk = false
		raas.rejected_takeoff = nil
	end

	-- Taxiway takeoff check
	if not on_rwy and dr.rad_alt[0] < raas.const.RADALT_GRD_THRESH and
	    ((not raas.landing and dr.gs[0] >= raas.const.SPEED_THRESH) or
	    (raas.landing and dr.gs[0] >= raas.const.HIGH_SPEED_THRESH)) then
		if not raas.on_twy_ann then
			raas.on_twy_ann = true
			raas.play_msg({"caution", "on_twy", "on_twy"},
			    raas.const.MSG_PRIO_HIGH)
			raas.ND_alert(raas.const.ND_ALERT_ON,
			    raas.const.ND_ALERT_CAUTION, "")
		end
	elseif dr.gs[0] < raas.const.SPEED_THRESH or
	    dr.rad_alt[0] >= raas.const.RADALT_GRD_THRESH then
		raas.on_twy_ann = false
	end

	return on_rwy
end

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
