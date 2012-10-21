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
#include <sys/utsname.h>
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

#include "spruce-maildir-store.h"
#include "spruce-maildir-folder.h"
#include "spruce-maildir-summary.h"
#include "spruce-maildir-utils.h"


static char *maildir_subdirs[] = { "cur", "new", "tmp" };


static void spruce_maildir_folder_class_init (SpruceMaildirFolderClass *klass);
static void spruce_maildir_folder_init (SpruceMaildirFolder *folder, SpruceMaildirFolderClass *klass);
static void spruce_maildir_folder_finalize (GObject *object);

static int maildir_open (SpruceFolder *folder, GError **err);
static int maildir_create (SpruceFolder *folder, int type, GError **err);
static int maildir_delete (SpruceFolder *folder, GError **err);
static int maildir_rename (SpruceFolder *folder, const char *newname, GError **err);
static void maildir_newname (SpruceFolder *folder, const char *parent, const char *name);
static int maildir_expunge (SpruceFolder *folder, GPtrArray *uids, GError **err);
static GPtrArray *maildir_list (SpruceFolder *folder, const char *pattern, GError **err);
static GMimeMessage *maildir_get_message (SpruceFolder *folder, const char *uid, GError **err);
static int maildir_append_message (SpruceFolder *folder, GMimeMessage *message,
				   SpruceMessageInfo *info, GError **err);
static GPtrArray *maildir_search (SpruceFolder *folder, GPtrArray *uids, const char *expression, GError **err);


static SpruceFolderClass *parent_class = NULL;


GType
spruce_maildir_folder_get_type (void)
{
	static GType type = 0;
	
	if (!type) {
		static const GTypeInfo info = {
			sizeof (SpruceMaildirFolderClass),
			NULL, /* base_class_init */
			NULL, /* base_class_finalize */
			(GClassInitFunc) spruce_maildir_folder_class_init,
			NULL, /* class_finalize */
			NULL, /* class_data */
			sizeof (SpruceMaildirFolder),
			0,    /* n_preallocs */
			(GInstanceInitFunc) spruce_maildir_folder_init,
		};
		
		type = g_type_register_static (SPRUCE_TYPE_FOLDER, "SpruceMaildirFolder", &info, 0);
	}
	
	return type;
}


static void
spruce_maildir_folder_class_init (SpruceMaildirFolderClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	SpruceFolderClass *folder_class = SPRUCE_FOLDER_CLASS (klass);
	
	parent_class = g_type_class_ref (SPRUCE_TYPE_FOLDER);
	
	object_class->finalize = spruce_maildir_folder_finalize;
	
	/* virtual method overload */
	folder_class->open = maildir_open;
	folder_class->create = maildir_create;
	folder_class->delete = maildir_delete;
	folder_class->rename = maildir_rename;
	folder_class->newname = maildir_newname;
	folder_class->expunge = maildir_expunge;
	folder_class->list = maildir_list;
	folder_class->get_message = maildir_get_message;
	folder_class->append_message = maildir_append_message;
	folder_class->search = maildir_search;
}

static void
spruce_maildir_folder_init (SpruceMaildirFolder *maildir, SpruceMaildirFolderClass *klass)
{
	SpruceFolder *folder = (SpruceFolder *) maildir;
	
	folder->separator = '/';
	folder->permanent_flags = SPRUCE_MESSAGE_ANSWERED | SPRUCE_MESSAGE_DELETED |
		SPRUCE_MESSAGE_DRAFT | SPRUCE_MESSAGE_FLAGGED | SPRUCE_MESSAGE_SEEN;
	
	maildir->path = NULL;
}

static void
spruce_maildir_folder_finalize (GObject *object)
{
	SpruceMaildirFolder *maildir = (SpruceMaildirFolder *) object;
	
	g_free (maildir->path);
	
	G_OBJECT_CLASS (parent_class)->finalize (object);
}


static gboolean
maildir_is_subdir (const char *name)
{
	int i;
	
	for (i = 0; i < 3; i++) {
		if (!strcmp (name, maildir_subdirs[i]))
			return TRUE;
	}
	
	return FALSE;
}

static char *
maildir_get_summary_filename (const char *maildir)
{
	/* /path/to/maildir/.summary */
	return g_build_filename (maildir, ".summary", NULL);
}

