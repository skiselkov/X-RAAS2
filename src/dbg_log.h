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
 */
/*
 * Copyright 2017 Saso Kiselkov. All rights reserved.
 */

#ifndef	_XRAAS_DBG_LOG_H_
#define	_XRAAS_DBG_LOG_H_

#include <acfutils/log.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	int all;
	int altimeter;
	int ann_state;
	int apch_cfg_chk;
	int config;
	int dbg_gui;
	int flt_state;
	int fs;
	int nd_alert;
	int pwr_state;
	int rwy_key;
	int snd;
	int startup;
	int tile;
	int wav;
	int ff_a320;
	int adc;
} debug_config_t;

extern debug_config_t xraas_debug_config;

#define	dbg_log(class, level, ...) \
	do { \
		if (xraas_debug_config.class >= level || \
		    xraas_debug_config.all >= level) { \
			log_impl(log_basename(__FILE__), __LINE__, \
			    "[" #class "/" #level "] " __VA_ARGS__); \
		} \
	} while (0)

#ifdef __cplusplus
}
#endif

#endif	/* _XRAAS_DBG_LOG_H_ */
