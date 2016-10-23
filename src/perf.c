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

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#include "math.h"
#include "log.h"
#include "helpers.h"
#include "perf.h"

#define	SECS_PER_HR	3600		/* Number of seconds in an hour */

#define	STEP_DEBUG

/*
 * Physical constants.
 */
#define	EARTH_GRAVITY	9.80665		/* Earth surface grav. acceleration */
#define	DRY_AIR_MOL	0.0289644	/* Molar mass of dry air */
#define	GAMMA		1.4		/* Specific heat ratio of dry air */
#define	R_univ		8.31447		/* Universal gas constant */
#define	R_spec		287.058		/* Specific gas constant of dry air */

/* Calculates gravitational force for mass `m' in kg on Earth */
#define	MASS2GFORCE(m)		((m) * EARTH_GRAVITY)

/*
 * Converts a true airspeed to Mach number.
 *
 * @param ktas True airspeed in knots.
 * @param oat Static outside air temperature in degrees C.
 *
 * @return Mach number.
 */
double
ktas2mach(double ktas, double oat)
{
	return (KT2MPS(ktas) / speed_sound(oat));
}

/*
 * Converts Mach number to true airspeed.
 *
 * @param ktas Mach number.
 * @param oat Static outside air temperature in degrees C.
 *
 * @return True airspeed in knots.
 */
double
mach2ktas(double mach, double oat)
{
	return (MPS2KT(mach * speed_sound(oat)));
}

/*
 * Converts true airspeed to calibrated airspeed.
 *
 * @param ktas True airspeed in knots.
 * @param pressure Static air pressure in hPa.
 * @param oat Static outside air temperature in degrees C.
 *
 * @return Calibrated airspeed in knots.
 */
double
ktas2kcas(double ktas, double pressure, double oat)
{
	double qc = impact_press(ktas2mach(ktas, oat), pressure);
	return (MPS2KT(ISA_SPEED_SOUND *
	    sqrt(5 * (pow(qc / ISA_SL_PRESS + 1, 0.2857142857) - 1))));
}

/*
 * Converts calibrated airspeed to true airspeed.
 *
 * @param ktas Calibrated airspeed in knots.
 * @param pressure Static air pressure in hPa.
 * @param oat Static outside air temperature in degrees C.
 *
 * @return True airspeed in knots.
 */
double
kcas2ktas(double kcas, double pressure, double oat)
{
	double	qc, mach;

	/*
	 * Take the CAS equation and solve for qc (impact pressure):
	 *
	 * qc = P0(((cas^2 / 5* a0^2) + 1)^3.5 - 1)
	 *
	 * Where P0 is pressure at sea level, cas is calibrated airspeed
	 * in m.s^-1 and a0 speed of sound at ISA temperature.
	 */
	qc = ISA_SL_PRESS * (pow((POW2(KT2MPS(kcas)) / (5 *
	    POW2(ISA_SPEED_SOUND))) + 1, 3.5) - 1);

	/*
	 * Next take the impact pressure equation and solve for Mach number:
	 *
	 * M = sqrt(5 * (((qc / P) + 1)^(2/7) - 1))
	 *
	 * Where qc is impact pressure and P is local static pressure.
	 */
	mach = sqrt(5 * (pow((qc / pressure) + 1, 0.2857142857142) - 1));

	/*
	 * Finally convert Mach number to true airspeed at local temperature.
	 */
	return (mach2ktas(mach, oat));
}

/*
 * Converts Mach number to equivalent airspeed (calibrated airspeed corrected
 * for compressibility).
 *
 * @param mach Mach number.
 * @param oat Static outside static air pressure in hPa.
 *
 * @return Equivalent airspeed in knots.
 */
double
mach2keas(double mach, double press)
{
	return (MPS2KT(ISA_SPEED_SOUND * mach * sqrt(press / ISA_SL_PRESS)));
}

/*
 * Converts equivalent airspeed (calibrated airspeed corrected for
 * compressibility) to Mach number.
 *
 * @param mach Equivalent airspeed in knots.
 * @param oat Static outside static air pressure in Pa.
 *
 * @return Mach number.
 */
double
keas2mach(double keas, double press)
{
	/*
	 * Take the mach-to-EAS equation and solve for Mach number:
	 *
	 * M = Ve / (a0 * sqrt(P / P0))
	 *
	 * Where Ve is equivalent airspeed in m.s^-1, P is local static
	 * pressure and P0 is ISA sea level pressure (in Pa).
	 */
	return (KT2MPS(keas) / (ISA_SPEED_SOUND * sqrt(press / ISA_SL_PRESS)));
}

/*
 * Calculates static air pressure from pressure altitude.
 *
 * @param alt Pressure altitude in feet.
 * @param qnh Local QNH in Pa.
 *
 * @return Air pressure in Pa.
 */
