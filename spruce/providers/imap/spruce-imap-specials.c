/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*  Spruce
 *  Copyright (C) 1999-2009 Jeffrey Stedfast
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>

#include "spruce-imap-specials.h"

#define ATOM_SPECIALS   (const unsigned char *) "(){]"
#define LWSP_SPECIALS   (const unsigned char *) " \t\r\n"
#define QUOTED_SPECIALS (const unsigned char *) "\\\""
#define LIST_WILDCARDS  (const unsigned char *) "*%"

#define NUM_ATOM_SPECIALS (sizeof (ATOM_SPECIALS) - 1)
#define NUM_LWSP_SPECIALS (sizeof (LWSP_SPECIALS) - 1)
#define NUM_QUOTED_SPECIALS (sizeof (QUOTED_SPECIALS) - 1)
#define NUM_LIST_WILDCARDS (sizeof (LIST_WILDCARDS) - 1)

unsigned char spruce_imap_specials[256] = {
          2,  2,  2,  2,  2,  2,  2,  2,  2,  6,  6,  2,  2,  6,  2,  2,
          2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
         20,  0,  8,  0,  0, 32,  0,  0,  1,  1, 32,  0,  0,  0,  0,  0,
          0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
          0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
          0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  8,  1,  0,  0,
          0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
          0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  1,  0,  0,  0,  2,
          2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
          2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
          2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
          2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
          2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
          2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
          2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
          2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
};


static void
imap_init_bits (unsigned short bit, unsigned short bitcopy, int remove,
		const unsigned char *inptr, size_t inlen)
{
	const unsigned char *inend = inptr + inlen;
	int i;
	
	if (!remove) {
		while (inptr < inend)
			spruce_imap_specials[*inptr++] |= bit;
		if (bitcopy) {
			for (i = 0; i < 256; i++) {
				if (spruce_imap_specials[i] & bitcopy)
					spruce_imap_specials[i] |= bit;
			}
		}
	} else {
		for (i = 0; i < 256; i++)
			spruce_imap_specials[i] |= bit;
		while (inptr < inend)
			spruce_imap_specials[*inptr++] &= ~bit;
		if (bitcopy) {
			for (i = 0; i < 256; i++) {
				if (spruce_imap_specials[i] & bitcopy)
					spruce_imap_specials[i] &= ~bit;
			}
		}
	}
}


void
spruce_imap_specials_init (void)
{
	int i;
	
	for (i = 0; i < 256; i++) {
		spruce_imap_specials[i] = 0;
		if (i <= 0x1f || i >= 0x7f)
			spruce_imap_specials[i] = IS_CTRL;
	}
	
	spruce_imap_specials[' '] |= IS_SPACE;
	
	imap_init_bits (IS_LWSP,     0, 0, LWSP_SPECIALS,   NUM_LWSP_SPECIALS);
	imap_init_bits (IS_ASPECIAL, 0, 0, ATOM_SPECIALS,   NUM_ATOM_SPECIALS);
	imap_init_bits (IS_QSPECIAL, 0, 0, QUOTED_SPECIALS, NUM_QUOTED_SPECIALS);
	imap_init_bits (IS_WILDCARD, 0, 0, LIST_WILDCARDS,  NUM_LIST_WILDCARDS);
}
