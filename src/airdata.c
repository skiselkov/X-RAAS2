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
#include "threading.h"

/* A320 interface */
#include <FF_A320/SharedValue.h>

#include "xraas2.h"

#define	HDG_ALIGN_THRESH	20	/* degrees */
#define	GPWC_ARPT_ELEV_THRESH	609	/* meters, 2000 feet */
#define	ADC_PRINTF_FMT \
	"ALT:%05.0fft/%02.2finHg/%04.0fm  POS:%02.04fdeg/%03.04fdeg/%05.0fm  " \
	"ATT:%03.0fdeg/%02.1fdeg  SPD:%03.0fkt/%03.0fmps TR:%05dft/%05dft " \
	"FL:%0.02f/%0.02f VR:%03.0fkt/%03.0fkt ILS:%03.02f/%4s/%.01f/%.01f"
#define	ADC_PRINTF_ARGS(adc) \
	(adc)->baro_alt, (adc)->baro_set, (adc)->rad_alt, (adc)->lat, \
	(adc)->lon, (adc)->elev, (adc)->hdg, (adc)->pitch, (adc)->cas, \
	(adc)->gs, (adc)->trans_alt, (adc)->trans_lvl, (adc)->takeoff_flaps, \
	(adc)->landing_flaps, (adc)->vref, (adc)->vapp, (adc)->ils_info.freq, \
	(adc)->ils_info.id, (adc)->ils_info.hdef, (adc)->ils_info.vdef

enum {
	XP_DEFAULT_INTERFACE,
	FF_A320_INTERFACE,
	INVALID_INTERFACE
};

static void xp_adc_get(adc_t *adc);

static bool_t ff_a320_intf_init(void);
static void ff_a320_intf_fini(void);
static void ff_a320_update(double step, void *tag);
static bool_t ff_a320_adc_get(adc_t *adc);
static const char *ff_a320_type2str(unsigned int t);


static int intf_type = INVALID_INTERFACE;

static adc_t adc_l;
const adc_t *adc = &adc_l;

static drs_t drs_l;
const drs_t *drs = &drs_l;


typedef struct ff_a320_rwy_info {
	bool_t		changed;
	bool_t		present;
	geo_pos3_t	thr_pos;
	double		length;
	double		width;
	double		track;
} ff_a320_rwy_info_t;

static struct {
	SharedValuesInterface	svi;
	mutex_t			lock;

	adc_t			adc;
	bool_t			sys_ok;
	struct {
		bool_t powered;
		bool_t alerting;
		bool_t suppressed;
		bool_t inhibit;
		bool_t inhibit_ex;
		bool_t inhibit_flaps;
	} status;
	struct {
		int powered;			/* flag 0/1 */
		int fault;			/* flag 0/1 */
		int fault_ex;			/* flag 0/1 */
		int inhibit;			/* flag 0/1 */
		int inhibit_ex;			/* flag 0/1 */
		int inhibit_flaps;		/* flag 0/1 */
		int alert;			/* flag 0/1 */

		int baro_alt;			/* meters */
		int baro_raw;			/* meters */
		int rad_alt;			/* meters */

		int lat;			/* radians from equator */
		int lon;			/* radians from 0th meridian */
		int elev;			/* meters AMSL */

		int hdg;			/* degrees true -180..+180 */

		int fpa;			/* degrees nose up */
		int aoa_value;			/* degrees down */
		int aoa_valid;			/* flag 0/1 */

		int cas;			/* meters/second */
		int gs;				/* meters/second */

		int rwy_lat;			/* radians from equator */
		int rwy_lon;			/* radians from 0th meridian */
		int rwy_len;			/* meters */
		int rwy_width;			/* meters */
		int rwy_track;			/* degrees true -180..+180 */
		int rwy_elev;			/* meters */

		int trans_alt;			/* meters */
		int trans_lvl;			/* meters */

		int takeoff_flaps;		/* int, 2-4 */
		int landing_flaps;		/* int, 3-5 */
		int vapp;			/* knots */

