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
 * Copyright 2017 Saso Kiselkov. All rights reserved.
 */

#include <stdlib.h>
#include <string.h>
#include <XPLMDataAccess.h>
#include <XPLMPlugin.h>

#include "airdata.h"
#include "assert.h"

/* A320 interface */
#include <FF_A320/SharedValue.h>

#include "xraas2.h"

enum {
	XP_DEFAULT_INTERFACE,
	FF_A320_INTERFACE,
	INVALID_INTERFACE
};

typedef bool_t (*adc_op2_t)(double *a, double *b);
typedef bool_t (*adc_op3_t)(double *a, double *b, double *c);

typedef struct {
	adc_op3_t	get_alt;
	adc_op3_t	get_pos;
	adc_op2_t	get_att;
	adc_op2_t	get_spd;
} adc_ops_t;

static bool_t xp_get_alt(double *baro_alt, double *baro_set, double *rad_alt);
static bool_t xp_get_pos(double *lat, double *lon, double *elev);
static bool_t xp_get_att(double *hdg, double *pitch);
static bool_t xp_get_spd(double *cas, double *gs);

static bool_t ff_a320_intf_init(void);
static void ff_a320_intf_fini(void);
static void ff_a320_intf_update(double step, void *tag);
static bool_t ff_a320_get_alt(double *baro_alt, double *baro_set,
    double *rad_alt);
static bool_t ff_a320_get_pos(double *lat, double *lon, double *elev);
static bool_t ff_a320_get_att(double *hdg, double *pitch);
static bool_t ff_a320_get_spd(double *cas, double *gs);


static int intf_type = INVALID_INTERFACE;

static adc_t adc_l;
const adc_t *adc = &adc_l;

static drs_t drs_l;
const drs_t *drs = &drs_l;

static adc_ops_t adc_ops = { NULL, NULL, NULL, NULL };


static SharedValuesInterface ff_a320_intf = {
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL
};
static struct {
	int powered_id;
	int fault_id;
	int fault_ex_id;
	int inhibit_id;
	int inhibit_ex_id;
	int inhibit_flaps_id;
	int alert_id;

	int baro_alt_id;
	int baro_set_id;
	int rad_alt_id;

	int lat_id;
	int lon_id;
	int elev_id;

	int hdg_id;

	int fpa_id;
	int aoa_value_id;
	int aoa_valid_id;

	int cas_id;
	int gs_id;
} ff_a320_ids;

static struct {
	int powered;
	int alerting;
	int suppressed;
	int inhibit;
	int inhibit_ex;
	int inhibit_flaps;
} ff_a320_status;

static bool_t ff_a320_sys_ok = B_TRUE;


static XPLMDataRef
dr_get(const char *drname)
{
	XPLMDataRef dr = XPLMFindDataRef(drname);
	VERIFY(dr != NULL);
	return (dr);
}

