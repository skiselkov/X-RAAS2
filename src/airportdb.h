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

#ifndef	_XRAAS_AIRPORTDB_H_
#define	_XRAAS_AIRPORTDB_H_

#include "avl.h"
#include "geom.h"
#include "list.h"
#include "helpers.h"

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct airportdb {
	char		*xpdir;
	char		*cachedir;
	int		xp_airac_cycle;

	avl_tree_t	apt_dat;
	avl_tree_t	geo_table;
} airportdb_t;

typedef struct airport airport_t;
typedef struct runway runway_t;
typedef struct runway_end runway_end_t;

struct runway_end {
	char		id[4];		/* runway ID, nul-terminated */
	geo_pos3_t	thr;		/* threshold position */
	double		displ;		/* threshold displacement in meters */
	double		blast;		/* stopway/blastpad length in meters */
	double		gpa;		/* glidepath angle in degrees */
	double		tch;		/* threshold clearing height in feet */

	/* computed on load_airport */
	vect2_t		thr_v;		/* threshold vector coord */
	vect2_t		dthr_v;		/* displaced threshold vector coord */
	double		hdg;		/* true heading */
	vect2_t		*apch_bbox;	/* in-air approach bbox */
	double		land_len;	/* length avail for landing in meters */
};

struct runway {
	airport_t	*arpt;
	double		width;
	runway_end_t	ends[2];
	char		joint_id[8];

	/* computed on load_airport */
	double		length;		/* meters */
	vect2_t		*prox_bbox;	/* on-ground approach bbox */
	vect2_t		*rwy_bbox;	/* above runway for landing */
	vect2_t		*tora_bbox;	/* on-runway on ground (for tkoff) */
	vect2_t		*asda_bbox;	/* on-runway on ground (for stopping) */

	avl_node_t	node;
};

struct airport {
	char		icao[5];	/* 4-letter ID, nul terminated */
	geo_pos3_t	refpt;		/* airport reference point location */
	bool_t		geo_linked;	/* airport is in geo_table */
	double		TA;		/* transition altitude in feet */
	double		TL;		/* transition level in feet */
	avl_tree_t	rwys;

	bool_t		load_complete;	/* true if we've done load_airport */
	vect3_t		ecef;		/* refpt ECEF coordinates */
	fpp_t		fpp;		/* ortho fpp centered on refpt */
	bool_t		in_navdb;	/* used by recreate_apt_dat_cache */

	avl_node_t	apt_dat_node;	/* apt_dat tree */
	list_node_t	cur_arpts_node;	/* cur_arpts list */
	avl_node_t	tile_node;	/* tiles in the airport_geo_tree */
};

void airportdb_create(airportdb_t *db, const char *xpdir);
void airportdb_destroy(airportdb_t *db);

bool_t recreate_cache(airportdb_t *db);

list_t *find_nearest_airports(airportdb_t *db, geo_pos2_t my_pos);
void free_nearest_airport_list(list_t *l);

void load_nearest_airport_tiles(airportdb_t *db, geo_pos2_t my_pos);
void unload_distant_airport_tiles(airportdb_t *db, geo_pos2_t my_pos);

airport_t *airport_lookup(airportdb_t *db, const char *icao, geo_pos2_t pos);
airport_t *matching_airport_in_tile_with_TATL(airportdb_t *db, geo_pos2_t pos,
    const char *search_icao);

#ifdef	__cplusplus
}
#endif

#endif	/* _XRAAS_AIRPORTDB_H_ */
