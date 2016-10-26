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

#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <OpenAL/alc.h>

#include "helpers.h"
#include "list.h"
#include "log.h"
#include "types.h"

#include "wav.h"

struct riff_chunk {
	bool_t		bswap;
	uint32_t	fourcc;
	uint8_t		*data;
	uint32_t	sz;

	/* populated if fourcc is RIFF_ID or LIST_ID */
	uint32_t	listcc;
	list_t		subchunks;

	list_node_t	node;
};

#define	RIFF_ID	0x46464952u	/* 'RIFF' */
#define	LIST_ID	0x54534C49u	/* 'LIST' */
#define	WAVE_ID	0x45564157u	/* 'WAVE' */
#define	FMT_ID	0x20746D66u	/* 'FMT ' */
#define	DATA_ID	0x61746164u	/* 'DATA' */

void
riff_free_chunk(riff_chunk_t *c)
{
	if (c->fourcc == RIFF_ID || c->fourcc == LIST_ID) {
		for (riff_chunk_t *sc = list_head(&c->subchunks); sc != NULL;
		    sc = list_head(&c->subchunks))
			riff_free_chunk(sc);
		list_destroy(&c->subchunks);
	}
	free(c);
}

static riff_chunk_t *
riff_parse_chunk(uint8_t *buf, size_t bufsz, bool_t bswap)
{
	riff_chunk_t *c = calloc(sizeof (*c), 1);

	memcpy(&c->fourcc, buf, sizeof (c->fourcc));
	memcpy(&c->sz, buf + 4, sizeof (c->sz));
	if (bswap) {
		c->fourcc = BSWAP32(c->fourcc);
		c->sz = BSWAP32(c->sz);
		c->bswap = B_TRUE;
	}
	if (c->sz > bufsz - 8) {
		free(c);
		return (NULL);
	}
	if (c->fourcc == RIFF_ID || c->fourcc == LIST_ID) {
		size_t consumed = 0;
		uint8_t *subbuf;

		/* check there's enough data for a listcc field */
		if (c->sz < 4) {
			free(c);
			return (NULL);
		}
		memcpy(&c->listcc, buf + 8, sizeof (c->listcc));
		if (bswap)
			c->listcc = BSWAP32(c->listcc);
		/* we exclude the listcc field from our data pointer */
		c->data = buf + 12;
		list_create(&c->subchunks, sizeof (riff_chunk_t),
		    offsetof(riff_chunk_t, node));

		subbuf = c->data;
		while (consumed != c->sz - 4) {
			riff_chunk_t *sc = riff_parse_chunk(subbuf,
			    c->sz - 4 - consumed, bswap);

			if (sc == NULL) {
				riff_free_chunk(c);
				return (NULL);
			}
			list_insert_tail(&c->subchunks, sc);
			consumed += sc->sz + 8;
			subbuf += sc->sz + 8;
			if (consumed & 1) {
				/* realign to two-byte boundary */
				consumed++;
				subbuf++;
			}
			ASSERT(consumed <= c->sz - 4);
			ASSERT(subbuf <= buf + bufsz);
		}
	} else {
		/* plain data chunk */
		c->data = buf + 8;
	}

	return (c);
}

static riff_chunk_t *
riff_parse(uint32_t filetype, uint8_t *buf, size_t bufsz)
{
	bool_t bswap;
	const uint32_t *buf32 = (uint32_t *)buf;
	uint32_t ftype;

	/* check the buffer isn't too small to be a valid RIFF file */
	if (bufsz < 3 * sizeof (uint32_t))
		return (NULL);

	/* make sure the header signature matches & determine endianness */
	if (((uint32_t *)buf)[0] == RIFF_ID)
		bswap = B_FALSE;
	else if (buf32[0] == BSWAP32(RIFF_ID))
		bswap = B_TRUE;
	else
		return (NULL);

	/* check the file size fits in our buffer */
	if ((bswap ? BSWAP32(buf32[1]) : buf32[1]) > bufsz - 8)
		return (NULL);
	/* check the file type requested by the caller */
	ftype = (bswap ? BSWAP32(buf32[2]) : buf32[2]);
	if (ftype != filetype)
		return (NULL);

	/* now we can be reasonably sure that this is a somewhat valid file */
	return (riff_parse_chunk(buf, bufsz, bswap));
}