static char *
maildir_build_filename (const char *toplevel_dir, const char *full_name)
{
	register const char *s = full_name;
	register char *d;
	char *path;
	
	path = g_malloc (strlen (toplevel_dir) + strlen (full_name) + 3);
	d = g_stpcpy (path, toplevel_dir);
	
	if (*full_name == '\0')
		return path;
	
	if (d[-1] != '/')
		*d++ = '/';
	
	*d++ = '.';
	while (*s) {
		if (*s == '/')
			*d++ = '.';
		else
			*d++ = *s;
		s++;
	}
	
	*d = '\0';
	
	return path;
}

#define maildir_store_build_filename(s,n) maildir_build_filename (((SpruceService *) s)->url->path, n)


SpruceFolder *
spruce_maildir_folder_new (SpruceStore *store, const char *full_name, GError **err)
{
	SpruceFolder *folder, *parent = NULL;
	SpruceMaildirFolder *maildir;
	char *path, *summary, *p;
	const char *name;
	struct stat st;
	
	g_return_val_if_fail (SPRUCE_IS_MAILDIR_STORE (store), NULL);
	g_return_val_if_fail (full_name != NULL, NULL);
	
	/* get the parent folder so we can set that... */
	path = g_strdup (full_name);
	if ((name = p = strrchr (path, '/'))) {
		/* this gets us the parent folder's pathname */
		*p = '\0';
		if ((parent = spruce_store_get_folder (store, path, err)) == NULL) {
			g_free (path);
			return NULL;
		}
	} else {
		name = full_name;
		if (*full_name != '\0')
			parent = spruce_store_get_folder (store, "", err);
	}
	
	g_free (path);
	
	path = maildir_build_filename (((SpruceService *) store)->url->path, full_name);
	
	if (*full_name == '\0') {
		full_name = "Inbox";
		name = "Inbox";
	}
	
	folder = g_object_new (SPRUCE_TYPE_MAILDIR_FOLDER, NULL);
	maildir = (SpruceMaildirFolder *) folder;
	
	spruce_folder_construct (folder, store, parent, name, full_name);
	((SpruceMaildirFolder *) folder)->path = path;
	
	if (parent)
		g_object_unref (parent);
	
	if (stat (path, &st) == -1) {
		/* if the path doesn't exist, then we are still in the clear */
		if (errno != ENOENT) {
			/* otherwise we are not... */
			g_set_error (err, SPRUCE_ERROR, errno, "%s", g_strerror (errno));
			g_object_unref (folder);
			return NULL;
		}
	} else if (!S_ISDIR (st.st_mode)) {
		g_set_error (err, SPRUCE_ERROR, ENOTDIR, "%s", g_strerror (ENOTDIR));
		g_object_unref (folder);
		return NULL;
	} else {
		/* FIXME: also check for the maildir subdirs? */
		folder->type = SPRUCE_FOLDER_CAN_HOLD_ANYTHING;
		folder->supports_searches = TRUE;
		folder->exists = TRUE;
		
		/* since the folder exists, instantiate a summary
		 * object and load the summary header (for cached
		 * unread, deleted, and total counts) */
		summary = maildir_get_summary_filename (maildir->path);
		folder->summary = spruce_maildir_summary_new (maildir->path);
		spruce_folder_summary_set_filename (folder->summary, summary);
		g_free (summary);
		
		spruce_folder_summary_header_load (folder->summary);
	}
	
	return folder;
}


enum {
	MAILDIR_ACCESS_NONE   = 0,
	MAILDIR_ACCESS_EXEC   = S_IXOTH,
	MAILDIR_ACCESS_WRITE  = S_IWOTH,
	MAILDIR_ACCESS_READ   = S_IROTH,
};

/**
 * maildir_access:
 * @path: maildir path
 * @mode: desired mode
 *
 * Checks the maildir @pathname.
 *
 * Returns the effective permissions on @pathname. On error, -1 is
 * returned and errno is set appropriately.
 **/
