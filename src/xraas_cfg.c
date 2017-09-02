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

#include <string.h>
#include <stdlib.h>

#include <acfutils/helpers.h>
#include <acfutils/log.h>
#include <acfutils/wav.h>

#include "init_msg.h"
#include "dbg_log.h"
#include "nd_alert.h"
#include "snd_sys.h"

#include "xraas_cfg.h"

const char *const monitor_conf_keys[NUM_MONITORS] = {
	"apch_rwy_on_gnd_mon",		/* APCH_RWY_ON_GND_MON */
	"apch_rwy_in_air_mon",		/* APCH_RWY_IN_AIR_MON */
	"apch_rwy_in_air_short_mon",	/* APCH_RWY_IN_AIR_SHORT_MON */
	"on_rwy_lineup_mon",		/* ON_RWY_LINEUP_MON */
	"on_rwy_lineup_short_mon",	/* ON_RWY_LINEUP_SHORT_MON */
	"on_rwy_lineup_flaps_mon",	/* ON_RWY_FLAP_MON */
	"on_rwy_tkoff_short_mon",	/* ON_RWY_TKOFF_SHORT_MON */
	"on_rwy_hold_mon",		/* ON_RWY_HOLDING_MON */
	"twy_tkoff_mon",		/* TWY_TKOFF_MON */
	"dist_rmng_land_mon",		/* DIST_RMNG_LAND_MON */
	"dist_rmng_rto_mon",		/* DIST_RMNG_RTO_MON */
	"twy_land_mon",			/* TWY_LAND_MON */
	"rwy_end_mon",			/* RWY_END_MON */
	"apch_too_high_upper_mon",	/* APCH_TOO_HIGH_UPPER_MON */
	"apch_too_high_lower_mon",	/* APCH_TOO_HIGH_LOWER_MON */
	"apch_too_fast_upper_mon",	/* APCH_TOO_FAST_UPPER_MON */
	"apch_too_fast_lower_mon",	/* APCH_TOO_FAST_LOWER_MON */
	"apch_flaps_upper_mon",		/* APCH_FLAPS_UPPER_MON */
	"apch_flaps_lower_mon",		/* APCH_FLAPS_LOWER_MON */
	"apch_unstable_mon",		/* APCH_UNSTABLE_MON */
	"altm_qne_mon",			/* ALTM_QNE_MON */
	"altm_qnh_mon",			/* ALTM_QNH_MON */
	"altm_qfe_mon",			/* ALTM_QFE_MON */
	"long_land_mon",		/* LONG_LAND_MON */
	"late_rotation_mon"		/* LATE_ROTATION_MON */
};

