# Copyright (c) 2016 Saso Kiselkov
#
# Permission is hereby granted, free of charge, to any person obtaining
# a copy of this software and associated documentation files (the
# "Software"), to deal in the Software without restriction, including
# without limitation the rights to use, copy, modify, merge, publish,
# distribute, sublicense, and/or sell copies of the Software, and to
# permit persons to whom the Software is furnished to do so, subject to
# the following conditions:
#
# The above copyright notice and this permission notice shall be
# included in all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
# EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
# IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
# CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
# TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
# SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.


# AIRCRAFT DEVELOPER NOTICE:
# Please contact X-RAAS's author, so your aircraft can be added to the
# built-in exclusion list for which X-RAAS will automatically avoid
# displaying the fallback ND alert overlay. Alternatively, you can ship
# a custom X-RAAS.cfg with RAAS_ND_alert_overlay_enabled=false.


# This is the ND message decode function. The dr_value arguments is the int
# value of the sim/multiplayer/position/plane19_taxi_light_on dataref.
# The function returns a tuple of two values:
#   * a string containing the decoded message to be displayed on the ND
#   * an integer color code (0 for green, 1 for amber)
# If decoding of dr_value failed, the None object is returned instead.
def XRAAS_ND_msg_decode(dr_value):
	msg_type = dr_value & 0x3f
	color_code = (dr_value >> 6) & 0x3

	def decode_rwy_suffix(val):
		return {
		    1: "R",
		    2: "L",
		    3: "C"
		}.get(val, "")

	if msg_type == 1:
		return ("FLAPS", color_code)
	elif msg_type == 2:
		return ("TOO HIGH", color_code)
	elif msg_type == 3:
		return ("TOO FAST", color_code)
	elif msg_type == 4:
		return ("UNSTABLE", color_code)
	elif msg_type == 5:
		return ("TAXIWAY", color_code)
	elif msg_type == 6:
		return ("SHORT RUNWAY", color_code)
	elif msg_type == 7:
		return ("ALTM SETTING", color_code)
	elif msg_type == 8 or msg_type == 9:
		msg = "APP" if msg_type == 8 else "ON"
		rwy_ID = (dr_value >> 8) & 0x3f
		rwy_suffix = (dr_value >> 14) & 0x3
		rwy_len = (dr_value >> 16) & 0xff

		if rwy_ID == 0:
			return ("%s TAXIWAY" % (msg), color_code)
		elif rwy_ID == 37:
			return ("%s RWYS" % (msg), color_code)
		else:
			if rwy_len == 0:
				return ("%s %02d%s" % (msg, rwy_ID,
				    decode_rwy_suffix(rwy_suffix)), color_code)
			else:
				return ("%s %02d%s %02d" % (msg,
				    rwy_ID, decode_rwy_suffix(rwy_suffix),
				    rwy_len), color_code)
	elif msg_type == 10:
		return ("LONG LANDING", color_code)
	elif msg_type == 11:
		return ("DEEP LANDING", color_code)

	return None
