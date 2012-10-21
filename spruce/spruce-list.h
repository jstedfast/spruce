/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*  Spruce
 *  Copyright (C) 1999-2009 Jeffrey Stedfast
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public License
 *  as published by the Free Software Foundation; either version 2.1
 *  of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
 *  02110-1301, USA.
 */


#ifndef __SPRUCE_LIST_H__
#define __SPRUCE_LIST_H__

#include <glib.h>
#include <string.h>

G_BEGIN_DECLS

typedef struct _SpruceListNode {
	struct _SpruceListNode *next;
	struct _SpruceListNode *prev;
} SpruceListNode;

typedef struct {
	SpruceListNode *head;
	SpruceListNode *tail;
	SpruceListNode *tailpred;
} SpruceList;

#define SPRUCE_LIST_INITIALIZER(l) { (SpruceListNode *) &l.tail, NULL, (SpruceListNode *) &l.head }

void spruce_list_init (SpruceList *list);

int spruce_list_is_empty (SpruceList *list);

int spruce_list_length (SpruceList *list);

SpruceListNode *spruce_list_unlink_head (SpruceList *list);
SpruceListNode *spruce_list_unlink_tail (SpruceList *list);

SpruceListNode *spruce_list_prepend (SpruceList *list, SpruceListNode *node);
SpruceListNode *spruce_list_append  (SpruceList *list, SpruceListNode *node);

SpruceListNode *spruce_list_unlink (SpruceListNode *node);

G_END_DECLS

#endif /* __SPRUCE_LIST_H__ */
