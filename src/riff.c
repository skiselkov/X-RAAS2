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

#include "assert.h"
#include "list.h"

#include "riff.h"

#define	RIFF_ID	0x46464952u	/* 'RIFF' */
#define	LIST_ID	0x54534C49u	/* 'LIST' */

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
		    sc = list_head(&c->subchunks)) {
			list_remove(&c->subchunks, sc);
			riff_free_chunk(sc);
		}
		list_destroy(&c->subchunks);
	}
	free(c);
}

static riff_chunk_t *
riff_parse_chunk(uint8_t *buf, size_t bufsz, bool_t bswap)
{
	riff_chunk_t *c = calloc(1, sizeof (*c));

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

riff_chunk_t *
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
uint8_t *
riff_find_chunk(riff_chunk_t *topchunk, size_t *chunksz, ...)
{
	va_list ap;
	uint32_t fourcc;

	ASSERT(topchunk != NULL);
	ASSERT(topchunk->fourcc == RIFF_ID);
	ASSERT(chunksz != NULL);

	va_start(ap, chunksz);
	while ((fourcc = va_arg(ap, uint32_t)) != 0) {
		riff_chunk_t *sc;

		ASSERT(fourcc != LIST_ID && fourcc != RIFF_ID);
		if (topchunk->fourcc != LIST_ID &&
		    topchunk->fourcc != RIFF_ID)
			return (NULL);
		for (sc = list_head(&topchunk->subchunks); sc != NULL;
		    sc = list_next(&topchunk->subchunks, sc)) {
			if (sc->fourcc == fourcc || (sc->listcc == fourcc &&
			    (sc->fourcc == LIST_ID || sc->fourcc == RIFF_ID)))
				break;
		}
		if (sc == NULL)
			return (NULL);
		topchunk = sc;
	}
	va_end(ap);

	ASSERT(topchunk != NULL);

	*chunksz = topchunk->sz;
	return (topchunk->data);
}