bool_t
adc_init(void)
{
	memset(&adc_l, 0, sizeof (adc_l));
	memset(&drs_l, 0, sizeof (drs_l));

	drs_l.baro_alt = dr_get("sim/flightmodel/misc/h_ind");
	drs_l.rad_alt = dr_get("sim/cockpit2/gauges/indicators/"
	    "radio_altimeter_height_ft_pilot");
	drs_l.cas = dr_get("sim/flightmodel/position/indicated_airspeed");
	drs_l.gs = dr_get("sim/flightmodel/position/groundspeed");
	drs_l.lat = dr_get("sim/flightmodel/position/latitude");
	drs_l.lon = dr_get("sim/flightmodel/position/longitude");
	drs_l.elev = dr_get("sim/flightmodel/position/elevation");
	drs_l.hdg = dr_get("sim/flightmodel/position/true_psi");
	drs_l.pitch = dr_get("sim/flightmodel/position/true_theta");
	drs_l.baro_set = dr_get("sim/cockpit/misc/barometer_setting");

	drs_l.baro_sl = dr_get("sim/weather/barometer_sealevel_inhg");

	drs_l.nw_offset = dr_get("sim/flightmodel/parts/tire_z_no_deflection");
	drs_l.flaprqst = dr_get("sim/flightmodel/controls/flaprqst");
	drs_l.gear = dr_get("sim/aircraft/parts/acf_gear_deploy");
	drs_l.gear_type = dr_get("sim/aircraft/parts/acf_gear_type");

	/*
	 * Aircraft.Gears.NLG.GearPosition
	 * Aircraft.Gears.MLGL.GearPosition
	 * Aircraft.Gears.MLGR.GearPosition
	 */

	drs_l.view_is_ext = dr_get("sim/graphics/view/view_is_external");

	drs_l.bus_volt = dr_get("sim/cockpit2/electrical/bus_volts");

	drs_l.avionics_on = dr_get("sim/cockpit/electrical/avionics_on");
	drs_l.num_engines = dr_get("sim/aircraft/engine/acf_num_engines");
	drs_l.mtow = dr_get("sim/aircraft/weight/acf_m_max");
	drs_l.ICAO = dr_get("sim/aircraft/view/acf_ICAO");
	drs_l.author = dr_get("sim/aircraft/view/acf_author");

	drs_l.gpws_prio = dr_get(xraas_state->config.GPWS_priority_dataref);
	drs_l.gpws_inop = dr_get(xraas_state->config.GPWS_inop_dataref);

	drs_l.replay_mode = dr_get("sim/operation/prefs/replay_mode");

	if (ff_a320_intf_init()) {
		intf_type = FF_A320_INTERFACE;
		adc_ops.get_alt = ff_a320_get_alt;
		adc_ops.get_pos = ff_a320_get_pos;
		adc_ops.get_att = ff_a320_get_att;
		adc_ops.get_spd = ff_a320_get_spd;
	} else {
		intf_type = XP_DEFAULT_INTERFACE;
		adc_ops.get_alt = xp_get_alt;
		adc_ops.get_pos = xp_get_pos;
		adc_ops.get_att = xp_get_att;
		adc_ops.get_spd = xp_get_spd;
	}

	return (B_TRUE);
}

void
adc_fini(void)
{
	memset(&adc_l, 0, sizeof (adc_l));
	memset(&drs_l, 0, sizeof (drs_l));

	if (intf_type == FF_A320_INTERFACE)
		ff_a320_intf_fini();

	intf_type = INVALID_INTERFACE;
}

bool_t
adc_collect(void)
{
	if (!adc_ops.get_alt(&adc_l.baro_alt, &adc_l.baro_set,
	    &adc_l.rad_alt) ||
	    !adc_ops.get_pos(&adc_l.lat, &adc_l.lon, &adc_l.elev) ||
	    !adc_ops.get_att(&adc_l.hdg, &adc_l.pitch) ||
	    !adc_ops.get_spd(&adc_l.cas, &adc_l.gs))
		return (B_FALSE);

	adc_l.baro_sl = XPLMGetDataf(drs_l.baro_sl);

	XPLMGetDatavf(drs_l.nw_offset, &adc_l.nw_offset, 0, 1);
	adc_l.flaprqst = XPLMGetDataf(drs_l.flaprqst);

	adc_l.n_gear = XPLMGetDatavf(drs_l.gear, adc_l.gear, 0, NUM_GEAR);
	VERIFY(adc_l.n_gear <= NUM_GEAR);
	XPLMGetDatavi(drs_l.gear_type, adc_l.gear_type, 0, adc_l.n_gear);

	return (B_TRUE);
}

static bool_t
xp_get_alt(double *baro_alt, double *baro_set, double *rad_alt)
{
	*baro_alt = XPLMGetDataf(drs_l.baro_alt);
	*baro_set = XPLMGetDataf(drs_l.baro_set);
	*rad_alt = XPLMGetDataf(drs_l.rad_alt);
	return (B_TRUE);
}

static bool_t
xp_get_pos(double *lat, double *lon, double *elev)
{
	*lat = XPLMGetDatad(drs_l.lat);
	*lon = XPLMGetDatad(drs_l.lon);
	*elev = XPLMGetDatad(drs_l.elev);
	return (B_TRUE);
}

static bool_t
xp_get_att(double *hdg, double *pitch)
{
	*hdg = XPLMGetDataf(drs_l.hdg);
	*pitch = XPLMGetDataf(drs_l.pitch);
	return (B_TRUE);
}

static bool_t
xp_get_spd(double *cas, double *gs)
{
	*cas = XPLMGetDataf(drs_l.cas);
	*gs = XPLMGetDataf(drs_l.gs);
	return (B_TRUE);
}



