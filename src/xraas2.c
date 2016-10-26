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
#include <XPLMNavigation.h>
#include <XPLMPlanes.h>
#include <XPLMProcessing.h>
#include <XPLMUtilities.h>

#include "avl.h"
#include "conf.h"
#include "geom.h"
#include "helpers.h"
#include "htbl.h"
#include "list.h"
#include "log.h"
#include "math.h"
#include "perf.h"
#include "types.h"

#define	XRAAS2_VERSION			"2.0"
#define	XRAAS2_PLUGIN_NAME		"X-RAAS " XRAAS2_VERSION
#define	XRAAS2_PLUGIN_SIG		"skiselkov.xraas2"
#define	XRAAS2_PLUGIN_DESC		"A simulation of the Runway " \
					"Awareness and Advisory System"

#define	EXEC_INTVAL			0.5		/* seconds */
#define	HDG_ALIGN_THRESH		25		/* degrees */

#define	SPEED_THRESH			20.5		/* m/s, 40 knots */
#define	HIGH_SPEED_THRESH		30.9		/* m/s, 60 knots */
#define	SLOW_ROLL_THRESH		5.15		/* m/s, 10 knots */
#define	STOPPED_THRESH			2.06		/* m/s, 4 knots */

#define	RWY_PROXIMITY_LAT_FRACT		3
#define	RWY_PROXIMITY_LON_DISPL		609.57		/* meters, 2000 ft */
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
#define	TATL_REMOTE_ARPT_DIST_LIMIT	500000	/* meters */
#define	MIN_BUS_VOLT			11	/* Volts */
#define	BUS_LOAD_AMPS			2	/* Amps */
#define	XRAAS_apt_dat_cache_version	3
#define	UNITS_APPEND_INTVAL		120	/* seconds */

#define	RWY_ID_KEY_SZ			16

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
	double		land_len;	/* length avail for landing in meters */
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

	avl_node_t	apt_dat_node;	/* apt_dat tree */
	list_node_t	cur_arpts_node;	/* cur_arpts list */
	list_node_t	tile_node;	/* tiles in the airport_geo_tree */
	list_node_t	nearest_node;	/* find_nearest_arpts */
};

typedef struct tile_s {
	int		lat;
	int		lon;
	list_t		arpts;
	avl_node_t	node;
} tile_t;

static bool_t enabled;
static int min_engines;				/* count */
static int min_mtow;				/* kg */
static bool_t allow_helos;
static bool_t auto_disable_notify;
static bool_t startup_notify;
static bool_t override_electrical;
static bool_t override_replay;
static bool_t use_tts;
static bool_t speak_units;

static bool_t use_imperial;
static int min_takeoff_dist;			/* meters */
static int min_landing_dist;			/* meters */
static int min_rotation_dist;			/* meters */
static double min_rotation_angle;		/* degrees */
static int stop_dist_cutoff;			/* meters */
static bool_t voice_female;
static double voice_volume;
static bool_t disable_ext_view;
static double min_landing_flap;			/* ratio, 0-1 */
static double min_takeoff_flap;			/* ratio, 0-1 */
static double max_takeoff_flap;			/* ratio, 0-1 */

static bool_t nd_alerts_enabled;
static int nd_alert_filter;
static bool_t nd_alert_overlay_enabled;
static bool_t nd_alert_overlay_force;
static int nd_alert_timeout;			/* seconds */

static int on_rwy_warn_initial;			/* seconds */
static int on_rwy_warn_repeat;			/* seconds */
static int on_rwy_warn_max_n;

static bool_t too_high_enabled;
static bool_t too_fast_enabled;
static double gpa_limit_mult;			/* multiplier */
static double gpa_limit_max;			/* degrees */

static const char *GPWS_priority_dataref = "sim/cockpit2/annunciators/GPWS";
static const char *GPWS_inop_dataref = "sim/cockpit/warnings/annunciators/GPWS";

static bool_t alt_setting_enabled;
static bool_t qnh_alt_enabled;
static bool_t qfe_alt_enabled;

static bool_t us_runway_numbers;

static int long_land_lim_abs;			/* meters */
static double long_land_lim_fract;		/* fraction, 0-1 */

//static bool_t debug_graphical = B_FALSE;
//static int debug_graphical_bg = 0;

static htbl_t on_rwy_ann;
static htbl_t apch_rwy_ann;
static bool_t apch_rwys_ann = B_FALSE;     /* when multiple met the criteria */
static htbl_t air_apch_rwy_ann ;
static bool_t air_apch_rwys_ann = B_FALSE; /* when multiple met the criteria */
static bool_t air_apch_short_rwy_ann = B_FALSE;
static htbl_t air_apch_flap1_ann;
static htbl_t air_apch_flap2_ann;
static htbl_t air_apch_flap3_ann;
static htbl_t air_apch_gpa1_ann;
static htbl_t air_apch_gpa2_ann;
static htbl_t air_apch_gpa3_ann;
static htbl_t air_apch_spd1_ann;
static htbl_t air_apch_spd2_ann;
static htbl_t air_apch_spd3_ann;
static bool_t on_twy_ann = B_FALSE;
static bool_t long_landing_ann = B_FALSE;
static bool_t short_rwy_takeoff_chk = B_FALSE;
static int64_t on_rwy_timer = -1;
static int on_rwy_warnings = 0;
static bool_t off_rwy_ann = B_FALSE;
static char rejected_takeoff[8] = { 0 };

static htbl_t accel_stop_max_spd;
static int accel_stop_ann_initial = 0;
static bool_t departed = B_FALSE;
static bool_t arriving = B_FALSE;
static bool_t landing = B_FALSE;

enum TATL_state_e {
	TATL_STATE_ALT,
	TATL_STATE_FL
};

static int TA = 0;
static int TL = 0;
#define TATL_FIELD_ELEV_UNSET -1000000
static int TATL_field_elev = TATL_FIELD_ELEV_UNSET;
static enum TATL_state_e TATL_state = TATL_STATE_ALT;
static int64_t TATL_transition = -1;
static char TATL_source[8] = { 0 };

//static bool_t view_is_ext = B_FALSE;
static int bus_loaded = -1;
static int last_elev = 0;
static double last_gs = 0;			/* in m/s */
static uint64_t last_units_call = 0;

static char xpdir[512] = { 0 };
static char xpprefsdir[512] = { 0 };
static char acf_path[512] = { 0 };
static char acf_filename[512] = { 0 };

static list_t *cur_arpts = NULL;
static avl_tree_t apt_dat;
static avl_tree_t airport_geo_tree;
static int64_t start_time = 0;
static int64_t last_exec_time = 0;
static int64_t last_airport_reload = 0;

static char *init_msg = NULL;
static int64_t init_msg_end = 0;

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

static void free_airport(airport_t *arpt);
static void log_init_msg(bool_t display, int timeout, int man_sect_number,
    const char *man_sect_name, const char *fmt, ...) PRINTF_ATTR(5);

static int
apt_dat_compar(const void *a, const void *b)
{
	const airport_t *aa = a, *ab = b;
	int res = strcmp(aa->icao, ab->icao);
	if (res < 0)
		return (-1);
	else if (res == 0)
		return (0);
	else
		return (1);
}