		int glideslope;			/* dots */
		int glideslope_valid;		/* bool */
		int localizer;			/* dots */
		int localizer_valid;		/* bool */
	} ids;

	ff_a320_rwy_info_t rwy_info;
} ff_a320;


static XPLMDataRef
dr_get(const char *drname)
{
	XPLMDataRef dr = XPLMFindDataRef(drname);
	VERIFY_MSG(dr != NULL, "dataref \"%s\" not found", drname);
	return (dr);
}

bool_t
adc_init(void)
{
	dbg_log(adc, 1, "init");

	memset(&ff_a320, 0, sizeof (ff_a320));
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

	if (*xraas_state->config.GPWS_priority_dataref != 0) {
		drs_l.gpws_prio =
		    dr_get(xraas_state->config.GPWS_priority_dataref);
	}
	if (*xraas_state->config.GPWS_inop_dataref != 0) {
		drs_l.gpws_inop = dr_get(xraas_state->config.GPWS_inop_dataref);
	}

	drs_l.replay_mode = dr_get("sim/operation/prefs/replay_mode");

	drs_l.nav1_frequency = dr_get("sim/cockpit/radios/nav1_freq_hz");
	drs_l.nav1_flag_glideslope =
	    dr_get("sim/cockpit2/radios/indicators/nav1_flag_glideslope");
	drs_l.nav1_nav_id =
	    dr_get("sim/cockpit2/radios/indicators/nav1_nav_id");
	drs_l.nav1_hdef_dots_pilot =
	    dr_get("sim/cockpit2/radios/indicators/nav1_hdef_dots_pilot");
	drs_l.nav1_vdef_dots_pilot =
	    dr_get("sim/cockpit2/radios/indicators/nav1_vdef_dots_pilot");
	drs_l.nav1_power =
	    dr_get("sim/cockpit2/radios/actuators/nav1_power");

	drs_l.nav2_frequency = dr_get("sim/cockpit/radios/nav2_freq_hz");
	drs_l.nav2_flag_glideslope =
	    dr_get("sim/cockpit2/radios/indicators/nav2_flag_glideslope");
	drs_l.nav2_nav_id =
	    dr_get("sim/cockpit2/radios/indicators/nav2_nav_id");
	drs_l.nav2_hdef_dots_pilot =
	    dr_get("sim/cockpit2/radios/indicators/nav2_hdef_dots_pilot");
	drs_l.nav2_vdef_dots_pilot =
	    dr_get("sim/cockpit2/radios/indicators/nav2_vdef_dots_pilot");
	drs_l.nav2_power =
	    dr_get("sim/cockpit2/radios/actuators/nav2_power");

	if (ff_a320_intf_init())
		intf_type = FF_A320_INTERFACE;
	else
		intf_type = XP_DEFAULT_INTERFACE;

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
	if (intf_type == FF_A320_INTERFACE) {
		if (!ff_a320_adc_get(&adc_l))
			return (B_FALSE);
	} else if (intf_type == XP_DEFAULT_INTERFACE) {
		xp_adc_get(&adc_l);
	} else {
		return (B_FALSE);
	}

	adc_l.baro_sl = XPLMGetDataf(drs_l.baro_sl);

	XPLMGetDatavf(drs_l.nw_offset, &adc_l.nw_offset, 0, 1);
	adc_l.flaprqst = XPLMGetDataf(drs_l.flaprqst);

	adc_l.n_gear = XPLMGetDatavf(drs_l.gear, adc_l.gear, 0, NUM_GEAR);
	VERIFY(adc_l.n_gear <= NUM_GEAR);
	XPLMGetDatavi(drs_l.gear_type, adc_l.gear_type, 0, adc_l.n_gear);

	dbg_log(adc, 2, "collect; " ADC_PRINTF_FMT, ADC_PRINTF_ARGS(&adc_l));

	return (B_TRUE);
}

