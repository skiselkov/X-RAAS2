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

#include <alc.h>

#include "assert.h"
#include "list.h"
#include "log.h"
#include "riff.h"
#include "types.h"

#include "wav.h"

#define	WAVE_ID	0x45564157u	/* 'WAVE' */
#define	FMT_ID	0x20746D66u	/* 'FMT ' */
#define	DATA_ID	0x61746164u	/* 'DATA' */

static ALCdevice *dev = NULL;
static ALCcontext *old_ctx = NULL, *my_ctx = NULL;
static bool_t use_shared = B_FALSE;
static bool_t ctx_saved = B_FALSE;
static bool_t openal_inited = B_FALSE;

/*
 * ctx_save/ctx_restore must be used to bracket all OpenAL calls. This makes
 * sure private contexts are handled properly (when in use). If shared
 * contexts are used, these functions are no-ops.
 */
static void
ctx_save(void)
{
	if (use_shared)
		return;

	ASSERT(!ctx_saved);
	dbg_log("wav", 1, "ctx_save()");
	old_ctx = alcGetCurrentContext();
	alcMakeContextCurrent(my_ctx);
	ctx_saved = B_TRUE;
}

static void
ctx_restore(void)
{
	if (use_shared)
		return;

	ASSERT(ctx_saved);
	dbg_log("wav", 1, "ctx_restore()");
	if (old_ctx != NULL)
		alcMakeContextCurrent(old_ctx);
	ctx_saved = B_FALSE;
}

void
openal_set_shared_ctx(bool_t flag)
{
	ASSERT(!openal_inited);
	dbg_log("wav", 1, "openal_set_shared_ctx = %d", flag);
	use_shared = flag;
}

bool_t
openal_init(void)
{
	ASSERT(!openal_inited);

	dbg_log("wav", 1, "openal_init");

	ctx_save();

	if (!use_shared) {
		dev = alcOpenDevice(NULL);
		if (dev == NULL) {
			logMsg("Cannot init audio system: device open "
			    "failed (%d)."
#if	IBM
			    "\nTry to configure X-RAAS with "
			    "\"shared_audio_ctx = true\" and retest."
#endif	/* IBM */
			    , alGetError());
			ctx_restore();
			return (B_FALSE);
		}
		my_ctx = alcCreateContext(dev, NULL);
		if (my_ctx == NULL) {
			logMsg("Cannot init audio system: create context "
			    "failed (%d)"
#if	IBM
			    "\nTry to configure X-RAAS with "
			    "\"shared_audio_ctx = true\" and retest."
#endif	/* IBM */
			    , alGetError());
			alcCloseDevice(dev);
			ctx_restore();
			return (B_FALSE);
		}
	}

	ctx_restore();

	openal_inited = B_TRUE;

	return (B_TRUE);
}

void
openal_fini()
{
	ASSERT(openal_inited);
	openal_inited = B_FALSE;

	dbg_log("wav", 1, "openal_fini");
	if (!use_shared) {
		alcDestroyContext(my_ctx);
		alcCloseDevice(dev);
		my_ctx = NULL;
		dev = NULL;
	}
	openal_inited = B_FALSE;
}

/*
 * Loads a WAV file from a file and returns a buffered representation
 * ready to be passed to OpenAL. Currently we only support mono or
 * stereo raw PCM (uncompressed) WAV files.
 */
