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
#include "geom.h"
#include "log.h"
#include "perf.h"

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
static void ff_a320_update(double step, void *tag);
static bool_t ff_a320_get_alt(double *baro_alt, double *baro_set,
    double *rad_alt);
static bool_t ff_a320_get_pos(double *lat, double *lon, double *elev);
static bool_t ff_a320_get_att(double *hdg, double *pitch);
static bool_t ff_a320_get_spd(double *cas, double *gs);
static const char *ff_a320_type2str(unsigned int t);


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
	int powered_id;			/* flag 0/1 */
	int fault_id;			/* flag 0/1 */
	int fault_ex_id;		/* flag 0/1 */
	int inhibit_id;			/* flag 0/1 */
	int inhibit_ex_id;		/* flag 0/1 */
	int inhibit_flaps_id;		/* flag 0/1 */
	int alert_id;			/* flag 0/1 */

	int baro_alt_id;		/* meters */
	int baro_raw_id;		/* meters */
	int rad_alt_id;			/* meters */

	int lat_id;			/* radians from equator */
	int lon_id;			/* radians from 0th meridian */
	int elev_id;			/* meters AMSL */

	int hdg_id;			/* degrees true -180..+180 */

	int fpa_id;			/* degrees nose up */
	int aoa_value_id;		/* degrees down */
	int aoa_valid_id;		/* flag 0/1 */

	int cas_id;			/* meters/second */
	int gs_id;			/* meters/second */
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
	dbg_log(adc, 1, "init");

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
	dbg_log(adc, 1, "fini");

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
	    !adc_ops.get_spd(&adc_l.cas, &adc_l.gs)) {
		dbg_log(adc, 1, "input fault");
		return (B_FALSE);
	}

	adc_l.baro_sl = XPLMGetDataf(drs_l.baro_sl);

	XPLMGetDatavf(drs_l.nw_offset, &adc_l.nw_offset, 0, 1);
	adc_l.flaprqst = XPLMGetDataf(drs_l.flaprqst);

	adc_l.n_gear = XPLMGetDatavf(drs_l.gear, adc_l.gear, 0, NUM_GEAR);
	VERIFY(adc_l.n_gear <= NUM_GEAR);
	XPLMGetDatavi(drs_l.gear_type, adc_l.gear_type, 0, adc_l.n_gear);

	dbg_log(adc, 2, "collect: A:%05.0f/%02.2f/%04.0f  "
	    "P:%02.04f/%03.04f/%05.0f  T:%03.0f/%02.1f  S:%03.0f/%03.0f",
	    adc_l.baro_alt, adc_l.baro_set, adc_l.rad_alt,
	    adc_l.lat, adc_l.lon, adc_l.elev,
	    adc_l.hdg, adc_l.pitch, adc_l.cas, adc_l.gs);

	return (B_TRUE);
}

static bool_t
xp_get_alt(double *baro_alt, double *baro_set, double *rad_alt)
{
	*baro_alt = XPLMGetDataf(drs_l.baro_alt);
	*baro_set = XPLMGetDataf(drs_l.baro_set);
	*rad_alt = XPLMGetDataf(drs_l.rad_alt);
	dbg_log(adc, 2, "xp alt: %.0f set: %.02f rad: %.0f", *baro_alt,
	    *baro_set, *rad_alt);
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
		dbg_log(ff_a320, 1, "init fail: not FF");
		return (B_FALSE);
	}

	memset(&ff_a320_intf, 0, sizeof (ff_a320_intf));

	plugin = XPLMFindPluginBySignature(XPLM_FF_SIGNATURE);
	if (plugin == XPLM_NO_PLUGIN_ID) {
		dbg_log(ff_a320, 1, "init fail: plugin not found");
		return (B_FALSE);
	}
	XPLMSendMessageToPlugin(plugin, XPLM_FF_MSG_GET_SHARED_INTERFACE,
	    &ff_a320_intf);
	if (ff_a320_intf.DataAddUpdate == NULL) {
		dbg_log(ff_a320, 1, "init fail: func vector empty");
		return (B_FALSE);
	}

	ff_a320_intf.DataAddUpdate(ff_a320_update, NULL);

	memset(&ff_a320_ids, 0xff, sizeof (ff_a320_ids));

	dbg_log(ff_a320, 1, "init successful");

	return (B_TRUE);
}

static inline int32_t
ff_a320_gets32(int id)
{
	int val;
	unsigned int type = ff_a320_intf.ValueType(id);

	if (type < Value_Type_sint8 || type > Value_Type_uint32) {
		dbg_log(ff_a320, 0, "get error: %s is of type %s",
		    ff_a320_intf.ValueName(id), ff_a320_type2str(type));
		return (-1);
	}
	ff_a320_intf.ValueGet(id, &val);

	return (val);
}

