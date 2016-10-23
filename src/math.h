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

#ifndef	_XRAAS2_MATH_H_
#define	_XRAAS2_MATH_H_

#ifdef	__cplusplus
extern "C" {
#endif

#define	POW3(x)	((x) * (x) * (x))
#define	POW2(x)	((x) * (x))
#define	ROUND_ERROR	1e-10

unsigned quadratic_solve(double a, double b, double c, double x[2]);
double fx_lin(double x, double x1, double y1, double x2, double y2);

#ifdef	__cplusplus
}
#endif

#endif	/* _XRAAS2_GEOM_H_ */
