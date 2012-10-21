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


#ifndef __SPRUCE_IMAP_SPECIALS_H__
#define __SPRUCE_IMAP_SPECIALS_H__

#include <glib.h>

G_BEGIN_DECLS

enum {
	IS_ASPECIAL   = (1 << 0),
	IS_CTRL       = (1 << 1),
	IS_LWSP       = (1 << 2), 
	IS_QSPECIAL   = (1 << 3),
	IS_SPACE      = (1 << 4),
	IS_WILDCARD   = (1 << 5),
};

extern unsigned char spruce_imap_specials[256];

#define is_atom(x) ((spruce_imap_specials[(unsigned char)(x)] & (IS_ASPECIAL|IS_SPACE|IS_CTRL|IS_WILDCARD|IS_QSPECIAL)) == 0)
#define is_ctrl(x) ((spruce_imap_specials[(unsigned char)(x)] & IS_CTRL) != 0)
#define is_lwsp(x) ((spruce_imap_specials[(unsigned char)(x)] & IS_LWSP) != 0)
#define is_type(x, t) ((spruce_imap_specials[(unsigned char)(x)] & (t)) != 0)
#define is_qsafe(x) ((spruce_imap_specials[(unsigned char)(x)] & (IS_QSPECIAL|IS_CTRL)) == 0)
#define is_wild(x)  ((spruce_imap_specials[(unsigned char)(x)] & IS_WILDCARD) != 0)

void spruce_imap_specials_init (void);

G_END_DECLS

#endif /* __SPRUCE_IMAP_SPECIALS_H__ */