static int
maildir_access (const char *path, int mode)
{
	char *subdir, *p;
	struct stat st;
	int dirmode;
	int i = -1;
	uid_t uid;
	gid_t gid;
	
	if (stat (path, &st) == -1)
		return -1;
	
	uid = geteuid ();
	gid = getegid ();
	
	subdir = g_alloca (strlen (path) + 5);
	p = g_stpcpy (subdir, path);
	*p++ = '/';
	
	do {
		/* make sure we are looking at a directory */
		if (!S_ISDIR (st.st_mode)) {
			errno = ENOTDIR;
			return -1;
		}
		
		/* FIXME: is there an easier way to do this?? */
		dirmode = 0;
		if (st.st_uid == uid) {
			dirmode |= (st.st_mode & S_IRUSR) ? MAILDIR_ACCESS_READ : 0;
			dirmode |= (st.st_mode & S_IWUSR) ? MAILDIR_ACCESS_WRITE : 0;
			dirmode |= (st.st_mode & S_IXUSR) ? MAILDIR_ACCESS_EXEC : 0;
		} else if (st.st_gid == gid) {
			dirmode |= (st.st_mode & S_IRGRP) ? MAILDIR_ACCESS_READ : 0;
			dirmode |= (st.st_mode & S_IWGRP) ? MAILDIR_ACCESS_WRITE : 0;
			dirmode |= (st.st_mode & S_IXGRP) ? MAILDIR_ACCESS_EXEC : 0;
		} else {
			dirmode |= (st.st_mode & S_IROTH) ? MAILDIR_ACCESS_READ : 0;
			dirmode |= (st.st_mode & S_IWOTH) ? MAILDIR_ACCESS_WRITE : 0;
			dirmode |= (st.st_mode & S_IXOTH) ? MAILDIR_ACCESS_EXEC : 0;
		}
		
		mode &= dirmode;
		
		strcpy (p, maildir_subdirs[i++]);
		if (stat (subdir, &st) == -1)
			return -1;
	} while (mode && i < 3);
	
	return mode;
}

static int
maildir_open (SpruceFolder *folder, GError **err)
{
	SpruceMaildirFolder *maildir = (SpruceMaildirFolder *) folder;
	int mode = SPRUCE_FOLDER_MODE_READ_WRITE;
	char *summary;
	
	if ((mode = maildir_access (maildir->path, mode)) == -1) {
		g_set_error (err, SPRUCE_ERROR, errno,
			     _("Cannot open folder `%s': %s"),
			     folder->full_name, g_strerror (errno));
		return -1;
	}
	
	if (mode == MAILDIR_ACCESS_NONE) {
		errno = EACCES;
		g_set_error (err, SPRUCE_ERROR, errno,
			     _("Cannot open folder `%s': %s"),
			     folder->full_name, g_strerror (errno));
		return -1;
	}
	
	folder->mode = mode;
	
	if (folder->summary == NULL) {
		summary = maildir_get_summary_filename (maildir->path);
		folder->summary = spruce_maildir_summary_new (maildir->path);
		spruce_folder_summary_set_filename (folder->summary, summary);
		g_free (summary);
	}
	
	/* load the summary */
	spruce_folder_summary_load (folder->summary);
	
	return 0;
}

static int
maildir_create (SpruceFolder *folder, int type, GError **err)
{
	SpruceMaildirFolder *maildir = (SpruceMaildirFolder *) folder;
	char *subdir, *p;
	int errnosave;
	int i = 0;
	
	if (mkdir (maildir->path, 0777) == -1) {
		g_set_error (err, SPRUCE_ERROR, errno,
			     _("Cannot create folder `%s': %s"),
			     folder->full_name, g_strerror (errno));
		return -1;
	}
	
	subdir = g_alloca (strlen (maildir->path) + 5);
	p = g_stpcpy (subdir, maildir->path);
	*p++ = '/';
	
	/* create the maildir subdirs */
	while (i < 3) {
		strcpy (p, maildir_subdirs[i++]);
		if (mkdir (subdir, 0777) == -1)
			goto exception;
	}
	
	folder->supports_searches = TRUE;
	folder->type = SPRUCE_FOLDER_CAN_HOLD_ANYTHING;
	folder->exists = TRUE;
	
	return 0;
	
 exception:
	
	g_set_error (err, SPRUCE_ERROR, errno,
		     _("Cannot create folder `%s': %s"),
		     folder->full_name, g_strerror (errno));
	
	errnosave = errno;
	
	/* try to remove any of the maildir subdirs we have created */
	while (i > 0) {
		strcpy (p, maildir_subdirs[--i]);
		rmdir (subdir);
	}
	
	rmdir (maildir->path);
	
	errno = errnosave;
	
	return -1;
}

