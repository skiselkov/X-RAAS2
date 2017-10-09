/*
 * CDDL HEADER START
 *
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 *
 * CDDL HEADER END
 *
 * Copyright 2017 Saso Kiselkov. All rights reserved.
 */

#include <stddef.h>
#include <string.h>

#include <XPLMProcessing.h>
#include <XPLMUtilities.h>

#include <acfutils/assert.h>
#include <acfutils/list.h>
#include <acfutils/wav.h>
#include <acfutils/time.h>

#include "dbg_log.h"
#include "init_msg.h"
#include "snd_sys.h"

typedef struct {
	msg_type_t	*msgs;
	int		num_msgs;
	int		cur_msg;
	msg_prio_t	prio;
	int64_t		started;

	list_node_t	node;
} ann_t;

typedef struct msg {
	const char *name;
	const char *text;
	wav_t *wav;
} msg_t;

static msg_t voice_msgs[NUM_MSGS] = {
	{ .name = "0", .text = "Zero, ", .wav = NULL },
	{ .name = "1", .text = "One, ", .wav = NULL },
	{ .name = "2", .text = "Two, ", .wav = NULL },
	{ .name = "3", .text = "Three, ", .wav = NULL },
	{ .name = "4", .text = "Four, ", .wav = NULL },
	{ .name = "5", .text = "Five, ", .wav = NULL },
	{ .name = "6", .text = "Six, ", .wav = NULL },
	{ .name = "7", .text = "Seven, ", .wav = NULL },
	{ .name = "8", .text = "Eight, ", .wav = NULL },
	{ .name = "9", .text = "Nine, ", .wav = NULL },
	{ .name = "30", .text = "Thirty, ", .wav = NULL },
	{ .name = "alt_set", .text = "Altimeter setting, ", .wav = NULL },
	{ .name = "apch", .text = "Approaching, ", .wav = NULL },
	{ .name = "avail", .text = "Available, ", .wav = NULL },
	{ .name = "caution", .text = "Caution! ", .wav = NULL },
	{ .name = "center", .text = "Center, ", .wav = NULL },
	{ .name = "deep_land", .text = "Deep landing! ", .wav = NULL },
	{ .name = "feet", .text = "Feet, ", .wav = NULL },
	{ .name = "flaps", .text = "Flaps! ", .wav = NULL },
	{ .name = "hundred", .text = "Hundred, ", .wav = NULL },
	{ .name = "left", .text = "Left, ", .wav = NULL },
	{ .name = "long_land", .text = "Long landing! ", .wav = NULL },
	{ .name = "meters", .text = "Meters, ", .wav = NULL },
	{ .name = "on_rwy", .text = "On runway, ", .wav = NULL },
	{ .name = "on_twy", .text = "On taxiway, ", .wav = NULL },
	{ .name = "right", .text = "Right, ", .wav = NULL },
	{ .name = "rmng", .text = "Remaining, ", .wav = NULL },
	{ .name = "rwys", .text = "Runways, ", .wav = NULL },
	{ .name = "pause", .text = " , , , ", .wav = NULL },
	{ .name = "short_rwy", .text = "Short runway! ", .wav = NULL },
	{ .name = "thousand", .text = "Thousand, ", .wav = NULL },
	{ .name = "too_fast", .text = "Too fast! ", .wav = NULL },
	{ .name = "too_high", .text = "Too high! ", .wav = NULL },
	{ .name = "twy", .text = "Taxiway! ", .wav = NULL },
	{ .name = "unstable", .text = "Unstable! ", .wav = NULL }
};

static bool_t inited = B_FALSE;
static bool_t view_is_ext = B_FALSE;
static list_t playback_queue;
static alc_t *alc = NULL;
static bool_t openal_shared = B_FALSE;

static void
set_sound_on(bool_t flag)
{
	for (int i = 0; i < NUM_MSGS; i++)
		wav_set_gain(voice_msgs[i].wav,
		    flag ? xraas_state->config.voice_volume : 0);
}

static int
resolve_priority_ordering(ann_t *ann, msg_prio_t new_prio)
{
	if (ann->prio > new_prio) {
		/* current message overrides us, be quiet */
		dbg_log(snd, 1, "priority too low, suppressing.");
		return (-1);
	}
	if (ann->prio == new_prio) {
		/* same priority, append */
		return (0);
	}

	/* we override the queue head, remove it and retry */
	dbg_log(snd, 1, "priority higher, stopping current annunciation.");
	list_remove(&playback_queue, ann);
	if (ann->cur_msg != -1)
		wav_stop(voice_msgs[ann->msgs[ann->cur_msg]].wav);
	free(ann->msgs);
	free(ann);
	return (1);
}

