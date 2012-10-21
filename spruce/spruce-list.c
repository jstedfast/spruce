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


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "spruce-list.h"

void
spruce_list_init (SpruceList *list)
{
	list->head = (SpruceListNode *) &list->tail;
	list->tail = NULL;
	list->tailpred = (SpruceListNode *) &list->head;
}

int
spruce_list_is_empty (SpruceList *list)
{
	return list->head == (SpruceListNode *) &list->tail;
}

int
spruce_list_length (SpruceList *list)
{
	SpruceListNode *node;
	int n = 0;
	
	node = list->head;
	while (node->next) {
		node = node->next;
		n++;
	}
	
	return n;
}

SpruceListNode *
spruce_list_unlink_head (SpruceList *list)
{
	SpruceListNode *n, *nn;
	
	n = list->head;
	nn = n->next;
	if (nn) {
		nn->prev = n->prev;
		list->head = nn;
		return n;
	}
	
	return NULL;
}

SpruceListNode *
spruce_list_unlink_tail (SpruceList *list)
{
	SpruceListNode *n, *np;
	
	n = list->tailpred;
	np = n->prev;
	if (np) {
		np->next = n->next;
		list->tailpred = np;
		return n;
	}
	
	return NULL;
}

SpruceListNode *
spruce_list_prepend (SpruceList *list, SpruceListNode *node)
{
	node->next = list->head;
	node->prev = (SpruceListNode *) &list->head;
	list->head->prev = node;
	list->head = node;
	
	return node;
}

SpruceListNode *
spruce_list_append (SpruceList *list, SpruceListNode *node)
{
	node->next = (SpruceListNode *) &list->tail;
	node->prev = list->tailpred;
	list->tailpred->next = node;
	list->tailpred = node;
	
	return node;
}

SpruceListNode *
spruce_list_unlink (SpruceListNode *node)
{
	node->next->prev = node->prev;
        node->prev->next = node->next;
	
	return node;
}