/*
 * If the airdata interface supplies external runway selection data, this
 * function returns the data sent to the GPWC. This is used in the FF A320
 * to feed exact runway scenery information to the aircraft's GPWC.
 */
bool_t
adc_gpwc_rwy_data(geo_pos3_t *thr_pos, double *len, double *width, double *trk)
{
	if (intf_type != FF_A320_INTERFACE || !ff_a320.rwy_info.present)
		return (B_FALSE);

	*thr_pos = ff_a320.rwy_info.thr_pos;
	*len = ff_a320.rwy_info.length;
	*width = ff_a320.rwy_info.width;
	*trk = ff_a320.rwy_info.track;

	return (B_TRUE);
}

static void
xp_adc_get(adc_t *adc)
{
	adc->baro_alt = XPLMGetDataf(drs_l.baro_alt);
	adc->baro_set = XPLMGetDataf(drs_l.baro_set);
	adc->rad_alt = XPLMGetDataf(drs_l.rad_alt);
	adc->lat = XPLMGetDatad(drs_l.lat);
	adc->lon = XPLMGetDatad(drs_l.lon);
	adc->elev = XPLMGetDatad(drs_l.elev);
	adc->hdg = XPLMGetDataf(drs_l.hdg);
	adc->pitch = XPLMGetDataf(drs_l.pitch);
	adc->cas = XPLMGetDataf(drs_l.cas);
	adc->gs = XPLMGetDataf(drs_l.gs);
	adc->takeoff_flaps = NAN;
	adc->landing_flaps = NAN;
	adc->vref = NAN;
	adc->vapp = NAN;

	if (XPLMGetDatai(drs_l.nav1_power) == 1 &&
	    XPLMGetDatai(drs_l.nav1_flag_glideslope) == 0) {
		adc->ils_info.active = B_TRUE;
		adc->ils_info.freq = XPLMGetDatai(drs_l.nav1_frequency) / 100.0;
		XPLMGetDatab(drs_l.nav1_nav_id, adc->ils_info.id, 0,
		    sizeof (adc->ils_info.id) - 1);
		adc->ils_info.id[sizeof (adc->ils_info.id) - 1] = 0;
		adc->ils_info.hdef = XPLMGetDataf(drs_l.nav1_hdef_dots_pilot);
		adc->ils_info.vdef = XPLMGetDataf(drs_l.nav1_vdef_dots_pilot);
	} else if (XPLMGetDatai(drs_l.nav2_power) == 1 &&
	    XPLMGetDatai(drs_l.nav2_flag_glideslope) == 0) {
		adc->ils_info.active = B_TRUE;
		adc->ils_info.freq = XPLMGetDatai(drs_l.nav2_frequency) / 100.0;
		XPLMGetDatab(drs_l.nav1_nav_id, adc->ils_info.id, 0,
		    sizeof (adc->ils_info.id) - 1);
		adc->ils_info.id[sizeof (adc->ils_info.id) - 1] = 0;
		adc->ils_info.hdef = XPLMGetDataf(drs_l.nav2_hdef_dots_pilot);
		adc->ils_info.vdef = XPLMGetDataf(drs_l.nav2_vdef_dots_pilot);
	} else {
		adc->ils_info.active = B_FALSE;
		adc->ils_info.freq = NAN;
		adc->ils_info.id[0] = 0;
		adc->ils_info.hdef = NAN;
		adc->ils_info.vdef = NAN;
	}
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

	memset(&ff_a320, 0, sizeof (ff_a320));

	plugin = XPLMFindPluginBySignature(XPLM_FF_SIGNATURE);
	if (plugin == XPLM_NO_PLUGIN_ID) {
		dbg_log(ff_a320, 1, "init fail: plugin not found");
		return (B_FALSE);
	}
	XPLMSendMessageToPlugin(plugin, XPLM_FF_MSG_GET_SHARED_INTERFACE,
	    &ff_a320);
	if (ff_a320.svi.DataAddUpdate == NULL) {
		dbg_log(ff_a320, 1, "init fail: func vector empty");
		return (B_FALSE);
	}

	ff_a320.svi.DataAddUpdate(ff_a320_update, NULL);

	memset(&ff_a320.ids, 0xff, sizeof (ff_a320.ids));
	mutex_init(&ff_a320.lock);

	dbg_log(ff_a320, 1, "init successful");

	return (B_TRUE);
}