static int
maildir_delete (SpruceFolder *folder, GError **err)
{
	SpruceMaildirFolder *maildir = (SpruceMaildirFolder *) folder;
	struct dirent *dent;
	struct stat st;
	size_t pathlen;
	GString *path;
	int errnosave;
	int ret, i;
	DIR *dir;
	
	/* FIXME: is it really necessary to delete the subdirs first,
	 * before deleting cur/new/tmp and the base folder dir? */
	
	/* delete any cruft that may exist (summary files and whatever else) */
	if (!(dir = opendir (maildir->path))) {
		g_set_error (err, SPRUCE_ERROR, errno,
			     _("Cannot delete folder `%s': %s"),
			     folder->full_name, g_strerror (errno));
		return -1;
	}
	
	path = g_string_new (maildir->path);
	g_string_append_c (path, G_DIR_SEPARATOR);
	pathlen = path->len;
	
	while ((dent = readdir (dir))) {
		if (!strcmp (dent->d_name, ".") || !strcmp (dent->d_name, ".."))
			continue;
		
		if (maildir_is_subdir (dent->d_name))
			continue;
		
		g_string_append (path, dent->d_name);
		
		if ((ret = stat (path->str, &st)) != -1) {
			if (S_ISDIR (st.st_mode))
				ret = spruce_rmdir (path->str);
			else
				ret = unlink (path->str);
		}
		
		if (ret == -1) {
			g_set_error (err, SPRUCE_ERROR, errno,
				     _("Cannot delete folder `%s': %s"),
				     folder->full_name, g_strerror (errno));
			
			errnosave = errno;
			g_string_free (path, TRUE);
			closedir (dir);
			errno = errnosave;
			
			return -1;
		}
		
		g_string_truncate (path, pathlen);
	}
	
	closedir (dir);
	
	/* delete each maildir subdir (and its contents) */
	for (i = 0; i < 3; i++) {
		g_string_append (path, maildir_subdirs[i]);
		if (spruce_rmdir (path->str) == -1 && errno != ENOENT)
			goto exception;
		
		g_string_truncate (path, pathlen);
	}
	
	if (spruce_rmdir (maildir->path) == -1 && errno != ENOENT)
		goto exception;
	
	if (folder->summary) {
		g_object_unref (folder->summary);
		folder->summary = NULL;
	}
	
	g_string_free (path, TRUE);
	
	folder->type = 0;
	
	return 0;
	
 exception:
	
	g_set_error (err, SPRUCE_ERROR, errno,
		     _("Cannot delete folder `%s': %s"),
		     folder->full_name, g_strerror (errno));
	
	errnosave = errno;
	
	/* FIXME: if we've managed to delete any of the special
	 * subdirs, then our summary may be out-of-sync - probably
	 * will need to clear/re-load it. */
	
	/* we failed to delete all of the maildir subdirs and/or the
	   toplevel maildir folder so we must now re-create the
	   subdirs */
	g_string_truncate (path, pathlen);
	while (i > 0) {
		g_string_append (path, maildir_subdirs[--i]);
		mkdir (path->str, 0777);
	}
	
	g_string_free (path, TRUE);
	
	errno = errnosave;
	
	return -1;
}

static int
maildir_rename (SpruceFolder *folder, const char *newname, GError **err)
{
	/* FIXME: rename all subdirs too */
	SpruceMaildirFolder *maildir = (SpruceMaildirFolder *) folder;
	char *summary, *newpath;
	
	newpath = maildir_store_build_filename (folder->store, newname);
	
	if (rename (maildir->path, newpath) == -1) {
		g_set_error (err, SPRUCE_ERROR, errno,
			     _("Cannot rename folder `%s': %s"),
			     folder->full_name, g_strerror (errno));
		g_free (newpath);
		return -1;
	}
	
	if (folder->summary) {
		/* no need to rename() the summary because it got renamed above along with everything else */
		summary = maildir_get_summary_filename (newpath);
		spruce_maildir_summary_set_maildir ((SpruceMaildirSummary *) folder->summary, newpath);
		spruce_folder_summary_set_filename (folder->summary, summary);
		g_free (summary);
	}
	
	g_free (maildir->path);
	maildir->path = newpath;
	
	return 0;
}

static void
maildir_newname (SpruceFolder *folder, const char *parent, const char *name)
{
	SpruceMaildirFolder *maildir = (SpruceMaildirFolder *) folder;
	char *summary;
	
	SPRUCE_FOLDER_CLASS (parent_class)->newname (folder, parent, name);
	
	g_free (maildir->path);
	maildir->path = maildir_store_build_filename (folder->store, folder->full_name);
	
	if (folder->summary) {
		summary = maildir_get_summary_filename (maildir->path);
		spruce_maildir_summary_set_maildir ((SpruceMaildirSummary *) folder->summary, maildir->path);
		spruce_folder_summary_set_filename (folder->summary, summary);
		g_free (summary);
	}
}