double
alt2press(double alt, double qnh)
{
	return (qnh * pow(1 - (ISA_TLR_PER_1M * FEET2MET(alt)) /
	    ISA_SL_TEMP_K, (EARTH_GRAVITY * DRY_AIR_MOL) /
	    (R_univ * ISA_TLR_PER_1M)));
}

/*
 * Calculates pressure altitude from static air pressure.
 *
 * @param alt Static air pressure in hPa.
 * @param qnh Local QNH in hPa.
 *
 * @return Pressure altitude in feet.
 */
double
press2alt(double press, double qnh)
{
	return (MET2FEET((ISA_SL_TEMP_K * (1 - pow(press / qnh,
	    (R_univ * ISA_TLR_PER_1M) / (EARTH_GRAVITY * DRY_AIR_MOL)))) /
	    ISA_TLR_PER_1M));
}

/*
 * Converts pressure altitude to flight level.
 *
 * @param alt Pressure altitude in feet.
 * @param qnh Local QNH in hPa.
 *
 * @return Flight level number.
 */
double
alt2fl(double alt, double qnh)
{
	return (press2alt(alt2press(alt, qnh), ISA_SL_PRESS) / 100);
}

/*
 * Converts flight level to pressure altitude.
 *
 * @param fl Flight level number.
 * @param qnh Local QNH in hPa.
 *
 * @return Pressure altitude in feet.
 */
double
fl2alt(double fl, double qnh)
{
	return (press2alt(alt2press(fl * 100, ISA_SL_PRESS), qnh));
}

/*
 * Converts static air temperature to total air temperature.
 *
 * @param sat Static air temperature in degrees C.
 * @param mach Flight mach number.
 *
 * @return Total air temperature in degrees C.
 */
double
sat2tat(double sat, double mach)
{
	return (KELVIN2C(C2KELVIN(sat) * (1 + ((GAMMA - 1) / 2) * POW2(mach))));
}

/*
 * Converts total air temperature to static air temperature.
 *
 * @param tat Total air temperature in degrees C.
 * @param mach Flight mach number.
 *
 * @return Static air temperature in degrees C.
 */
double
tat2sat(double tat, double mach)
{
	return (KELVIN2C(C2KELVIN(tat) / (1 + ((GAMMA - 1) / 2) * POW2(mach))));
}

/*
 * Converts static air temperature to ISA deviation.
 *
 * @param fl Flight level (barometric altitude at QNE in 100s of ft).
 * @param sat Static air temperature in degrees C.
 *
 * @return ISA deviation in degress C.
 */
double
sat2isadev(double fl, double sat)
{
	return (sat - (ISA_SL_TEMP_C - ((fl / 10) * ISA_TLR_PER_1000FT)));
}

/*
 * Converts ISA deviation to static air temperature.
 *
 * @param fl Flight level (barometric altitude at QNE in 100s of ft).
 * @param isadev ISA deviation in degrees C.
 *
 * @return Local static air temperature.
 */
double
isadev2sat(double fl, double isadev)
{
	return (isadev + ISA_SL_TEMP_C - ((fl / 10) * ISA_TLR_PER_1000FT));
}

/*
 * Returns the speed of sound in m/s in dry air at `oat' degrees C (static).
 */
double
speed_sound(double oat)
{
	/*
	 * This is an approximation that for common flight temperatures
	 * (-65 to +65) is less than 0.1% off.
	 */
	return (20.05 * sqrt(C2KELVIN(oat)));
}

/*
 * Calculates air density.
 *
 * @param pressure Static air pressure in Pa.
 * @param oat Static outside air temperature in degrees C.
 *
 * @return Local air density in kg.m^-3.
 */
double
air_density(double pressure, double oat)
{
	/*
	 * Density of dry air is:
	 *
	 * rho = p / (R_spec * T)
	 *
	 * Where p is local static air pressure, R_spec is the specific gas
	 * constant for dry air and T is absolute temperature.
	 */
	return (pressure / (R_spec * C2KELVIN(oat)));
}

/*
 * Calculates impact pressure. This is dynamic pressure with air
 * compressibility considered.
 *
 * @param pressure Static air pressure in Pa.
 * @param mach Flight mach number.
 *
 * @return Impact pressure in Pa.
 */
double
impact_press(double mach, double pressure)
{
	/*
	 * In isentropic flow, impact pressure for air (gamma = 1.4) is:
	 *
	 * qc = P((1 + 0.2 * M^2)^(7/2) - 1)
	 *
	 * Where P is local static air pressure and M is flight mach number.
	 */
	return (pressure * (pow(1 + 0.2 * POW2(mach), 3.5) - 1));
}

/*
 * Calculates dynamic pressure.
 *
 * @param pressure True airspeed in knots.
 * @param press Static air pressure in Pa.
 * @param oat Static outside air temperature in degrees C.
 *
 * @return Dynamic pressure in Pa.
 */
double
dyn_press(double ktas, double press, double oat)
{
	return (0.5 * air_density(press, oat) * POW2(KT2MPS(ktas)));
}