static inline int32_t
ff_a320_gets32(int id)
{
	int val;
	unsigned int type = ff_a320.svi.ValueType(id);
	ASSERT_MSG(type >= Value_Type_sint8 && type <= Value_Type_uint32,
	    "%s isn't an integer type, instead it is %s",
	    ff_a320.svi.ValueName(id), ff_a320_type2str(type));
	ff_a320.svi.ValueGet(id, &val);
	return (val);
}

static inline float
ff_a320_getf32(int id)
{
	float val;
	unsigned int type = ff_a320.svi.ValueType(id);
	ASSERT_MSG(type == Value_Type_float32,
	    "%s isn't a float32 type, instead it is %s",
	    ff_a320.svi.ValueName(id), ff_a320_type2str(type));
	ff_a320.svi.ValueGet(id, &val);
	return (val);
}

static inline void
ff_a320_setf32(int id, float val)
{
	unsigned int type = ff_a320.svi.ValueType(id);
	ASSERT_MSG(type == Value_Type_float32,
	    "%s isn't a float32 type, instead it is %s",
	    ff_a320.svi.ValueName(id), ff_a320_type2str(type));
	ff_a320.svi.ValueSet(id, &val);
}

static inline double
ff_a320_getf64(int id)
{
	double val;
	unsigned int type = ff_a320.svi.ValueType(id);
	ASSERT_MSG(type == Value_Type_float64,
	    "%s isn't a float64 type, instead it is %s",
	    ff_a320.svi.ValueName(id), ff_a320_type2str(type));
	ff_a320.svi.ValueGet(id, &val);
	return (val);
}

static inline void
ff_a320_setf64(int id, double val)
{
	unsigned int type = ff_a320.svi.ValueType(id);
	ASSERT_MSG(type == Value_Type_float64,
	    "%s isn't a float64 type, instead it is %s",
	    ff_a320.svi.ValueName(id), ff_a320_type2str(type));
	ff_a320.svi.ValueSet(id, &val);
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
	int id = ff_a320.svi.ValueIdByName(name);
	char units[32];

	ff_a320_units2str(id, units);
	dbg_log(ff_a320, 3, "%-44s  %-8s  %02x  %-10s  %s", name,
	    ff_a320_type2str(ff_a320.svi.ValueType(id)),
	    ff_a320.svi.ValueFlags(id),
	    ff_a320.svi.ValueDesc(id),
	    units);

	return (id);
}

/*
 * Translates the flaps configuration parameter values from the FF A320
 * model into flap handle positions we get from X-Plane.
 */
static double
ff_a320_flaps2flaprqst(int config)
{
	switch (config) {
	case 0:
		return (0.0);
	case 1:
	case 2:
		return (0.25);
	case 3:
		return (0.5);
	case 4:
		return (0.75);
	case 5:
		return (1.0);
	default:
		return (NAN);
	}
}