static void
reset_config(xraas_state_t *state)
{
	memset(&state->config, 0, sizeof (state->config));

	/*
	 * No need to set B_FALSE/zero values here, since the config has
	 * already been bzero'ed.
	 */
	state->config.enabled = B_TRUE;
	state->config.min_engines = 2;
	state->config.min_mtow = 5700;
	state->config.auto_disable_notify = B_TRUE;
	state->config.startup_notify = B_TRUE;
	state->config.use_imperial = B_TRUE;
	state->config.voice_female = B_TRUE;
	state->config.voice_volume = 1.0;
	state->config.min_takeoff_dist = 1000;
	state->config.min_landing_dist = 800;
	state->config.min_rotation_dist = 400;
	state->config.min_rotation_angle = 3;
	state->config.stop_dist_cutoff = 1600;
	state->config.min_landing_flap = 0.5;
	state->config.min_takeoff_flap = 0.1;
	state->config.max_takeoff_flap = 0.75;
	state->config.on_rwy_warn_initial = 60;
	state->config.on_rwy_warn_repeat = 120;
	state->config.on_rwy_warn_max_n = 3;
	state->config.gpa_limit_mult = 2;
	state->config.gpa_limit_max = 8;
	state->config.disable_ext_view = B_TRUE;
	state->config.speak_units = B_TRUE;
	state->config.long_land_lim_abs = 610;	/* 2000 feet */
	state->config.long_land_lim_fract = 0.25;
	state->config.nd_alert_filter = ND_ALERT_ROUTINE;
#if	ACF_TYPE != FF_A320_ACF_TYPE
	state->config.nd_alerts_enabled = B_TRUE;
	state->config.nd_alert_overlay_enabled = B_TRUE;
#endif	/* ACF_TYPE != FF_A320_ACF_TYPE */
	state->config.nd_alert_timeout = 7;
	strlcpy(state->config.nd_alert_overlay_font,
	    ND_alert_overlay_default_font,
	    sizeof (state->config.nd_alert_overlay_font));
	state->config.nd_alert_overlay_font_size =
	    ND_alert_overlay_default_font_size;

	for (int i = 0; i < NUM_MONITORS; i++) {
		state->config.monitors[i] = B_TRUE;
#if	ACF_TYPE == FF_A320_ACF_TYPE
		if (i == ON_RWY_FLAP_MON)
			state->config.monitors[i] = B_FALSE;
#endif	/* ACF_TYPE == FF_A320_ACF_TYPE */
	}

	/* The QFE monitor is the exception - off by default */
	state->config.monitors[ALTM_QFE_MON] = B_FALSE;

	memset(&xraas_debug_config, 0, sizeof (xraas_debug_config));

	strlcpy(state->config.GPWS_priority_dataref,
	    "sim/cockpit2/annunciators/GPWS",
	    sizeof (state->config.GPWS_priority_dataref));
	strlcpy(state->config.GPWS_inop_dataref,
	    "sim/cockpit/warnings/annunciators/GPWS",
	    sizeof (state->config.GPWS_inop_dataref));


#if	ACF_TYPE == FF_A320_ACF_TYPE
	/* Tuned defaults for the A320 */
	state->config.min_landing_dist = 1100;
	state->config.min_takeoff_dist = 1500;
#endif	/* FF_A320_ACF_TYPE */
}

static void
reset_state(xraas_state_t *state)
{
	state->on_rwy_timer = -1;
	state->TATL_field_elev = TATL_FIELD_ELEV_UNSET;
	state->TATL_transition = -1;
	reset_config(state);
}

static void
process_conf(xraas_state_t *state, conf_t *conf)
{
	const char *str;

#define	CONF_GET(type, varname) \
	do { \
		/* first try the new name, then the old one */ \
		if (!conf_get_ ## type(conf, #varname, \
		    &state->config.varname)) \
			(void) conf_get_ ## type(conf, "raas_" #varname, \
			    &state->config.varname); \
	} while (0)
	CONF_GET(b, enabled);
	CONF_GET(b, allow_helos);
	CONF_GET(i, min_engines);
	CONF_GET(i, min_mtow);
	CONF_GET(b, auto_disable_notify);
	CONF_GET(b, startup_notify);
	CONF_GET(b, use_imperial);
	CONF_GET(b, voice_female);
	CONF_GET(d, voice_volume);
#if	!LIN
	CONF_GET(b, use_tts);
#endif	/* !LIN */
	CONF_GET(b, us_runway_numbers);
	CONF_GET(i, min_takeoff_dist);
	CONF_GET(i, min_landing_dist);
	CONF_GET(i, min_rotation_dist);
	CONF_GET(d, min_rotation_angle);
	CONF_GET(i, stop_dist_cutoff);
	CONF_GET(d, min_landing_flap);
	CONF_GET(d, min_takeoff_flap);
	CONF_GET(d, max_takeoff_flap);
	CONF_GET(i, on_rwy_warn_initial);
	CONF_GET(i, on_rwy_warn_repeat);
	CONF_GET(i, on_rwy_warn_max_n);
	CONF_GET(d, gpa_limit_mult);
	CONF_GET(d, gpa_limit_max);
	CONF_GET(b, disable_ext_view);
	CONF_GET(b, override_electrical);
	CONF_GET(b, override_replay);
	CONF_GET(b, speak_units);
	CONF_GET(b, say_deep_landing);
	CONF_GET(i, long_land_lim_abs);
	CONF_GET(d, long_land_lim_fract);
	CONF_GET(b, nd_alerts_enabled);
	CONF_GET(i, nd_alert_filter);
	CONF_GET(b, nd_alert_overlay_enabled);
	CONF_GET(b, nd_alert_overlay_force);
	CONF_GET(i, nd_alert_timeout);
	CONF_GET(b, debug_graphical);
	if (conf_get_str(conf, "nd_alert_overlay_font", &str)) {
		strlcpy(state->config.nd_alert_overlay_font, str,
		    sizeof (state->config.nd_alert_overlay_font));
	}
	CONF_GET(i, nd_alert_overlay_font_size);