void
play_msg(msg_type_t *msg, size_t msg_len, msg_prio_t prio)
{
	ann_t *ann;

	if (xraas_state->config.use_tts) {
		char *buf;
		size_t buflen = 0;
		for (size_t i = 0; i < msg_len; i++)
			buflen += strlen(voice_msgs[msg[i]].text);
		buf = malloc(buflen + 1);
		for (size_t i = 0; i < msg_len; i++)
			strcat(buf, voice_msgs[msg[i]].text);
		dbg_log(snd, 1, "TTS: \"%s\"", buf);
		XPLMSpeakString(buf);
		free(buf);
		free(msg);
		return;
	}

	ASSERT(inited);

top:
	if ((ann = list_head(&playback_queue)) != NULL) {
		switch (resolve_priority_ordering(ann, prio)) {
		case -1:
			free(msg);
			return;
		case 0:
			break;
		case 1:
			goto top;
		}
	}
	/*
	 * At this point the queue only contains messages of equal priotity
	 * to our own, queue up at the end.
	 */
	ann = calloc(1, sizeof (*ann));
	ann->msgs = msg;
	ann->num_msgs = msg_len;
	ann->prio = prio;
	ann->cur_msg = -1;
	list_insert_tail(&playback_queue, ann);
}

bool_t
modify_cur_msg(msg_type_t *msg, size_t msg_len, msg_prio_t prio)
{
	ann_t *ann;

	if (xraas_state->config.use_tts)
		return (B_FALSE);

	ASSERT(inited);

top:
	if ((ann = list_head(&playback_queue)) != NULL) {
		switch (resolve_priority_ordering(ann, prio)) {
		case -1:
			return (B_FALSE);
		case 0:
			break;
		case 1:
			goto top;
		}
	}

	ann = list_head(&playback_queue);
	if (ann == NULL) {
		/* nothing to modify, so just play */
		play_msg(msg, msg_len, prio);
		return (B_TRUE);
	} else if (ann->msgs[0] == msg[0] && ann->cur_msg <= 0) {
		/*
		 * we're in time to modify the existing annunciation,
		 * so go for it
		 */
		free(ann->msgs);
		ann->msgs = msg;
		ann->num_msgs = msg_len;
		return (B_TRUE);
	} else {
		/* messages don't match or we're too late, fail */
		return (B_FALSE);
	}
}

static float
snd_sched_cb(float elapsed_since_last_call, float elapsed_since_last_floop,
    int counter, void *refcon)
{
	int64_t now;
	ann_t *ann;

	ASSERT(inited);

	UNUSED(elapsed_since_last_call);
	UNUSED(elapsed_since_last_floop);
	UNUSED(counter);
	UNUSED(refcon);

	ann = list_head(&playback_queue);
	if (ann == NULL)
		return (-1.0);

	/*
	 * Make sure our messages are only audible when we're inside
	 * the cockpit and AC power is on.
	 */
	if (view_is_ext && (!view_is_external() ||
	    !xraas_state->config.disable_ext_view)) {
		dbg_log(snd, 1, "view has moved inside, unmuting");
		set_sound_on(B_TRUE);
		view_is_ext = B_FALSE;
	} else if (!view_is_ext && view_is_external() &&
	    xraas_state->config.disable_ext_view) {
		dbg_log(snd, 1, "view has moved outside, muting");
		set_sound_on(B_FALSE);
		view_is_ext = B_TRUE;
	}

	/*
	 * Stop audio when GPWS is overriding us - we'll restart the
	 * annunciation once it's over.
	 */
	if (GPWS_has_priority()) {
		if (ann->cur_msg >= 0) {
			dbg_log(snd, 1, "GPWS priority override, pausing");
			wav_stop(voice_msgs[ann->msgs[ann->cur_msg]].wav);
			ann->cur_msg = -1;
		}
		return (-1.0);
	}

	/* Stop audio when power is down and drain the queue. */
	if (!xraas_is_on()) {
		dbg_log(snd, 1, "lost power, stopping sound");
		if (ann->cur_msg >= 0)
			wav_stop(voice_msgs[ann->msgs[ann->cur_msg]].wav);
		do {
			list_remove(&playback_queue, ann);
			free(ann->msgs);
			free(ann);
		} while ((ann = list_head(&playback_queue)) != NULL);
		return (-1.0);
	}

	now = microclock();

	ASSERT(ann->cur_msg < ann->num_msgs);
	if (ann->cur_msg == -1 || now - ann->started >
	    SEC2USEC(voice_msgs[ann->msgs[ann->cur_msg]].wav->duration)) {
		if (ann->cur_msg >= 0)
			wav_stop(voice_msgs[ann->msgs[ann->cur_msg]].wav);
		ann->cur_msg++;
		if (ann->cur_msg < ann->num_msgs) {
			ann->started = now;
			if (!wav_play(
			    voice_msgs[ann->msgs[ann->cur_msg]].wav)) {
				log_init_msg(B_TRUE, INIT_ERR_MSG_TIMEOUT, NULL,
				    NULL, "Cannot play sound, OpenAL error.\n"
				    "See Log.txt for more information.");
			}
		} else {
			list_remove(&playback_queue, ann);
			free(ann->msgs);
			free(ann);
		}
	}

	return (-1.0);
}

