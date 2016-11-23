/*
 * Copyright (c) 2016 Saso Kiselkov
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <string.h>
#include <stdio.h>

enum {
	ALERT_FLAPS =		1,
	ALERT_TOO_HIGH =	2,
	ALERT_TOO_FAST =	3,
	ALERT_UNSTABLE =	4,
	ALERT_TWY =		5,
	ALERT_SHORT_RWY =	6,
	ALERT_ALTM_SETTING =	7,
	ALERT_APP =		8,
	ALERT_ON =		9,
	ALERT_LONG_LAND =	10,
	ALERT_DEEP_LAND =	11
};

#define MSGLEN	16

static const char *
decode_rwy_suffix(int val)
{
	switch (val) {
	case 1: return "R";
	case 2: return "L";
	case 3: return "C";
	default: return "";
	}
}

int
XRAAS_ND_msg_decode(int dr_value, char decoded_msg[MSGLEN], int *color_code)
{
	int msg_type = dr_value & 0x3f;

	*color_code = (dr_value >> 6) & 0x3;

	switch (msg_type) {
	case ALERT_FLAPS:
		strcpy(decoded_msg, "FLAPS");
		return (1);
	case ALERT_TOO_HIGH:
		strcpy(decoded_msg, "TOO HIGH");
		return (1);
	case ALERT_TOO_FAST:
		strcpy(decoded_msg, "TOO FAST");
		return (1);
	case ALERT_UNSTABLE:
		strcpy(decoded_msg, "UNSTABLE");
		return (1);
	case ALERT_TWY:
		strcpy(decoded_msg, "TAXIWAY");
		return (1);
	case ALERT_SHORT_RWY:
		strcpy(decoded_msg, "SHORT RUNWAY");
		return (1);
	case ALERT_ALTM_SETTING:
		strcpy(decoded_msg, "ALTM SETTING");
		return (1);
	case ALERT_APP:
	case ALERT_ON: {
		const char *msg = (msg_type == ALERT_APP) ? "APP" : "ON";
		int rwy_ID = (dr_value >> 8) & 0x3f;
		int rwy_suffix = (dr_value >> 14) & 0x3;
		int rwy_len = (dr_value >> 16) & 0xff;

		if (rwy_ID == 0) {
			snprintf(decoded_msg, MSGLEN, "%s TAXIWAY", msg);
		} else if (rwy_ID == 37) {
			snprintf(decoded_msg, MSGLEN, "%s RWYS", msg);
		} else {
			if (rwy_len == 0)
				snprintf(decoded_msg, MSGLEN, "%s %02d%s", msg,
				    rwy_ID, decode_rwy_suffix(rwy_suffix));
			else
				snprintf(decoded_msg, MSGLEN, "%s %02d%s %02d",
				    msg, rwy_ID, decode_rwy_suffix(rwy_suffix),
				    rwy_len);
		}
		return (1);
	}
	case ALERT_LONG_LAND:
		strcpy(decoded_msg, "LONG LANDING");
		return (1);
	case ALERT_DEEP_LAND:
		strcpy(decoded_msg, "DEEP LANDING");
		return (1);
	}

	return (0);
}