static bool_t
ff_a320_intf_init(void)
{
	XPLMPluginID plugin;
	char author[64];

	memset(author, 0, sizeof (author));
	XPLMGetDatab(drs->author, author, 0, sizeof (author) - 1);
	if (strcmp(author, "FlightFactor") != 0) {
		logMsg("ff_a320_intf init fail: not FF");
		return (B_FALSE);
	}

	memset(&ff_a320_intf, 0, sizeof (ff_a320_intf));

	plugin = XPLMFindPluginBySignature(XPLM_FF_SIGNATURE);
	if (plugin == XPLM_NO_PLUGIN_ID) {
		logMsg("ff_a320_intf init fail: plugin not found");
		return (B_FALSE);
	}
	XPLMSendMessageToPlugin(plugin, XPLM_FF_MSG_GET_SHARED_INTERFACE,
	    &ff_a320_intf);
	if (ff_a320_intf.DataAddUpdate == NULL) {
		logMsg("ff_a320_intf init fail: func vector empty");
		return (B_FALSE);
	}

	ff_a320_intf.DataAddUpdate(ff_a320_intf_update, NULL);

	memset(&ff_a320_ids, 0xff, sizeof (ff_a320_ids));

	return (B_TRUE);
}

static inline int
ff_a320_geti(int id)
{
	int val;
	ff_a320_intf.ValueGet(id, &val);
	return (val);
}

static inline double
ff_a320_getd(int id)
{
	double val;
	ff_a320_intf.ValueGet(id, &val);
	return (val);
}

static void
ff_a320_intf_update(double step, void *tag)
{
	UNUSED(step);
	UNUSED(tag);

	VERIFY(ff_a320_intf.ValueIdByName != NULL);
	VERIFY(ff_a320_intf.ValueGet != NULL);

	if (ff_a320_ids.baro_alt_id == -1) {
		ff_a320_ids.powered_id = ff_a320_intf.ValueIdByName(
		    "Aircraft.Navigation.GPWC.Powered");
		ff_a320_ids.fault_id = ff_a320_intf.ValueIdByName(
		    "Aircraft.Navigation.GPWC.Fault");
		ff_a320_ids.fault_ex_id = ff_a320_intf.ValueIdByName(
		    "Aircraft.Navigation.GPWC.FaultEx");
		ff_a320_ids.inhibit_id = ff_a320_intf.ValueIdByName(
		    "Aircraft.Navigation.GPWC.Inhibit");
		ff_a320_ids.inhibit_ex_id = ff_a320_intf.ValueIdByName(
		    "Aircraft.Navigation.GPWC.InhibitEx");
		ff_a320_ids.inhibit_flaps_id = ff_a320_intf.ValueIdByName(
		    "Aircraft.Navigation.GPWC.InhibitFlaps");
		ff_a320_ids.alert_id = ff_a320_intf.ValueIdByName(
		    "Aircraft.Navigation.GPWC.Alert");

		ff_a320_ids.baro_alt_id = ff_a320_intf.ValueIdByName(
		    "Aircraft.Navigation.GPWC.AltitudeBaro");
		ff_a320_ids.baro_set_id = ff_a320_intf.ValueIdByName(
		    "Aircraft.Navigation.GPWC.Altitude");
		ff_a320_ids.rad_alt_id = ff_a320_intf.ValueIdByName(
		    "Aircraft.Navigation.GPWC.Height");

		ff_a320_ids.lat_id = ff_a320_intf.ValueIdByName(
		    "Aircraft.Navigation.GPWC.PositionLat");
		ff_a320_ids.lon_id = ff_a320_intf.ValueIdByName(
		    "Aircraft.Navigation.GPWC.PositionLon");
		ff_a320_ids.elev_id = ff_a320_intf.ValueIdByName(
		    "Aircraft.Navigation.GPWC.Elevation");
		ff_a320_ids.hdg_id = ff_a320_intf.ValueIdByName(
		    "Aircraft.Navigation.GPWC.Heading");

		ff_a320_ids.fpa_id = ff_a320_intf.ValueIdByName(
		    "Aircraft.Navigation.GPWC.Path");
		ff_a320_ids.aoa_value_id = ff_a320_intf.ValueIdByName(
		    "Aircraft.Navigation.ADIRS.Sensors.AOA1.Value");
		ff_a320_ids.aoa_valid_id = ff_a320_intf.ValueIdByName(
		    "Aircraft.Navigation.ADIRS.Sensors.AOA1.Valid");

		ff_a320_ids.cas_id = ff_a320_intf.ValueIdByName(
		    "Aircraft.Navigation.GPWC.AirSpeed");
		ff_a320_ids.gs_id = ff_a320_intf.ValueIdByName(
		    "Aircraft.Navigation.GPWC.Speed");
	}

	if (ff_a320_geti(ff_a320_ids.fault_id) != 0 ||
	    ff_a320_geti(ff_a320_ids.fault_ex_id) != 0) {
		ff_a320_sys_ok = B_FALSE;
		return;
	}

	ff_a320_status.powered = ff_a320_geti(ff_a320_ids.powered_id);
	ff_a320_status.inhibit = ff_a320_geti(ff_a320_ids.inhibit_id);
	ff_a320_status.inhibit_ex = ff_a320_geti(ff_a320_ids.inhibit_ex_id);
	ff_a320_status.inhibit_flaps = ff_a320_geti(
	    ff_a320_ids.inhibit_flaps_id);
	ff_a320_status.alerting = ff_a320_geti(ff_a320_ids.alert_id);


	adc_l.baro_alt = ff_a320_geti(ff_a320_ids.baro_alt_id);
	adc_l.baro_set = 1013.25;
//	adc_l.baro_set = ff_a320_getd(ff_a320_ids.baro_set_id);
	adc_l.rad_alt = ff_a320_geti(ff_a320_ids.rad_alt_id);

	adc_l.lat = ff_a320_geti(ff_a320_ids.lat_id);
	adc_l.lon = ff_a320_geti(ff_a320_ids.lon_id);
	adc_l.elev = ff_a320_geti(ff_a320_ids.elev_id);

	adc_l.hdg = ff_a320_getd(ff_a320_ids.hdg_id);
	adc_l.pitch = XPLMGetDataf(drs_l.pitch);

	// ff_a320_intf.ValueGet(ff_a320_ids.pitch_id, &adc_l.pitch);

	adc_l.cas = ff_a320_getd(ff_a320_ids.cas_id);
	adc_l.gs = ff_a320_getd(ff_a320_ids.gs_id);

	ff_a320_sys_ok = B_TRUE;
}

