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
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>

#include <glib.h>
#include <glib/gi18n.h>

#include <gmime/gmime.h>
#include <spruce/spruce-error.h>
#include <spruce/spruce-file-utils.h>
#include <spruce/spruce-folder-search.h>

#include "spruce-mbox-store.h"
#include "spruce-mbox-folder.h"
#include "spruce-mbox-summary.h"


static struct {
	const char *str;
	int strlen;
} ignore_names[] = {
	{ "~",                 1 },
	{ ".summary",          8 },
	
	/* evolution specials */
	{ ".cmeta",            6 },
	{ ".ev-summary",      11 },
	{ ".ibex.index",      11 },
	{ ".ibex.index.data", 16 },
	
	/* mozilla specials */
	{ ".msf",              4 },
};


static void spruce_mbox_folder_class_init (SpruceMboxFolderClass *klass);
static void spruce_mbox_folder_init (SpruceMboxFolder *folder, SpruceMboxFolderClass *klass);
static void spruce_mbox_folder_finalize (GObject *object);

static int mbox_open (SpruceFolder *folder, GError **err);
static int mbox_close (SpruceFolder *folder, gboolean expunge, GError **err);
static int mbox_create (SpruceFolder *folder, int type, GError **err);
static int mbox_delete (SpruceFolder *folder, GError **err);
static int mbox_rename (SpruceFolder *folder, const char *newname, GError **err);
static void mbox_newname (SpruceFolder *folder, const char *parent, const char *name);
static int mbox_expunge (SpruceFolder *folder, GPtrArray *uids, GError **err);
static GPtrArray *mbox_list (SpruceFolder *folder, const char *pattern, GError **err);
static GMimeMessage *mbox_get_message (SpruceFolder *folder, const char *uid, GError **err);
static int mbox_append_message (SpruceFolder *folder, GMimeMessage *message,
				SpruceMessageInfo *info, GError **err);
static GPtrArray *mbox_search (SpruceFolder *folder, GPtrArray *uids, const char *expression, GError **err);

static void parser_got_xspruce (GMimeParser *parser, const char *header, const char *value,
				gint64 offset, gpointer user_data);
static char *mbox_create_from_line (GMimeMessage *message);


static SpruceFolderClass *parent_class = NULL;


GType
spruce_mbox_folder_get_type (void)
{
	static GType type = 0;
	
	if (!type) {
		static const GTypeInfo info = {
			sizeof (SpruceMboxFolderClass),
			NULL, /* base_class_init */
			NULL, /* base_class_finalize */
			(GClassInitFunc) spruce_mbox_folder_class_init,
			NULL, /* class_finalize */
			NULL, /* class_data */
			sizeof (SpruceMboxFolder),
			0,    /* n_preallocs */
			(GInstanceInitFunc) spruce_mbox_folder_init,
		};
		
		type = g_type_register_static (SPRUCE_TYPE_FOLDER, "SpruceMboxFolder", &info, 0);
	}
	
	return type;
}


static void
spruce_mbox_folder_class_init (SpruceMboxFolderClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	SpruceFolderClass *folder_class = SPRUCE_FOLDER_CLASS (klass);
	
	parent_class = g_type_class_ref (SPRUCE_TYPE_FOLDER);
	
	object_class->finalize = spruce_mbox_folder_finalize;
	
	/* virtual method overload */
	folder_class->open = mbox_open;
	folder_class->close = mbox_close;
	folder_class->create = mbox_create;
	folder_class->delete = mbox_delete;
	folder_class->rename = mbox_rename;
	folder_class->newname = mbox_newname;
	folder_class->expunge = mbox_expunge;
	folder_class->list = mbox_list;
	folder_class->get_message = mbox_get_message;
	folder_class->append_message = mbox_append_message;
	folder_class->search = mbox_search;
}

static void
spruce_mbox_folder_init (SpruceMboxFolder *mbox, SpruceMboxFolderClass *klass)
{
	SpruceFolder *folder = (SpruceFolder *) mbox;
	
	folder->separator = '/';
	folder->permanent_flags = SPRUCE_MESSAGE_ANSWERED | SPRUCE_MESSAGE_DELETED |
		SPRUCE_MESSAGE_DRAFT | SPRUCE_MESSAGE_FLAGGED | SPRUCE_MESSAGE_SEEN;
	
	mbox->stream = NULL;
	mbox->path = NULL;
}