static void
ff_a320_update(double step, void *tag)
{
	double alt_uncorr;
	adc_t ff_adc;

	UNUSED(step);
	UNUSED(tag);

	VERIFY(ff_a320.svi.ValueIdByName != NULL);
	VERIFY(ff_a320.svi.ValueGet != NULL);
	VERIFY(ff_a320.svi.ValueType != NULL);
	VERIFY(ff_a320.svi.ValueName != NULL);

	memset(&ff_adc, 0, sizeof (ff_adc));

	if (ff_a320.ids.baro_alt == -1) {
		ff_a320.ids.powered =
		    ff_a320_val_id("Aircraft.Navigation.GPWC.Powered");
		ff_a320.ids.fault =
		    ff_a320_val_id("Aircraft.Navigation.GPWC.Fault");
		ff_a320.ids.fault_ex =
		    ff_a320_val_id("Aircraft.Navigation.GPWC.FaultEx");
		ff_a320.ids.inhibit =
		    ff_a320_val_id("Aircraft.Navigation.GPWC.Inhibit");
		ff_a320.ids.inhibit_ex =
		    ff_a320_val_id("Aircraft.Navigation.GPWC.InhibitEx");
		ff_a320.ids.inhibit_flaps =
		    ff_a320_val_id("Aircraft.Navigation.GPWC.InhibitFlaps");
		ff_a320.ids.alert =
		    ff_a320_val_id("Aircraft.Navigation.GPWC.Alert");

		ff_a320.ids.baro_alt =
		    ff_a320_val_id("Aircraft.Navigation.GPWC.AltitudeBaro");
		ff_a320.ids.baro_raw =
		    ff_a320_val_id("Aircraft.Navigation.GPWC.Altitude");
		ff_a320.ids.rad_alt =
		    ff_a320_val_id("Aircraft.Navigation.GPWC.Height");

		ff_a320.ids.lat =
		    ff_a320_val_id("Aircraft.Navigation.GPWC.PositionLat");
		ff_a320.ids.lon =
		    ff_a320_val_id("Aircraft.Navigation.GPWC.PositionLon");
		ff_a320.ids.elev =
		    ff_a320_val_id("Aircraft.Navigation.GPWC.Elevation");
		ff_a320.ids.hdg =
		    ff_a320_val_id("Aircraft.Navigation.GPWC.Heading");

		ff_a320.ids.fpa =
		    ff_a320_val_id("Aircraft.Navigation.GPWC.Path");
		ff_a320.ids.aoa_value = ff_a320_val_id(
		    "Aircraft.Navigation.ADIRS.Sensors.AOA1.Value");
		ff_a320.ids.aoa_valid = ff_a320_val_id(
		    "Aircraft.Navigation.ADIRS.Sensors.AOA1.Valid");

		ff_a320.ids.cas =
		    ff_a320_val_id("Aircraft.Navigation.GPWC.AirSpeed");
		ff_a320.ids.gs =
		    ff_a320_val_id("Aircraft.Navigation.GPWC.Speed");


		ff_a320.ids.rwy_lat = ff_a320_val_id(
		    "Aircraft.Navigation.GPWC.RunwayPositionLat");
		ff_a320.ids.rwy_lon = ff_a320_val_id(
		    "Aircraft.Navigation.GPWC.RunwayPositionLon");
		ff_a320.ids.rwy_len =
		    ff_a320_val_id("Aircraft.Navigation.GPWC.RunwayLength");
		ff_a320.ids.rwy_width =
		    ff_a320_val_id("Aircraft.Navigation.GPWC.RunwayWidth");
		ff_a320.ids.rwy_track =
		    ff_a320_val_id("Aircraft.Navigation.GPWC.RunwayTrack");
		ff_a320.ids.rwy_elev =
		    ff_a320_val_id("Aircraft.Navigation.GPWC.RunwayElevation");

		ff_a320.ids.trans_alt = ff_a320_val_id(
		    "Aircraft.Navigation.GPWC.TransitionAltitude");
		ff_a320.ids.trans_lvl =
		    ff_a320_val_id("Aircraft.Navigation.GPWC.TransitionLevel");

		ff_a320.ids.takeoff_flaps =
		    ff_a320_val_id("Aircraft.TakeoffConfig");
		ff_a320.ids.landing_flaps =
		    ff_a320_val_id("Aircraft.LandingConfig");
		ff_a320.ids.vapp =
		    ff_a320_val_id("Aircraft.Navigation.GPWC.ApproachSpeed");

		ff_a320.ids.glideslope =
		    ff_a320_val_id("Aircraft.Navigation.GPWC.GlideSlope");
		ff_a320.ids.glideslope_valid =
		    ff_a320_val_id("Aircraft.Navigation.GPWC.GlideSlopeValid");
		ff_a320.ids.localizer =
		    ff_a320_val_id("Aircraft.Navigation.GPWC.Localizer");
		ff_a320.ids.localizer_valid =
		    ff_a320_val_id("Aircraft.Navigation.GPWC.LocalizerValid");
	}

	ff_a320.status.powered = ff_a320_gets32(ff_a320.ids.powered);

	if (ff_a320_gets32(ff_a320.ids.fault) != 0 ||
	    ff_a320_gets32(ff_a320.ids.fault_ex) != 0) {
		ff_a320.sys_ok = B_FALSE;
		return;
	}

	ff_a320.status.inhibit = ff_a320_gets32(ff_a320.ids.inhibit);
	ff_a320.status.inhibit_ex = ff_a320_gets32(ff_a320.ids.inhibit_ex);
	ff_a320.status.inhibit_flaps =
	    ff_a320_gets32(ff_a320.ids.inhibit_flaps);
	ff_a320.status.alerting = ff_a320_gets32(ff_a320.ids.alert);

	ff_adc.baro_alt = MET2FEET(ff_a320_getf32(ff_a320.ids.baro_alt));
	alt_uncorr = MET2FEET(ff_a320_getf32(ff_a320.ids.baro_raw));
	/*
	 * Since we don't get access to the raw baro setting on the altimeter,
	 * we work it out from the baro-corrected and baro-uncorrected
	 * altimeter readings. 0.01 inHg equals to approximately 10 ft of
	 * difference, or about 3.047m. No need to be super accurate with
	 * this, we only need it to check that the altimeter setting is QNE
	 * when doing the alt->FL transition.
	 */
	ff_adc.baro_set = 29.92 + ((adc_l.baro_alt - alt_uncorr) / 1000.0);
	ff_adc.rad_alt = MET2FEET(ff_a320_getf32(ff_a320.ids.rad_alt));

	ff_adc.lat = RAD2DEG(ff_a320_getf64(ff_a320.ids.lat));
	ff_adc.lon = RAD2DEG(ff_a320_getf64(ff_a320.ids.lon));
	ff_adc.elev = ff_a320_getf32(ff_a320.ids.elev);

	ff_adc.hdg = ff_a320_getf32(ff_a320.ids.hdg);
	if (ff_adc.hdg < 0)
		ff_adc.hdg += 360.0;
	ff_adc.pitch = XPLMGetDataf(drs_l.pitch);

	ff_adc.cas = MPS2KT(ff_a320_getf32(ff_a320.ids.cas));
	ff_adc.gs = ff_a320_getf32(ff_a320.ids.gs);

	ff_adc.trans_alt = MET2FEET(ff_a320_getf32(ff_a320.ids.trans_alt));
	ff_adc.trans_lvl = MET2FEET(ff_a320_getf32(ff_a320.ids.trans_lvl));


	ff_adc.takeoff_flaps = ff_a320_flaps2flaprqst(ff_a320_gets32(
	    ff_a320.ids.takeoff_flaps));
	ff_adc.landing_flaps = ff_a320_flaps2flaprqst(ff_a320_gets32(
	    ff_a320.ids.landing_flaps));
	ff_adc.vref = NAN;
	ff_adc.vapp = ff_a320_getf32(ff_a320.ids.vapp);

	dbg_log(ff_a320, 4, "update; " ADC_PRINTF_FMT,
	    ADC_PRINTF_ARGS(&ff_adc));

	if (ff_a320.rwy_info.changed) {
		if (ff_a320.rwy_info.present) {
			double trk = ff_a320.rwy_info.track;
			if (trk > 180.0)
				trk -= 360.0;
			ff_a320_setf64(ff_a320.ids.rwy_lat,
			    DEG2RAD(ff_a320.rwy_info.thr_pos.lat));
			ff_a320_setf64(ff_a320.ids.rwy_lon,
			    DEG2RAD(ff_a320.rwy_info.thr_pos.lon));
			ff_a320_setf32(ff_a320.ids.rwy_len,
			    ff_a320.rwy_info.length);
			ff_a320_setf32(ff_a320.ids.rwy_width,
			    ff_a320.rwy_info.width);
			ff_a320_setf32(ff_a320.ids.rwy_track, trk);
			ff_a320_setf32(ff_a320.ids.rwy_elev,
			    ff_a320.rwy_info.thr_pos.lon);
		} else {
			ff_a320_setf64(ff_a320.ids.rwy_lat, 4 * M_PI);
			ff_a320_setf64(ff_a320.ids.rwy_lon, 4 * M_PI);
			ff_a320_setf32(ff_a320.ids.rwy_len, -1.0);
			ff_a320_setf32(ff_a320.ids.rwy_width, -1.0);
			ff_a320_setf32(ff_a320.ids.rwy_track, -1.0);
			ff_a320_setf32(ff_a320.ids.rwy_elev, -1000.0);
		}
		ff_a320.rwy_info.changed = B_FALSE;
	}

	if (ff_a320_gets32(ff_a320.ids.localizer_valid) == 1 &&
	    ff_a320_gets32(ff_a320.ids.glideslope_valid) == 1) {
		ff_adc.ils_info.active = B_TRUE;
		ff_adc.ils_info.freq = 0;
		ff_adc.ils_info.id[0] = 0;
		ff_adc.ils_info.hdef = ff_a320_getf32(ff_a320.ids.localizer);
		ff_adc.ils_info.vdef = ff_a320_getf32(ff_a320.ids.glideslope);
	} else {
		ff_adc.ils_info.active = B_FALSE;
		ff_adc.ils_info.freq = NAN;
		ff_adc.ils_info.id[0] = 0;
		ff_adc.ils_info.hdef = NAN;
		ff_adc.ils_info.vdef = NAN;
	}

	mutex_enter(&ff_a320.lock);
	memcpy(&ff_a320.adc, &ff_adc, sizeof (ff_a320.adc));
	ff_a320.sys_ok = B_TRUE;
	mutex_exit(&ff_a320.lock);
}