static void
ff_a320_intf_fini(void)
{
	if (ff_a320_intf.DataDelUpdate != NULL)
		ff_a320_intf.DataDelUpdate(ff_a320_intf_update, NULL);
	memset(&ff_a320_intf, 0, sizeof (ff_a320_intf));
}

static bool_t
ff_a320_get_alt(double *baro_alt, double *baro_set, double *rad_alt)
{
	VERIFY(intf_type == FF_A320_INTERFACE);
	UNUSED(baro_alt);
	UNUSED(baro_set);
	UNUSED(rad_alt);
	return (ff_a320_sys_ok);
}

static bool_t
ff_a320_get_pos(double *lat, double *lon, double *elev)
{
	VERIFY(intf_type == FF_A320_INTERFACE);
	UNUSED(lat);
	UNUSED(lon);
	UNUSED(elev);
	return (ff_a320_sys_ok);
}

static bool_t
ff_a320_get_att(double *hdg, double *pitch)
{
	VERIFY(intf_type == FF_A320_INTERFACE);
	UNUSED(hdg);
	UNUSED(pitch);
	return (ff_a320_sys_ok);
}

static bool_t
ff_a320_get_spd(double *cas, double *gs)
{
	VERIFY(intf_type == FF_A320_INTERFACE);
	UNUSED(cas);
	UNUSED(gs);
	return (ff_a320_sys_ok);
}

bool_t
ff_a320_is_loaded(void)
{
	return (intf_type == FF_A320_INTERFACE);
}

bool_t
ff_a320_powered(void)
{
	return (ff_a320_status.powered);
}

bool_t
ff_a320_suppressed(void)
{
	return (ff_a320_status.suppressed);
}

bool_t
ff_a320_alerting(void)
{
	return (ff_a320_status.alerting);
}

bool_t
ff_a320_inhibit(void)
{
	return (ff_a320_status.inhibit);
}

bool_t
ff_a320_inhibit_ex(void)
{
	return (ff_a320_status.inhibit_ex);
}

bool_t
ff_a320_inhibit_flaps(void)
{
	return (ff_a320_status.inhibit_flaps);
}