static void
spruce_mbox_folder_finalize (GObject *object)
{
	SpruceMboxFolder *mbox = (SpruceMboxFolder *) object;
	
	if (mbox->stream)
		g_object_unref (mbox->stream);
	
	g_free (mbox->path);
	
	G_OBJECT_CLASS (parent_class)->finalize (object);
}


static char *
mbox_get_summary_filename (const char *mbox)
{
	/* /path/to/.mbox.summary */
	const char *filename;
	
	if ((filename = strrchr (mbox, '/')))
		return g_strdup_printf ("%.*s/.%s.summary", (int) (filename - mbox), mbox, filename + 1);
	else
		return g_strdup_printf (".%s.summary", mbox);
}

static char *
mbox_build_filename (const char *toplevel_dir, const char *full_name)
{
	const char *inptr = full_name;
	int subdirs = 0;
	char *path, *p;
	
	if (*full_name == '\0')
		return g_strdup (toplevel_dir);
	
	while (*inptr != '\0') {
		if (*inptr == '/')
			subdirs++;
		inptr++;
	}
	
	path = g_malloc (strlen (toplevel_dir) + (inptr - full_name) + (4 * subdirs) + 2);
	p = g_stpcpy (path, toplevel_dir);
	
	if (p[-1] != '/')
		*p++ = '/';
	
	inptr = full_name;
	while (*inptr != '\0') {
		while (*inptr != '/' && *inptr != '\0')
			*p++ = *inptr++;
		
		if (*inptr == '/') {
			p = g_stpcpy (p, ".sbd/");
			inptr++;
			
			/* strip extranaeous '/'s */
			while (*inptr == '/')
				inptr++;
		}
	}
	
	*p = '\0';
	
	return path;
}

#define mbox_store_build_filename(s,n) mbox_build_filename (((SpruceService *) s)->url->path, n)


static gboolean
ignore_name (const char *name, int sbd)
{
	int len, i;
	
	len = strlen (name);
	
	if (sbd && !strcmp (name + len - 4, ".sbd"))
		return TRUE;
	
	for (i = 0; i < G_N_ELEMENTS (ignore_names); i++) {
		if (ignore_names[i].strlen > len)
			continue;
		
		if (!strcmp (name + len - ignore_names[i].strlen, ignore_names[i].str))
			return TRUE;
	}
	
	return FALSE;
}


SpruceFolder *
spruce_mbox_folder_new (SpruceStore *store, const char *full_name, GError **err)
{
	SpruceFolder *folder, *parent = NULL;
	char *summary, *path, *dirpath, *p;
	SpruceMboxFolder *mbox;
	const char *name;
	struct stat st;
	
	g_return_val_if_fail (SPRUCE_IS_MBOX_STORE (store), NULL);
	g_return_val_if_fail (full_name != NULL, NULL);
	
	/* get the parent folder so we can set that... */
	path = g_strdup (full_name);
	if ((name = p = strrchr (path, '/'))) {
		if (ignore_name (name, TRUE))
			goto illegal;
		
		/* this gets us the parent folder's full_name */
		*p = '\0';
		if ((parent = spruce_store_get_folder (store, path, err)) == NULL) {
			g_free (path);
			return NULL;
		}
	} else {
		name = full_name;
		
		if (ignore_name (name, TRUE))
			goto illegal;
		
		if (*full_name != '\0')
			parent = spruce_store_get_folder (store, "", err);
	}
	
	g_free (path);
	
	folder = g_object_new (SPRUCE_TYPE_MBOX_FOLDER, NULL);
	mbox = (SpruceMboxFolder *) folder;
	
	spruce_folder_construct (folder, store, parent, name, full_name);
	path = mbox_store_build_filename (store, full_name);
	((SpruceMboxFolder *) folder)->path = path;
	
	if (parent)
		g_object_unref (parent);
	
	if (*full_name != '\0') {
		if (stat (path, &st) == 0 && S_ISREG (st.st_mode)) {
			folder->type |= SPRUCE_FOLDER_CAN_HOLD_MESSAGES;
			folder->supports_searches = TRUE;
			folder->exists = TRUE;
			
			/* since the folder exists, instantiate a
			 * summary object and load the summary header
			 * (for cached unread, deleted, and total
			 * counts) */
			summary = mbox_get_summary_filename (mbox->path);
			folder->summary = spruce_mbox_summary_new (mbox->path);
			spruce_folder_summary_set_filename (folder->summary, summary);
			g_free (summary);
			
			spruce_folder_summary_header_load (folder->summary);
		}
		
		dirpath = g_strdup_printf ("%s.sbd", path);
		if (stat (dirpath, &st) && S_ISDIR (st.st_mode)) {
			folder->type |= SPRUCE_FOLDER_CAN_HOLD_FOLDERS;
			folder->exists = TRUE;
		}
		
		g_free (dirpath);
	} else {
		/* special root folder */
		folder->exists = TRUE;
	}
	
	return folder;
	
 illegal:
	
	g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_FOLDER_ILLEGAL_NAME,
		     _("Cannot get folder `%s': illegal mailbox name"),
		     name);
	
	return NULL;
}