wav_t *
wav_load(const char *filename, const char *descr_name)
{
	wav_t *wav = NULL;
	FILE *fp;
	size_t filesz;
	riff_chunk_t *riff = NULL;
	uint8_t *filebuf = NULL;
	uint8_t *chunkp;
	size_t chunksz;
	int sample_sz;
	ALuint err;
	ALfloat zeroes[3] = { 0.0, 0.0, 0.0 };

	ASSERT(openal_inited);

	dbg_log("wav", 1, "Loading wav file %s", filename);

	if ((fp = fopen(filename, "rb")) == NULL) {
		logMsg("Error loading WAV file \"%s\": can't open file.",
		    filename);
		return (NULL);
	}

	fseek(fp, 0, SEEK_END);
	filesz = ftell(fp);
	fseek(fp, 0, SEEK_SET);

	if ((wav = calloc(1, sizeof (*wav))) == NULL)
		goto errout;
	if ((filebuf = malloc(filesz)) == NULL)
		goto errout;
	if (fread(filebuf, 1, filesz, fp) != filesz)
		goto errout;
	if ((riff = riff_parse(WAVE_ID, filebuf, filesz)) == NULL) {
		logMsg("Error loading WAV file \"%s\": file doesn't appear to "
		    "be valid RIFF.", filename);
		goto errout;
	}

	wav->name = strdup(descr_name);

	chunkp = riff_find_chunk(riff, &chunksz, FMT_ID, 0);
	if (chunkp == NULL || chunksz != sizeof (wav->fmt)) {
		logMsg("Error loading WAV file \"%s\": file missing FMT chunk.",
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
		logMsg("Error loading WAV file \"%s\": unsupported audio "
		    "format.", filename);
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

	ctx_save();
	alGenBuffers(1, &wav->albuf);
	if ((err = alGetError()) != AL_NO_ERROR) {
		logMsg("Error loading WAV file %s: alGenBuffers failed (%d).",
		    filename, err);
		ctx_restore();
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
		logMsg("Error loading WAV file %s: alBufferData failed (%d).",
		    filename, err);
		ctx_restore();
		goto errout;
	}

	alGenSources(1, &wav->alsrc);
	if ((err = alGetError()) != AL_NO_ERROR) {
		logMsg("Error loading WAV file %s: alGenSources failed (%d).",
		    filename, err);
		ctx_restore();
		goto errout;
	}
#define	CHECK_ERROR(stmt) \
	do { \
		stmt; \
		if ((err = alGetError()) != AL_NO_ERROR) { \
			logMsg("Error loading WAV file %s, \"%s\" failed " \
			    "with error %d", filename, #stmt, err); \
			alDeleteSources(1, &wav->alsrc); \
			wav->alsrc = 0; \
			ctx_restore(); \
			goto errout; \
		} \
	} while (0)
	CHECK_ERROR(alSourcei(wav->alsrc, AL_BUFFER, wav->albuf));
	CHECK_ERROR(alSourcef(wav->alsrc, AL_PITCH, 1.0));
	CHECK_ERROR(alSourcef(wav->alsrc, AL_GAIN, 1.0));
	CHECK_ERROR(alSourcei(wav->alsrc, AL_LOOPING, 0));
	CHECK_ERROR(alSourcefv(wav->alsrc, AL_POSITION, zeroes));
	CHECK_ERROR(alSourcefv(wav->alsrc, AL_VELOCITY, zeroes));

	ctx_restore();

	dbg_log("wav", 1, "wav load complete, duration %.2fs", wav->duration);

	riff_free_chunk(riff);
	free(filebuf);
	fclose(fp);

	return (wav);

errout:
	if (filebuf != NULL)
		free(filebuf);
	if (riff != NULL)
		riff_free_chunk(riff);
	wav_free(wav);
	fclose(fp);

	return (NULL);
}

/*
 * Destroys a WAV file as returned by wav_load().
 */
void
wav_free(wav_t *wav)
{
	if (wav == NULL)
		return;

	dbg_log("wav", 1, "wav_free %s", wav->name);

	ASSERT(openal_inited);

	ctx_save();
	free(wav->name);
	if (wav->alsrc != 0) {
		alSourceStop(wav->alsrc);
		alDeleteSources(1, &wav->alsrc);
	}
	if (wav->albuf != 0)
		alDeleteBuffers(1, &wav->albuf);
	ctx_restore();

	free(wav);
}

/*
 * Sets the audio gain (volume) of a WAV file from 0.0 (silent) to 1.0
 * (full volume).
 */
void
wav_set_gain(wav_t *wav, float gain)
{
	ALuint err;

	if (wav == NULL || wav->alsrc == 0)
		return;

	dbg_log("wav", 1, "wav_set_gain %s %f", wav->name, (double)gain);

	ASSERT(openal_inited);

	ctx_save();

	alSourcef(wav->alsrc, AL_GAIN, gain);
	if ((err = alGetError()) != AL_NO_ERROR)
		logMsg("Error changing gain of WAV %s, error %d.",
		    wav->name, err);

	ctx_restore();
}

/*
 * Starts playback of a WAV file loaded through wav_load. Playback volume
 * is full (1.0) or the last value set by wav_set_gain.
 */
void
wav_play(wav_t *wav)
{
	ALuint err;

	if (wav == NULL)
		return;

	dbg_log("wav", 1, "wav_play %s", wav->name);

	ASSERT(openal_inited);

	ctx_save();

	alSourcePlay(wav->alsrc);
	if ((err = alGetError()) != AL_NO_ERROR)
		logMsg("Can't play sound: alSourcePlay failed (%d).", err);

	ctx_restore();
}

/*
 * Stops playback of a WAV file started via wav_play and resets the playback
 * position back to the start of the file.
 */
void
wav_stop(wav_t *wav)
{
	if (wav == NULL)
		return;

	dbg_log("wav", 1, "wav_stop %s", wav->name);

	if (wav->alsrc == 0)
		return;

	ASSERT(openal_inited);
	ctx_save();
	alSourceStop(wav->alsrc);
	ctx_restore();
}