bool_t
snd_sys_init(const char *plugindir)
{
	const char *gender_dir;

	dbg_log(snd, 1, "snd_sys_init");

	if (xraas_state->config.use_tts)
		return (B_TRUE);

	ASSERT(!inited);

	/* no WAV/OpenAL calls before this */
	alc = openal_init(NULL, openal_shared);
	if (alc == NULL)
		return (B_FALSE);

	gender_dir = (xraas_state->config.voice_female ? "female" : "male");

	for (msg_type_t msg = 0; msg < NUM_MSGS; msg++) {
		char fname[32];
		char *pathname;

		ASSERT(voice_msgs[msg].wav == NULL);
		snprintf(fname, sizeof (fname), "%s.opus",
		    voice_msgs[msg].name);
		pathname = mkpathname(plugindir, "data", "msgs", gender_dir,
		    fname, NULL);
		if (!file_exists(pathname, NULL)) {
			/* Try the uncompressed WAV version. */
			free(pathname);
			snprintf(fname, sizeof (fname), "%s.wav",
			    voice_msgs[msg].name);
			pathname = mkpathname(plugindir, "data", "msgs",
			    gender_dir, fname, NULL);
		}
		voice_msgs[msg].wav = wav_load(pathname, voice_msgs[msg].name,
		    alc);
		if (voice_msgs[msg].wav == NULL) {
			log_init_msg(B_TRUE, INIT_ERR_MSG_TIMEOUT, NULL, NULL,
			    "X-RAAS initialization error: cannot load WAV "
			    "files.\n"
			    "See Log.txt for more information.");
			free(pathname);
			goto errout;
		}
		wav_set_gain(voice_msgs[msg].wav,
		    xraas_state->config.voice_volume);
		free(pathname);
	}

	list_create(&playback_queue, sizeof (ann_t), offsetof(ann_t, node));
	XPLMRegisterFlightLoopCallback(snd_sched_cb, -1.0, NULL);

	inited = B_TRUE;

	return (B_TRUE);

errout:
	for (msg_type_t msg = 0; msg < NUM_MSGS; msg++) {
		if (voice_msgs[msg].wav != NULL) {
			wav_free(voice_msgs[msg].wav);
			voice_msgs[msg].wav = NULL;
		}
	}
	openal_fini(alc);
	alc = NULL;

	return (B_FALSE);
}

void
snd_sys_fini(void)
{
	dbg_log(snd, 1, "snd_sys_fini");

	if (!inited)
		return;
	ASSERT(!xraas_state->config.use_tts);

	for (ann_t *ann = list_head(&playback_queue); ann != NULL;
	    ann = list_head(&playback_queue)) {
		list_remove(&playback_queue, ann);
		free(ann->msgs);
		free(ann);
	}

	XPLMUnregisterFlightLoopCallback(snd_sched_cb, NULL);
	list_destroy(&playback_queue);

	for (msg_type_t msg = 0; msg < NUM_MSGS; msg++) {
		if (voice_msgs[msg].wav != NULL) {
			wav_free(voice_msgs[msg].wav);
			voice_msgs[msg].wav = NULL;
		}
	}

	/* no more OpenAL/WAV calls after this */
	openal_fini(alc);
	alc = NULL;

	inited = B_FALSE;
}

void
snd_sys_set_shared(bool_t flag)
{
	openal_shared = flag;
}