static int
mbox_open (SpruceFolder *folder, GError **err)
{
	SpruceMboxFolder *mbox = (SpruceMboxFolder *) folder;
	int mode = SPRUCE_FOLDER_MODE_READ_WRITE;
	GMimeStream *stream;
	char *summary;
	int flags, fd;
	
	flags = O_RDWR;
 retry:
	if ((fd = open (mbox->path, flags, 0666)) == -1) {
		if (errno == EROFS) {
			/* try opening the mbox read-only */
			mode = SPRUCE_FOLDER_MODE_READ;
			flags = O_RDONLY;
			goto retry;
		}
		
		g_set_error (err, SPRUCE_ERROR, errno, _("Cannot open folder `%s': %s"),
			     folder->full_name, g_strerror (errno));
		return -1;
	}
	
	/* FIXME: perform some file locking... */
	
	folder->mode = mode;
	
	stream = g_mime_stream_fs_new (fd);
	mbox->stream = stream;
	
	if (folder->summary == NULL) {
		summary = mbox_get_summary_filename (mbox->path);
		folder->summary = spruce_mbox_summary_new (mbox->path);
		spruce_folder_summary_set_filename (folder->summary, summary);
		g_free (summary);
	}
	
	/* load the summary */
	spruce_folder_summary_load (folder->summary);
	
	return 0;
}

static int
mbox_close (SpruceFolder *folder, gboolean expunge, GError **err)
{
	SpruceMboxFolder *mbox = (SpruceMboxFolder *) folder;
	
	if (SPRUCE_FOLDER_CLASS (parent_class)->close (folder, expunge, err) == -1)
		return -1;
	
	g_mime_stream_flush (mbox->stream);
	g_object_unref (mbox->stream);
	mbox->stream = NULL;
	
	/* FIXME: unlock the mbox */
	
	return 0;
}

static int
mbox_create (SpruceFolder *folder, int type, GError **err)
{
	SpruceMboxFolder *mbox = (SpruceMboxFolder *) folder;
	char *path, *p;
	int fd;
	
	if ((p = strrchr (mbox->path, '/'))) {
		path = g_strndup (mbox->path, p - mbox->path);
		if (spruce_mkdir (path, 0777) == -1 && errno != EEXIST) {
			g_set_error (err, SPRUCE_ERROR, errno, _("Cannot create folder `%s': %s"),
				     folder->full_name, g_strerror (errno));
			g_free (path);
			return -1;
		}
		
		g_free (path);
	}
	
	if (type & SPRUCE_FOLDER_CAN_HOLD_MESSAGES) {
		if ((fd = open (mbox->path, O_CREAT | O_TRUNC | O_RDWR, 0666)) == -1) {
			g_set_error (err, SPRUCE_ERROR, errno, _("Cannot create folder `%s': %s"),
				     folder->full_name, g_strerror (errno));
			return -1;
		}
		
		close (fd);
		
		folder->supports_searches = TRUE;
	}
	
	if (type & SPRUCE_FOLDER_CAN_HOLD_FOLDERS) {
		path = g_strdup_printf ("%s.sbd", mbox->path);
		if (mkdir (path, 0777) == -1) {
			g_set_error (err, SPRUCE_ERROR, errno,
				     _("Cannot create folder `%s': %s"),
				     folder->full_name, g_strerror (errno));
			g_free (path);
			return -1;
		}
		
		g_free (path);
	}
	
	folder->type = type;
	folder->exists = TRUE;
	
	return 0;
}

