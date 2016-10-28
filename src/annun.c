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
 * Copyright 2016 Saso Kiselkov. All rights reserved.
 */

#include <stddef.h>
#include <string.h>

#include <XPLMProcessing.h>
#include <XPLMUtilities.h>

#include "assert.h"
#include "list.h"
#include "wav.h"

#include "annun.h"

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
static const xraas_state_t *state;

static list_t playback_queue;

void
play_msg(msg_type_t *msg, size_t msg_len, msg_prio_t prio)
{
	ann_t *ann;

	ASSERT(inited);

	if (state->use_tts) {
		char *buf;
		size_t buflen = 0;
		for (size_t i = 0; i < msg_len; i++)
			buflen += strlen(voice_msgs[msg[i]].text);
		buf = malloc(buflen + 1);
		for (size_t i = 0; i < msg_len; i++)
			strcat(buf, voice_msgs[msg[i]].text);
		dbg_log("tts", 1, "TTS: \"%s\"", buf);
		XPLMSpeakString(buf);
		free(buf);
		return;
	}

top:
	ann = list_head(&playback_queue);
	if (ann != NULL) {
		if (ann->prio > prio) {
			/* current message overrides us, be quiet */
			dbg_log("snd", 1, "priority too low, suppressing.");
			free(msg);
			return;
		}
		if (ann->prio < prio) {
			/* we override the queue head, remove it and retry */
			dbg_log("snd", 1, "priority higher, stopping "
			    "current annunciation.");
			list_remove(&playback_queue, ann);
			if (ann->cur_msg != -1)
				wav_stop(
				    voice_msgs[ann->msgs[ann->cur_msg]].wav);
			free(ann->msgs);
			free(ann);
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
	now = microclock();

	ASSERT(ann->cur_msg < ann->num_msgs);
	if (ann->cur_msg == -1 || now - ann->started >
	    SEC2USEC(voice_msgs[ann->msgs[ann->cur_msg]].wav->duration)) {
		if (ann->cur_msg >= 0)
			wav_stop(voice_msgs[ann->msgs[ann->cur_msg]].wav);
		ann->cur_msg++;
		if (ann->cur_msg < ann->num_msgs) {
			ann->started = now;
			wav_play(voice_msgs[ann->msgs[ann->cur_msg]].wav,
			    state->voice_volume);
		} else {
			list_remove(&playback_queue, ann);
			free(ann->msgs);
			free(ann);
		}
	}

	return (-1.0);
}

bool_t
snd_sys_init(const char *plugindir, const xraas_state_t *global_conf)
{
	const char *gender_dir;

	ASSERT(!inited);
	inited = B_TRUE;

	state = global_conf;
	if (state->use_tts)
		return (B_TRUE);
	gender_dir = (state->voice_female ? "female" : "male");

	for (msg_type_t msg = 0; msg < NUM_MSGS; msg++) {
		char fname[32];
		char *pathname;

		ASSERT(voice_msgs[msg].wav == NULL);
		snprintf(fname, sizeof (fname), "%s.wav", voice_msgs[msg].name);
		pathname = mkpathname(plugindir, "msgs", gender_dir, fname,
		    NULL);
		voice_msgs[msg].wav = wav_load(pathname, voice_msgs[msg].name);
		free(pathname);
		if (voice_msgs[msg].wav == NULL)
			return (B_FALSE);
	}

	list_create(&playback_queue, sizeof (ann_t), offsetof(ann_t, node));
	XPLMRegisterFlightLoopCallback(snd_sched_cb, -1.0, NULL);

	return (B_TRUE);
}

void
snd_sys_fini(void)
{
	ASSERT(inited);
	inited = B_FALSE;

	if (state->use_tts)
		return;

	for (ann_t *ann = list_head(&playback_queue); ann != NULL;
	    ann = list_head(&playback_queue)) {
		list_remove(&playback_queue, ann);
		free(ann->msgs);
		free(ann);
	}

	XPLMUnregisterFlightLoopCallback(snd_sched_cb, NULL);
	list_destroy(&playback_queue);

	for (msg_type_t msg = 0; msg < NUM_MSGS; msg++) {
		wav_free(voice_msgs[msg].wav);
		voice_msgs[msg].wav = NULL;
	}

	/* no more OpenAL/WAV calls after this */
	audio_fini();
}
