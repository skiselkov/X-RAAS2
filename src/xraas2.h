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

#ifndef	_XRAAS2_H_
#define	_XRAAS2_H_

#include "airportdb.h"
#include "avl.h"
#include "geom.h"
#include "list.h"
#include "rwy_key_tbl.h"
#include "types.h"

#ifdef	__cplusplus
extern "C" {
#endif

#define	XRAAS2_VERSION			"2.1"
#define	TATL_FIELD_ELEV_UNSET		-1000000
#define	RWY_PROXIMITY_TIME_FACT		2		/* seconds */

typedef enum TATL_state_e {
	TATL_STATE_ALT,
	TATL_STATE_FL
} TATL_state_t;

typedef struct {
	bool_t		enabled;
	const char	*name;
	const char	*conf_key;
} monitor_t;

enum {
	APCH_RWY_ON_GND_MON = 0,
	APCH_RWY_IN_AIR_MON,
	APCH_RWY_IN_AIR_SHORT_MON,
	ON_RWY_LINEUP_MON,
	ON_RWY_LINEUP_SHORT_MON,
	ON_RWY_FLAP_MON,
	ON_RWY_TKOFF_SHORT_MON,
	ON_RWY_HOLDING_MON,
	TWY_TKOFF_MON,
	DIST_RMNG_LAND_MON,
	DIST_RMNG_RTO_MON,
	TWY_LAND_MON,
	RWY_END_MON,
	APCH_TOO_HIGH_UPPER_MON,
	APCH_TOO_HIGH_LOWER_MON,
	APCH_TOO_FAST_UPPER_MON,
	APCH_TOO_FAST_LOWER_MON,
	APCH_FLAPS_UPPER_MON,
	APCH_FLAPS_LOWER_MON,
	APCH_UNSTABLE_MON,
	ALTM_QNE_MON,
	ALTM_QNH_MON,
	ALTM_QFE_MON,
	LONG_LAND_MON,
	LATE_ROTATION_MON,
	NUM_MONITORS
};

typedef struct xraas_state {
	struct {
		bool_t	enabled;

		int		min_engines;		/* count */
		int		min_mtow;		/* kg */
		bool_t		allow_helos;
		bool_t		auto_disable_notify;
		bool_t		startup_notify;
		bool_t		override_electrical;
		bool_t		override_replay;
		bool_t		use_tts;
		bool_t		speak_units;
		bool_t		use_imperial;

		/* monitor enablings */
		bool_t		monitors[NUM_MONITORS];

		int		min_takeoff_dist;	/* meters */
		int		min_landing_dist;	/* meters */
		int		min_rotation_dist;	/* meters */
		double		min_rotation_angle;	/* degrees */
		int		stop_dist_cutoff;	/* meters */
		bool_t		voice_female;
		double		voice_volume;
		bool_t		disable_ext_view;

		double		min_landing_flap;	/* ratio, 0-1 */
		double		min_takeoff_flap;	/* ratio, 0-1 */
		double		max_takeoff_flap;	/* ratio, 0-1 */

		bool_t		nd_alerts_enabled;
		int		nd_alert_filter;	/* nd_alert_level_t */
		bool_t		nd_alert_overlay_enabled;
		bool_t		nd_alert_overlay_force;
		int		nd_alert_timeout;		/* seconds */
		char		nd_alert_overlay_font[MAX_PATH]; /* file name */
		int		nd_alert_overlay_font_size; /* pixel value */

		int		on_rwy_warn_initial;	/* seconds */
		int		on_rwy_warn_repeat;	/* seconds */
		int		on_rwy_warn_max_n;	/* count */

		double		gpa_limit_mult;		/* multiplier */
		double		gpa_limit_max;		/* degrees */

		char		GPWS_priority_dataref[128];
		char		GPWS_inop_dataref[128];

		bool_t		us_runway_numbers;

		bool_t		say_deep_landing;	/* Say 'DEEP landing' */
		int		long_land_lim_abs;	/* meters */
		double		long_land_lim_fract;	/* fraction, 0-1 */

		bool_t		openal_shared;
		bool_t		debug_graphical;
		bool_t		debug;
	} config;

	bool_t		input_faulted;	/* when adc_collect failed */

	rwy_key_tbl_t	on_rwy_ann;
	rwy_key_tbl_t	apch_rwy_ann;
	bool_t		apch_rwys_ann;		/* when multiple met criteria */
	rwy_key_tbl_t	air_apch_rwy_ann;
	bool_t		air_apch_rwys_ann;	/* when multiple met criteria */
	bool_t		air_apch_short_rwy_ann;
	rwy_key_tbl_t	air_apch_flap1_ann;
	rwy_key_tbl_t	air_apch_flap2_ann;
	rwy_key_tbl_t	air_apch_flap3_ann;
	rwy_key_tbl_t	air_apch_gpa1_ann;
	rwy_key_tbl_t	air_apch_gpa2_ann;
	rwy_key_tbl_t	air_apch_gpa3_ann;
	rwy_key_tbl_t	air_apch_spd1_ann;
	rwy_key_tbl_t	air_apch_spd2_ann;
	rwy_key_tbl_t	air_apch_spd3_ann;
	bool_t		on_twy_ann;
	bool_t		long_landing_ann;
	bool_t		short_rwy_takeoff_chk;
	int64_t		on_rwy_timer;
	int		on_rwy_warnings;
	bool_t		off_rwy_ann;
	bool_t		unstable_ann;
	struct {
		char	icao[5];
		char	rwy_id[8];
	} rejected_takeoff;

	rwy_key_tbl_t	accel_stop_max_spd;
	int		accel_stop_ann_initial;

	bool_t		departed;
	bool_t		arriving;
	bool_t		landing;
	int		TATL_field_elev;
	TATL_state_t	TATL_state;
	int64_t		TATL_transition;
	char		TATL_source[8];

	bool_t		view_is_ext;
	double		last_elev;			/* in meters */
	double		last_gs;			/* in m/s */
	uint64_t	last_units_call;		/* microclock time */

	list_t		*cur_arpts;
	airportdb_t	airportdb;
	int64_t		last_airport_reload;
} xraas_state_t;

extern bool_t xraas_inited;
extern const xraas_state_t *xraas_state;
extern const char *xraas_xpdir;
extern const char *xraas_prefsdir;
extern const char *xraas_acf_dirpath;
extern const char *xraas_acf_livpath;
extern const char *xraas_plugindir;

void xraas_init(void);
void xraas_fini(void);
bool_t xraas_is_on(void);
bool_t view_is_external(void);
bool_t GPWS_has_priority(void);
vect2_t acf_vel_vector(double time_fact);

const airport_t *find_nearest_curarpt(void);

#ifdef	__cplusplus
}
#endif

#endif	/* _XRAAS2_H_ */