static int
mbox_delete (SpruceFolder *folder, GError **err)
{
	SpruceMboxFolder *mbox = (SpruceMboxFolder *) folder;
	char *path;
	
	if (folder->type & SPRUCE_FOLDER_CAN_HOLD_FOLDERS) {
		path = g_strdup_printf ("%s.sbd", mbox->path);
		if (spruce_rmdir (mbox->path) == -1 && errno != ENOENT) {
			g_set_error (err, SPRUCE_ERROR, errno,
				     _("Cannot delete folder `%s': %s"),
				     folder->full_name, g_strerror (errno));
			g_free (path);
			return -1;
		}
		
		g_free (path);
	}
	
	if (folder->type & SPRUCE_FOLDER_CAN_HOLD_MESSAGES) {
		path = mbox_get_summary_filename (mbox->path);
		if (unlink (path) == -1 && errno != ENOENT) {
			g_set_error (err, SPRUCE_ERROR, errno, _("Cannot delete folder `%s': %s"),
				     folder->full_name, g_strerror (errno));
			g_free (path);
			return -1;
		}
		
		if (unlink (mbox->path) == -1 && errno != ENOENT) {
			g_set_error (err, SPRUCE_ERROR, errno, _("Cannot delete folder `%s': %s"),
				     folder->full_name, g_strerror (errno));
			return -1;
		}
		
		if (folder->summary) {
			g_object_unref (folder->summary);
			folder->summary = NULL;
		}
	}
	
	folder->type = 0;
	
	return 0;
}

static int
mbox_rename (SpruceFolder *folder, const char *newname, GError **err)
{
	SpruceMboxFolder *mbox = (SpruceMboxFolder *) folder;
	char *summary, *newpath, *olddir, *newdir;
	const char *oldsum, *basename;
	
	if (!(basename = strrchr (newname, '/')))
		basename = newname;
	else
		basename++;
	
	if (ignore_name (basename, TRUE)) {
		g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_FOLDER_ILLEGAL_NAME,
			     _("Cannot rename folder `%s' to `%s': illegal mailbox name"),
			     folder->full_name, newname);
		return -1;
	}
	
	newpath = mbox_store_build_filename (folder->store, newname);
	
	if (folder->type & SPRUCE_FOLDER_CAN_HOLD_MESSAGES) {
		if (rename (mbox->path, newpath) == -1) {
			g_set_error (err, SPRUCE_ERROR, errno, _("Cannot rename folder `%s' to `%s': %s"),
				     folder->full_name, newname, g_strerror (errno));
			g_free (newpath);
			return -1;
		}
	}
	
	if (folder->type & SPRUCE_FOLDER_CAN_HOLD_FOLDERS) {
		/* Note: All SpruceFolder objects listen to their
		 * parent folder's "renamed" signal.
		 * SpruceFolder::newname() will be called for
		 * each of our subfolders. */
		olddir = g_strdup_printf ("%s.sbd", mbox->path);
		newdir = g_strdup_printf ("%s.sbd", newpath);
		
		if (rename (olddir, newdir) == -1) {
			g_free (olddir);
			g_free (newdir);
			goto undo;
		}
		
		g_free (olddir);
		g_free (newdir);
	}
	
	g_free (mbox->path);
	mbox->path = newpath;
	
	if (folder->summary) {
		/* the summary file is renamed last in case any of the above renames fails */
		summary = mbox_get_summary_filename (newpath);
		if ((oldsum = spruce_folder_summary_get_filename (folder->summary))) {
			if (rename (oldsum, summary) == -1 && errno != ENOENT)
				unlink (oldsum);
		}
		
		spruce_mbox_summary_set_mbox ((SpruceMboxSummary *) folder->summary, newpath);
		spruce_folder_summary_set_filename (folder->summary, summary);
		g_free (summary);
	}
	
	return 0;
	
 undo:
	
	g_set_error (err, SPRUCE_ERROR, errno, _("Cannot rename folder `%s' to `%s': %s"),
		     folder->full_name, newname, g_strerror (errno));
	
	if (rename (newpath, mbox->path) == -1) {
		/* FIXME: now what? theoretically this should never happen */
	}
	
	g_free (newpath);
	
	return -1;
}

static void
mbox_newname (SpruceFolder *folder, const char *parent, const char *name)
{
	SpruceMboxFolder *mbox = (SpruceMboxFolder *) folder;
	const char *oldsum;
	char *summary;
	
	SPRUCE_FOLDER_CLASS (parent_class)->newname (folder, parent, name);
	
	g_free (mbox->path);
	mbox->path = mbox_store_build_filename (folder->store, folder->full_name);
	
	if (folder->summary) {
		/* the summary file is renamed last in case any of the above renames fails */
		summary = mbox_get_summary_filename (mbox->path);
		if ((oldsum = spruce_folder_summary_get_filename (folder->summary))) {
			if (rename (oldsum, summary) == -1 && errno != ENOENT)
				unlink (oldsum);
		}
		
		spruce_mbox_summary_set_mbox ((SpruceMboxSummary *) folder->summary, mbox->path);
		spruce_folder_summary_set_filename (folder->summary, summary);
		g_free (summary);
	}
}