/*
 * Locates a specific chunk in a RIFF file. In `topchunk' pass the top-level
 * RIFF file chunk. The `chunksz' will be filled with the size of the chunk
 * being searched for (if found). The remaining arguments must be a
 * 0-terminated list of uint32_t FourCCs of the nested chunks. Don't include
 * the top-level chunk ID.
 * Returns a pointer to the body of the chunk (if found, and chunksz will be
 * populated with the amount of data in the chunk), or NULL if not found.
 */
static uint8_t *
riff_find_chunk(riff_chunk_t *topchunk, size_t *chunksz, ...)
{
	va_list ap;
	uint32_t fourcc;

	ASSERT(topchunk != NULL);
	ASSERT(topchunk->fourcc == RIFF_ID);
	ASSERT(chunksz != NULL);

	va_start(ap, chunksz);
	while ((fourcc = va_arg(ap, uint32_t)) != 0) {
		ASSERT(fourcc != LIST_ID && fourcc != RIFF_ID);
		if (topchunk->fourcc != LIST_ID &&
		    topchunk->fourcc != RIFF_ID)
			return (NULL);
		for (riff_chunk_t *c = list_head(&topchunk->subchunks);
		    c != NULL; c = list_next(&topchunk->subchunks, c)) {
			if (c->fourcc == fourcc || (c->listcc == fourcc &&
			    (c->fourcc == LIST_ID || c->fourcc == RIFF_ID))) {
				topchunk = c;
				continue;
			}
		}
		return (NULL);
	}
	va_end(ap);

	ASSERT(topchunk != NULL);

	*chunksz = topchunk->sz;
	return (topchunk->data);
}

/*
 * Loads a WAV file from a file and returns a buffered representation
 * ready to be passed to OpenAL. Currently we only support mono or
 * stereo raw PCM (uncompressed) WAV files.
 */
wav_t *
xraas_wav_load(const char *filename)
{
	wav_t *wav = NULL;
	FILE *fp = fopen(filename, "rb");
	size_t filesz;
	riff_chunk_t *riff = NULL;
	uint8_t *filebuf = NULL;
	uint8_t *chunkp;
	size_t chunksz;
	int sample_sz;
	ALuint err;

	if (fp == NULL) {
		logMsg("Error loading WAV file %s: can't open file.", filename);
		return (NULL);
	}

	fseek(fp, 0, SEEK_END);
	filesz = ftell(fp);
	fseek(fp, 0, SEEK_SET);

	if ((wav = calloc(sizeof (*wav), 1)) == NULL)
		goto errout;
	if ((filebuf = malloc(filesz)) == NULL)
		goto errout;
	if (fread(filebuf, 1, filesz, fp) != filesz)
		goto errout;
	if ((riff = riff_parse(WAVE_ID, filebuf, filesz)) == NULL) {
		logMsg("Error loading WAV file %s: file doesn't appear to "
		    "be valid RIFF.", filename);
		goto errout;
	}

	chunkp = riff_find_chunk(riff, &chunksz, FMT_ID, 0);
	if (chunkp == NULL || chunksz != sizeof (wav->fmt)) {
		logMsg("Error loading WAV file %s: file missing FMT chunk.",
		    filename);
		goto errout;
	}
	memcpy(&wav->fmt, chunkp, sizeof (wav->fmt));
	if (riff->bswap) {
		wav->fmt.datafmt = BSWAP16(wav->fmt.datafmt);
		wav->fmt.n_channels = BSWAP16(wav->fmt.n_channels);
		wav->fmt.srate = BSWAP32(wav->fmt.srate);
		wav->fmt.byte_rate = BSWAP32(wav->fmt.byte_rate);
		wav->fmt.bps = BSWAP16(wav->fmt.bps);
	}

	/* format support check */
	if (wav->fmt.datafmt != 1 ||
	    (wav->fmt.n_channels != 1 && wav->fmt.n_channels != 2) ||
	    (wav->fmt.bps != 8 && wav->fmt.bps != 16)) {
		logMsg("Error loading WAV file %s: unsupported audio format.",
		    filename);
		goto errout;
	}

	/*
	 * Check the DATA chunk is present and contains the correct number
	 * of samples.
	 */
	sample_sz = (wav->fmt.n_channels * wav->fmt.bps) / 8;
	chunkp = riff_find_chunk(riff, &chunksz, DATA_ID, 0);
	if (chunkp == NULL || (chunksz & (sample_sz - 1)) != 0) {
		logMsg("Error loading WAV file %s: DATA chunk missing or "
		    "contains bad number of samples.", filename);
		goto errout;
	}

	wav->duration = ((double)(chunksz / sample_sz)) / wav->fmt.srate;

	/* BSWAP the samples if necessary */
	if (riff->bswap && wav->fmt.bps == 16) {
		for (uint16_t *s = (uint16_t *)chunkp;
		    (uint8_t *)s < chunkp + chunksz;
		    s++)
			*s = BSWAP16(*s);
	}

	alGenBuffers(1, &wav->albuf);
	if ((err = alGetError()) != AL_NO_ERROR) {
		logMsg("Error loading WAV file %s: cannot generate AL "
		    "buffer (%d).", filename, err);
		goto errout;
	}
	if (wav->fmt.bps == 16)
		alBufferData(wav->albuf, wav->fmt.n_channels == 2 ?
		    AL_FORMAT_STEREO16 : AL_FORMAT_MONO16,
		    chunkp, chunksz, wav->fmt.srate);
	else
		alBufferData(wav->albuf, wav->fmt.n_channels == 2 ?
		    AL_FORMAT_STEREO8 : AL_FORMAT_MONO8,
		    chunkp, chunksz, wav->fmt.srate);

	if ((err = alGetError()) != AL_NO_ERROR) {
		logMsg("Error loading WAV file %s: cannot buffer data (%d).",
		    filename, err);
		goto errout;
	}

	riff_free_chunk(riff);
	free(filebuf);
	fclose(fp);

	return (wav);

errout:
	if (filebuf)
		free(filebuf);
	if (riff)
		riff_free_chunk(riff);
	xraas_wav_free(wav);
	fclose(fp);

	return (NULL);
}

