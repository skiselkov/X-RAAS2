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

#include "airdata.h"
#include "assert.h"

#include "xraas2.h"

static adc_t adc_l;
const adc_t *adc = &adc_l;

static drs_t drs_l;
const drs_t *drs = &drs_l;

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
	drs_l.nw_offset = dr_get("sim/flightmodel/parts/tire_z_no_deflection");
	drs_l.flaprqst = dr_get("sim/flightmodel/controls/flaprqst");
	drs_l.gear = dr_get("sim/aircraft/parts/acf_gear_deploy");
	drs_l.gear_type = dr_get("sim/aircraft/parts/acf_gear_type");
	drs_l.baro_set = dr_get("sim/cockpit/misc/barometer_setting");
	drs_l.baro_sl = dr_get("sim/weather/barometer_sealevel_inhg");
	drs_l.view_is_ext = dr_get("sim/graphics/view/view_is_external");
	drs_l.bus_volt = dr_get("sim/cockpit2/electrical/bus_volts");
	drs_l.avionics_on = dr_get("sim/cockpit/electrical/avionics_on");
	drs_l.num_engines = dr_get("sim/aircraft/engine/acf_num_engines");
	drs_l.mtow = dr_get("sim/aircraft/weight/acf_m_max");
	drs_l.ICAO = dr_get("sim/aircraft/view/acf_ICAO");

	drs_l.gpws_prio = dr_get(xraas_state->config.GPWS_priority_dataref);
	drs_l.gpws_inop = dr_get(xraas_state->config.GPWS_inop_dataref);

	drs_l.replay_mode = dr_get("sim/operation/prefs/replay_mode");

	return (B_TRUE);
}

void
adc_fini(void)
{
	memset(&adc_l, 0, sizeof (adc_l));
	memset(&drs_l, 0, sizeof (drs_l));
}

bool_t
adc_collect(void)
{
	adc_l.rad_alt = XPLMGetDataf(drs_l.rad_alt);
	adc_l.baro_alt = XPLMGetDataf(drs_l.baro_alt);
	adc_l.cas = XPLMGetDataf(drs_l.cas);
	adc_l.gs = XPLMGetDataf(drs_l.gs);
	adc_l.lat = XPLMGetDatad(drs_l.lat);
	adc_l.lon = XPLMGetDatad(drs_l.lon);
	adc_l.elev = XPLMGetDatad(drs_l.elev);
	adc_l.hdg = XPLMGetDataf(drs_l.hdg);
	adc_l.pitch = XPLMGetDataf(drs_l.pitch);
	XPLMGetDatavf(drs_l.nw_offset, &adc_l.nw_offset, 0, 1);
	adc_l.flaprqst = XPLMGetDataf(drs_l.flaprqst);

	adc_l.n_gear = XPLMGetDatavf(drs_l.gear, adc_l.gear, 0, NUM_GEAR);
	VERIFY(adc_l.n_gear <= NUM_GEAR);
	XPLMGetDatavi(drs_l.gear_type, adc_l.gear_type, 0, adc_l.n_gear);

	adc_l.baro_set = XPLMGetDataf(drs_l.baro_set);
	adc_l.baro_sl = XPLMGetDataf(drs_l.baro_sl);

	return (B_TRUE);
}
