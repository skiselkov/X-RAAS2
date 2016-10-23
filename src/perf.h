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
 * Copyright 2015 Saso Kiselkov. All rights reserved.
 */

#ifndef	_OPENFMC_PERF_H_
#define	_OPENFMC_PERF_H_

#include "geom.h"

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Temperature unit conversions.
 */
#define	KELVIN2C(k)	((k) - 273.15)
#define	C2KELVIN(c)	((c) + 273.15)
#define	FAH2C(f)	(((f) - 32) * 0.555555)
#define	C2FAH(c)	(((c) * 1.8) + 32)
#define	FAH2KELVIN(f)	(((f) + 459.67) * 0.5555555555)
#define	KELVIN2FAH(k)	(((k) * 1.8) - 459.67)

/*
 * Length and velocity unit conversions.
 */
#define	FEET2MET(x)	((x) * 0.3048)		/* feet to meters */
#define	MET2FEET(x)	((x) * 3.2808398950131)	/* meters to feet */
#define	NM2MET(x)	((x) * 1852)		/* nautical miles to meters */
#define	MET2NM(x)	((x) / 1852.0)		/* meters to nautical miles */
#define	KT2MPS(k)	(NM2MET(k) / 3600)	/* knots to m/s */
#define	MPS2KT(k)	(MET2NM(k) * 3600)	/* m/s to knots */

/*
 * Other unit conversions
 */
#define	HPA2PA(x)	((x) / 100)
#define	PA2HPA(x)	((x) * 100)

/*
 * ISA (International Standard Atmosphere) parameters.
 */
#define	ISA_SL_TEMP_C		15.0	/* Sea level temperature in degrees C */
#define	ISA_SL_TEMP_K		288.15	/* Sea level temperature in Kelvin */
#define	ISA_SL_PRESS		101325	/* Sea level pressure in Pa */
#define	ISA_SL_DENS		1.225	/* Sea level density in kg/m^3 */
#define	ISA_TLR_PER_1000FT	1.98	/* Temperature lapse rate per 1000ft */
#define	ISA_TLR_PER_1M		0.0065	/* Temperature lapse rate per 1 meter */
#define	ISA_SPEED_SOUND		340.3	/* Speed of sound at sea level */
#define	ISA_TP_ALT		36089	/* Tropopause altitude in feet */

double alt2press(double alt, double qnh);
double press2alt(double press, double qnh);

double alt2fl(double alt, double qnh);
double fl2alt(double alt, double qnh);

double ktas2mach(double ktas, double oat);
double mach2ktas(double mach, double oat);

double ktas2kcas(double ktas, double pressure, double oat);
double kcas2ktas(double kcas, double pressure, double oat);

double mach2keas(double mach, double press);
double keas2mach(double keas, double press);

double sat2tat(double sat, double mach);
double tat2sat(double tat, double mach);

double sat2isadev(double fl, double sat);
double isadev2sat(double fl, double isadev);

double speed_sound(double oat);
double air_density(double pressure, double oat);
double impact_press(double mach, double pressure);
double dyn_press(double ktas, double press, double oat);

#ifdef	__cplusplus
}
#endif

#endif	/* _OPENFMC_PERF_H_ */