#undef	CONF_GET

	for (int i = 0; i < NUM_MONITORS; i++) {
#if	ACF_TYPE == FF_A320_ACF_TYPE
		if (i == ON_RWY_FLAP_MON)
			continue;
#endif	/* ACF_TYPE == FF_A320_ACF_TYPE */
		if (conf_get_b(conf, monitor_conf_keys[i],
		    &state->config.monitors[i])) {
			int l = strlen(monitor_conf_keys[i]) + 6;
			char buf[l];
			snprintf(buf, l, "raas_%s", monitor_conf_keys[i]);
			(void) conf_get_b(conf, buf,
			    &state->config.monitors[i]);
		}
	}

	if (conf_get_b(conf, "openal_shared", &state->config.openal_shared))
		snd_sys_set_shared(state->config.openal_shared);
	else
		snd_sys_set_shared(B_FALSE);

	if (conf_get_str(conf, "gpws_prio_dr", &str))
		strlcpy(state->config.GPWS_priority_dataref, str,
		    sizeof (state->config.GPWS_priority_dataref));
	if (conf_get_str(conf, "gpws_inop_dr", &str))
		strlcpy(state->config.GPWS_inop_dataref, str,
		    sizeof (state->config.GPWS_inop_dataref));

#define	CONF_GET_DEBUG(value) \
	conf_get_i(conf, "debug_" #value, &xraas_debug_config.value)
	CONF_GET_DEBUG(all);
	CONF_GET_DEBUG(altimeter);
	CONF_GET_DEBUG(ann_state);
	CONF_GET_DEBUG(apch_cfg_chk);
	CONF_GET_DEBUG(config);
	CONF_GET_DEBUG(dbg_gui);
	CONF_GET_DEBUG(flt_state);
	CONF_GET_DEBUG(fs);
	CONF_GET_DEBUG(nd_alert);
	CONF_GET_DEBUG(pwr_state);
	CONF_GET_DEBUG(rwy_key);
	CONF_GET_DEBUG(snd);
	CONF_GET_DEBUG(startup);
	CONF_GET_DEBUG(tile);
	CONF_GET_DEBUG(wav);
	CONF_GET_DEBUG(adc);
	CONF_GET_DEBUG(ff_a320);
#undef	CONF_GET_DEBUG
}

/*
 * Loads a config file at path `cfgname' if it exists. If the file doesn't
 * exist, this function just returns true. If the config file contains errors,
 * this function shows an init message and returns false, otherwise it returns
 * true.
 */
static bool_t
load_config(xraas_state_t *state, const char *dirname)
{
	char *cfgname = mkpathname(dirname, "X-RAAS.cfg", NULL);
	FILE *cfg_f = fopen(cfgname, "r");

	if (cfg_f != NULL) {
		conf_t *conf;
		int errline;

		dbg_log(config, 1, "loading config file: %s", cfgname);
		if ((conf = conf_read(cfg_f, &errline)) == NULL) {
			log_init_msg(B_TRUE, INIT_ERR_MSG_TIMEOUT, "5",
			    "Configuration", "X-RAAS startup error: syntax "
			    "error on line %d in config file:\n%s\n"
			    "Please correct this and then hit Plugins "
			    "-> Admin, Disable & Enable X-RAAS.", errline,
			    cfgname);
			fclose(cfg_f);
			return (B_FALSE);
		}
		process_conf(state, conf);
		conf_free(conf);
		fclose(cfg_f);
	}
	free(cfgname);
	return (B_TRUE);
}

/*
 * Loads the global and aircraft-specific X-RAAS config files.
 */
bool_t
load_configs(xraas_state_t *state)
{
	reset_state(state);
	/* order is important here, first load the global one */
#ifndef	XRAAS_IS_EMBEDDED
	if (!load_config(state, xraas_prefsdir))
		return (B_FALSE);
#endif	/* !XRAAS_IS_EMBEDDED */
	if (!load_config(state, xraas_acf_dirpath))
		return (B_FALSE);
	if (!load_config(state, xraas_acf_livpath))
		return (B_FALSE);
	return (B_TRUE);
}
