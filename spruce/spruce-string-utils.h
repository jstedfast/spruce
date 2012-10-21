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


#ifndef __SPRUCE_STRING_UTILS_H__
#define __SPRUCE_STRING_UTILS_H__

#include <glib.h>
#include <sys/types.h>

G_BEGIN_DECLS

const char *spruce_strdown (char *str);

size_t spruce_base64_decode (char *buf, size_t buflen);
char *spruce_base64_encode (const char *in, size_t inlen);

G_END_DECLS

#endif /* __SPRUCE_STRING_UTILS_H__ */
