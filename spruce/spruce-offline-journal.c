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

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <errno.h>

#include <glib/gi18n.h>

#include <spruce/spruce-error.h>
#include <spruce/spruce-folder.h>
#include <spruce/spruce-offline-journal.h>


static void spruce_offline_journal_class_init (SpruceOfflineJournalClass *klass);
static void spruce_offline_journal_init (SpruceOfflineJournal *journal, SpruceOfflineJournalClass *klass);
static void spruce_offline_journal_finalize (GObject *object);


static GObjectClass *parent_class = NULL;


GType
spruce_offline_journal_get_type (void)
{
	static GType type = 0;
	
	if (!type) {
		static const GTypeInfo info = {
			sizeof (SpruceOfflineJournalClass),
			NULL, /* base_class_init */
			NULL, /* base_class_finalize */
			(GClassInitFunc) spruce_offline_journal_class_init,
			NULL, /* class_finalize */
			NULL, /* class_data */
			sizeof (SpruceOfflineJournal),
			0,    /* n_preallocs */
			(GInstanceInitFunc) spruce_offline_journal_init,
		};
		
		type = g_type_register_static (G_TYPE_OBJECT, "SpruceOfflineJournal", &info, 0);
	}
	
	return type;
}


static void
spruce_offline_journal_class_init (SpruceOfflineJournalClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	
	parent_class = g_type_class_ref (G_TYPE_OBJECT);
	
	object_class->finalize = spruce_offline_journal_finalize;
	
	/* virtual methods */
}

static void
spruce_offline_journal_init (SpruceOfflineJournal *journal, SpruceOfflineJournalClass *klass)
{
	spruce_list_init (&journal->queue);
	journal->filename = NULL;
	journal->folder = NULL;
}

static void
spruce_offline_journal_finalize (GObject *object)
{
	SpruceOfflineJournal *journal = (SpruceOfflineJournal *) object;
	SpruceListNode *entry;
	
	while ((entry = spruce_list_unlink_head (&journal->queue)))
		SPRUCE_OFFLINE_JOURNAL_GET_CLASS (journal)->entry_free (journal, entry);
	
	g_free (journal->filename);
	
	G_OBJECT_CLASS (parent_class)->finalize (object);
}


/**
 * spruce_offline_journal_construct:
 * @journal: a #SpruceOfflineJournal object
 * @folder: a #SpruceFolder object
 * @filename: a filename to save/load the journal
 *
 * Constructs a journal object.
 **/
void
spruce_offline_journal_construct (SpruceOfflineJournal *journal, SpruceFolder *folder, const char *filename)
{
	SpruceListNode *entry;
	FILE *fp;
	
	journal->filename = g_strdup (filename);
	journal->folder = folder;
	
	if (!(fp = fopen (filename, "rb")))
		return;
	
	while ((entry = SPRUCE_OFFLINE_JOURNAL_GET_CLASS (journal)->entry_load (journal, fp)))
		spruce_list_append (&journal->queue, entry);
	
	fclose (fp);
}


/**
 * spruce_offline_journal_set_filename:
 * @journal: a #SpruceOfflineJournal object
 * @filename: a filename to load/save the journal to
 *
 * Set the filename where the journal should load/save from.
 **/
void
spruce_offline_journal_set_filename (SpruceOfflineJournal *journal, const char *filename)
{
	g_return_if_fail (SPRUCE_IS_OFFLINE_JOURNAL (journal));
	
	g_free (journal->filename);
	journal->filename = g_strdup (filename);
}


/**
 * spruce_offline_journal_write:
 * @journal: a #SpruceOfflineJournal object
 * @err: a #GError
 *
 * Save the journal to disk.
 *
 * Returns %0 on success or %-1 on fail
 **/
int
spruce_offline_journal_write (SpruceOfflineJournal *journal, GError **err)
{
	SpruceListNode *entry;
	FILE *fp;
	int fd;
	
	if ((fd = open (journal->filename, O_CREAT | O_TRUNC | O_WRONLY, 0666)) == -1) {
		g_set_error (err, SPRUCE_ERROR, errno,
			     _("Cannot write offline journal for folder `%s': %s"),
			     journal->folder->full_name, g_strerror (errno));
		return -1;
	}
	
	fp = fdopen (fd, "w");
	entry = journal->queue.head;
	while (entry->next) {
		if (SPRUCE_OFFLINE_JOURNAL_GET_CLASS (journal)->entry_write (journal, entry, fp) == -1)
			goto exception;
		entry = entry->next;
	}
	
	if (fsync (fd) == -1)
		goto exception;
	
	fclose (fp);
	
	return 0;
	
 exception:
	
	g_set_error (err, SPRUCE_ERROR, errno,
		     _("Cannot write offline journal for folder `%s': %s"),
		     journal->folder->full_name, g_strerror (errno));
	
	fclose (fp);
	
	return -1;
}


/**
 * spruce_offline_journal_replay:
 * @journal: a #SpruceOfflineJournal object
 * @err: a #GError
 *
 * Replay all entries in the journal.
 *
 * Returns %0 on success (no entry failed to replay) or %-1 on fail
 **/
int
spruce_offline_journal_replay (SpruceOfflineJournal *journal, GError **err)
{
	SpruceListNode *entry, *next;
	GError *lerr = NULL;
	int failed = FALSE;
	
	entry = journal->queue.head;
	while (entry->next) {
		next = entry->next;
		if (SPRUCE_OFFLINE_JOURNAL_GET_CLASS (journal)->entry_play (journal, entry, &lerr) == -1) {
			if (!failed)
				g_propagate_error (err, lerr);
			else
				g_clear_error (&lerr);
			failed = TRUE;
		} else {
			spruce_list_unlink (entry);
		}
		
		entry = next;
	}
	
	if (failed)
		return -1;
	
	return 0;
}