static int
mbox_expunge (SpruceFolder *folder, GPtrArray *uids, GError **err)
{
	SpruceMboxFolder *mbox = (SpruceMboxFolder *) folder;
	char *filename, *from, *flags;
	SpruceMboxMessageInfo *minfo;
	gboolean expunge = FALSE;
	SpruceMessageInfo *info;
	GMimeMessage *message;
	GHashTable *uid_hash;
	GMimeStream *stream;
	GMimeParser *parser;
	GPtrArray *summary;
	int fd, max, i, j;
	
	if (SPRUCE_FOLDER_CLASS (parent_class)->expunge (folder, uids, err) == -1)
		return -1;
	
	max = spruce_folder_summary_count (folder->summary);
	
	if (uids) {
		/* create a uid hash while also checking if there is a need to expunge */
		uid_hash = g_hash_table_new (g_str_hash, g_str_equal);
		for (i = 0; i < uids->len; i++) {
			info = spruce_folder_summary_uid (folder->summary, uids->pdata[i]);
			if (info->flags & SPRUCE_MESSAGE_DELETED) {
				g_hash_table_insert (uid_hash, uids->pdata[i], uids->pdata[i]);
				expunge = TRUE;
			}
			
			spruce_folder_summary_info_unref (folder->summary, info);
		}
	} else {
		/* check if there is a need to expunge */
		for (i = 0; i < max; i++) {
			info = spruce_folder_summary_index (folder->summary, i);
			if (info->flags & SPRUCE_MESSAGE_DELETED) {
				spruce_folder_summary_info_unref (folder->summary, info);
				expunge = TRUE;
				break;
			}
			
			spruce_folder_summary_info_unref (folder->summary, info);
		}
		
		uid_hash = NULL;
	}
	
	if (!expunge) {
		/* nothing to expunge */
		if (uid_hash)
			g_hash_table_destroy (uid_hash);
		
		return 0;
	}
	
	if (g_mime_stream_flush (mbox->stream) == -1) {
		g_set_error (err, SPRUCE_ERROR, errno, _("Cannot expunge folder `%s': %s"),
			     folder->full_name, g_strerror (errno));
		
		if (uid_hash)
			g_hash_table_destroy (uid_hash);
		
		return -1;
	}
	
 retry:
	
	filename = g_strdup_printf ("%s.%u.XXXXXX", mbox->path, getpid ());
	if (mktemp (filename) == NULL) {
		g_set_error (err, SPRUCE_ERROR, errno, _("Cannot expunge folder `%s': %s"),
			     folder->full_name, g_strerror (errno));
		g_free (filename);
		
		if (uid_hash)
			g_hash_table_destroy (uid_hash);
		
		return -1;
	}
	
	if ((fd = open (filename, O_CREAT | O_TRUNC | O_EXCL | O_RDWR, 0666)) == -1) {
		g_free (filename);
		
		/* if it was because the file already exists, just try again */
		if (errno == EEXIST)
			goto retry;
		
		g_set_error (err, SPRUCE_ERROR, errno, _("Cannot expunge folder `%s': %s"),
			     folder->full_name, g_strerror (errno));
		
		if (uid_hash)
			g_hash_table_destroy (uid_hash);
		
		return -1;
	}
	
	stream = g_mime_stream_fs_new (fd);
	
	for (i = 0; i < max; i++) {
		info = (SpruceMessageInfo *) folder->summary->messages->pdata[i];
		
		if ((info->flags & SPRUCE_MESSAGE_DELETED) &&
		    (!uid_hash || g_hash_table_lookup (uid_hash, info->uid)))
			continue;
		
		minfo = (SpruceMboxMessageInfo *) info;
		
		if (minfo->frompos == -1 ||
		    g_mime_stream_seek (mbox->stream, minfo->frompos, SEEK_SET) == -1)
			goto exception;
		
		parser = g_mime_parser_new ();
		g_mime_parser_init_with_stream (parser, mbox->stream);
		g_mime_parser_set_scan_from (parser, TRUE);
		
		if (!(message = g_mime_parser_construct_message (parser))) {
			g_object_unref (parser);
			goto exception;
		}
		
		flags = spruce_mbox_summary_flags_encode (minfo);
		g_mime_object_set_header ((GMimeObject *) message, "X-Spruce", flags);
		g_free (flags);
		
		from = g_mime_parser_get_from (parser);
		if (g_mime_stream_printf (stream, "%s\n", from) == -1 ||
		    g_mime_object_write_to_stream ((GMimeObject *) message, stream) == -1) {
			g_object_unref (message);
			g_free (from);
			goto exception;
		}
		
		g_object_unref (message);
		g_free (from);
	}
	
	if (g_mime_stream_flush (stream) == -1)
		goto exception;
	
	g_object_unref (stream);
	stream = NULL;
	
	if (rename (filename, mbox->path) == -1)
		goto exception;
	
	if (uid_hash)
		g_hash_table_destroy (uid_hash);
	
	g_free (filename);
	
	/* need to regenerate the summary */
	spruce_folder_summary_reload (folder->summary);
	
	if (folder->mode == SPRUCE_FOLDER_MODE_READ_WRITE)
		fd = open (mbox->path, O_RDWR);
	else
		fd = open (mbox->path, O_RDONLY);
	
	/* FIXME: unlock the old fd and lock the new */
	
	g_object_unref (mbox->stream);
	
	if (fd != -1)
		mbox->stream = g_mime_stream_fs_new (fd);
	else
		mbox->stream = NULL;
	
	return 0;
	
 exception:
	
	g_set_error (err, SPRUCE_ERROR, errno, _("Cannot expunge folder `%s': %s"),
		     folder->full_name, g_strerror (errno));
	
	if (uid_hash)
		g_hash_table_destroy (uid_hash);
	
	if (stream)
		g_object_unref (stream);
	
	unlink (filename);
	g_free (filename);
	
	return -1;
}