static int
airport_geo_tree_compar(const void *a, const void *b)
{
	const tile_t *ta = a, *tb = b;

	if (ta->lat < tb->lat) {
		return (-1);
	} else if (ta->lat == tb->lat) {
		if (ta->lon < tb->lon)
			return (-1);
		else if (ta->lon == tb->lon)
			return (0);
		else
			return (1);
	} else {
		return (1);
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

#if 0
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
#endif

static int64_t
microclock(void)
{
#if	IBM
	LARGE_INTEGER val, freq;
	QueryPerformanceFrequency(&freq);
	QueryPerformanceCounter(&val);
	return ((val.QuadPart * 1000000ll) / freq.QuadPart);
#else	/* !IBM */
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return ((tv.tv_sec * 1000000ll) + tv.tv_usec);
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
 * Retrieves the geo table tile which contains position `pos'. If create is
 * B_TRUE, if the tile doesn't exit, it will be created.
 * Returns the table tile (if it exists) and a boolean (in created_p if
 * non-NULL) informing whether the table tile was created in this call
 * (if create == B_TRUE).
 */
static tile_t *
geo_table_get_tile(geo_pos2_t pos, bool_t create, bool_t *created_p)
{
	bool_t created = B_FALSE;
	tile_t srch = { .lat = pos.lat, .lon = pos.lon };
	tile_t *tile;
	avl_index_t where;

	tile = avl_find(&airport_geo_tree, &srch, &where);
	if (tile == NULL && create) {
		tile = malloc(sizeof (*tile));
		tile->lat = pos.lat;
		tile->lon = pos.lon;
		list_create(&tile->arpts, sizeof (airport_t),
		    offsetof(airport_t, tile_node));
		avl_insert(&airport_geo_tree, tile, where);
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

	drs.gpws_prio = dr_get(GPWS_priority_dataref);
	drs.gpws_inop = dr_get(GPWS_inop_dataref);

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

static void
play_msg(char **msg, size_t msg_len, msg_prio_t msg_prio)
{
	/* TODO: implement audio */
	free_strlist(msg, msg_len);
	UNUSED(msg_prio);
}

static void
ND_alert(nd_alert_msg_type_t msg_type, nd_alert_level_t level,
    const char *rwy_id, double distance_rmng)
{
	UNUSED(msg_type);
	UNUSED(level);
	UNUSED(rwy_id);
	UNUSED(distance_rmng);
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

#if 0
static bool_t
GPWS_has_priority(void)
{
	return (drs.gpws_prio ? XPLMGetDatai(drs.gpws_prio) != 0 : B_FALSE);
}
#endif

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

static airport_t *
apt_dat_lookup(const char *icao)
{
	airport_t srch;
	strlcpy(srch.icao, icao, sizeof (srch.icao));
	return (avl_find(&apt_dat, &srch, NULL));
}

static void
apt_dat_insert(airport_t *arpt)
{
	avl_index_t where;
	VERIFY(avl_find(&apt_dat, arpt, &where) == NULL);
	avl_insert(&apt_dat, arpt, where);
}

/*
 * Parses an apt.dat (or X-RAAS_apt_dat.cache) file, parses its contents
 * and reconstructs our apt_dat tree. This is called at the start of
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

	dbg_log("tile", 2, "map_apt_dat(\"%s\")", apt_dat_fname);

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
			arpt = apt_dat_lookup(new_icao);
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
				apt_dat_insert(arpt);
				if (!IS_NULL_GEO_POS(pos)) {
					tile_t *tile = geo_table_get_tile(
					    GEO3_TO_GEO2(pos), B_FALSE, NULL);
					ASSERT(tile != NULL);
					list_insert_tail(&tile->arpts, arpt);
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
 * The apt.dat filenames relative to the X-Plane main folder (xpdir), not
 * full filesystem paths.
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
			log_init_msg(B_TRUE, INIT_ERR_MSG_TIMEOUT, 2,
			    "Installation", "X-RAAS navdata error: your "
			    "Airports.txt is missing or unreadable. "
			    "Please correct this and recreate the cache.");
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
			arpt = apt_dat_lookup(icao);

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
create_directory(const char *dirname)
{
	ASSERT(dirname != NULL);
	mkdir(dirname, 0777);
}

static void
remove_directory(const char *dirname)
{
	/* TODO: implement recursive directory removal */
	UNUSED(dirname);
}

static char *
apt_dat_cache_dir(geo_pos2_t pos, const char *suffix)
{
	/* TODO: implement this */
	char lat_lon[16];
	snprintf(lat_lon, sizeof (lat_lon), "%+03.0f%+04.0f",
	    floor(pos.lat / 10) * 10, floor(pos.lon / 10) * 10);

	if (suffix != NULL)
		return (mkpathname(xpprefsdir, "X-RAAS_apt_dat_cache", lat_lon,
		    suffix, NULL));
	else
		return (mkpathname(xpprefsdir, "X-RAAS_apt_dat_cache", lat_lon,
		    NULL));
}

static void
write_apt_dat(const airport_t *arpt)
{
	char lat_lon[16];
	char *fname;
	FILE *fp;

	snprintf(lat_lon, sizeof (lat_lon), "%+03.0f+%04.0f",
	    arpt->refpt.lat, arpt->refpt.lon);
	fname = apt_dat_cache_dir(GEO3_TO_GEO2(arpt->refpt), lat_lon);

	fp = fopen(fname, "a");
	VERIFY(fp != NULL);

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
	free(fname);
}

/*
 * Takes the current state of the raas.apt_dat table and writes all the
 * airports in it to the X-RAAS_apt_dat.cache so that a subsequent run
 * of X-RAAS can pick this info up.
 */
static bool_t
recreate_apt_dat_cache(void)
{
	char *version_filename;
	int version = -1;
	FILE *version_file;

	version_filename = mkpathname(xpprefsdir, "X-RAAS_apt_dat_cache",
	    "version", NULL);
	version_file = fopen(version_filename, "r");
	if (version_file != NULL) {
		if (fscanf(version_file, "%d", &version) != 1)
			version = -1;
		fclose(version_file);
	}

	if (version == XRAAS_apt_dat_cache_version) {
		/* cache version current, no need to rebuild it */
		free(version_filename);
		dbg_log("tile", 1, "X-RAAS_apt_dat_cache up to date");
		return (B_TRUE);
	}
	dbg_log("tile", 1, "X-RAAS_apt_dat_cache out of date");

	size_t n_apt_dat_files;
	char **apt_dat_files = find_all_apt_dats(&n_apt_dat_files);
	bool_t success = B_TRUE;
	char *filename;
	void *cookie = NULL;

	/* First scan all the provided apt.dat files */
	for (size_t i = 0; i < n_apt_dat_files; i++)
		map_apt_dat(apt_dat_files[i]);

	free(apt_dat_files);

	/* remove airports without runways */
	airport_t *arpt, *next_arpt;
	for (arpt = avl_first(&apt_dat); arpt != NULL; arpt = next_arpt) {
		next_arpt = AVL_NEXT(&apt_dat, arpt);
		if (list_head(&arpt->rwys) == NULL)
			avl_remove(&apt_dat, arpt);
	}
	if (!load_airports_txt()) {
		success = B_FALSE;
		goto out;
	}

	filename = mkpathname(xpprefsdir, "X-RAAS_apt_dat_cache", NULL);
	remove_directory(filename);
	create_directory(filename);
	free(filename);
	version_file = fopen(version_filename, "w");
	if (version_file == NULL) {
		success = B_FALSE;
		goto out;
	}
	fprintf(version_file, "%d\n", XRAAS_apt_dat_cache_version);
	fclose(version_file);

	for (arpt = avl_first(&apt_dat); arpt != NULL;
	    arpt = AVL_NEXT(&apt_dat, arpt)) {
		char *dirname = apt_dat_cache_dir(GEO3_TO_GEO2(arpt->refpt),
		    NULL);
		create_directory(dirname);
		write_apt_dat(arpt);
		free(dirname);
	}
out:
	while ((arpt = avl_destroy_nodes(&airport_geo_tree, &cookie)) != NULL)
		free_airport(arpt);

	free_strlist(apt_dat_files, n_apt_dat_files);
	free(version_filename);

	return (B_FALSE);
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
make_apch_prox_bbox(const runway_t *rwy, int end_i)
{
	const runway_end_t *end, *oend;
	const fpp_t *fpp = &rwy->arpt->fpp;
	double limit_left = 1000000, limit_right = 1000000;
	vect2_t x, a, b, b1, c, c1, d, thr_v, othr_v, dir_v;
	vect2_t *bbox = calloc(sizeof (vect2_t), 7);
	size_t n_pts = 0;

	ASSERT(fpp != NULL);
	ASSERT(end_i == 0 || end_i == 1);

	end = &rwy->ends[end_i];
	oend = &rwy->ends[!end_i];

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
			if (atoi(orwy->ends[0].id) == my_num_id)
				orwy_end = &orwy->ends[0];
			else if (atoi(orwy->ends[1].id) == my_num_id)
				orwy_end = &orwy->ends[1];
			else
				continue;

			/*
			 * This is a parallel runway, measure the
			 * distance to it from us.
			 */
			othr_v = geo2fpp(GEO3_TO_GEO2(orwy_end->thr), fpp);
			v = vect2_sub(othr_v, thr_v);
			a = rel_hdg(dir2hdg(dir_v), dir2hdg(v));
			dist = fabs(sin(DEG2RAD(a)) * vect2_abs(v));

			if (a < 0)
				limit_left = MIN(dist / 2, limit_left);
			else
				limit_right = MIN(dist / 2, limit_right);
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
	vect2_t dt1v = geo2fpp(GEO3_TO_GEO2(rwy->ends[0].thr), &rwy->arpt->fpp);
	vect2_t dt2v = geo2fpp(GEO3_TO_GEO2(rwy->ends[1].thr), &rwy->arpt->fpp);
	double displ1 = rwy->ends[0].displ;
	double displ2 = rwy->ends[1].displ;
	double blast1 = rwy->ends[0].blast;
	double blast2 = rwy->ends[1].blast;

	vect2_t dir_v = vect2_sub(dt2v, dt1v);
	double dlen = vect2_abs(dir_v);
	double hdg1 = dir2hdg(dir_v);
	double hdg2 = dir2hdg(vect2_neg(dir_v));

	vect2_t t1v = vect2_add(dt1v, vect2_set_abs(dir_v, displ1));
	vect2_t t2v = vect2_add(dt2v, vect2_set_abs(vect2_neg(dir_v), displ2));
	double len = vect2_abs(vect2_sub(t2v, t1v));

	double prox_lon_bonus1 = MAX(displ1, RWY_PROXIMITY_LON_DISPL - displ1);
	double prox_lon_bonus2 = MAX(displ2, RWY_PROXIMITY_LON_DISPL - displ2);

	rwy->ends[0].thr_v = t1v;
	rwy->ends[1].thr_v = t2v;
	rwy->ends[0].dthr_v = dt1v;
	rwy->ends[1].dthr_v = dt2v;
	rwy->ends[0].hdg = hdg1;
	rwy->ends[1].hdg = hdg2;
	rwy->ends[0].land_len = vect2_abs(vect2_sub(dt2v, t1v));
	rwy->ends[1].land_len = vect2_abs(vect2_sub(dt1v, t2v));
	rwy->length = len;

	ASSERT(rwy->rwy_bbox == NULL);

	rwy->rwy_bbox = make_rwy_bbox(t1v, dir_v, rwy->width, len, 0);
	rwy->tora_bbox = make_rwy_bbox(dt1v, dir_v, rwy->width, dlen, 0);
	rwy->asda_bbox = make_rwy_bbox(dt1v, dir_v, rwy->width,
	    dlen + blast2, blast1);
	rwy->prox_bbox = make_rwy_bbox(t1v, dir_v, RWY_PROXIMITY_LAT_FRACT *
	    rwy->width, len + prox_lon_bonus2, prox_lon_bonus1);

	rwy->ends[0].apch_bbox = make_apch_prox_bbox(rwy, 0);
	rwy->ends[1].apch_bbox = make_apch_prox_bbox(rwy, 1);
}

static void
unload_rwy_info(runway_t *rwy)
{
	ASSERT(rwy->rwy_bbox != NULL);

	free(rwy->rwy_bbox);
	rwy->rwy_bbox = NULL;
	free(rwy->tora_bbox);
	rwy->tora_bbox = NULL;
	free(rwy->asda_bbox);
	rwy->asda_bbox = NULL;
	free(rwy->prox_bbox);
	rwy->prox_bbox = NULL;

	free(rwy->ends[0].apch_bbox);
	rwy->ends[0].apch_bbox = NULL;
	free(rwy->ends[1].apch_bbox);
	rwy->ends[1].apch_bbox = NULL;
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

	arpt->fpp = ortho_fpp_init(GEO3_TO_GEO2(arpt->refpt), 0, &wgs84,
	    B_FALSE);
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
 * search in a specified airport_geo_tree tile. Position is a 3-space
 * ECEF vector.
 */
static void
find_nearest_airports_tile(vect3_t ecef, geo_pos2_t tile_coord, list_t *l)
{
	tile_t *tile = geo_table_get_tile(tile_coord, B_FALSE, NULL);

	if (tile == NULL)
		return;
	for (airport_t *arpt = list_head(&tile->arpts); arpt != NULL;
	    arpt = list_next(&tile->arpts, arpt)) {
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
{
	vect3_t ecef = sph2ecef(GEO_POS3(reflat, reflon, 0));
	list_t *l;

	l = malloc(sizeof (*l));
	list_create(l, sizeof (airport_t), offsetof(airport_t, nearest_node));
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
	for (airport_t *a = list_head(l); a != NULL; a = list_head(l))
		list_remove(l, a);
	free(l);
}

static void
load_airports_in_tile(geo_pos2_t tile_pos)
{
	bool_t created;
	char *cache_dir, *fname;
	char lat_lon[16];

	(void) geo_table_get_tile(tile_pos, B_TRUE, &created);
	if (!created)
		return;

	cache_dir = apt_dat_cache_dir(tile_pos, NULL);
	snprintf(lat_lon, sizeof (lat_lon), "%03.0f_%03.0f",
	    tile_pos.lat, tile_pos.lon);
	fname = mkpathname(cache_dir, lat_lon);
	map_apt_dat(fname);
	free(cache_dir);
	free(fname);
}

static void
free_tile(tile_t *tile)
{
	for (airport_t *arpt = list_head(&tile->arpts); arpt != NULL;
	    arpt = list_head(&tile->arpts)) {
		list_remove(&tile->arpts, arpt);
		unload_airport(arpt);
		free_airport(arpt);
	}
	list_destroy(&tile->arpts);
	free(tile);
}

static void
unload_tile(tile_t *tile)
{
	avl_remove(&airport_geo_tree, tile);
	free_tile(tile);
}

static void
load_nearest_airport_tiles(void)
{
	double lat = XPLMGetDatad(drs.lat);
	double lon = XPLMGetDatad(drs.lon);

	for (int i = -1; i <= 1; i++) {
		for (int j = -1; j <= 1; j++)
			load_airports_in_tile(GEO_POS2(lat + i, lon + j));
	}
}

#if 0
static double
lon_delta(double x, double y)
{
	double u = MAX(x, y), d = MIN(x, y);

	if (u - d <= 180)
		return (u - d);
	else
		return ((180 - u) - (-180 - d));
}
#endif

static void
unload_distant_airport_tiles_i(tile_t *tile, geo_pos2_t my_pos)
{
	if (tile->lat < my_pos.lat - 1 || tile->lat > my_pos.lat + 1 ||
	    tile->lon < my_pos.lon - 1 || tile->lon > my_pos.lon + 1) {
		dbg_log("tile", 1, "unloading tile %d x %d",
		    tile->lat, tile->lon);
		unload_tile(tile);
	}
}

static void
unload_distant_airport_tiles(void)
{
	geo_pos2_t my_pos = GEO_POS2(XPLMGetDatad(drs.lat),
	    XPLMGetDatad(drs.lon));
	tile_t *tile, *next_tile;

	for (tile = avl_first(&airport_geo_tree); tile != NULL;
	    tile = next_tile) {
		next_tile = AVL_NEXT(&airport_geo_tree, tile);
		unload_distant_airport_tiles_i(tile, my_pos);
	}
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
	int64_t now = microclock();

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
	float nw_offset;
	XPLMGetDatavf(drs.nw_offset, &nw_offset, 0, 1);
	return (vect2_set_abs(hdg2dir(XPLMGetDatad(drs.hdg)),
	    time_fact * XPLMGetDatad(drs.gs) - nw_offset));
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

	if (first_digit != '0' || !us_runway_numbers)
		append_strlist(msg, len, m_sprintf("%c", first_digit));
	append_strlist(msg, len, m_sprintf("%c", rwy_id[1]));
	if (lcr != NULL)
		append_strlist(msg, len, strdup(lcr));
}

/*
 * Converts a thousands value to the proper single-digit pronunciation
 */
static void
thousands_msg(char ***msg, size_t *len, unsigned thousands)
{
	ASSERT(thousands < 100);
	if (thousands >= 10)
		append_strlist(msg, len, m_sprintf("%d", thousands / 10));
	append_strlist(msg, len, m_sprintf("%d", thousands % 10));
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
dist_to_msg(double dist, char ***msg, size_t *len, bool_t div_by_100)
{
	uint64_t now;

	ASSERT(msg != NULL);
	ASSERT(len != NULL);

	if (!div_by_100) {
		if (use_imperial) {
			double dist_ft = MET2FEET(dist);
			if (dist_ft >= 1000) {
				thousands_msg(msg, len, dist_ft / 1000);
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

			if (dist_thousands > 0 && dist_hundreds > 0) {
				thousands_msg(msg, len, dist_thousands);
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
			hundreds = (((int)dist) % 1000) / 100;
		}
		if (thousands != 0) {
			thousands_msg(msg, len, thousands);
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
rwy_id_key(const char *arpt_icao, const char *rwy_id, char key[RWY_ID_KEY_SZ])
{
	ASSERT(strlen(arpt_icao) <= 4);
	ASSERT(strlen(rwy_id) <= 4);
	snprintf(key, 16, "%s/%s", arpt_icao, rwy_id);
}

static void
do_approaching_rwy(const airport_t *arpt, const runway_t *rwy,
    int end, bool_t on_ground)
{
	char key[RWY_ID_KEY_SZ];
	const runway_end_t *rwy_end;

	ASSERT(arpt != NULL);
	ASSERT(rwy != NULL);
	ASSERT(end == 0 || end == 1);

	rwy_end = &rwy->ends[end];
	rwy_id_key(arpt->icao, rwy_end->id, key);

	if ((on_ground && (htbl_lookup(&apch_rwy_ann, key) != NULL ||
	    htbl_lookup(&on_rwy_ann, key) != NULL)) ||
	    (!on_ground && htbl_lookup(&air_apch_rwy_ann, key) != NULL))
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

		if ((on_ground && htbl_lookup(&apch_rwy_ann, key) == NULL) ||
		    (!on_ground && htbl_lookup(&air_apch_rwy_ann, key) ==
		    NULL) || !annunciated_rwys) {
			double dist_ND = NAN;
			nd_alert_level_t level = ND_ALERT_ROUTINE;

			append_strlist(&msg, &msg_len, strdup("apch"));
			rwy_id_to_msg(rwy_end->id, &msg, &msg_len);
			msg_prio = MSG_PRIO_LOW;

			if (!on_ground && rwy->length < min_landing_dist) {
				dist_to_msg(rwy->length, &msg, &msg_len,
				    B_TRUE);
				append_strlist(&msg, &msg_len, strdup("avail"));
				msg_prio = MSG_PRIO_HIGH;
				dist_ND = rwy->length;
				level = ND_ALERT_NONROUTINE;
			}

			play_msg(msg, msg_len, msg_prio);
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

	if (vect2_in_poly(pos_v, rwy->prox_bbox) ||
	    vect2poly_isect(vel_v, pos_v, rwy->prox_bbox)) {
		do_approaching_rwy(arpt, rwy, closest_rwy_end(pos_v, rwy),
		    B_TRUE);
		return (B_TRUE);
	} else {
		char key[RWY_ID_KEY_SZ];
		rwy_id_key(arpt->icao, rwy->ends[0].id, key);
		htbl_remove(&apch_rwy_ann, key, B_TRUE);
		rwy_id_key(arpt->icao, rwy->ends[1].id, key);
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
	pos_v = geo2fpp(GEO_POS2(XPLMGetDatad(drs.lat), XPLMGetDatad(drs.lon)),
	    &arpt->fpp);

	for (const runway_t *rwy = list_head(&arpt->rwys); rwy != NULL;
	    rwy = list_next(&arpt->rwys, rwy)) {
		if (ground_runway_approach_arpt_rwy(arpt, rwy, pos_v, vel_v))
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
			dbg_log("flt_state", 1, "landing = false");
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
		ND_alert(ND_ALERT_FLAPS, ND_ALERT_CAUTION, NULL, NAN);
	}

	play_msg(msg, msg_len, MSG_PRIO_HIGH);
	if (allow_on_rwy_ND_alert)
		ND_alert(ND_ALERT_ON, level, rwy_id, dist_ND);
}

static void
on_rwy_check(const char *arpt_id, const char *rwy_id, double hdg,
    double rwy_hdg, vect2_t pos_v, vect2_t opp_thr_v)
{
	int64_t now = microclock();
	double rhdg = fabs(rel_hdg(hdg, rwy_hdg));
	char key[RWY_ID_KEY_SZ];

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

	if (on_rwy_timer != -1 && strcmp(rejected_takeoff, "") == 0 &&
	    ((now - on_rwy_timer > SEC2USEC(on_rwy_warn_initial) &&
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
			    strcmp(rejected_takeoff, "") != 0, B_FALSE);
		dbg_log("ann_state", 1, "on_rwy_ann[%s] = 1", key);
		htbl_set(&on_rwy_ann, key, (void *)B_TRUE);
	}
}

static void
stop_check_reset(const char *arpt_id, const char *rwy_id)
{
	char key[RWY_ID_KEY_SZ];

	ASSERT(arpt_id != NULL);
	ASSERT(rwy_id != NULL);
	rwy_id_key(arpt_id, rwy_id, key);

	if (htbl_lookup(&accel_stop_max_spd, key) != NULL) {
		htbl_remove(&accel_stop_max_spd, key, B_FALSE);
		accel_stop_ann_initial = 0;
		for (int i = 0; !isnan(accel_stop_distances[i].min); i++)
			accel_stop_distances[i].ann = B_FALSE;
	}
}

static void
takeoff_rwy_dist_check(vect2_t opp_thr_v, vect2_t pos_v)
{
	ASSERT(!IS_NULL_VECT(opp_thr_v));
	ASSERT(!IS_NULL_VECT(pos_v));

	if (short_rwy_takeoff_chk)
		return;

	double dist = vect2_abs(vect2_sub(opp_thr_v, pos_v));
	if (dist < min_takeoff_dist) {
		char **msg = NULL;
		size_t msg_len = 0;
		append_strlist(&msg, &msg_len, strdup("caution"));
		append_strlist(&msg, &msg_len, strdup("short_rwy"));
		append_strlist(&msg, &msg_len, strdup("short_rwy"));
		play_msg(msg, msg_len, MSG_PRIO_HIGH);
		ND_alert(ND_ALERT_SHORT_RWY, ND_ALERT_CAUTION, NULL, NAN);
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

	for (int i = 0; !isnan(accel_stop_distances[i].min); i++) {
		accel_stop_dist_t *asd = &accel_stop_distances[i];
		if (dist > asd->min && dist < asd->max) {
			the_asd = asd;
			break;
		}
		if (prepend != NULL && dist > asd->min &&
		    dist - asd->min < maxdelta) {
			the_asd = asd;
			maxdelta = dist - asd->min;
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
stop_check(const runway_t *rwy, int end, double hdg, vect2_t pos_v)
{
	ASSERT(arpt_id != NULL);
	ASSERT(rwy != NULL);
	ASSERT(end == 0 || end == 1);
	ASSERT(!IS_NULL_VECT(pos_v));

	const char *arpt_id = rwy->arpt->icao;
	int oend = !end;
	const runway_end_t *rwy_end = &rwy->ends[end];
	const runway_end_t *orwy_end = &rwy->ends[oend];
	vect2_t opp_thr_v = orwy_end->thr_v;
	long gs = XPLMGetDatad(drs.gs);
	long maxspd;
	double dist = vect2_abs(vect2_sub(opp_thr_v, pos_v));
	double rhdg = fabs(rel_hdg(hdg, rwy_end->hdg));
	char key[RWY_ID_KEY_SZ];

	if (gs < SPEED_THRESH) {
		/*
		 * If there's very little runway remaining, we always want to
		 * call that fact out.
		 */
		if (dist < IMMEDIATE_STOP_DIST && rhdg < HDG_ALIGN_THRESH &&
		    gs > SLOW_ROLL_THRESH)
			perform_rwy_dist_remaining_callouts(opp_thr_v, pos_v,
			    NULL, 0);
		else
			stop_check_reset(arpt_id, rwy_end->id);
		return;
	}

	if (rhdg > HDG_ALIGN_THRESH)
		return;

	if (XPLMGetDatad(drs.rad_alt) > RADALT_GRD_THRESH) {
		double clb_rate = conv_per_min(MET2FEET(XPLMGetDatad(drs.elev) -
		    last_elev));

		stop_check_reset(arpt_id, rwy_end->id);
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
			double dist_lim = MAX(MAX(
			    rwy->length - long_land_lim_abs,
			    rwy->length * (1 - long_land_lim_fract)),
			    MIN(rwy->length, min_landing_dist));
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
					    "long_landing_ann = true");
					long_landing_ann = B_TRUE;
					ND_alert(ND_ALERT_LONG_LAND,
					    ND_ALERT_CAUTION, NULL, NAN);
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
		strlcpy(rejected_takeoff, rwy_end->id,
		    sizeof (rejected_takeoff));

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
	if (strcmp(rejected_takeoff, rwy_end->id) == 0 ||
	    (landing && dist < MAX(rwy->length / 2, stop_dist_cutoff) &&
	    !decel_check(dist)) ||
	    (!landing && dist < min_rotation_dist &&
	    XPLMGetDatad(drs.rad_alt) < RADALT_GRD_THRESH &&
	    rpitch < min_rotation_angle))
		perform_rwy_dist_remaining_callouts(opp_thr_v, pos_v, NULL, 0);
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
	double hdg = XPLMGetDatad(drs.hdg);
	bool_t airborne = (XPLMGetDatad(drs.rad_alt) > RADALT_GRD_THRESH);
	const char *arpt_id = arpt->icao;

	for (const runway_t *rwy = list_head(&arpt->rwys); rwy != NULL;
	    rwy = list_next(&arpt->rwys, rwy)) {
		ASSERT(rwy->tora_bbox != NULL);
		if (!airborne && vect2_in_poly(pos_v, rwy->tora_bbox)) {
			on_rwy = B_TRUE;
			on_rwy_check(arpt_id, rwy->ends[0].id, hdg,
			    rwy->ends[0].hdg, pos_v, rwy->ends[0].dthr_v);
			on_rwy_check(arpt_id, rwy->ends[1].id, hdg,
			    rwy->ends[1].hdg, pos_v, rwy->ends[1].dthr_v);
		} else {
			char key1[16], key2[16];

			rwy_id_key(arpt_id, rwy->ends[0].id, key1);
			rwy_id_key(arpt_id, rwy->ends[1].id, key2);

			if (htbl_lookup(&on_rwy_ann, key1) != NULL) {
				dbg_log("ann_state", 1,
				    "on_rwy_ann[%s] = nil", key1);
				htbl_remove(&on_rwy_ann, key1, B_FALSE);
			}
			if (htbl_lookup(&on_rwy_ann, key2) != NULL) {
				dbg_log("ann_state", 1,
				    "on_rwy_ann[%s] = nil", key2);
				htbl_remove(&on_rwy_ann, key2, B_FALSE);
			}
			if (strcmp(rejected_takeoff, rwy->ends[0].id) == 0 ||
			    strcmp(rejected_takeoff, rwy->ends[1].id) == 0) {
				dbg_log("ann_state", 1,
				    "rejected_takeoff = nil");
				*rejected_takeoff = 0;
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

	if (XPLMGetDatad(drs.rad_alt) < RADALT_DEPART_THRESH) {
		for (const airport_t *arpt = list_head(cur_arpts);
		    arpt != NULL; arpt = list_next(cur_arpts, arpt)) {
			if (ground_on_runway_aligned_arpt(arpt))
				on_rwy = B_TRUE;
		}
	}

	if (on_rwy && XPLMGetDatad(drs.gs) < STOPPED_THRESH) {
		if (on_rwy_timer == -1)
			on_rwy_timer = microclock();
	} else {
		on_rwy_timer = -1;
		on_rwy_warnings = 0;
	}

	if (!on_rwy) {
		short_rwy_takeoff_chk = B_FALSE;
		*rejected_takeoff = 0;
	}

	/* Taxiway takeoff check */
	if (!on_rwy && XPLMGetDatad(drs.rad_alt) < RADALT_GRD_THRESH &&
	    ((!landing && XPLMGetDatad(drs.gs) >= SPEED_THRESH) ||
	    (landing && XPLMGetDatad(drs.gs) >= HIGH_SPEED_THRESH))) {
		if (!on_twy_ann) {
			char **msg = NULL;
			size_t msg_len = 0;

			on_twy_ann = B_TRUE;
			append_strlist(&msg, &msg_len, strdup("caution"));
			append_strlist(&msg, &msg_len, strdup("on_twy"));
			append_strlist(&msg, &msg_len, strdup("on_twy"));
			play_msg(msg, msg_len, MSG_PRIO_HIGH);
			ND_alert(ND_ALERT_ON, ND_ALERT_CAUTION, NULL, NAN);
		}
	} else if (XPLMGetDatad(drs.gs) < SPEED_THRESH ||
	    XPLMGetDatad(drs.rad_alt) >= RADALT_GRD_THRESH) {
		on_twy_ann = B_FALSE;
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
			return (MIN(MIN(gpa_limit_mult, mult) *
			    rwy_gpa, gpa_limit_max));
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

	ASSERT(vref != NULL);

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
 * relative to the landing speed selected in the FMC with an extra added on
 * top based on our height above the runway threshold:
 * 1) If the aircraft is outside the approach speed limit protection envelope
 *	(below 300 feet or above 950 feet above runway elevation), this
 *	function returns a very high speed value to guarantee that any
 *	comparison with the actual airspeed will indicate "in range".
 * 2) If the FMC doesn't expose landing speed information, or the information
 *	has not yet been entered, this function again returns a very high
 *	spped limit value.
 * 3) If the FMC exposes landing speed information and the information has
 *	been set, the computed margin value is:
 *	a) if the landing speed is based on the reference landing speed (Vref),
 *	   +30 knots between 300 and 500 feet, and between 500 and 950
 *	   increasing linearly from +30 knots at 500 feet to +40 knots at
 *	   950 feet.
 *	b) if the landing speed is based on the approach landing speed (Vapp),
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

static bool_t
apch_config_chk(const char *arpt_id, const char *rwy_id, double height_abv_thr,
    double gpa_act, double rwy_gpa, double win_ceil, double win_floor,
    char ***msg, size_t *msg_len, htbl_t *flap_ann_table, htbl_t *gpa_ann_table,
    htbl_t *spd_ann_table, bool_t critical, bool_t add_pause,
    double dist_from_thr, bool_t check_gear)
{
	ASSERT(arpt_id != NULL);
	ASSERT(rwy_id != NULL);

	double clb_rate = conv_per_min(MET2FEET(XPLMGetDatad(drs.elev) -
	    last_elev));
	char key[RWY_ID_KEY_SZ];

	rwy_id_key(arpt_id, rwy_id, key);

	if (height_abv_thr < win_ceil && height_abv_thr > win_floor &&
	    (!gear_is_up() || !check_gear) &&
	    clb_rate < GOAROUND_CLB_RATE_THRESH) {
		dbg_log("apch_config_chk", 2, "check at %.0f/%.0f",
		    win_ceil, win_floor);
		dbg_log("apch_config_chk", 2, "gpa_act = %.02f rwy_gpa = %.02f",
		    gpa_act, rwy_gpa);
		if (htbl_lookup(flap_ann_table, key) == NULL &&
		    XPLMGetDatad(drs.flaprqst) < min_landing_flap) {
			dbg_log("apch_config_chk", 1, "FLAPS: flaprqst = %f "
			    "min_flap = %f", XPLMGetDatad(drs.flaprqst),
			    min_landing_flap);
			if (gpws_flaps_ovrd()) {
				dbg_log("apch_config_chk", 1,
				    "FLAPS: flaps ovrd active");
			} else {
				if (!critical) {
					append_strlist(msg, msg_len,
					    strdup("flaps"));
					if (add_pause)
						append_strlist(msg, msg_len,
						    strdup("flaps"));
					append_strlist(msg, msg_len,
					    strdup("flaps"));
					ND_alert(ND_ALERT_FLAPS,
					    ND_ALERT_CAUTION, NULL, NAN);
				} else {
					append_strlist(msg, msg_len,
					    strdup("unstable"));
					append_strlist(msg, msg_len,
					    strdup("unstable"));
					ND_alert(ND_ALERT_UNSTABLE,
					    ND_ALERT_CAUTION, NULL, NAN);
				}
			}
			htbl_set(flap_ann_table, key, (void *)B_TRUE);
			return (B_TRUE);
		} else if (htbl_lookup(gpa_ann_table, key) == NULL &&
		    rwy_gpa != 0 &&
		    gpa_act > gpa_limit(rwy_gpa, dist_from_thr)) {
			dbg_log("apch_config_chk", 1, "TOO HIGH: "
			    "gpa_act = %.02f gpa_limit = %.02f",
			    gpa_act, gpa_limit(rwy_gpa, dist_from_thr));
			if (gpws_terr_ovrd()) {
				dbg_log("apch_config_chk", 1,
				    "TOO HIGH: terr ovrd active");
			} else {
				if (!critical) {
					append_strlist(msg, msg_len,
					    strdup("too_high"));
					if (add_pause)
						append_strlist(msg, msg_len,
						    strdup("pause"));
					append_strlist(msg, msg_len,
					    strdup("too_high"));
					ND_alert(ND_ALERT_TOO_HIGH,
					    ND_ALERT_CAUTION, NULL, NAN);
				} else {
					append_strlist(msg, msg_len,
					    strdup("unstable"));
					append_strlist(msg, msg_len,
					    strdup("unstable"));
					ND_alert(ND_ALERT_UNSTABLE,
					    ND_ALERT_CAUTION, NULL, NAN);
				}
			}
			htbl_set(gpa_ann_table, key, (void *)B_TRUE);
			return (B_TRUE);
		} else if (htbl_lookup(spd_ann_table, key) == NULL &&
		    too_fast_enabled && XPLMGetDatad(drs.airspeed) >
		    apch_spd_limit(height_abv_thr)) {
			dbg_log("apch_config_chk", 1, "TOO FAST: "
			    "airspeed = %.0f apch_spd_limit = %.0f",
			    XPLMGetDatad(drs.airspeed), apch_spd_limit(
			    height_abv_thr));
			if (gpws_terr_ovrd()) {
				dbg_log("apch_config_chk", 1,
				    "TOO FAST: terr ovrd active");
			} else if (gpws_flaps_ovrd()) {
				dbg_log("apch_config_chk", 1,
				    "TOO FAST: flaps ovrd active");
			} else {
				if (!critical) {
					append_strlist(msg, msg_len,
					    strdup("too_fast"));
					if (add_pause)
						append_strlist(msg, msg_len,
						    strdup("pause"));
					append_strlist(msg, msg_len,
					    strdup("too_fast"));
					ND_alert(ND_ALERT_TOO_FAST,
					    ND_ALERT_CAUTION, NULL, NAN);
				} else {
					append_strlist(msg, msg_len,
					    strdup("unstable"));
					append_strlist(msg, msg_len,
					    strdup("unstable"));
					ND_alert(ND_ALERT_UNSTABLE,
					    ND_ALERT_CAUTION, NULL, NAN);
				}
			}
			htbl_set(spd_ann_table, key, (void *)B_TRUE);
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
	char key[RWY_ID_KEY_SZ];

	rwy_id_key(arpt_id, rwy_id, key);

	if (in_prox_bbox && fabs(rel_hdg(hdg, rwy_hdg)) < HDG_ALIGN_THRESH) {
		char **msg = NULL;
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

		if (too_high_enabled && tch != 0 && rwy_gpa != 0 &&
		    fabs(elev - telev) < BOGUS_THR_ELEV_LIMIT)
			gpa_act = RAD2DEG(atan(above_tch / dist));
		else
			gpa_act = 0;

		if (apch_config_chk(arpt_id, rwy_id, alt - telev,
		    gpa_act, rwy_gpa, RWY_APCH_FLAP1_THRESH,
		    RWY_APCH_FLAP2_THRESH, &msg, &msg_len,
		    &air_apch_flap1_ann, &air_apch_gpa1_ann,
		    &air_apch_spd1_ann, B_FALSE, B_TRUE, dist, B_TRUE) ||
		    apch_config_chk(arpt_id, rwy_id, alt - telev,
		    gpa_act, rwy_gpa, RWY_APCH_FLAP2_THRESH,
		    RWY_APCH_FLAP3_THRESH, &msg, &msg_len,
		    &air_apch_flap2_ann, &air_apch_gpa2_ann,
		    &air_apch_spd2_ann, B_FALSE, B_FALSE, dist, B_FALSE) ||
		    apch_config_chk(arpt_id, rwy_id, alt - telev,
		    gpa_act, rwy_gpa, RWY_APCH_FLAP3_THRESH,
		    RWY_APCH_FLAP4_THRESH, &msg, &msg_len,
		    &air_apch_flap3_ann, &air_apch_gpa3_ann,
		    &air_apch_spd3_ann, B_TRUE, B_FALSE, dist, B_FALSE))
			msg_prio = MSG_PRIO_HIGH;

		/* If we are below 700 ft AFE and we haven't annunciated yet */
		if (alt - telev < RWY_APCH_ALT_MAX &&
		    htbl_lookup(&air_apch_rwy_ann, key) == NULL &&
		    !number_in_rngs(XPLMGetDatad(drs.rad_alt),
		    RWY_APCH_SUPP_WINDOWS, NUM_RWY_APCH_SUPP_WINDOWS)) {
			/* Don't annunciate if we are too low */
			if (alt - telev > RWY_APCH_ALT_MIN)
				do_approaching_rwy(arpt, rwy,
				    closest_rwy_end(pos_v, rwy), B_FALSE);
			htbl_set(&air_apch_rwy_ann, key, (void *)B_TRUE);
		}

		if (alt - telev < SHORT_RWY_APCH_ALT_MAX &&
		    alt - telev > SHORT_RWY_APCH_ALT_MIN &&
		    rwy_end->land_len < min_landing_dist &&
		    !air_apch_short_rwy_ann) {
			append_strlist(&msg, &msg_len, strdup("caution"));
			append_strlist(&msg, &msg_len, strdup("short_rwy"));
			append_strlist(&msg, &msg_len, strdup("short_rwy"));
			msg_prio = MSG_PRIO_HIGH;
			air_apch_short_rwy_ann = B_TRUE;
			ND_alert(ND_ALERT_SHORT_RWY, ND_ALERT_CAUTION,
			    NULL, NAN);
		}

		if (msg_len > 0)
			play_msg(msg, msg_len, msg_prio);

		return (B_TRUE);
	} else if (htbl_lookup(&air_apch_rwy_ann, key) != NULL &&
	    !in_prox_bbox) {
		htbl_remove(&air_apch_rwy_ann, key, B_FALSE);
	}

	return (B_FALSE);
}

static void
reset_airport_approach_table(htbl_t *tbl, const airport_t *arpt)
{
	ASSERT(tbl != NULL);
	ASSERT(arpt_id != NULL);

	if (htbl_count(tbl) == 0)
		return;

	for (const runway_t *rwy = list_head(&arpt->rwys); rwy != NULL;
	    rwy = list_next(&arpt->rwys, rwy)) {
		char key[RWY_ID_KEY_SZ];

		rwy_id_key(arpt->icao, rwy->ends[0].id, key);
		htbl_remove(tbl, key, B_TRUE);
		rwy_id_key(arpt->icao, rwy->ends[1].id, key);
		htbl_remove(tbl, key, B_TRUE);
	}
}

static unsigned
air_runway_approach_arpt(const airport_t *arpt)
{
	ASSERT(arpt != NULL);

	unsigned in_apch_bbox = 0;
	double alt = MET2FEET(XPLMGetDatad(drs.elev));
	double hdg = XPLMGetDatad(drs.hdg);
	double elev = arpt->refpt.elev;

	if (alt > elev + RWY_APCH_FLAP1_THRESH ||
	    alt < elev - ARPT_APCH_BLW_ELEV_THRESH) {
		reset_airport_approach_table(&air_apch_flap1_ann, arpt);
		reset_airport_approach_table(&air_apch_flap2_ann, arpt);
		reset_airport_approach_table(&air_apch_flap3_ann, arpt);
		reset_airport_approach_table(&air_apch_gpa1_ann, arpt);
		reset_airport_approach_table(&air_apch_gpa2_ann, arpt);
		reset_airport_approach_table(&air_apch_gpa3_ann, arpt);
		reset_airport_approach_table(&air_apch_spd1_ann, arpt);
		reset_airport_approach_table(&air_apch_spd2_ann, arpt);
		reset_airport_approach_table(&air_apch_spd3_ann, arpt);
		reset_airport_approach_table(&air_apch_rwy_ann, arpt);
		return (0);
	}

	vect2_t pos_v = geo2fpp(GEO_POS2(XPLMGetDatad(drs.lat),
	    XPLMGetDatad(drs.lon)), &arpt->fpp);

	for (const runway_t *rwy = list_head(&arpt->rwys); rwy != NULL;
	    rwy = list_next(&arpt->rwys, rwy)) {
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
	if (XPLMGetDatad(drs.rad_alt) < RADALT_GRD_THRESH)
		return;

	unsigned in_apch_bbox = 0;
	double clb_rate = conv_per_min(MET2FEET(XPLMGetDatad(drs.elev) -
	    last_elev));

	for (const airport_t *arpt = list_head(cur_arpts); arpt != NULL;
	    arpt = list_next(cur_arpts, arpt))
		in_apch_bbox += air_runway_approach_arpt(arpt);

	/*
	 * If we are neither over an approach bbox nor a runway, and we're
	 * not climbing and we're in a landing configuration, we're most
	 * likely trying to land onto something that's not a runway.
	 */
	if (in_apch_bbox == 0 && clb_rate < 0 && !gear_is_up() &&
	    XPLMGetDatad(drs.flaprqst) >= min_landing_flap) {
		if (XPLMGetDatad(drs.rad_alt) <= OFF_RWY_HEIGHT_MAX) {
			/* only annunciate if we're above the minimum height */
			if (XPLMGetDatad(drs.rad_alt) >= OFF_RWY_HEIGHT_MIN &&
			    !off_rwy_ann && !gpws_terr_ovrd()) {
				char **msg = NULL;
				size_t msg_len = 0;

				append_strlist(&msg, &msg_len,
				    strdup("caution"));
				append_strlist(&msg, &msg_len, strdup("twy"));
				append_strlist(&msg, &msg_len,
				    strdup("caution"));
				append_strlist(&msg, &msg_len, strdup("twy"));
				play_msg(msg, msg_len, MSG_PRIO_HIGH);
				ND_alert(ND_ALERT_TWY, ND_ALERT_CAUTION,
				    NULL, NAN);
			}
			off_rwy_ann = B_TRUE;
		} else {
			/*
			 * Annunciation gets reset once we climb through
			 * the maximum annunciation altitude.
			 */
			off_rwy_ann = B_FALSE;
		}
	}
	if (in_apch_bbox == 0)
		air_apch_short_rwy_ann = B_FALSE;
	if (in_apch_bbox <= 1)
		air_apch_rwys_ann = B_FALSE;
}

static const airport_t *
find_nearest_curarpt(void)
{
	double min_dist = ARPT_LOAD_LIMIT;
	vect3_t pos_ecef = sph2ecef(GEO_POS3(XPLMGetDatad(drs.lat),
	    XPLMGetDatad(drs.lon), XPLMGetDatad(drs.elev)));
	const airport_t *cur_arpt = NULL;

	for (const airport_t *arpt = list_head(cur_arpts); arpt != NULL;
	    arpt = list_next(cur_arpts, arpt)) {
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
	if (!alt_setting_enabled ||
	    XPLMGetDatad(drs.rad_alt) < RADALT_GRD_THRESH)
		return;

	const airport_t *cur_arpt = find_nearest_curarpt();
	bool_t field_changed = B_FALSE;

	if (cur_arpt != NULL) {
		const char *arpt_id = cur_arpt->icao;
		dbg_log("altimeter", 2, "find_nearest_curarpt() = %s", arpt_id);
		TA = cur_arpt->TA;
		TL = cur_arpt->TL;
		TATL_field_elev = cur_arpt->refpt.elev;
		if (strcmp(arpt_id, TATL_source) != 0) {
			strlcpy(TATL_source, arpt_id, sizeof (TATL_source));
			field_changed = B_TRUE;
			dbg_log("altimeter", 1, "TATL_source: %s "
			    "TA: %d TL: %d field_elev: %d", arpt_id,
			    TA, TL, TATL_field_elev);
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
		    strcmp(TATL_source, outID) != 0 &&
		    vect3_abs(vect3_sub(pos_ecef, arpt_ecef)) <
		    TATL_REMOTE_ARPT_DIST_LIMIT) {
			load_airports_in_tile(GEO_POS2(outLat, outLon));
			cur_arpt = apt_dat_lookup(outID);
			if (cur_arpt == NULL) {
				/* Grab the first airport in that tile */
				tile_t *tile = geo_table_get_tile(
				    GEO_POS2(outLat, outLon), B_FALSE, NULL);
				if (tile != NULL) {
					cur_arpt = list_head(&tile->arpts);
					if (cur_arpt != NULL) {
						dbg_log("altimeter", 2,
						    "fallback airport = %s",
						    cur_arpt->icao);
					}
				}
			}
		}

		if (cur_arpt != NULL) {
			TA = cur_arpt->TA;
			TL = cur_arpt->TL;
			TATL_field_elev = cur_arpt->refpt.elev;
			strlcpy(TATL_source, cur_arpt->icao,
			    sizeof (TATL_source));
			field_changed = B_TRUE;
			dbg_log("altimeter", 1, "TATL_source: %s "
			    "TA: %d TA: %d field_elev: %d", cur_arpt->icao,
			    TA, TL, TATL_field_elev);
		}
	}

	if (TL == 0) {
		if (field_changed)
			dbg_log("altimeter", 1, "TL = 0");
		if (TA != 0) {
			if (XPLMGetDatad(drs.baro_sl) > STD_BARO_REF) {
				TL = TA;
			} else {
				double qnh = XPLMGetDatad(drs.baro_sl) * 33.85;
				TL = TA + 28 * (1013 - qnh);
			}
			if (field_changed)
				dbg_log("altimeter", 1, "TL(auto) = %d", TL);
		}
	}
	if (TA == 0) {
		if (field_changed)
			dbg_log("altimeter", 1, "TA(auto) = %d", TA);
		TA = TL;
	}

	double elev = MET2FEET(XPLMGetDatad(drs.elev));

	if (TA != 0 && elev > TA && TATL_state == TATL_STATE_ALT) {
		TATL_transition = microclock();
		TATL_state = TATL_STATE_FL;
		dbg_log("altimeter", 1, "elev > TA (%d) transitioning "
		    "TATL_state = fl", TA);
	}

	if (TL != 0 && elev < TA && XPLMGetDatad(drs.baro_alt) < TL &&
	    /*
	     * If there's a gap between the altitudes and flight levels, don't
	     * transition until we're below the TA
	     */
	    (TA == 0 || elev < TA) && TATL_state == TATL_STATE_FL) {
		TATL_transition = microclock();
		TATL_state = TATL_STATE_ALT;
		dbg_log("altimeter", 1, "baro_alt < TL (%d) "
		    "transitioning TATL_state = alt", TL);
	}

	int64_t now = microclock();
	if (TATL_transition != -1) {
		if (/* We have transitioned into ALT mode */
		    TATL_state == TATL_STATE_ALT &&
		    /* The fixed timeout has passed, OR */
		    (now - TATL_transition > ALTM_SETTING_TIMEOUT ||
		    /*
		     * The field has a known elevation and we are within
		     * 1500 feet of it
		     */
		    (TATL_field_elev != TATL_FIELD_ELEV_UNSET &&
		    (elev < TATL_field_elev + ALTM_SETTING_ALT_CHK_LIMIT)))) {
			double d_qnh = 0, d_qfe = 0;

			if (qnh_alt_enabled)
				d_qnh = fabs(elev - XPLMGetDatad(drs.baro_alt));
			if (TATL_field_elev != TATL_FIELD_ELEV_UNSET &&
			    qfe_alt_enabled)
				d_qfe = fabs(XPLMGetDatad(drs.baro_alt) -
				    (elev - TATL_field_elev));
			dbg_log("altimeter", 1, "alt check; d_qnh: %.1f "
			    " d_qfe: %.1f", d_qnh, d_qfe);
			if (/* The set baro is out of bounds for QNH, OR */
			    d_qnh > ALTIMETER_SETTING_QNH_ERR_LIMIT ||
			    /* Set baro is out of bounds for QFE */
			    d_qfe > ALTM_SETTING_QFE_ERR_LIMIT) {
				char **msg = NULL;
				size_t msg_len = 0;
				append_strlist(&msg, &msg_len,
				    strdup("alt_set"));
				play_msg(msg, msg_len, MSG_PRIO_LOW);
				ND_alert(ND_ALERT_ALTM_SETTING,
				    ND_ALERT_CAUTION, NULL, NAN);
			}
			TATL_transition = -1;
		} else if (TATL_state == TATL_STATE_FL &&
		    now - TATL_transition > ALTM_SETTING_TIMEOUT) {
			double d_ref = fabs(XPLMGetDataf(drs.baro_set) -
			    STD_BARO_REF);
			dbg_log("altimeter", 1, "fl check; d_ref: %.1f", d_ref);
			if (d_ref > ALTM_SETTING_BARO_ERR_LIMIT) {
				char **msg = NULL;
				size_t msg_len = 0;
				append_strlist(&msg, &msg_len,
				    strdup("alt_set"));
				play_msg(msg, msg_len, MSG_PRIO_LOW);
				ND_alert(ND_ALERT_ALTM_SETTING,
				    ND_ALERT_CAUTION, NULL, NAN);
			}
			TATL_transition = -1;
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
	if (bus_loaded == xbusnr) {
		float val;
		XPLMGetDatavf(drs.plug_bus_load, &val, xbusnr, 1);
		val -= BUS_LOAD_AMPS;
		XPLMSetDatavf(drs.plug_bus_load, &val, xbusnr, 1);
		bus_loaded = -1;
	}
	if (bus_loaded == -1) {
		float val;
		XPLMGetDatavf(drs.plug_bus_load, &val, busnr, 1);
		val += BUS_LOAD_AMPS;
		XPLMSetDatavf(drs.plug_bus_load, &val, busnr, 1);
		bus_loaded = busnr;
	}
}

/*
 * Returns true if X-RAAS has electrical power from the aircraft.
 */
static bool_t
is_on(void)
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
	} else if (bus_loaded != -1) {
		float bus_load;
		XPLMGetDatavf(drs.plug_bus_load, &bus_load, bus_loaded, 1);
		bus_load -= BUS_LOAD_AMPS;
		XPLMSetDatavf(drs.plug_bus_load, &bus_load, bus_loaded, 1);
		bus_loaded = -1;
	}

	return ((turned_on || override_electrical) &&
	    (XPLMGetDatai(drs.replay_mode) == 0 || override_replay));
}

static void
raas_exec(void)
{
	int64_t now = microclock();
#if 0
	int64_t time_s, time_e;
#endif

	/*
	 * Before we start, wait a set delay, because X-Plane's datarefs
	 * needed for proper init are unstable, so we'll give them an
	 * extra second to fix themselves.
	 */
	if (now - start_time < SEC2USEC(STARTUP_DELAY) ||
	    now - last_exec_time < SEC2USEC(EXEC_INTVAL))
		return;

	last_exec_time = now;
	if (!is_on()) {
		dbg_log("power_state", 1, "is_on = false");
		return;
	}

#if 0
	/* TODO: implement ND alerts */
	if dr.ND_alert[0] > 0 and
	    now - raas.ND_alert_start_time > RAAS_ND_alert_timeout then
		if raas.GPWS_has_priority() then
			-- If GPWS priority has overridden us, keep the
			-- alert displayed until after the priority
			-- has been lifted.
			raas.ND_alert_start_time = now
		else
			dr.ND_alert[0] = 0
		end
	end
#endif

	load_nearest_airports();

	if (XPLMGetDatad(drs.rad_alt) > RADALT_FLARE_THRESH) {
		if (!departed) {
			departed = B_TRUE;
			dbg_log("flt_state", 1, "departed = true");
		}
		if (!arriving) {
			arriving = B_TRUE;
			dbg_log("flt_state", 1, "arriving = true");
		}
		if (long_landing_ann) {
			dbg_log("ann_state", 1, "long_landing_ann = false");
			long_landing_ann = B_FALSE;
		}
	} else if (XPLMGetDatad(drs.rad_alt) < RADALT_GRD_THRESH) {
		if (departed) {
			dbg_log("flt_state", 1, "landing = true");
			dbg_log("flt_state", 1, "departed = false");
			landing = B_TRUE;
		}
		departed = B_FALSE;
		if (XPLMGetDatad(drs.gs) < SPEED_THRESH)
			arriving = B_FALSE;
	}
	if (XPLMGetDataf(drs.gs) < SPEED_THRESH && long_landing_ann) {
		dbg_log("ann_state", 1, "long_landing_ann = false");
		long_landing_ann = B_FALSE;
	}

#if 0
	if RAAS_debug["profile"] ~= nil then
		time_s = raas.time()
	end
#endif

	ground_runway_approach();
	ground_on_runway_aligned();
	air_runway_approach();
	altimeter_setting();

#if 0
	if RAAS_debug["profile"] ~= nil then
		time_e = raas.time()
		raas.dbg.log("profile", 1, string.format("raas.exec: " ..
		    "%.03f ms [Lua: %d kB]", ((time_e - time_s) * 1000),
		    collectgarbage("count")))
	end
#endif

	last_elev = XPLMGetDatad(drs.elev);
	last_gs = XPLMGetDatad(drs.gs);
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

static char *
man_ref(int section_number, const char *section_name)
{
	return (m_sprintf("For more information, please refer to the X-RAAS "
	    "user manual in docs%cmanual.pdf, section %d \"%s\".", DIRSEP,
	    section_number, section_name));
}

/*
 * This is to be called ONCE per X-RAAS startup to log an initial startup
 * message and then exit.
 */
static void
log_init_msg(bool_t display, int timeout, int man_sect_number,
    const char *man_sect_name, const char *fmt, ...)
{
	char *mref = (man_sect_number == -1 ?
	    man_ref(man_sect_number, man_sect_name) : NULL);
	va_list ap;
	int len;
	char *msg;

	va_start(ap, fmt);
	len = vsnprintf(NULL, 0, fmt, ap);
	va_end(ap);

	if (mref != NULL)
		/* +1 for newline and another for terminating nul */
		msg = calloc(1, len + 1 + strlen(mref) + 1);
	else
		/* +1 for terminating nul */
		msg = calloc(1, len + 1);

	va_start(ap, fmt);
	vsnprintf(msg, len + 1, fmt, ap);
	va_end(ap);

	if (mref != NULL) {
		msg[len] = '\n';
		strcpy(&msg[len + 1], mref);
		free(mref);
		mref = NULL;
	}

	logMsg("%s", msg);
	if (display) {
		init_msg = msg;
		init_msg_end = microclock() + SEC2USEC(timeout);
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
reset_config(void)
{
	enabled = B_TRUE;
	allow_helos = B_FALSE;
	min_engines = 2;
	min_mtow = 5700;
	auto_disable_notify = B_TRUE;
	startup_notify = B_TRUE;
	use_imperial = B_TRUE;
	voice_female = B_TRUE;
	voice_volume = 1.0;
	use_tts = B_FALSE;
	us_runway_numbers = B_FALSE;
	min_takeoff_dist = 1000;
	min_landing_dist = 800;
	min_rotation_dist = 400;
	min_rotation_angle = 3;
	stop_dist_cutoff = 1600;
	min_landing_flap = 0.5;
	min_takeoff_flap = 0.1;
	max_takeoff_flap = 0.75;
	on_rwy_warn_initial = 60;
	on_rwy_warn_repeat = 120;
	on_rwy_warn_max_n = 3;
	too_high_enabled = B_TRUE;
	too_fast_enabled = B_TRUE;
	gpa_limit_mult = 2;
	gpa_limit_max = 8;
	alt_setting_enabled = B_TRUE;
	qnh_alt_enabled = B_TRUE;
	qfe_alt_enabled = B_FALSE;
	disable_ext_view = B_TRUE;
	override_electrical = B_FALSE;
	override_replay = B_FALSE;
	speak_units = B_TRUE;
	long_land_lim_abs = 610;	/* 2000 feet */
	long_land_lim_fract = 0.25;
	nd_alerts_enabled = B_TRUE;
	nd_alert_filter = ND_ALERT_ROUTINE;
	nd_alert_overlay_enabled = B_TRUE;
	nd_alert_overlay_force = B_FALSE;
	nd_alert_timeout = 7;
}

static void
process_conf(conf_t *conf)
{
#define	GET_CONF(type, varname) \
	(void) xraas_conf_get_ ## type(conf, "raas_" #varname, &varname)
	GET_CONF(b, enabled);
	GET_CONF(b, allow_helos);
	GET_CONF(i, min_engines);
	GET_CONF(i, min_mtow);
	GET_CONF(b, auto_disable_notify);
	GET_CONF(b, startup_notify);
	GET_CONF(b, use_imperial);
	GET_CONF(b, voice_female);
	GET_CONF(d, voice_volume);
	GET_CONF(b, use_tts);
	GET_CONF(b, us_runway_numbers);
	GET_CONF(i, min_takeoff_dist);
	GET_CONF(i, min_landing_dist);
	GET_CONF(i, min_rotation_dist);
	GET_CONF(d, min_rotation_angle);
	GET_CONF(i, stop_dist_cutoff);
	GET_CONF(d, min_landing_flap);
	GET_CONF(d, min_takeoff_flap);
	GET_CONF(d, max_takeoff_flap);
	GET_CONF(i, on_rwy_warn_initial);
	GET_CONF(i, on_rwy_warn_repeat);
	GET_CONF(i, on_rwy_warn_max_n);
	GET_CONF(b, too_high_enabled);
	GET_CONF(b, too_fast_enabled);
	GET_CONF(d, gpa_limit_mult);
	GET_CONF(d, gpa_limit_max);
	GET_CONF(b, alt_setting_enabled);
	GET_CONF(b, qnh_alt_enabled);
	GET_CONF(b, qfe_alt_enabled);
	GET_CONF(b, disable_ext_view);
	GET_CONF(b, override_electrical);
	GET_CONF(b, override_replay);
	GET_CONF(b, speak_units);
	GET_CONF(i, long_land_lim_abs);
	GET_CONF(d, long_land_lim_fract);
	GET_CONF(b, nd_alerts_enabled);
	GET_CONF(i, nd_alert_filter);
	GET_CONF(b, nd_alert_overlay_enabled);
	GET_CONF(b, nd_alert_overlay_force);
	GET_CONF(i, nd_alert_timeout);
#undef	GET_CONF
}

/*
 * Loads a config file at path `cfgname' if it exists. If the file doesn't
 * exist, this function just returns true. If the config file contains errors,
 * this function shows an init message and returns false, otherwise it returns
 * true.
 */
static bool_t
load_config(bool_t global_cfg)
{
	char *cfgname;
	FILE *cfg_f;

	if (global_cfg)
		cfgname = mkpathname(acf_path, "X-RAAS.cfg", NULL);
	else
		cfgname = mkpathname(xpdir, "Resources", "plugins", "X-RAAS",
		    "X-RAAS.cfg", NULL);

	cfg_f = fopen(cfgname, "r");
	if (cfg_f != NULL) {
		conf_t *conf;
		int errline;

		dbg_log("config", 1, "loading config file: %s", cfgname);
		if ((conf = xraas_parse_conf(cfg_f, &errline)) == NULL) {
			log_init_msg(B_TRUE, INIT_ERR_MSG_TIMEOUT,
			    5, "Configuration", "X-RAAS startup error: syntax "
			    "error on line %d in config file:\n%s\n"
			    "Please correct this and then hit Plugins "
			    "-> Admin, Disable & Enable X-RAAS.", errline,
			    cfgname);
			fclose(cfg_f);
			return (B_FALSE);
		}
		process_conf(conf);
		fclose(cfg_f);
		xraas_free_conf(conf);
	}
	free(cfgname);
	return (B_TRUE);
}

/*
 * Loads the global and aircraft-specific X-RAAS config files.
 */
static bool_t
load_configs(void)
{
	/* order is important here, first load the global one */
	reset_config();
	if (!load_config(B_TRUE))
		return (B_FALSE);
	if (!load_config(B_FALSE))
		return (B_FALSE);
	return (B_TRUE);
}

static void
xraas_init(void)
{
	/* these must go ahead of config parsing */
	start_time = microclock();
	XPLMGetNthAircraftModel(0, acf_filename, acf_path);

	if (!load_configs)
		return;

	if (!enabled)
		return;

	raas_dr_reset();

	if (chk_acf_is_helo() && !allow_helos)
		return;
	if (XPLMGetDatai(drs.num_engines) < min_engines ||
	    XPLMGetDatad(drs.mtow) < min_mtow) {
		char icao[8];
		memset(icao, 0, sizeof (icao));
		XPLMGetDatab(drs.ICAO, icao, 0, sizeof (icao) - 1);
		log_init_msg(auto_disable_notify, INIT_ERR_MSG_TIMEOUT,
		    3, "Activating X-RAAS in the aircraft",
		    "X-RAAS: auto-disabled: aircraft below X-RAAS limits:\n"
		    "X-RAAS configuration: minimum number of engines: %d; "
		    "minimum MTOW: %d kg\n"
		    "Your aircraft: (%s) number of engines: %d; "
		    "MTOW: %.0f kg\n", min_engines, min_mtow, icao,
		    XPLMGetDatai(drs.num_engines), XPLMGetDatad(drs.mtow));
		return;
	}

#define	AIRPORT_TABLE_SZ	512
#define	RUNWAY_TABLE_SZ		128
#define	GEO_TABLE_SZ		128
#define	ICAO_SZ			4

	avl_create(&apt_dat, apt_dat_compar, sizeof (airport_t),
	    offsetof(airport_t, apt_dat_node));
	avl_create(&airport_geo_tree, airport_geo_tree_compar,
	    sizeof (tile_t), offsetof(tile_t, node));

#define	CREATE_RWY_TABLE(x)	\
	htbl_create((x), RUNWAY_TABLE_SZ, RWY_ID_KEY_SZ, B_FALSE)
	CREATE_RWY_TABLE(&accel_stop_max_spd);
	CREATE_RWY_TABLE(&on_rwy_ann);
	CREATE_RWY_TABLE(&apch_rwy_ann);
	CREATE_RWY_TABLE(&air_apch_rwy_ann);
	CREATE_RWY_TABLE(&air_apch_flap1_ann);
	CREATE_RWY_TABLE(&air_apch_flap2_ann);
	CREATE_RWY_TABLE(&air_apch_flap3_ann);
	CREATE_RWY_TABLE(&air_apch_gpa1_ann);
	CREATE_RWY_TABLE(&air_apch_gpa2_ann);
	CREATE_RWY_TABLE(&air_apch_gpa3_ann);
	CREATE_RWY_TABLE(&air_apch_spd1_ann);
	CREATE_RWY_TABLE(&air_apch_spd2_ann);
	CREATE_RWY_TABLE(&air_apch_spd3_ann);
#undef	CREATE_RWY_TABLE

	if (!recreate_apt_dat_cache())
		return;

	XPLMRegisterFlightLoopCallback(raas_exec_cb, EXEC_INTVAL, NULL);
}

static void
xraas_fini(void)
{
	tile_t *tile;
	void *cookie = NULL;

	if (!enabled)
		return;

	if (bus_loaded != -1) {
		float bus_load;
		XPLMGetDatavf(drs.plug_bus_load, &bus_load, bus_loaded, 1);
		bus_load -= BUS_LOAD_AMPS;
		XPLMSetDatavf(drs.plug_bus_load, &bus_load, bus_loaded, 1);
		bus_loaded = -1;
	}
#if 0
	if raas.cur_msg["snd"] ~= nil then
		stop_sound(raas.cur_msg["snd"])
		raas.cur_msg = {}
	end
#endif

	/* airports are freed in the free_tile function */
	while (avl_destroy_nodes(&airport_geo_tree, &cookie) != NULL)
		;
	avl_destroy(&apt_dat);

	while ((tile = avl_destroy_nodes(&airport_geo_tree, &cookie)) != NULL)
		free_tile(tile);
	avl_destroy(&airport_geo_tree);

#define	DESTROY_SIMPLE_TABLE(x) \
	do { \
		htbl_empty((x), NULL, NULL); \
		htbl_destroy((x)); \
	} while (0)
	DESTROY_SIMPLE_TABLE(&accel_stop_max_spd);
	DESTROY_SIMPLE_TABLE(&on_rwy_ann);
	DESTROY_SIMPLE_TABLE(&apch_rwy_ann);
	DESTROY_SIMPLE_TABLE(&air_apch_rwy_ann);
	DESTROY_SIMPLE_TABLE(&air_apch_flap1_ann);
	DESTROY_SIMPLE_TABLE(&air_apch_flap2_ann);
	DESTROY_SIMPLE_TABLE(&air_apch_flap3_ann);
	DESTROY_SIMPLE_TABLE(&air_apch_gpa1_ann);
	DESTROY_SIMPLE_TABLE(&air_apch_gpa2_ann);
	DESTROY_SIMPLE_TABLE(&air_apch_gpa3_ann);
	DESTROY_SIMPLE_TABLE(&air_apch_spd1_ann);
	DESTROY_SIMPLE_TABLE(&air_apch_spd2_ann);
	DESTROY_SIMPLE_TABLE(&air_apch_spd3_ann);
#undef	DESTROY_SIMPLE_TABLE

	if (init_msg != NULL) {
		free(init_msg);
		init_msg = NULL;
	}

	XPLMUnregisterFlightLoopCallback(raas_exec_cb, NULL);
}

PLUGIN_API int
XPluginStart(char *outName, char *outSig, char *outDesc)
{
	strcpy(outName, XRAAS2_PLUGIN_NAME);
	strcpy(outSig, XRAAS2_PLUGIN_SIG);
	strcpy(outDesc, XRAAS2_PLUGIN_DESC);

	XPLMGetSystemPath(xpdir);
	XPLMGetPrefsPath(xpprefsdir);

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
XPluginReceiveMessage(XPLMPluginID inFromWho, int inMessage, void *inParam)
{
	UNUSED(inFromWho);
	UNUSED(inMessage);
	UNUSED(inParam);
}