/*
 * Destroys a WAV file as returned by xraas_wav_load().
 */
void
xraas_wav_free(wav_t *wav)
{
	if (wav != NULL) {
		if (wav->alsrc != 0) {
			alSourceStop(wav->alsrc);
			alDeleteSources(1, &wav->alsrc);
		}
		if (wav->albuf != 0)
			alDeleteBuffers(1, &wav->albuf);
		free(wav);
	}
}

static bool_t
audio_init(void)
{
	ALCcontext *ctx = alcGetCurrentContext();

	if (ctx == NULL) {
		ALCdevice *dev = alcOpenDevice(NULL);

		if (dev == NULL) {
			logMsg("Cannot init audio system: device open "
			    "failed (%d)", alGetError());
			return (B_FALSE);
		}
		ctx = alcCreateContext(dev, NULL);
		if (ctx == NULL) {
			logMsg("Cannot init audio system: create context "
			    "failed (%d)", alGetError());
			alcCloseDevice(dev);
			return (B_FALSE);
		}
		alcMakeContextCurrent(ctx);
	}

	return (B_TRUE);
}

void
xraas_wav_play(wav_t *wav, double gain)
{
	ALuint err;

	if (!audio_init)
		return;

	if (wav->alsrc == 0) {
		ALfloat zeroes[3] = { 0.0, 0.0, 0.0 };

		alGenSources(1, &wav->alsrc);
		if ((err = alGetError()) != AL_NO_ERROR) {
			logMsg("Can't play sound: alGenSources failed (%d).",
			    err);
			return;
		}
		alSourcei(wav->alsrc, AL_BUFFER, wav->albuf);
		alSourcef(wav->alsrc, AL_PITCH, 1.0);
		alSourcef(wav->alsrc, AL_GAIN, gain);
		alSourcei(wav->alsrc, AL_LOOPING, 0);
		alSourcefv(wav->alsrc, AL_POSITION, zeroes);
		alSourcefv(wav->alsrc, AL_VELOCITY, zeroes);
	}

	alSourcePlay(wav->alsrc);
	if ((err = alGetError()) != AL_NO_ERROR)
		logMsg("Can't play sound: alSourcePlay failed (%d).", err);
}

void
xraas_wav_stop(wav_t *wav)
{
	if (wav->alsrc == 0)
		return;
	alSourceStop(wav->alsrc);
}