static inline float
ff_a320_getf32(int id)
{
	float val;
	unsigned int type = ff_a320_intf.ValueType(id);

	if (type != Value_Type_float32) {
		dbg_log(ff_a320, 0, "get error: %s is of type %s",
		    ff_a320_intf.ValueName(id), ff_a320_type2str(type));
		return (NAN);
	}
	ff_a320_intf.ValueGet(id, &val);

	return (val);
}

static inline double
ff_a320_getf64(int id)
{
	double val;
	unsigned int type = ff_a320_intf.ValueType(id);

	if (type != Value_Type_float64) {
		dbg_log(ff_a320, 0, "get error: %s is of type %s",
		    ff_a320_intf.ValueName(id), ff_a320_type2str(type));
		return (NAN);
	}
	ff_a320_intf.ValueGet(id, &val);

	return (val);
}

static const char *
ff_a320_type2str(unsigned int t)
{
	switch (t) {
	case Value_Type_Deleted:
		return "deleted";
	case Value_Type_Object:
		return "object";
	case Value_Type_sint8:
		return "sint8";
	case Value_Type_uint8:
		return "uint8";
	case Value_Type_sint16:
		return "sint16";
	case Value_Type_uint16:
		return "uint16";
	case Value_Type_sint32:
		return "sint32";
	case Value_Type_uint32:
		return "uint32";
	case Value_Type_float32:
		return "float32";
	case Value_Type_float64:
		return "float64";
	case Value_Type_String:
		return "string";
	case Value_Type_Time:
		return "time";
	default:
		return "unknown";
	}
}

static void
ff_a320_units2str(unsigned int units, char buf[32])
{
	snprintf(buf, 32, "%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s"
	    "%s%s%s%s%s%s%s%s%s%s%s%s",
	    (units & Value_Unit_Object) ? "O" : "",
	    (units & Value_Unit_Failure) ? "F" : "",
	    (units & Value_Unit_Button) ? "B" : "",
	    (units & Value_Unit_Ratio) ? "R" : "",
	    (units & Value_Unit_State) ? "s" : "",
	    (units & Value_Unit_Flags) ? "f" : "",
	    (units & Value_Unit_Ident) ? "I" : "",
	    (units & Value_Unit_Length) ? "M" : "",
	    (units & Value_Unit_Speed) ? "S" : "",
	    (units & Value_Unit_Accel) ? "^" : "",
	    (units & Value_Unit_Force) ? "N" : "",
	    (units & Value_Unit_Weight) ? "K" : "",
	    (units & Value_Unit_Angle) ? "D" : "",
	    (units & Value_Unit_AngularSpeed) ? "@" : "",
	    (units & Value_Unit_AngularAccel) ? "c" : "",
	    (units & Value_Unit_Temperature) ? "t" : "",
	    (units & Value_Unit_Pressure) ? "P" : "",
	    (units & Value_Unit_Flow) ? "L" : "",
	    (units & Value_Unit_Voltage) ? "V" : "",
	    (units & Value_Unit_Frequency) ? "H" : "",
	    (units & Value_Unit_Current) ? "A" : "",
	    (units & Value_Unit_Power) ? "W": "",
	    (units & Value_Unit_Density) ? "d" : "",
	    (units & Value_Unit_Volume) ? "v" : "",
	    (units & Value_Unit_Conduction) ? "S" : "",
	    (units & Value_Unit_Capacity) ? "C" : "",
	    (units & Value_Unit_Heat) ? "T" : "",
	    (units & Value_Unit_Position) ? "r" : "",
	    (units & Value_Unit_TimeDelta) ? "'" : "",
	    (units & Value_Unit_TimeStart) ? "`" : "",
	    (units & Value_Unit_Label) ? "9" : "");
}

static int
ff_a320_val_id(const char *name)
{
	int id = ff_a320_intf.ValueIdByName(name);
	char units[32];

	ff_a320_units2str(id, units);
	dbg_log(ff_a320, 3, "%-44s  %-8s  %02x  %-10s  %s", name,
	    ff_a320_type2str(ff_a320_intf.ValueType(id)),
	    ff_a320_intf.ValueFlags(id),
	    ff_a320_intf.ValueDesc(id),
	    units);

	return (id);
}

