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

#ifndef	_XRAAS2_H_
#define	_XRAAS2_H_

#include "airportdb.h"
#include "avl.h"
#include "geom.h"
#include "list.h"
#include "types.h"

#ifdef	__cplusplus
extern "C" {
#endif

#define	TATL_FIELD_ELEV_UNSET		-1000000
#define	RWY_PROXIMITY_TIME_FACT		2		/* seconds */

typedef enum TATL_state_e {
	TATL_STATE_ALT,
	TATL_STATE_FL
} TATL_state_t;

typedef struct xraas_state {
	bool_t		enabled;
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
	int		min_takeoff_dist;		/* meters */
	int		min_landing_dist;		/* meters */
	int		min_rotation_dist;		/* meters */
	double		min_rotation_angle;		/* degrees */
	int		stop_dist_cutoff;		/* meters */
	bool_t		voice_female;
	double		voice_volume;
	bool_t		disable_ext_view;
	double		min_landing_flap;		/* ratio, 0-1 */
	double		min_takeoff_flap;		/* ratio, 0-1 */
	double		max_takeoff_flap;		/* ratio, 0-1 */

	bool_t		nd_alerts_enabled;
	int		nd_alert_filter;		/* nd_alert_level_t */
	bool_t		nd_alert_overlay_enabled;
	bool_t		nd_alert_overlay_force;
	int		nd_alert_timeout;		/* seconds */

	int64_t		on_rwy_warn_initial;		/* seconds */
	int64_t		on_rwy_warn_repeat;		/* seconds */
	int		on_rwy_warn_max_n;		/* count */

	bool_t		too_high_enabled;
	bool_t		too_fast_enabled;
	double		gpa_limit_mult;			/* multiplier */
	double		gpa_limit_max;			/* degrees */

	char		GPWS_priority_dataref[128];
	char		GPWS_inop_dataref[128];

	bool_t		alt_setting_enabled;
	bool_t		qnh_alt_enabled;
	bool_t		qfe_alt_enabled;

	bool_t		us_runway_numbers;

	int		long_land_lim_abs;		/* meters */
	double		long_land_lim_fract;		/* fraction, 0-1 */

	bool_t		debug_graphical;
	bool_t		debug;

	avl_tree_t	on_rwy_ann;
	avl_tree_t	apch_rwy_ann;
	bool_t		apch_rwys_ann;		/* when multiple met criteria */
	avl_tree_t	air_apch_rwy_ann;
	bool_t		air_apch_rwys_ann;	/* when multiple met criteria */
	bool_t		air_apch_short_rwy_ann;
	avl_tree_t	air_apch_flap1_ann;
	avl_tree_t	air_apch_flap2_ann;
	avl_tree_t	air_apch_flap3_ann;
	avl_tree_t	air_apch_gpa1_ann;
	avl_tree_t	air_apch_gpa2_ann;
	avl_tree_t	air_apch_gpa3_ann;
	avl_tree_t	air_apch_spd1_ann;
	avl_tree_t	air_apch_spd2_ann;
	avl_tree_t	air_apch_spd3_ann;
	bool_t		on_twy_ann;
	bool_t		long_landing_ann;
	bool_t		short_rwy_takeoff_chk;
	int64_t		on_rwy_timer;
	int		on_rwy_warnings;
	bool_t		off_rwy_ann;
	char		rejected_takeoff[8];

	avl_tree_t	accel_stop_max_spd;
	int		accel_stop_ann_initial;

	bool_t		departed;
	bool_t		arriving;
	bool_t		landing;
	int		TA;
	int		TL;
	int		TATL_field_elev;
	TATL_state_t	TATL_state;
	int64_t		TATL_transition;
	char		TATL_source[8];

	bool_t		view_is_ext;
	int		bus_loaded;
	int		last_elev;
	double		last_gs;			/* in m/s */
	uint64_t	last_units_call;

	list_t		*cur_arpts;
	airportdb_t	airportdb;
	int64_t		last_airport_reload;

	char		*init_msg;
	int64_t		init_msg_end;
} xraas_state_t;

#define	INIT_ERR_MSG_TIMEOUT		25		/* seconds */
void log_init_msg(bool_t display, int timeout, int man_sect_number,
    const char *man_sect_name, const char *fmt, ...) PRINTF_ATTR(5);

bool_t xraas_is_on(void);
bool_t view_is_external(void);
bool_t GPWS_has_priority(void);
vect2_t acf_vel_vector(double time_fact);

const airport_t *find_nearest_curarpt(void);

#ifdef	__cplusplus
}
#endif

#endif	/* _XRAAS2_H_ */