static bool_t
ff_a320_adc_get(adc_t *adc)
{
	mutex_enter(&ff_a320.lock);
	memcpy(adc, &ff_a320.adc, sizeof (*adc));
	mutex_exit(&ff_a320.lock);

	return (ff_a320.sys_ok);
}

static void
ff_a320_intf_fini(void)
{
	if (ff_a320.svi.DataDelUpdate != NULL)
		ff_a320.svi.DataDelUpdate(ff_a320_update, NULL);
	mutex_destroy(&ff_a320.lock);
	memset(&ff_a320, 0, sizeof (ff_a320));
	dbg_log(ff_a320, 1, "fini");
}

bool_t
ff_a320_is_loaded(void)
{
	return (intf_type == FF_A320_INTERFACE);
}

static void
ff_a320_rwy_info_set(bool_t present, geo_pos3_t thr, double length,
    double width, double track)
{
	mutex_enter(&ff_a320.lock);
	if (present && (!ff_a320.rwy_info.present ||
	    memcmp(&ff_a320.rwy_info.thr_pos, &thr, sizeof (thr)) != 0 ||
	    ff_a320.rwy_info.length != length ||
	    ff_a320.rwy_info.width != width ||
	    ff_a320.rwy_info.track != track)) {
		dbg_log(ff_a320, 1, "rwy_info_set: p:%03.02f/%03.02f/%04.0f "
		    "len:%04.0f w:%02.0f t:%03f", thr.lat, thr.lon, thr.elev,
		    length, width, track);
		ff_a320.rwy_info.changed = B_TRUE;
		ff_a320.rwy_info.present = B_TRUE;
		ff_a320.rwy_info.thr_pos = thr;
		ff_a320.rwy_info.length = length;
		ff_a320.rwy_info.width = width;
		ff_a320.rwy_info.track = track;
	} else if (!present && ff_a320.rwy_info.present) {
		dbg_log(ff_a320, 1, "rwy_info_set: nil");
		memset(&ff_a320.rwy_info, 0, sizeof (ff_a320.rwy_info));
		ff_a320.rwy_info.changed = B_TRUE;
		ff_a320.rwy_info.present = B_FALSE;
	}
	mutex_exit(&ff_a320.lock);
}

