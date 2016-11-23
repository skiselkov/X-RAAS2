-- Copyright (c) 2016 Saso Kiselkov
--
-- Permission is hereby granted, free of charge, to any person obtaining
-- a copy of this software and associated documentation files (the
-- "Software"), to deal in the Software without restriction, including
-- without limitation the rights to use, copy, modify, merge, publish,
-- distribute, sublicense, and/or sell copies of the Software, and to
-- permit persons to whom the Software is furnished to do so, subject to
-- the following conditions:
--
-- The above copyright notice and this permission notice shall be
-- included in all copies or substantial portions of the Software.
--
-- THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
-- EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
-- MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
-- IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
-- CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
-- TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
-- SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.


-- AIRCRAFT DEVELOPER NOTICE:
-- DO NOT remove this file from your X-RAAS installation. It is used by
-- X-RAAS to implement the fallback ND overlay.
-- Also, please contact X-RAAS's author, so your aircraft can be added to
-- the built-in exclusion list for which X-RAAS will automatically avoid
-- displaying the fallback ND alert overlay. Alternatively, you can ship
-- a custom X-RAAS.cfg with RAAS_ND_alert_overlay_enabled=false.


-- LUA COMPATIBILITY NOTICE:
-- If your FMS plugin runs on the FlyWithLua engine, then all you need is
-- this file. However, if you're using any other Lua interpreter, please
-- make sure your Lua interpreter has the Lua BitOp module available in it.
-- Alternatively, you can rewrite the bitwise operations below to use
-- whatever you have available.


-- This is the ND message decode function. The dr_value argument is the int
-- value of the sim/multiplayer/position/plane19_taxi_light_on dataref.
-- The function returns two values:
--   * a string containing the decoded message to be displayed on the ND
--   * an integer color code (0 for green, 1 for amber)
-- If decoding of dr_value failed, two `nil' values are returned instead.
function XRAAS_ND_msg_decode(dr_value)
	local bit = require 'bit'
	local msg_type = bit.band(dr_value, 0x3f)
	local color_code = bit.band(bit.rshift(dr_value, 6), 0x3)

	local function decode_rwy_suffix(val)
		if val == 1 then
			return "R"
		elseif val == 2 then
			return "L"
		elseif val == 3 then
			return "C"
		else
			return ""
		end
	end

	if msg_type == 1 then
		return "FLAPS", color_code
	elseif msg_type == 2 then
		return "TOO HIGH", color_code
	elseif msg_type == 3 then
		return "TOO FAST", color_code
	elseif msg_type == 4 then
		return "UNSTABLE", color_code
	elseif msg_type == 5 then
		return "TAXIWAY", color_code
	elseif msg_type == 6 then
		return "SHORT RUNWAY", color_code
	elseif msg_type == 7 then
		return "ALTM SETTING", color_code
	elseif msg_type == 8 or msg_type == 9 then
		local msg
		local rwy_ID = bit.band(bit.rshift(dr_value, 8), 0x3f)
		local rwy_suffix = bit.band(bit.rshift(dr_value, 14), 0x3)
		local rwy_len = bit.band(bit.rshift(dr_value, 16), 0xff)

		if msg_type == 8 then
			msg = "APP"
		else
			msg = "ON"
		end
		if rwy_ID == 0 then
			return string.format("%s TAXIWAY", msg), color_code
		elseif rwy_ID == 37 then
			return string.format("%s RWYS", msg), color_code
		else
			if rwy_len == 0 then
				return string.format("%s %02d%s", msg, rwy_ID,
				    decode_rwy_suffix(rwy_suffix)), color_code
			else
				return string.format("%s %02d%s %02d", msg,
				    rwy_ID, decode_rwy_suffix(rwy_suffix),
				    rwy_len), color_code
			end
		end
	elseif msg_type == 10 then
		return "LONG LANDING", color_code
	elseif msg_type == 11 then
		return "DEEP LANDING", color_code
	else
		return nil, nil
	end
end
