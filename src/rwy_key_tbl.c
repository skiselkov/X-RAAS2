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
 * Copyright 2015 Saso Kiselkov. All rights reserved.
 */

#include <string.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>

#include "airportdb.h"
#include "log.h"

#include "rwy_key_tbl.h"

#define	RWY_ID_KEY_SZ			16

typedef struct rwy_key_s {
	char		key[RWY_ID_KEY_SZ];
	int		value;
	avl_node_t	node;
} rwy_key_t;

static int
compar(const void *a, const void *b)
{
	const rwy_key_t *ka = a, *kb = b;
	int res = strcmp(ka->key, kb->key);
	if (res < 0)
		return (-1);
	else if (res == 0)
		return (0);
	else
		return (1);
}

void
rwy_key_tbl_create(avl_tree_t *tree)
{
	avl_create(tree, compar, sizeof (rwy_key_t),
	    offsetof(rwy_key_t, node));
}

void
rwy_key_tbl_destroy(avl_tree_t *tree)
{
	void *cookie = NULL;
	rwy_key_t *key;

	while ((key = avl_destroy_nodes(tree, &cookie)) != NULL)
		free(key);
	avl_destroy(tree);
}

void
rwy_key_tbl_empty(avl_tree_t *tree)
{
	for (rwy_key_t *key; (key = avl_first(tree)) != NULL;) {
		avl_remove(tree, key);
		free(key);
	}
}


void
rwy_key_tbl_remove_impl(avl_tree_t *tree, const char *name,
    const char *arpt_id, const char *rwy_id)
{
	rwy_key_t srch, *key;

	snprintf(srch.key, sizeof (srch.key), "%s/%s", arpt_id, rwy_id);
	if ((key = avl_find(tree, &srch, NULL)) != NULL) {
	        dbg_log("rwy_key", 1, "%s[%s/%s] = nil", name, arpt_id, rwy_id);
		avl_remove(tree, key);
		free(key);
	}
}

void
rwy_key_tbl_set_impl(avl_tree_t *tree, const char *name,
    const char *arpt_id, const char *rwy_id, int value)
{
	rwy_key_t srch, *key;
	avl_index_t where;

	snprintf(srch.key, sizeof (srch.key), "%s/%s", arpt_id, rwy_id);
	if ((key = avl_find(tree, &srch, &where)) == NULL) {
		key = calloc(1, sizeof (*key));
		snprintf(key->key, sizeof (key->key), "%s/%s", arpt_id, rwy_id);
		avl_insert(tree, key, where);
	}
	if (key->value != value) {
		dbg_log("rwy_key", 1, "%s[%s/%s] = %d", name, arpt_id, rwy_id,
		    value);
		key->value = value;
        }
}

/*
 * Removes any runway keys from `tree' which pertain to airports not in
 * `curarpt_list'. This is to deal with cases where the aircraft quickly
 * shifts (repositions), the cur_arpts list gets reloaded and so X-RAAS
 * never gets the opportunity to properly examine whether we've left the
 * runway proximity areas.
 */
void
rwy_key_tbl_remove_distant_impl(avl_tree_t *tree, const char *name,
    const list_t *curarpt_list)
{
	rwy_key_t *key, *next;

	for (key = avl_first(tree); key != NULL; key = next) {
		bool_t found = B_FALSE;

		next = AVL_NEXT(tree, key);
		for (const airport_t *arpt = list_head(curarpt_list);
		    arpt != NULL; arpt = list_next(curarpt_list, arpt)) {
			if (strstr(key->key, arpt->icao) == key->key) {
				found = B_TRUE;
				break;
			}
		}
		if (!found) {
			dbg_log("rwy_key", 1, "%s[%s] = nil", name, key->key);
			avl_remove(tree, key);
			free(key);
		}
	}
}

int
rwy_key_tbl_get(avl_tree_t *tree, const char *arpt_id, const char *rwy_id)
{
	rwy_key_t srch, *key;
	avl_index_t where;

	snprintf(srch.key, sizeof (srch.key), "%s/%s", arpt_id, rwy_id);
	if ((key = avl_find(tree, &srch, &where)) == NULL)
		return (0);
	return (key->value);
}