static void
ff_a320_update(double step, void *tag)
{
	double alt_uncorr;

	UNUSED(step);
	UNUSED(tag);

	VERIFY(ff_a320_intf.ValueIdByName != NULL);
	VERIFY(ff_a320_intf.ValueGet != NULL);

	if (ff_a320_ids.baro_alt_id == -1) {
		ff_a320_ids.powered_id = ff_a320_val_id(
		    "Aircraft.Navigation.GPWC.Powered");
		ff_a320_ids.fault_id = ff_a320_val_id(
		    "Aircraft.Navigation.GPWC.Fault");
		ff_a320_ids.fault_ex_id = ff_a320_val_id(
		    "Aircraft.Navigation.GPWC.FaultEx");
		ff_a320_ids.inhibit_id = ff_a320_val_id(
		    "Aircraft.Navigation.GPWC.Inhibit");
		ff_a320_ids.inhibit_ex_id = ff_a320_val_id(
		    "Aircraft.Navigation.GPWC.InhibitEx");
		ff_a320_ids.inhibit_flaps_id = ff_a320_val_id(
		    "Aircraft.Navigation.GPWC.InhibitFlaps");
		ff_a320_ids.alert_id = ff_a320_val_id(
		    "Aircraft.Navigation.GPWC.Alert");

		ff_a320_ids.baro_alt_id = ff_a320_val_id(
		    "Aircraft.Navigation.GPWC.AltitudeBaro");
		ff_a320_ids.baro_raw_id = ff_a320_val_id(
		    "Aircraft.Navigation.GPWC.Altitude");
		ff_a320_ids.rad_alt_id = ff_a320_val_id(
		    "Aircraft.Navigation.GPWC.Height");

		ff_a320_ids.lat_id = ff_a320_val_id(
		    "Aircraft.Navigation.GPWC.PositionLat");
		ff_a320_ids.lon_id = ff_a320_val_id(
		    "Aircraft.Navigation.GPWC.PositionLon");
		ff_a320_ids.elev_id = ff_a320_val_id(
		    "Aircraft.Navigation.GPWC.Elevation");
		ff_a320_ids.hdg_id = ff_a320_val_id(
		    "Aircraft.Navigation.GPWC.Heading");

		ff_a320_ids.fpa_id = ff_a320_val_id(
		    "Aircraft.Navigation.GPWC.Path");
		ff_a320_ids.aoa_value_id = ff_a320_val_id(
		    "Aircraft.Navigation.ADIRS.Sensors.AOA1.Value");
		ff_a320_ids.aoa_valid_id = ff_a320_val_id(
		    "Aircraft.Navigation.ADIRS.Sensors.AOA1.Valid");

		ff_a320_ids.cas_id = ff_a320_val_id(
		    "Aircraft.Navigation.GPWC.AirSpeed");
		ff_a320_ids.gs_id = ff_a320_val_id(
		    "Aircraft.Navigation.GPWC.Speed");
	}

	ff_a320_status.powered = ff_a320_gets32(ff_a320_ids.powered_id);

	if (ff_a320_gets32(ff_a320_ids.fault_id) != 0 ||
	    ff_a320_gets32(ff_a320_ids.fault_ex_id) != 0) {
		ff_a320_sys_ok = B_FALSE;
		return;
	}

	ff_a320_status.inhibit = ff_a320_gets32(ff_a320_ids.inhibit_id);
	ff_a320_status.inhibit_ex = ff_a320_gets32(ff_a320_ids.inhibit_ex_id);
	ff_a320_status.inhibit_flaps =
	    ff_a320_gets32(ff_a320_ids.inhibit_flaps_id);
	ff_a320_status.alerting = ff_a320_gets32(ff_a320_ids.alert_id);

	adc_l.baro_alt = MET2FEET(ff_a320_getf32(ff_a320_ids.baro_alt_id));
	alt_uncorr = MET2FEET(ff_a320_getf32(ff_a320_ids.baro_raw_id));
	/*
	 * Since we don't get access to the raw baro setting on the altimeter,
	 * we work it out from the baro-corrected and baro-uncorrected
	 * altimeter readings. 0.01 inHg equals to approximately 10 ft of
	 * difference, or about 3.047m. No need to be super accurate with
	 * this, we only need it to check that the altimeter setting is QNE
	 * when doing the alt->FL transition.
	 */
	adc_l.baro_set = 29.92 + ((adc_l.baro_alt - alt_uncorr) / 1000.0);
	adc_l.rad_alt = MET2FEET(ff_a320_getf32(ff_a320_ids.rad_alt_id));

	adc_l.lat = RAD2DEG(ff_a320_getf64(ff_a320_ids.lat_id));
	adc_l.lon = RAD2DEG(ff_a320_getf64(ff_a320_ids.lon_id));
	adc_l.elev = ff_a320_getf32(ff_a320_ids.elev_id);

	/*
	adc_l.hdg = ff_a320_getf32(ff_a320_ids.hdg_id);
	if (adc_l.hdg < 0)
		adc_l.hdg += 360.0;
	*/
	adc_l.hdg = XPLMGetDataf(drs_l.hdg);
	adc_l.pitch = XPLMGetDataf(drs_l.pitch);

	adc_l.cas = MPS2KT(ff_a320_getf32(ff_a320_ids.cas_id));
	adc_l.gs = ff_a320_getf32(ff_a320_ids.gs_id);

	ff_a320_sys_ok = B_TRUE;
}

static void
ff_a320_intf_fini(void)
{
	if (ff_a320_intf.DataDelUpdate != NULL)
		ff_a320_intf.DataDelUpdate(ff_a320_update, NULL);
	memset(&ff_a320_intf, 0, sizeof (ff_a320_intf));
	dbg_log(ff_a320, 1, "fini");
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
	dbg_log(ff_a320, 1, "powered: %s",
	    ff_a320_status.powered ? "true" : "false");
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
