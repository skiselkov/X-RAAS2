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

#ifndef	_XRAAS_RIFF_H_
#define	_XRAAS_RIFF_H_

#include "types.h"

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct riff_chunk {
	bool_t		bswap;
	uint32_t	fourcc;
	uint8_t		*data;
	uint32_t	sz;

	/* populated if fourcc is RIFF_ID or LIST_ID */
	uint32_t	listcc;
	list_t		subchunks;

	list_node_t	node;
} riff_chunk_t;

void riff_free_chunk(riff_chunk_t *c);
riff_chunk_t *riff_parse(uint32_t filetype, uint8_t *buf, size_t bufsz);
uint8_t *riff_find_chunk(riff_chunk_t *topchunk, size_t *chunksz, ...);

#ifdef	__cplusplus
}
#endif

#endif	/* _XRAAS_RIFF_H_ */
