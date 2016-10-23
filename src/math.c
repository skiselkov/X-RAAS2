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

#include <math.h>

#include "helpers.h"
#include "math.h"

/*
 * Solves quadratic equation ax^2 + bx + c = 0. Solutions are placed in 'x'.
 * Returns the number of solutions (0, 1 or 2).
 */
unsigned
quadratic_solve(double a, double b, double c, double x[2])
{
	double tmp;

	/* Actually just a linear equation. */
	if (a == 0) {
		if (b == 0)
			return (0);
		x[0] = -c / b;
		return (1);
	}

	tmp = POW2(b) - 4 * a * c;
	if (tmp > ROUND_ERROR) {
		double tmp_sqrt = sqrt(tmp);
		x[0] = (-b + tmp_sqrt) / (2 * a);
		x[1] = (-b - tmp_sqrt) / (2 * a);
		return (2);
	} else if (tmp > -ROUND_ERROR) {
		x[0] = -b / (2 * a);
		return (1);
	} else {
		return (0);
	}
}

/*
 * Interpolates a linear function defined by two points.
 *
 * @param x Point who's 'y' value we're looking for on the function.
 * @param x1 First reference point's x coordinate.
 * @param y1 First reference point's y coordinate.
 * @param x2 Second reference point's x coordinate.
 * @param y2 Second reference point's y coordinate.
 */
double
fx_lin(double x, double x1, double y1, double x2, double y2)
{
	return (((x - x1) / (x2 - x1)) * (y2 - y1) + y1);
}