void
ff_a320_find_nearest_rwy(void)
{
	double min_dist = 1e10;
	ff_a320_rwy_info_t info;

	memset(&info, 0, sizeof (info));

	/*
	 * We search using three methods. First we attempt to look for any
	 * runway in who's approach bbox we are located and aligned (heading
	 * within 20 degrees of runway heading). Alternatively, if that
	 * won't find anything, we also check a runway's rwy_bbox and
	 * alignment. And if that fails, we look for the nearest runway.
	 */

	for (airport_t *arpt = list_head(xraas_state->cur_arpts); arpt != NULL;
	    arpt = list_next(xraas_state->cur_arpts, arpt)) {
		vect2_t p = geo2fpp(GEO_POS2(adc->lat, adc->lon), &arpt->fpp);
		ASSERT(arpt->load_complete);

		if (fabs(arpt->refpt.elev - adc->elev) > GPWC_ARPT_ELEV_THRESH)
			continue;
		for (runway_t *rwy = avl_first(&arpt->rwys); rwy != NULL;
		    rwy = AVL_NEXT(&arpt->rwys, rwy)) {
			for (int i = 0; i < 2; i++) {
				runway_end_t *re = &rwy->ends[i];
				double dist = vect2_abs(vect2_sub(re->thr_v,
				    p));
				if (fabs(rel_hdg(adc->hdg, re->hdg)) <
				    HDG_ALIGN_THRESH &&
				    (vect2_in_poly(p, re->apch_bbox) ||
				    vect2_in_poly(p, rwy->rwy_bbox))) {
					/* succeed on a bbox match */
					ff_a320_rwy_info_set(B_TRUE, re->thr,
					    rwy->length, rwy->width, re->hdg);
					return;
				} else if (dist < min_dist) {
					min_dist = dist;
					info.present = B_TRUE;
					info.thr_pos = re->thr;
					info.length = rwy->length;
					info.width = rwy->width;
					info.track = re->hdg;
				}
			}
		}
	}

	dbg_log(ff_a320, 2, "rwy_info %s", info.present ? "present" : "absent");
	ff_a320_rwy_info_set(info.present, info.thr_pos, info.length,
	    info.width, info.track);
}

bool_t
ff_a320_powered(void)
{
	if (!ff_a320.status.powered)
		dbg_log(ff_a320, 2, "powered: false");
	return (ff_a320.status.powered);
}

bool_t
ff_a320_suppressed(void)
{
	if (ff_a320.status.suppressed)
		dbg_log(ff_a320, 2, "suppressed: true");
	return (ff_a320.status.suppressed);
}

bool_t
ff_a320_alerting(void)
{
	if (ff_a320.status.alerting)
		dbg_log(ff_a320, 2, "alerting: true");
	return (ff_a320.status.alerting);
}

bool_t
ff_a320_inhibit(void)
{
	if (ff_a320.status.inhibit)
		dbg_log(ff_a320, 2, "inhibit: true");
	return (ff_a320.status.inhibit);
}

bool_t
ff_a320_inhibit_ex(void)
{
	if (ff_a320.status.inhibit_ex)
		dbg_log(ff_a320, 2, "inhibit_ex: true");
	return (ff_a320.status.inhibit_ex);
}

bool_t
ff_a320_inhibit_flaps(void)
{
	if (ff_a320.status.inhibit_flaps)
		dbg_log(ff_a320, 2, "inhibit_flaps: true");
	return (ff_a320.status.inhibit_flaps);
}
