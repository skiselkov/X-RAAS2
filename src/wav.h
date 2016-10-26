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

#ifndef	_XRAAS_WAV_H_
#define	_XRAAS_WAV_H_

#include <stdint.h>
#include <OpenAL/al.h>

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct riff_chunk riff_chunk_t;

typedef struct wav_fmt_hdr {
	uint16_t	datafmt;	/* PCM = 1 */
	uint16_t	n_channels;
	uint32_t	srate;		/* sample rate in Hz */
	uint32_t	byte_rate;	/* (srate * bps * #channels) / 8 */
	uint16_t	padding;	/* unused */
	uint16_t	bps;		/* bits per sample */
} wav_fmt_hdr_t;

typedef struct wav_s {
	wav_fmt_hdr_t	fmt;
	double		duration;	/* in seconds */
	ALuint		albuf;
	ALuint		alsrc;
} wav_t;

wav_t *xraas_wav_load(const char *filename);
void xraas_wav_free(wav_t *wav);
void xraas_wav_play(wav_t *wav, double gain);
void xraas_wav_stop(wav_t *wav);

#ifdef	__cplusplus
}
#endif

#endif	/* _XRAAS_WAV_H_ */