struct _expunge_data {
	SpruceFolderSummary *summary;
	GHashTable *hash;
	guint32 left;
};

static int
maildir_expunge_messages_cb (const char *maildir, const char *subdir,
			     const char *d_name, void *user_data)
{
	struct _expunge_data *expunge = user_data;
	SpruceMessageInfo *info;
	char *path;
	
	if ((info = g_hash_table_lookup (expunge->hash, d_name))) {
		path = g_strdup_printf ("%s/%s/%s", maildir, subdir, d_name);
		if (unlink (path) == -1 && errno != ENOENT) {
			g_free (path);
			return -1;
		}
		
		/* we expunged this message so remove it from our summary */
		spruce_folder_summary_remove (expunge->summary, info);
		g_hash_table_remove (expunge->hash, info->uid);
		expunge->left--;
	}
	
	return expunge->left > 0;
}

static void
expunge_message (gpointer key, gpointer val, gpointer user_data)
{
	SpruceFolderSummary *summary = user_data;
	SpruceMessageInfo *info = val;
	
	spruce_folder_summary_remove (summary, info);
}

static int
maildir_expunge_messages (SpruceFolder *folder, GPtrArray *expunge)
{
	/* Maildir message filenames have the form: uid:info,flags */
	SpruceMaildirFolder *maildir = (SpruceMaildirFolder *) folder;
	struct _expunge_data *ed;
	SpruceMessageInfo *info;
	GHashTable *hash;
	int i;
	
	hash = g_hash_table_new ((GHashFunc) maildir_hash, (GCompareFunc) maildir_equal);
	for (i = 0; i < expunge->len; i++) {
		info = (SpruceMessageInfo *) expunge->pdata[i];
		g_hash_table_insert (hash, info->uid, info);
	}
	
	ed = g_new (struct _expunge_data, 1);
	ed->summary = folder->summary;
	ed->left = expunge->len;
	ed->hash = hash;
	
	if (maildir_foreach (maildir->path, maildir_expunge_messages_cb, ed) == -1)
		goto exception;
	
	g_free (ed);
	
	/* any messages that we didn't find can safely be assumed to
	   be expunged - probably by another client */
	g_hash_table_foreach (hash, expunge_message, folder->summary);
	g_hash_table_destroy (hash);
	
	return 0;
	
 exception:
	
	g_free (ed);
	
	g_hash_table_destroy (hash);
	
	return -1;
}

static int
maildir_expunge (SpruceFolder *folder, GPtrArray *uids, GError **err)
{
	SpruceMessageInfo *info;
	GPtrArray *expunge;
	int ret, max, i;
	
	if (SPRUCE_FOLDER_CLASS (parent_class)->expunge (folder, uids, err) == -1)
		return -1;
	
	expunge = g_ptr_array_new ();
	
	if (uids) {
		for (i = 0; i < uids->len; i++) {
			info = spruce_folder_summary_uid (folder->summary, uids->pdata[i]);
			if (info->flags & SPRUCE_MESSAGE_DELETED) {
				g_ptr_array_add (expunge, info);
				continue;
			}
			
			spruce_folder_summary_info_unref (folder->summary, info);
		}
	} else {
		max = spruce_folder_summary_count (folder->summary);
		for (i = 0; i < max; i++) {
			info = spruce_folder_summary_index (folder->summary, i);
			if (info->flags & SPRUCE_MESSAGE_DELETED) {
				g_ptr_array_add (expunge, info);
				continue;
			}
			
			spruce_folder_summary_info_unref (folder->summary, info);
		}
	}
	
	if (expunge->len == 0) {
		g_ptr_array_free (expunge, TRUE);
		return 0;
	}
	
	ret = maildir_expunge_messages (folder, expunge);
	
	for (i = 0; i < expunge->len; i++) {
		info = (SpruceMessageInfo *) expunge->pdata[i];
		spruce_folder_summary_info_unref (folder->summary, info);
	}
	
	g_ptr_array_free (expunge, TRUE);
	
	if (ret == -1)
		g_set_error (err, SPRUCE_ERROR, errno,
			     _("Cannot expunge folder `%s': %s"),
			     folder->full_name, g_strerror (errno));
	
	return ret;
}

