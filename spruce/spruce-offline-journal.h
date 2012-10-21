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


#ifndef __SPRUCE_OFFLINE_JOURNAL_H__
#define __SPRUCE_OFFLINE_JOURNAL_H__

#include <stdio.h>
#include <stdarg.h>

#include <glib.h>
#include <glib-object.h>

#include <spruce/spruce-list.h>

G_BEGIN_DECLS

#define SPRUCE_TYPE_OFFLINE_JOURNAL            (spruce_offline_journal_get_type ())
#define SPRUCE_OFFLINE_JOURNAL(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), SPRUCE_TYPE_OFFLINE_JOURNAL, SpruceOfflineJournal))
#define SPRUCE_OFFLINE_JOURNAL_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), SPRUCE_TYPE_OFFLINE_JOURNAL, SpruceOfflineJournalClass))
#define SPRUCE_IS_OFFLINE_JOURNAL(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SPRUCE_TYPE_OFFLINE_JOURNAL))
#define SPRUCE_IS_OFFLINE_JOURNAL_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), SPRUCE_TYPE_OFFLINE_JOURNAL))
#define SPRUCE_OFFLINE_JOURNAL_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), SPRUCE_TYPE_OFFLINE_JOURNAL, SpruceOfflineJournalClass))

typedef struct _SpruceOfflineJournal SpruceOfflineJournal;
typedef struct _SpruceOfflineJournalClass SpruceOfflineJournalClass;
typedef struct _SpruceOfflineJournalEntry SpruceOfflineJournalEntry;

struct _SpruceFolder;

struct _SpruceOfflineJournal {
	GObject parent_object;
	
	struct _SpruceFolder *folder;
	SpruceList queue;
	char *filename;
};

struct _SpruceOfflineJournalClass {
	GObjectClass parent_class;
	
	/* entry methods */
	void (* entry_free) (SpruceOfflineJournal *journal, SpruceListNode *entry);
	
	SpruceListNode * (* entry_load) (SpruceOfflineJournal *journal, FILE *in);
	int (* entry_write) (SpruceOfflineJournal *journal, SpruceListNode *entry, FILE *out);
	int (* entry_play) (SpruceOfflineJournal *journal, SpruceListNode *entry, GError **err);
};


GType spruce_offline_journal_get_type (void);

void spruce_offline_journal_construct (SpruceOfflineJournal *journal, struct _SpruceFolder *folder, const char *filename);
void spruce_offline_journal_set_filename (SpruceOfflineJournal *journal, const char *filename);

int spruce_offline_journal_write (SpruceOfflineJournal *journal, GError **err);
int spruce_offline_journal_replay (SpruceOfflineJournal *journal, GError **err);

G_END_DECLS

#endif /* __SPRUCE_OFFLINE_JOURNAL_H__ */
