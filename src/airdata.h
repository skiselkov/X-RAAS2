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

#ifndef	_XRAAS_AIRDATA_H_
#define	_XRAAS_AIRDATA_H_

#include <stdlib.h>
#include <XPLMDataAccess.h>

#include "types.h"

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * The airdata.[hc] file concentrates the interface to the aircraft's
 * air data (airspeed, altitude, heading, etc.) collection logic. This
 * allows plugging into different kinds of air data inputs depending on
 * the simulation requirements. By default we use X-Plane's datarefs
 * for this, but we can plug into an aircraft specifically, if it
 * provides a more accurate simulation of the air data sources.
 */

#define	NUM_GEAR	10

typedef struct {
	double	baro_alt;	/* feet */
	double	baro_set;	/* in.Hg */
	double	baro_sl;	/* in.Hg */
	double	rad_alt;	/* feet */

	double	lat;		/* degrees */
	double	lon;		/* degrees */
	double	elev;		/* meters */

	double	hdg;		/* degrees true */
	double	pitch;		/* degrees nose up */

	double	cas;		/* knots */
	double	gs;		/* meters/second */

	int	trans_alt;	/* feet */
	int	trans_lvl;	/* feet */

	float	nw_offset;
	double	flaprqst;

	size_t	n_gear;
	float	gear[NUM_GEAR];
	int	gear_type[NUM_GEAR];
} adc_t;

typedef struct {
	XPLMDataRef baro_alt;
	XPLMDataRef rad_alt;
	XPLMDataRef cas;
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
	XPLMDataRef view_is_ext;
	XPLMDataRef bus_volt;
	XPLMDataRef avionics_on;
	XPLMDataRef num_engines;
	XPLMDataRef mtow;
	XPLMDataRef ICAO;
	XPLMDataRef author;
	XPLMDataRef gpws_prio;
	XPLMDataRef gpws_inop;
	XPLMDataRef replay_mode;
} drs_t;

extern const adc_t *adc;
extern const drs_t *drs;

bool_t adc_init(void);
void adc_fini(void);
bool_t adc_collect(void);

void ff_a320_find_nearest_rwy(void);
bool_t ff_a320_is_loaded(void);
bool_t ff_a320_powered(void);
bool_t ff_a320_suppressed(void);
bool_t ff_a320_alerting(void);
bool_t ff_a320_inhibit(void);
bool_t ff_a320_inhibit_ex(void);
bool_t ff_a320_inhibit_flaps(void);

#ifdef	__cplusplus
}
#endif

#endif	/* _XRAAS_AIRDATA_H_ */