static GPtrArray *
mbox_list (SpruceFolder *folder, const char *pattern, GError **err)
{
	SpruceMboxFolder *mbox = (SpruceMboxFolder *) folder;
	SpruceFolder *subfolder;
	char *full_name, *name;
	GHashTable *list_hash;
	struct dirent *dent;
	GPatternSpec *pspec;
	GPtrArray *list;
	GString *path;
	DIR *dir;
	size_t n;
	
	path = g_string_new (mbox->path);
	if (*folder->full_name != '\0')
		g_string_append_len (path, ".sbd", 4);
	
	if (!(dir = opendir (path->str))) {
		g_set_error (err, SPRUCE_ERROR, errno,
			     _("Cannot list subfolders of `%s': %s"),
			     folder->full_name, g_strerror (errno));
		g_string_free (path, TRUE);
		return NULL;
	}
	
	list_hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	g_string_append_c (path, G_DIR_SEPARATOR);
	pspec = g_pattern_spec_new (pattern);
	list = g_ptr_array_new ();
	n = path->len;
	
	while ((dent = readdir (dir))) {
		/* ignore ".", "..", hidden files/dirs, and others */
		if (dent->d_name[0] == '.' || ignore_name (dent->d_name, FALSE))
			continue;
		
		g_string_truncate (path, n);
		
		/* FIXME: really should be converting d_name to UTF-8 */
		g_string_append (path, dent->d_name);
		name = path->str + n;
		
		/* trim .sbd for pattern matching */
		if (g_str_has_suffix (name, ".sbd"))
			path->str[path->len - 4] = '\0';
		
		/* check if we've already gotten this folder */
		if ((subfolder = g_hash_table_lookup (list_hash, name)))
			continue;
		
		if (!g_pattern_match_string (pspec, name))
			continue;
		
		if (*folder->full_name)
			full_name = g_strdup_printf ("%s/%s", folder->full_name, name);
		else
			full_name = g_strdup (name);
		
		subfolder = spruce_mbox_folder_new (folder->store, full_name, NULL);
		g_free (full_name);
		
		if (subfolder == NULL)
			continue;
		
		g_hash_table_insert (list_hash, g_strdup (name), subfolder);
		g_ptr_array_add (list, subfolder);
	}
	
	g_hash_table_destroy (list_hash);
	g_pattern_spec_free (pspec);
	g_string_free (path, TRUE);
	closedir (dir);
	
	return list;
}