static GPtrArray *
maildir_list (SpruceFolder *folder, const char *pattern, GError **err)
{
	SpruceFolder *subfolder, *toplevel;
	char *pdirname, *name;
	struct dirent *dent;
	GPatternSpec *pspec;
	size_t pdirlen, n;
	GPtrArray *list;
	GString *path;
	DIR *dir;
	
	toplevel = folder;
	while (toplevel->parent)
		toplevel = toplevel->parent;
	
	if (!(dir = opendir (((SpruceMaildirFolder *) toplevel)->path))) {
		g_set_error (err, SPRUCE_ERROR, errno,
			     _("Cannot list subfolders of `%s': %s"),
			     folder->full_name, g_strerror (errno));
		return NULL;
	}
	
	if (folder != toplevel) {
		register const char *s;
		register char *d;
		
		s = folder->full_name;
		pdirlen = strlen (s) + 1;
		d = pdirname = g_alloca (pdirlen + 1);
		while (*s != '\0') {
			if (*s == '/')
				*d++ = '.';
			else
				*d++ = *s;
			s++;
		}
		
		*d++ = '.';
		*d++ = '\0';
	} else {
		pdirname = "";
		pdirlen = 0;
	}
	
	path = g_string_new (((SpruceMaildirFolder *) toplevel)->path);
	g_string_append_c (path, '.');
	n = path->len;
	
	pspec = g_pattern_spec_new (pattern);
	list = g_ptr_array_new ();
	
	while ((dent = readdir (dir))) {
		/* ignore "." and ".." */
		if (!strcmp (dent->d_name, ".") || !strcmp (dent->d_name, ".."))
			continue;
		
		/* ignore special sub-folders */
		if (!strcmp (dent->d_name, "cur") ||
		    !strcmp (dent->d_name, "new") ||
		    !strcmp (dent->d_name, "tmp"))
			continue;
		
		g_string_truncate (path, n);
		
		/* FIXME: really should be converting d_name to UTF-8 */
		g_string_append (path, dent->d_name);
		name = path->str + n;
		
		/* check that @name is a subdir of @pdirname */
		if (strncmp (pdirname, name, pdirlen) != 0)
			continue;
		
		if (!g_pattern_match_string (pspec, name + pdirlen))
			continue;
		
		if (!(subfolder = spruce_maildir_folder_new (folder->store, name, NULL)))
			continue;
		
		g_ptr_array_add (list, subfolder);
	}
	
	g_pattern_spec_free (pspec);
	g_string_free (path, TRUE);
	closedir (dir);
	
	return list;
}

static GMimeMessage *
maildir_get_message (SpruceFolder *folder, const char *uid, GError **err)
{
	SpruceMaildirFolder *maildir = (SpruceMaildirFolder *) folder;
	char *filename, *cur, *new, *subdir, *p;
	SpruceMessageInfo *info;
	GMimeMessage *message;
	GMimeStream *stream;
	GMimeParser *parser;
	struct dirent *dent;
	int fd, i = 0;
	DIR *dir;
	
	if (!(info = spruce_folder_summary_uid (folder->summary, uid)))
		goto not_found;
	
	subdir = g_alloca (strlen (maildir->path) + 5);
	p = g_stpcpy (subdir, maildir->path);
	*p++ = '/';
	
	/* don't bother checking tmp/ */
	while (i < 2) {
		strcpy (p, maildir_subdirs[i++]);
		if (!(dir = opendir (subdir)))
			continue;
		
		while ((dent = readdir (dir))) {
			if (dent->d_name[0] == '.')
				continue;
			
			if (maildir_equal (dent->d_name, info->uid))
				goto found;
		}
		
		closedir (dir);
	}
	
 not_found:
	
	g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_FOLDER_NO_SUCH_MESSAGE,
		     _("Cannot get message %s from folder `%s': no such message"),
		     uid, folder->full_name);
	
	return NULL;
	
 found:
	
	filename = g_strdup_printf ("%s/%s", subdir, dent->d_name);
	
	/* if the message is in the 'new' subdir, then we want to move it to the 'cur' subdir */
	if (!strcmp (maildir_subdirs[i - 1], "new")) {
		new = filename;
		cur = g_strdup_printf ("%s/cur/%s", maildir->path, dent->d_name);
		if (rename (new, cur) == -1 && errno != EEXIST) {
			/* well, we can still read it I suppose? */
			g_free (cur);
		} else {
			g_free (new);
			filename = cur;
		}
	}
	
	closedir (dir);
	
	if ((fd = open (filename, O_RDONLY)) == -1) {
		g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_FOLDER_NO_SUCH_MESSAGE,
			     _("Cannot get message %s from folder `%s': %s"),
			     uid, folder->full_name, g_strerror (errno));
		
		g_free (filename);
		return NULL;
	}
	
	stream = g_mime_stream_fs_new (fd);
	
	parser = g_mime_parser_new ();
	g_mime_parser_init_with_stream (parser, stream);
	g_object_unref (stream);
	
	if (!(message = g_mime_parser_construct_message (parser))) {
		g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_FOLDER_NO_SUCH_MESSAGE,
			     _("Cannot get message %s from folder `%s': internal parser error"),
			     uid, folder->full_name);
	}
	
	g_object_unref (parser);
	
	spruce_folder_summary_info_unref (folder->summary, info);
	
	return message;
}

