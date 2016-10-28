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

#include <string.h>
#include <stdlib.h>

#include "helpers.h"
#include "log.h"
#include "nd_alert.h"
#include "wav.h"

#include "xraas_cfg.h"

static void
reset_config(xraas_state_t *state)
{
	state->enabled = B_TRUE;
	state->allow_helos = B_FALSE;
	state->min_engines = 2;
	state->min_mtow = 5700;
	state->auto_disable_notify = B_TRUE;
	state->startup_notify = B_TRUE;
	state->use_imperial = B_TRUE;
	state->voice_female = B_TRUE;
	state->voice_volume = 1.0;
	state->use_tts = B_FALSE;
	state->us_runway_numbers = B_FALSE;
	state->min_takeoff_dist = 1000;
	state->min_landing_dist = 800;
	state->min_rotation_dist = 400;
	state->min_rotation_angle = 3;
	state->stop_dist_cutoff = 1600;
	state->min_landing_flap = 0.5;
	state->min_takeoff_flap = 0.1;
	state->max_takeoff_flap = 0.75;
	state->on_rwy_warn_initial = 60;
	state->on_rwy_warn_repeat = 120;
	state->on_rwy_warn_max_n = 3;
	state->too_high_enabled = B_TRUE;
	state->too_fast_enabled = B_TRUE;
	state->gpa_limit_mult = 2;
	state->gpa_limit_max = 8;
	state->alt_setting_enabled = B_TRUE;
	state->qnh_alt_enabled = B_TRUE;
	state->qfe_alt_enabled = B_FALSE;
	state->disable_ext_view = B_TRUE;
	state->override_electrical = B_FALSE;
	state->override_replay = B_FALSE;
	state->speak_units = B_TRUE;
	state->long_land_lim_abs = 610;	/* 2000 feet */
	state->long_land_lim_fract = 0.25;
	state->nd_alerts_enabled = B_TRUE;
	state->nd_alert_filter = ND_ALERT_ROUTINE;
	state->nd_alert_overlay_enabled = B_TRUE;
	state->nd_alert_overlay_force = B_FALSE;
	state->nd_alert_timeout = 7;

	audio_set_shared_ctx(B_FALSE);

	xraas_debug = 0;
}

static void
reset_state(xraas_state_t *state)
{
	memset(state, 0, sizeof (*state));

	state->enabled = B_TRUE;

	my_strlcpy(state->GPWS_priority_dataref,
	    "sim/cockpit2/annunciators/GPWS",
	    sizeof (state->GPWS_priority_dataref));
	my_strlcpy(state->GPWS_inop_dataref,
	    "sim/cockpit/warnings/annunciators/GPWS",
	    sizeof (state->GPWS_inop_dataref));

	state->on_rwy_timer = -1;

	state->TATL_field_elev = TATL_FIELD_ELEV_UNSET;
	state->TATL_transition = -1;
	state->bus_loaded = -1;

	reset_config(state);
}

static void
process_conf(xraas_state_t *state, conf_t *conf)
{
	bool_t shared_ctx = B_FALSE;
	const char *str;

#define	GET_CONF(type, varname) \
	do { \
		/* first try the new name, then the old one */ \
		if (!conf_get_ ## type(conf, #varname, &state->varname)) \
			(void) conf_get_ ## type(conf, "raas_" #varname, \
			    &state->varname); \
	} while (0)
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
	GET_CONF(ll, on_rwy_warn_initial);
	GET_CONF(ll, on_rwy_warn_repeat);
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

	conf_get_i(conf, "debug", &xraas_debug);
	if (conf_get_b(conf, "shared_audio_ctx", &shared_ctx))
		audio_set_shared_ctx(shared_ctx);

	if (conf_get_str(conf, "gpws_prio_dr", &str))
		my_strlcpy(state->GPWS_priority_dataref, str,
		    sizeof (state->GPWS_priority_dataref));
	if (conf_get_str(conf, "gpws_inop_dr", &str))
		my_strlcpy(state->GPWS_inop_dataref, str,
		    sizeof (state->GPWS_inop_dataref));
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

		dbg_log("config", 1, "loading config file: %s", cfgname);
		if ((conf = parse_conf(cfg_f, &errline)) == NULL) {
			log_init_msg(B_TRUE, INIT_ERR_MSG_TIMEOUT,
			    5, "Configuration", "X-RAAS startup error: syntax "
			    "error on line %d in config file:\n%s\n"
			    "Please correct this and then hit Plugins "
			    "-> Admin, Disable & Enable X-RAAS.", errline,
			    cfgname);
			fclose(cfg_f);
			return (B_FALSE);
		}
		process_conf(state, conf);
		free_conf(conf);
		fclose(cfg_f);
	}
	free(cfgname);
	return (B_TRUE);
}

/*
 * Loads the global and aircraft-specific X-RAAS config files.
 */
bool_t
load_configs(xraas_state_t *state, const char *plugindir, const char *acf_path)
{
	reset_state(state);
	/* order is important here, first load the global one */
	if (!load_config(state, plugindir))
		return (B_FALSE);
	if (!load_config(state, acf_path))
		return (B_FALSE);
	return (B_TRUE);
}