static void
parser_got_xspruce (GMimeParser *parser, const char *header, const char *value,
		    gint64 offset, gpointer user_data)
{
	SpruceMboxMessageInfo *info = user_data;
	
	/* update the offset of the X-Spruce header in case it has
           either changed or we didn't have the offset before */
	if (offset != -1)
		info->flagspos = offset;
}

static GMimeMessage *
mbox_get_message (SpruceFolder *folder, const char *uid, GError **err)
{
	SpruceMboxFolder *mbox = (SpruceMboxFolder *) folder;
	SpruceMboxMessageInfo *info;
	GMimeMessage *message;
	GMimeStream *stream;
	GMimeParser *parser;
	gint64 offset;
	
	if (!(info = (SpruceMboxMessageInfo *) spruce_folder_summary_uid (folder->summary, uid))) {
		g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_FOLDER_NO_SUCH_MESSAGE,
			     _("Cannot get message %s from folder `%s': no such message"),
			     uid, folder->full_name);
		return NULL;
	}
	
	g_assert (info->frompos > -1);
	
	stream = mbox->stream;
	offset = info->frompos;
	
	if (g_mime_stream_seek (stream, offset, SEEK_SET) == -1) {
		g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_FOLDER_NO_SUCH_MESSAGE,
			     _("Cannot get message %s from folder `%s': %s"),
			     uid, folder->full_name, g_strerror (errno));
		spruce_folder_summary_info_unref (folder->summary, (SpruceMessageInfo *) info);
		return NULL;
	}
	
	parser = g_mime_parser_new ();
	g_mime_parser_init_with_stream (parser, stream);
	g_mime_parser_set_scan_from (parser, TRUE);
	g_mime_parser_set_header_regex (parser, "^X-Spruce$", parser_got_xspruce, &info);
	
	if (!(message = g_mime_parser_construct_message (parser))) {
		g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_FOLDER_NO_SUCH_MESSAGE,
			     _("Cannot get message %s from folder `%s': internal parser error"),
			     uid, folder->full_name);
	}
	
	g_object_unref (parser);
	
	spruce_folder_summary_info_unref (folder->summary, (SpruceMessageInfo *) info);
	
	return message;
}

