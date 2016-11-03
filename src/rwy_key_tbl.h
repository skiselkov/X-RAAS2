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

#ifndef	_XRAAS_RWY_KEY_TBL_H_
#define	_XRAAS_RWY_KEY_TBL_H_

#include "avl.h"
#include "list.h"

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct {
	avl_tree_t	tree;
	char		*name;
} rwy_key_tbl_t;

void rwy_key_tbl_create(rwy_key_tbl_t *tbl, const char *name);
void rwy_key_tbl_destroy(rwy_key_tbl_t *tbl);
void rwy_key_tbl_empty(rwy_key_tbl_t *tbl);
void rwy_key_tbl_remove(rwy_key_tbl_t *tbl, const char *arpt_id,
    const char *rwy_id);
void rwy_key_tbl_set(rwy_key_tbl_t *tbl, const char *arpt_id,
    const char *rwy_id, int value);
void rwy_key_tbl_remove_distant(rwy_key_tbl_t *tbl, const list_t *curarpt_list);

int rwy_key_tbl_get(rwy_key_tbl_t *tbl, const char *arpt_id,
    const char *rwy_id);

#ifdef	__cplusplus
}
#endif

#endif	/* _XRAAS_RWY_KEY_TBL_H_ */
