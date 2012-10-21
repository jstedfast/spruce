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


#ifndef __SPRUCE_IMAP_UTILS_H__
#define __SPRUCE_IMAP_UTILS_H__

#include <glib.h>

G_BEGIN_DECLS

/* IMAP UTF-7 folder name encoding */
char *spruce_imap_utf7_utf8 (const char *in);
char *spruce_imap_utf8_utf7 (const char *in);

/* IMAP flag merging */
typedef struct {
	guint32 changed;
	guint32 bits;
} flags_diff_t;

void spruce_imap_flags_diff (flags_diff_t *diff, guint32 old, guint32 new);
guint32 spruce_imap_flags_merge (flags_diff_t *diff, guint32 flags);
guint32 spruce_imap_merge_flags (guint32 original, guint32 local, guint32 server);


struct _SpruceIMAPEngine;
struct _SpruceIMAPCommand;
struct _SpruceFolderSummary;
struct _spruce_imap_token_t;

int spruce_imap_get_uid_set (struct _SpruceIMAPEngine *engine, struct _SpruceFolderSummary *summary, GPtrArray *infos, int cur, size_t linelen, char **set);

void spruce_imap_utils_set_unexpected_token_error (GError **err, struct _SpruceIMAPEngine *engine, struct _spruce_imap_token_t *token);

int spruce_imap_parse_flags_list (struct _SpruceIMAPEngine *engine, guint32 *flags, GError **err);

enum {
	SPRUCE_IMAP_FOLDER_MARKED          = (1 << 0),
	SPRUCE_IMAP_FOLDER_UNMARKED        = (1 << 1),
	SPRUCE_IMAP_FOLDER_NOSELECT        = (1 << 2),
	SPRUCE_IMAP_FOLDER_NOINFERIORS     = (1 << 3),
	SPRUCE_IMAP_FOLDER_HAS_CHILDREN    = (1 << 4),
	SPRUCE_IMAP_FOLDER_HAS_NO_CHILDREN = (1 << 5),
};

typedef struct _spruce_imap_list_t {
	guint32 flags;
	char delim;
	char *name;
} spruce_imap_list_t;

int spruce_imap_untagged_list (struct _SpruceIMAPEngine *engine, struct _SpruceIMAPCommand *ic,
			       guint32 index, struct _spruce_imap_token_t *token, GError **err);


enum {
	SPRUCE_IMAP_STATUS_UNKNOWN,
	SPRUCE_IMAP_STATUS_MESSAGES,
	SPRUCE_IMAP_STATUS_RECENT,
	SPRUCE_IMAP_STATUS_UIDNEXT,
	SPRUCE_IMAP_STATUS_UIDVALIDITY,
	SPRUCE_IMAP_STATUS_UNSEEN,
};

typedef struct _spruce_imap_status_attr {
	struct _spruce_imap_status_attr *next;
	guint32 type;
	guint32 value;
} spruce_imap_status_attr_t;

typedef struct {
	spruce_imap_status_attr_t *attr_list;
	char *mailbox;
} spruce_imap_status_t;

void spruce_imap_status_free (spruce_imap_status_t *status);

int spruce_imap_untagged_status (struct _SpruceIMAPEngine *engine, struct _SpruceIMAPCommand *ic,
				 guint32 index, struct _spruce_imap_token_t *token, GError **err);

G_END_DECLS

#endif /* __SPRUCE_IMAP_UTILS_H__ */
