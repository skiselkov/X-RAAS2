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
 * Copyright 2016 Saso Kiselkov. All rights reserved.
 */

#include <stdlib.h>

#include "dr_intf.h"

static int
read_int_cb(void *refcon)
{
	int *ptr = refcon;
	return (*(ptr));
}

static void
write_int_cb(void *refcon, int value)
{
	int *ptr = refcon;
	*ptr = value;
}

static float
read_float_cb(void *refcon)
{
	float *ptr = refcon;
	return (*(ptr));
}

static void
write_float_cb(void *refcon, float value)
{
	float *ptr = refcon;
	*ptr = value;
}

/*
 * Sets up an integer dataref that will read and optionally write to
 * an int*.
 */
XPLMDataRef
dr_intf_add_i(const char *dr_name, int *value, bool_t writable)
{
	return (XPLMRegisterDataAccessor(dr_name, xplmType_Int, writable,
	    read_int_cb, writable ? write_int_cb : NULL, NULL, NULL,
	    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, value,
	    writable ? value : NULL));
}

/*
 * Sets up a float dataref that will read and optionally write to
 * an float*.
 */
XPLMDataRef
dr_intf_add_f(const char *dr_name, float *value, bool_t writable)
{
	return (XPLMRegisterDataAccessor(dr_name, xplmType_Float, writable,
	    NULL, NULL, read_float_cb, writable ? write_float_cb : NULL,
	    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, value,
	    writable ? value : NULL));
}

/*
 * Destroys a dataref previously set up using dr_intf_add_{i,f}.
 */
void
dr_intf_remove(XPLMDataRef dr)
{
	XPLMUnregisterDataAccessor(dr);
}
