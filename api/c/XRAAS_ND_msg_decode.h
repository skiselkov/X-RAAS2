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

/*
 * AIRCRAFT DEVELOPER NOTICE:
 * Please contact X-RAAS's author, so your aircraft can be added to the
 * built-in exclusion list for which X-RAAS will automatically avoid
 * displaying the fallback ND alert overlay. Alternatively, you can ship
 * a custom X-RAAS.cfg with RAAS_ND_alert_overlay_enabled=false.
 */

#ifndef	_ND_DR_DECODE_H_
#define	_ND_DR_DECODE_H_

#ifdef __cplusplus
extern "C" {
#endif

/*
 * This is the ND message decode function. Arguments:
 *
 *  dr_value: integer value of the
 *	sim/multiplayer/position/plane19_taxi_light_on dataref.
 *  decoded_msg: pointer to a buffer capable of holding at least 16 bytes.
 *	This will be filled with the decoded message.
 *  color_code: pointer to an integer, which will be filled with the decoded
 *	color value (see below).
 *
 * This function returns 1 if decoding the dataref value was successful
 * (decoded_msg and color_code were both populated), or 0 if decoding failed.
 */
int XRAAS_ND_msg_decode(int dr_value, char decoded_msg[16], int *color_code);

/*
 * Color code values returned in "color_code" from XRAAS_ND_msg_decode().
 */
enum {
	XRAAS_ND_ALERT_GREEN = 0,
	XRAAS_ND_ALERT_AMBER = 1
};

#ifdef __cplusplus
}
#endif

#endif	/* _ND_DR_DECODE_H_ */
