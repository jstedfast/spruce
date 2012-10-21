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

#include <gmime/gmime-utils.h>

#include "spruce-string-utils.h"


const char *
spruce_strdown (char *str)
{
	register char *s = str;
	
	while (*s) {
		if (*s >= 'A' && *s <= 'Z')
			*s += 0x20;
		
		s++;
	}
	
	return str;
}


size_t
spruce_base64_decode (char *buf, size_t buflen)
{
	int state = 0;
	unsigned int save = 0;
	
	return g_mime_encoding_base64_decode_step ((unsigned char *) buf, buflen,
						(unsigned char *) buf, &state, &save);
}


char *
spruce_base64_encode (const char *in, size_t inlen)
{
	register unsigned char *s, *d;
	unsigned char *out;
	int state = 0, outlen;
	unsigned int save = 0;
	
	s = d = out = g_malloc (((inlen * 4) / 3) + 5);
	outlen = g_mime_encoding_base64_encode_close ((unsigned char *) in, inlen,
						   out, &state, &save);
	out[outlen] = '\0';
	
	while (*s) {
		if (*s == '\n')
			s++;
		else
			*d++ = *s++;
	}
	
	*d = '\0';
	
	return (char *) out;
}
