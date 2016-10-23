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
 * NOTE:
 * You don't need this file in your FMS. This is only a sample to show you
 * how to use the XRAAS_ND_msg_decode function and to test the decoder.
 */


#include <stdio.h>
#include <assert.h>

#include "XRAAS_ND_msg_decode.h"

static void
test_decode(int value)
{
	char buf[16];
	int color;

	assert(XRAAS_ND_msg_decode(value, buf, &color) == 1);
	printf("0x%08x\t%s\t%s\n", value,
	    color == XRAAS_ND_ALERT_GREEN ? "GREEN" : "AMBER", buf);
}

int
main(void)
{
	printf("RAW VALUE\tCOLOR\tMESSAGE\n"
	    "----------\t-----\t-------\n");
	test_decode(0x00000041);
	test_decode(0x00000042);
	test_decode(0x00000043);
	test_decode(0x00000044);
	test_decode(0x00000045);
	test_decode(0x00000046);
	test_decode(0x00000047);
	test_decode(0x00002308);
	test_decode(0x00006308);
	test_decode(0x00002508);
	test_decode(0x00142348);
	test_decode(0x00086348);
	test_decode(0x00000049);
	test_decode(0x00002309);
	test_decode(0x00006309);
	test_decode(0x00002509);
	test_decode(0x0014E349);
	test_decode(0x0008A349);
	test_decode(0x0000004A);

	return (0);
}