static char *tm_months[] = {
	"Jan", "Feb", "Mar", "Apr", "May", "Jun",
	"Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

static char *tm_days[] = {
	"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
};

static char *
mbox_create_from_line (GMimeMessage *message)
{
	const char *sender, *received;
	time_t date = 0;
	int offset = 0;
	GString *from;
	struct tm tm;
	char *ret;
	
	from = g_string_new ("From ");
	
	if (!(sender = g_mime_object_get_header ((GMimeObject *) message, "Sender")))
		sender = g_mime_object_get_header ((GMimeObject *) message, "From");
	
	if (sender != NULL) {
		/* parse the address */
		InternetAddressList *addrlist;
		InternetAddress *addr;
		int count, i;
		
		if ((addrlist = internet_address_list_parse_string (sender))) {
			count = internet_address_list_length (addrlist);
			
			for (i = 0; i < count; i++) {
				addr = internet_address_list_get_address (addrlist, i);
				if (INTERNET_ADDRESS_IS_MAILBOX (addr)) {
					g_string_append (from, INTERNET_ADDRESS_MAILBOX (addr)->addr);
					break;
				}
			}
			
			g_object_unref (addrlist);
		}
	}
	
	if (from->len == 5)
		g_string_append (from, "postmaster@localhost");
	
	/* try to use the date in the Received header */
	if ((received = g_mime_object_get_header ((GMimeObject *) message, "Received"))) {
		if ((received = strrchr (received, ';')))
			date = g_mime_utils_header_decode_date (received, &offset);
	}
	
	/* fall back to the Date header... */
	if (date == (time_t) 0)
		g_mime_message_get_date (message, &date, &offset);
	
	/* when all else fails, use the current time? */
	if (date == (time_t) 0)
		date = time (NULL);
	
	/* *Sigh* This needs a slightly different format than the Date
           header so we can't re-use g_mime_utils_header_format_date() */
	
	date += ((offset / 100) * (60 * 60)) + (offset % 100) * 60;
	
#ifdef HAVE_GMTIME_R
	gmtime_r (&date, &tm);
#else
	memcpy (&tm, gmtime (&date), sizeof (tm));
#endif
	
	g_string_append_printf (from, " %s %s %2d %02d:%02d:%02d %4d\n",
				tm_days[tm.tm_wday], tm_months[tm.tm_mon],
				tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec,
				tm.tm_year + 1900);
	
	ret = from->str;
	g_string_free (from, FALSE);
	
	return ret;
}

static int
mbox_append_message (SpruceFolder *folder, GMimeMessage *message, SpruceMessageInfo *info, GError **err)
{
	SpruceMboxFolder *mbox = (SpruceMboxFolder *) folder;
	SpruceMboxMessageInfo *mbox_info = NULL;
	GMimeStream *filtered_stream = NULL;
	GMimeFilter *from_filter;
	char *xspruce, *from;
	gint64 offset;
	int fd;
	
	if ((offset = g_mime_stream_seek (mbox->stream, 0, SEEK_END)) == -1) {
		g_set_error (err, SPRUCE_ERROR, errno, _("Cannot append to folder `%s': %s"),
			     folder->full_name, g_strerror (errno));
		return -1;
	}
	
	from = mbox_create_from_line (message);
	
	if (g_mime_stream_printf (mbox->stream, offset == 0 ? "%s" : "\n%s", from) == -1) {
		g_free (from);
		goto undo;
	}
	
	g_free (from);
	
	mbox_info = (SpruceMboxMessageInfo *) spruce_folder_summary_info_new_from_message (folder->summary, message);
	((SpruceMessageInfo *) mbox_info)->flags = info ? info->flags : 0;
	mbox_info->frompos = offset == 0 ? offset : offset + 1;
	mbox_info->flagspos = -1;
	
	spruce_folder_summary_add (folder->summary, (SpruceMessageInfo *) mbox_info);
	
	xspruce = spruce_mbox_summary_flags_encode (mbox_info);
	g_assert (xspruce != NULL);
	
	g_mime_object_set_header ((GMimeObject *) message, "X-Spruce", xspruce);
	g_free (xspruce);
	
	from_filter = g_mime_filter_from_new (GMIME_FILTER_FROM_MODE_ESCAPE);
	filtered_stream = g_mime_stream_filter_new (mbox->stream);
	g_mime_stream_filter_add (GMIME_STREAM_FILTER (filtered_stream), from_filter);
	
	if (g_mime_object_write_to_stream ((GMimeObject *) message, filtered_stream) == -1)
		goto undo;
	
	if (g_mime_stream_flush (filtered_stream) == -1)
		goto undo;
	
	g_object_unref (filtered_stream);
	
	spruce_folder_summary_info_unref (folder->summary, (SpruceMessageInfo *) mbox_info);
	spruce_folder_summary_touch (folder->summary);
	
	return 0;
	
 undo:
	
	g_set_error (err, SPRUCE_ERROR, errno, _("Cannot append to folder `%s': %s"),
		     folder->full_name, g_strerror (errno));
	
	if (filtered_stream)
		g_object_unref (filtered_stream);
	
	/* remove and destroy our message-info */
	if (mbox_info) {
		spruce_folder_summary_remove (folder->summary, (SpruceMessageInfo *) mbox_info);
		spruce_folder_summary_info_unref (folder->summary, (SpruceMessageInfo *) mbox_info);
	}
	
	/* remove any X-Spruce header */
	g_mime_object_remove_header ((GMimeObject *) message, "X-Spruce");
	
	/* truncate the file back to its original length */
	g_mime_stream_seek (mbox->stream, 0, SEEK_SET);
	fd = ((GMimeStreamFs *) mbox->stream)->fd;
	
	while (ftruncate (fd, offset) == -1 && errno == EINTR)
		;
	
	return -1;
}

static GPtrArray *
mbox_search (SpruceFolder *folder, GPtrArray *uids, const char *expression, GError **err)
{
	SpruceFolderSearch *search;
	GPtrArray *matches, *summary;
	SpruceMessageInfo *info;
	int i;
	
	summary = g_ptr_array_new ();
	for (i = 0; i < uids->len; i++) {
		if ((info = spruce_folder_summary_uid (folder->summary, uids->pdata[i])))
			g_ptr_array_add (summary, info);
	}
	
	search = spruce_folder_search_new ();
	spruce_folder_search_set_folder (search, folder);
	spruce_folder_search_set_summary (search, summary);
	
	matches = spruce_folder_search_match_all (search, expression);
	g_object_unref (search);
	
	for (i = 0; i < summary->len; i++) {
		info = summary->pdata[i];
		spruce_folder_summary_info_unref (folder->summary, info);
	}
	
	return matches;
}