static int
maildir_append_message (SpruceFolder *folder, GMimeMessage *message, SpruceMessageInfo *info, GError **err)
{
	SpruceMaildirFolder *maildir = (SpruceMaildirFolder *) folder;
	SpruceMessageInfo *minfo = NULL;
	GMimeStream *stream = NULL;
	char *flags, *uid = NULL;
	char *new, *tmp = NULL;
	struct utsname name;
	int retries = 0;
	char *hostname;
	int errnosave;
	int fd = -1;
	
	minfo = spruce_folder_summary_info_new_from_message (folder->summary, message);
	minfo->flags = info->flags;
	
	if (uname (&name) == -1)
		hostname = "localhost.localdomain";
	else
		hostname = name.nodename;
	
	while (retries < 5) {
		uid = g_strdup_printf ("%ld.%d.%s", time (NULL), getpid (), hostname);
		tmp = g_strdup_printf ("%s/tmp/%s", maildir->path, uid);
		if ((fd = open (tmp, O_WRONLY | O_CREAT | O_TRUNC | O_EXCL, 0666)) != -1)
			break;
		
		retries++;
		g_free (uid);
		g_free (tmp);
		sleep (1);
	}
	
	if (fd == -1) {
		g_set_error (err, SPRUCE_ERROR, errno,
			     _("Cannot append to folder `%s': %s"),
			     folder->full_name, g_strerror (errno));
		spruce_folder_summary_info_unref (folder->summary, minfo);
		return -1;
	}
	
	stream = g_mime_stream_fs_new (fd);
	
	/* since the uid and flags are stored as part of the filename, we don't need this header */
	g_mime_object_remove_header ((GMimeObject *) message, "X-Spruce");
	
	if (g_mime_object_write_to_stream ((GMimeObject *) message, stream) == -1)
		goto exception;
	
	if (g_mime_stream_flush (stream) == -1)
		goto exception;
	
	g_object_unref (stream);
	stream = NULL;
	
	flags = spruce_maildir_summary_flags_encode (minfo);
	new = g_alloca (strlen (tmp) + 3 + strlen (flags) + 1);
	sprintf (new, "%s/new/%s:2,%s", maildir->path, uid, flags);
	g_free (flags);
	
	/* okay, now that it has been written to disk - we need to move it into the new/ subdir */
	if (rename (tmp, new) == -1)
		goto exception;
	
	/* set the message-info's uid */
	minfo->uid = uid;
	uid = strchr (uid, ':');
	*uid = '\0';
	
	spruce_folder_summary_add (folder->summary, minfo);
	spruce_folder_summary_info_unref (folder->summary, minfo);
	spruce_folder_summary_touch (folder->summary);
	
	return 0;
	
 exception:
	
	g_set_error (err, SPRUCE_ERROR, errno,
		     _("Cannot append to folder `%s': %s"),
		     folder->full_name, g_strerror (errno));
	
	errnosave = errno;
	
	if (stream) {
		g_object_unref (stream);
		unlink (tmp);
	}
	
	g_free (tmp);
	g_free (uid);
	
	/* destroy our message-info */
	spruce_folder_summary_info_unref (folder->summary, minfo);
	
	errno = errnosave;
	
	return -1;
}

static GPtrArray *
maildir_search (SpruceFolder *folder, GPtrArray *uids, const char *expression, GError **err)
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
