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
#include <spruce/spruce-cache-stream.h>
#include <spruce/spruce-folder-search.h>

#include "spruce-imap-utils.h"
#include "spruce-imap-store.h"
#include "spruce-imap-engine.h"
#include "spruce-imap-folder.h"
#include "spruce-imap-stream.h"
#include "spruce-imap-command.h"
#include "spruce-imap-summary.h"

#define d(x) x

static void spruce_imap_folder_class_init (SpruceIMAPFolderClass *klass);
static void spruce_imap_folder_init (SpruceIMAPFolder *folder, SpruceIMAPFolderClass *klass);
static void spruce_imap_folder_finalize (GObject *object);

static int imap_open (SpruceFolder *folder, GError **err);
static int imap_close (SpruceFolder *folder, gboolean expunge, GError **err);
static int imap_create (SpruceFolder *folder, int type, GError **err);
static int imap_delete (SpruceFolder *folder, GError **err);
static int imap_rename (SpruceFolder *folder, const char *newname, GError **err);
static void imap_newname (SpruceFolder *folder, const char *parent, const char *name);
static int imap_sync (SpruceFolder *folder, gboolean expunge, GError **err);
static int imap_expunge (SpruceFolder *folder, GPtrArray *uids, GError **err);
static GPtrArray *imap_list (SpruceFolder *folder, const char *pattern, GError **err);
static GPtrArray *imap_lsub (SpruceFolder *folder, const char *pattern, GError **err);
static int imap_subscribe (SpruceFolder *folder, GError **err);
static int imap_unsubscribe (SpruceFolder *folder, GError **err);
static GMimeMessage *imap_get_message (SpruceFolder *folder, const char *uid, GError **err);
static int imap_append_message (SpruceFolder *folder, GMimeMessage *message,
				SpruceMessageInfo *info, GError **err);
static int imap_copy_messages (SpruceFolder *src, GPtrArray *uids, SpruceFolder *dest, GError **err);
static int imap_move_messages (SpruceFolder *src, GPtrArray *uids, SpruceFolder *dest, GError **err);
static GPtrArray *imap_search (SpruceFolder *folder, GPtrArray *uids, const char *expression, GError **err);


static SpruceFolder *parent_class = NULL;


GType
spruce_imap_folder_get_type (void)
{
	static GType type = 0;
	
	if (!type) {
		static const GTypeInfo info = {
			sizeof (SpruceIMAPFolderClass),
			NULL, /* base_class_init */
			NULL, /* base_class_finalize */
			(GClassInitFunc) spruce_imap_folder_class_init,
			NULL, /* class_finalize */
			NULL, /* class_data */
			sizeof (SpruceIMAPFolder),
			0,    /* n_preallocs */
			(GInstanceInitFunc) spruce_imap_folder_init,
		};
		
		type = g_type_register_static (SPRUCE_TYPE_FOLDER, "SpruceIMAPFolder", &info, 0);
	}
	
	return type;
}


static void
spruce_imap_folder_class_init (SpruceIMAPFolderClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	SpruceFolderClass *folder_class = SPRUCE_FOLDER_CLASS (klass);
	
	parent_class = g_type_class_ref (SPRUCE_TYPE_FOLDER);
	
	object_class->finalize = spruce_imap_folder_finalize;
	
	/* virtual method overload */
	folder_class->open = imap_open;
	folder_class->close = imap_close;
	folder_class->create = imap_create;
	folder_class->delete = imap_delete;
	folder_class->rename = imap_rename;
	folder_class->newname = imap_newname;
	folder_class->sync = imap_sync;
	folder_class->expunge = imap_expunge;
	folder_class->list = imap_list;
	folder_class->lsub = imap_lsub;
	folder_class->subscribe = imap_subscribe;
	folder_class->unsubscribe = imap_unsubscribe;
	folder_class->get_message = imap_get_message;
	folder_class->append_message = imap_append_message;
	folder_class->copy_messages = imap_copy_messages;
	folder_class->move_messages = imap_move_messages;
	folder_class->search = imap_search;
}

static void
spruce_imap_folder_init (SpruceIMAPFolder *imap, SpruceIMAPFolderClass *klass)
{
	SpruceFolder *folder = (SpruceFolder *) imap;
	
	folder->separator = '/';
	folder->permanent_flags = SPRUCE_MESSAGE_ANSWERED | SPRUCE_MESSAGE_DELETED |
		SPRUCE_MESSAGE_DRAFT | SPRUCE_MESSAGE_FLAGGED | SPRUCE_MESSAGE_SEEN;
	
	imap->cache = NULL;
	imap->cachedir = NULL;
	imap->utf7_name = NULL;
}

static void
spruce_imap_folder_finalize (GObject *object)
{
	SpruceIMAPFolder *imap = (SpruceIMAPFolder *) object;
	
	g_free (imap->cachedir);
	g_free (imap->utf7_name);
	
	if (imap->cache)
		g_object_unref (imap->cache);
	
	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static char *
imap_get_summary_filename (const char *path)
{
	/* /path/to/imap/summary */
	return g_build_filename (path, "summary", NULL);
}

static char *
imap_build_filename (const char *toplevel_dir, const char *full_name)
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
	
	path = g_malloc (strlen (toplevel_dir) + (inptr - full_name) + (12 * subdirs) + 2);
	p = g_stpcpy (path, toplevel_dir);
	
	if (p[-1] != '/')
		*p++ = '/';
	
	inptr = full_name;
	while (*inptr != '\0') {
		while (*inptr != '/' && *inptr != '\0')
			*p++ = *inptr++;
		
		if (*inptr == '/') {
			p = g_stpcpy (p, "/subfolders/");
			inptr++;
			
			/* strip extranaeous '/'s */
			while (*inptr == '/')
				inptr++;
		}
	}
	
	*p = '\0';
	
	return path;
}

static char *
imap_store_build_filename (void *store, const char *full_name)
{
	SpruceService *service = (SpruceService *) store;
	const char *storage_path;
	char *toplevel_dir;
	char *path;
	
	/* FIXME: perhaps get_storage_path() should take 2 (or 3?) args:
	 *
	 * char *get_storage_path (SpruceSession *session, SpruceService *service [, SpruceFolder *folder]);
	 *
	 * this can return something like:
	 *
	 * "/base/storage/path" / "url->protocol" / "account name (or url_to_string?)" [ / "folder->full_name" ]
	 **/
	
	storage_path = spruce_session_get_storage_path (service->session);
	
	path = g_strdup_printf ("%s@%s", service->url->user, service->url->host);
	toplevel_dir = g_build_filename (storage_path, "imap", path, NULL);
	g_free (path);
	
	path = imap_build_filename (toplevel_dir, full_name);
	g_free (toplevel_dir);
	
	return path;
}

static char
imap_get_path_delim (SpruceIMAPEngine *engine, const char *full_name)
{
	SpruceIMAPNamespace *namespace;
	const char *slash;
	size_t len;
	char *top;
	
	if ((slash = strchr (full_name, '/')))
		len = (slash - full_name);
	else
		len = strlen (full_name);
	
	top = g_alloca (len + 1);
	memcpy (top, full_name, len);
	top[len] = '\0';
	
 retry:
	namespace = engine->namespaces.personal;
	while (namespace != NULL) {
		if (!strcmp (namespace->path, top))
			return namespace->sep;
		namespace = namespace->next;
	}
	
	namespace = engine->namespaces.other;
	while (namespace != NULL) {
		if (!strcmp (namespace->path, top))
			return namespace->sep;
		namespace = namespace->next;
	}
	
	namespace = engine->namespaces.shared;
	while (namespace != NULL) {
		if (!strcmp (namespace->path, top))
			return namespace->sep;
		namespace = namespace->next;
	}
	
	if (top[0] != '\0') {
		/* look for a default namespace? */
		top[0] = '\0';
		goto retry;
	}
	
	return '/';
}

static int
imap_folder_query_properties (SpruceFolder *folder, GError **err)
{
	SpruceIMAPEngine *engine = ((SpruceIMAPStore *) folder->store)->engine;
	spruce_imap_list_t *list = NULL;
	SpruceIMAPCommand *ic;
	const char *utf7_name;
	GPtrArray *array;
	int id, i;
	
	ic = spruce_imap_engine_queue (engine, NULL, "LIST \"\" %F\r\n", folder);
	spruce_imap_command_register_untagged (ic, "LIST", spruce_imap_untagged_list);
	ic->user_data = array = g_ptr_array_new ();
	
	while ((id = spruce_imap_engine_iterate (engine)) < ic->id && id != -1)
		;
	
	if (id == -1 || ic->status != SPRUCE_IMAP_COMMAND_COMPLETE) {
		for (i = 0; i < array->len; i++) {
			list = array->pdata[i];
			g_free (list->name);
			g_free (list);
		}
		
		g_propagate_error (err, ic->err);
		ic->err = NULL;
		
		g_ptr_array_free (array, TRUE);
		
		spruce_imap_command_unref (ic);
		
		return -1;
	}
	
	switch (ic->result) {
	case SPRUCE_IMAP_RESULT_OK:
		/* odds are there is only 1 item, but just in case: search for our folder */
		utf7_name = spruce_imap_folder_utf7_name ((SpruceIMAPFolder *) folder);
		for (i = 0; i < array->len; i++) {
			list = array->pdata[i];
			if (!strcmp (list->name, utf7_name))
				break;
			list = NULL;
		}
		
		if (list) {
			folder->separator = list->delim;
			folder->exists = TRUE;
			
			if (list->flags & SPRUCE_IMAP_FOLDER_NOINFERIORS) {
				/* this folder can only hold messages */
				folder->type = SPRUCE_FOLDER_CAN_HOLD_MESSAGES;
				folder->supports_searches = TRUE;
			} else {
				/* this folder can hold folders (and possibly messages) */
				folder->type = SPRUCE_FOLDER_CAN_HOLD_FOLDERS;
				if (!(list->flags & SPRUCE_IMAP_FOLDER_NOSELECT))
					folder->type |= SPRUCE_FOLDER_CAN_HOLD_MESSAGES;
			}
		} else {
			folder->exists = FALSE;
		}
		break;
	case SPRUCE_IMAP_RESULT_NO:
		/* the folder doesn't exist */
		folder->exists = FALSE;
		break;
	case SPRUCE_IMAP_RESULT_BAD:
		/* either our code sent a bad command or the server is buggy? */
		/* FIXME: what to do here? */
		folder->exists = FALSE;
		break;
	}
	
	for (i = 0; i < array->len; i++) {
		list = array->pdata[i];
		g_free (list->name);
		g_free (list);
	}
	
	g_ptr_array_free (array, TRUE);
	
	spruce_imap_command_unref (ic);
	
	return 0;
}

SpruceFolder *
spruce_imap_folder_new (SpruceStore *store, const char *full_name, gboolean query, GError **err)
{
	SpruceIMAPEngine *engine = ((SpruceIMAPStore *) store)->engine;
	SpruceFolder *folder, *parent = NULL;
	SpruceIMAPFolder *imap_folder;
	char delim, *path, *p;
	const char *name;
	
	g_return_val_if_fail (SPRUCE_IS_IMAP_STORE (store), NULL);
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
	
	if (!parent || parent->full_name[0] == '\0') {
		/* if our immediate parent is "" then we can't blindly use its separator */
		delim = imap_get_path_delim (engine, full_name);
	} else
		delim = parent->separator;
	
	imap_folder = g_object_new (SPRUCE_TYPE_IMAP_FOLDER, NULL);
	folder = (SpruceFolder *) imap_folder;
	
	spruce_folder_construct (folder, store, parent, name, full_name);
	folder->separator = delim;
	
	g_free (path);
	
	if (delim != '/') {
		char *real_name;
		
		p = real_name = g_strdup (full_name);
		while (*p != '\0') {
			if (*p == '/')
				*p = delim;
			p++;
		}
		
		imap_folder->utf7_name = spruce_imap_utf8_utf7 (real_name);
		g_free (real_name);
	} else {
		imap_folder->utf7_name = spruce_imap_utf8_utf7 (full_name);
	}
	
	if (query && *full_name) {
		if (imap_folder_query_properties (folder, err) == -1) {
			g_object_unref (folder);
			return NULL;
		}
	} else if (full_name[0]== '\0') {
		folder->type = SPRUCE_FOLDER_CAN_HOLD_FOLDERS;
		folder->exists = TRUE;
	} else {
		/* presumably our caller has LIST or LSUB info so
		 * doesn't need us to query the server for additional
		 * info */
	}
	
	folder->summary = spruce_imap_summary_new (folder);
	imap_folder->cachedir = imap_store_build_filename (store, folder->full_name);
	spruce_mkdir (imap_folder->cachedir, 0777);
	
	path = imap_get_summary_filename (imap_folder->cachedir);
	spruce_folder_summary_set_filename (folder->summary, path);
	g_free (path);
	
	path = g_build_filename (imap_folder->cachedir, "cache", NULL);
	imap_folder->cache = spruce_cache_new (path, (guint64) -1);
	g_free (path);
	
	spruce_folder_summary_header_load (folder->summary);
	
	return folder;
}

SpruceFolder *
spruce_imap_folder_new_list (SpruceFolder *parent, spruce_imap_list_t *list)
{
	SpruceStore *store = parent->store;
	SpruceIMAPFolder *imap_folder;
	char *full_name, *path;
	SpruceFolder *folder;
	const char *name;
	
	g_return_val_if_fail (SPRUCE_IS_IMAP_FOLDER (parent), NULL);
	g_return_val_if_fail (list != NULL, NULL);
	
	full_name = spruce_imap_utf7_utf8 (list->name);
	if (!(list->delim && (name = strrchr (full_name, list->delim))))
		name = full_name;
	
	imap_folder = g_object_new (SPRUCE_TYPE_IMAP_FOLDER, NULL);
	folder = (SpruceFolder *) imap_folder;
	
	spruce_folder_construct (folder, parent->store, parent, name, full_name);
	imap_folder->utf7_name = g_strdup (list->name);
	folder->separator = list->delim;
	folder->exists = TRUE;
	g_free (full_name);
	
	if (list->flags & SPRUCE_IMAP_FOLDER_NOINFERIORS) {
		/* this folder can only hold messages */
		folder->type = SPRUCE_FOLDER_CAN_HOLD_MESSAGES;
		folder->supports_searches = TRUE;
	} else {
		/* this folder can hold folders (and possibly messages) */
		folder->type = SPRUCE_FOLDER_CAN_HOLD_FOLDERS;
		if (!(list->flags & SPRUCE_IMAP_FOLDER_NOSELECT))
			folder->type |= SPRUCE_FOLDER_CAN_HOLD_MESSAGES;
	}
	
	folder->summary = spruce_imap_summary_new (folder);
	imap_folder->cachedir = imap_store_build_filename (store, folder->full_name);
	spruce_mkdir (imap_folder->cachedir, 0777);
	
	path = imap_get_summary_filename (imap_folder->cachedir);
	spruce_folder_summary_set_filename (folder->summary, path);
	g_free (path);
	
	path = g_build_filename (imap_folder->cachedir, "cache", NULL);
	imap_folder->cache = spruce_cache_new (path, (guint64) -1);
	g_free (path);
	
	spruce_folder_summary_header_load (folder->summary);
	
	return folder;
}

const char *
spruce_imap_folder_utf7_name (SpruceIMAPFolder *folder)
{
	return folder->utf7_name;
}

static int
imap_open (SpruceFolder *folder, GError **err)
{
	SpruceIMAPEngine *engine = ((SpruceIMAPStore *) folder->store)->engine;
	
	spruce_folder_summary_load (folder->summary);
	
	if (spruce_imap_engine_select_folder (engine, folder, err) == -1)
		return -1;
	
	return spruce_imap_summary_flush_updates (folder->summary, err);
}

static int
imap_close (SpruceFolder *folder, gboolean expunge, GError **err)
{
	SpruceIMAPEngine *engine = ((SpruceIMAPStore *) folder->store)->engine;
	SpruceIMAPCommand *ic = NULL;
	GPtrArray *expunged = NULL;
	SpruceMessageInfo *info;
	int id, i;
	
	/* sync any flags, but don't expunge */
	if (imap_sync (folder, FALSE, err) == -1)
		goto exception;
	
	if (expunge) {
		/* sending CLOSE is faster because we won't get flooded with untagged EXPUNGE responses */
		expunged = g_ptr_array_new ();
		for (i = 0; i < spruce_folder_summary_count (folder->summary); i++) {
			info = spruce_folder_summary_index (folder->summary, i);
			if (!(info->flags & SPRUCE_MESSAGE_DELETED)) {
				spruce_folder_summary_info_unref (folder->summary, info);
			} else {
				g_ptr_array_add (expunged, info);
			}
		}
		
		ic = spruce_imap_engine_queue (engine, folder, "CLOSE\r\n");
	} else if (engine->capa & SPRUCE_IMAP_CAPABILITY_UNSELECT) {
		/* Note: we only bother with UNSELECT to allow the
		 * server to unload state... this 'else' code branch
		 * could just as easily be dropped */
		ic = spruce_imap_engine_queue (engine, folder, "UNSELECT\r\n");
	} else {
		goto closed;
	}
	
	while ((id = spruce_imap_engine_iterate (engine)) < ic->id && id != -1)
		;
	
	if (id == -1 || ic->status != SPRUCE_IMAP_COMMAND_COMPLETE) {
		g_propagate_error (err, ic->err);
		ic->err = NULL;
		goto exception;
	}
	
	switch (ic->result) {
	case SPRUCE_IMAP_RESULT_OK:
		if (expunged) {
			for (i = 0; i < expunged->len; i++) {
				info = expunged->pdata[i];
				spruce_folder_summary_remove (folder->summary, info);
				spruce_folder_summary_info_unref (folder->summary, info);
			}
			
			g_ptr_array_free (expunged, TRUE);
		}
		break;
	case SPRUCE_IMAP_RESULT_NO:
		/* FIXME: would be good to save the NO reason into the err message */
		g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_FOLDER_ILLEGAL_NAME,
			     _("Cannot close folder `%s': Invalid mailbox name"),
			     folder->full_name);
		goto exception;
		break;
	case SPRUCE_IMAP_RESULT_BAD:
		g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_FOLDER_ILLEGAL_NAME,
			     _("Cannot close folder `%s': Bad command"),
			     folder->full_name);
		goto exception;
		break;
	}
	
	spruce_imap_command_unref (ic);
	
 closed:
	
	spruce_folder_summary_save (folder->summary);
	spruce_folder_summary_unload (folder->summary);
	folder->mode = 0;
	
	return 0;
	
 exception:
	
	if (expunged) {
		for (i = 0; i < expunged->len; i++)
			spruce_folder_summary_info_unref (folder->summary, expunged->pdata[i]);
		g_ptr_array_free (expunged, TRUE);
	}
	
	if (ic)
		spruce_imap_command_unref (ic);
	
	return -1;
}

static int
imap_create (SpruceFolder *folder, int type, GError **err)
{
	SpruceIMAPEngine *engine = ((SpruceIMAPStore *) folder->store)->engine;
	SpruceIMAPCommand *ic;
	int id, retval;
	char *name;
	
	if (type == SPRUCE_FOLDER_CAN_HOLD_FOLDERS) {
		/* tack on the dir sep to hint to the server we want to hold folders */
		name = g_strdup_printf ("%s%c", ((SpruceIMAPFolder *) folder)->utf7_name, folder->separator);
		ic = spruce_imap_engine_queue (engine, NULL, "CREATE %S\r\n", name);
		g_free (name);
	} else {
		/* if we want it to hold messages (/and folders) we don't want to tack on the dir sep */
		ic = spruce_imap_engine_queue (engine, NULL, "CREATE %F\r\n", folder);
	}
	
	while ((id = spruce_imap_engine_iterate (engine)) < ic->id && id != -1)
		;
	
	if (id == -1 || ic->status != SPRUCE_IMAP_COMMAND_COMPLETE) {
		g_propagate_error (err, ic->err);
		ic->err = NULL;
		spruce_imap_command_unref (ic);
		return -1;
	}
	
	switch (ic->result) {
	case SPRUCE_IMAP_RESULT_OK:
		/* query the type of folder that the server /actually/ created */
		retval = imap_folder_query_properties (folder, err);
		break;
	case SPRUCE_IMAP_RESULT_NO:
		/* FIXME: would be good to save the NO reason into the err message */
		g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_FOLDER_ILLEGAL_NAME,
			     _("Cannot create folder `%s': Invalid mailbox name"),
			     folder->full_name);
		retval = -1;
		break;
	case SPRUCE_IMAP_RESULT_BAD:
		g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_FOLDER_ILLEGAL_NAME,
			     _("Cannot create folder `%s': Bad command"),
			     folder->full_name);
		retval = -1;
		break;
	default:
		g_assert_not_reached ();
		retval = -1;
	}
	
	spruce_imap_command_unref (ic);
	
	return retval;
}

static int
imap_delete (SpruceFolder *folder, GError **err)
{
	SpruceIMAPEngine *engine = ((SpruceIMAPStore *) folder->store)->engine;
	SpruceIMAPFolder *imap_folder = (SpruceIMAPFolder *) folder;
	SpruceIMAPCommand *ic;
	int id, retval = 0;
	
	if (!strcmp (folder->full_name, "") || !strcmp (folder->full_name, "INBOX")) {
		g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_FOLDER_ILLEGAL_NAME,
			     _("Cannot delete folder `%s': Special folder"),
			     folder->full_name);
		
		return -1;
	}
	
	ic = spruce_imap_engine_queue (engine, NULL, "DELETE %F\r\n", folder);
	while ((id = spruce_imap_engine_iterate (engine)) < ic->id && id != -1)
		;
	
	if (id == -1 || ic->status != SPRUCE_IMAP_COMMAND_COMPLETE) {
		g_propagate_error (err, ic->err);
		ic->err = NULL;
		spruce_imap_command_unref (ic);
		return -1;
	}
	
	switch (ic->result) {
	case SPRUCE_IMAP_RESULT_OK:
		spruce_cache_delete (imap_folder->cache, NULL);
		folder->exists = FALSE;
		folder->type = 0;
		break;
	case SPRUCE_IMAP_RESULT_NO:
		/* FIXME: would be good to save the NO reason into the err message */
		g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_FOLDER_ILLEGAL_NAME,
			     _("Cannot delete folder `%s': Invalid mailbox name"),
			     folder->full_name);
		retval = -1;
		break;
	case SPRUCE_IMAP_RESULT_BAD:
		g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_FOLDER_ILLEGAL_NAME,
			     _("Cannot delete folder `%s': Bad command"),
			     folder->full_name);
		retval = -1;
		break;
	}
	
	spruce_imap_command_unref (ic);
	
	return retval;
}


static void
imap_move_cache (SpruceFolder *folder, const char *newname)
{
	SpruceIMAPFolder *imap_folder = (SpruceIMAPFolder *) folder;
	char *new_cachedir, *new_summary_path, *new_cache;
	const char *old_summary_path;
	
	/* update the cachedir location */
	new_cachedir = imap_store_build_filename (folder->store, newname);
	if (rename (imap_folder->cachedir, new_cachedir) == -1) {
		/* have to manually move everything over (or try) */
		spruce_mkdir (new_cachedir, 0777);
		
		/* rename the stream cache dir */
		new_cache = g_build_filename (new_cachedir, "cache", NULL);
		if (spruce_cache_rename (imap_folder->cache, new_cache, NULL) == -1) {
			spruce_cache_delete (imap_folder->cache, NULL);
			g_free (imap_folder->cache->basedir);
			imap_folder->cache->basedir = new_cache;
		} else {
			g_free (new_cache);
		}
		
		/* rename the summary file */
		if (folder->summary) {
			new_summary_path = imap_get_summary_filename (new_cachedir);
			if ((old_summary_path = spruce_folder_summary_get_filename (folder->summary))) {
				if (rename (old_summary_path, new_summary_path) == -1 && errno != ENOENT)
					unlink (old_summary_path);
			}
			
			spruce_folder_summary_set_filename (folder->summary, new_summary_path);
			g_free (new_summary_path);
		}
		
		/* rm -rf the old cachedir... or what's left of it */
		spruce_rmdir (imap_folder->cachedir);
	} else {
		/* entire cache dir and all contents moved
		 * successfully, just update filenames */
		
		/* update the stream cache dir */
		g_free (imap_folder->cache->basedir);
		imap_folder->cache->basedir = g_build_filename (new_cachedir, "cache", NULL);
		
		/* update the summary filename */
		if (folder->summary) {
			new_summary_path = imap_get_summary_filename (new_cachedir);
			spruce_folder_summary_set_filename (folder->summary, new_summary_path);
			g_free (new_summary_path);
		}
	}
	
	g_free (imap_folder->cachedir);
	imap_folder->cachedir = new_cachedir;
}

static int
imap_rename (SpruceFolder *folder, const char *newname, GError **err)
{
	SpruceIMAPEngine *engine = ((SpruceIMAPStore *) folder->store)->engine;
	SpruceIMAPFolder *imap_folder = (SpruceIMAPFolder *) folder;
	SpruceIMAPCommand *ic;
	char *utf7, *new, *p;
	int id, retval = 0;
	
	if (!strcmp (folder->full_name, "") || !strcmp (folder->full_name, "INBOX")) {
		g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_FOLDER_ILLEGAL_NAME,
			     _("Cannot rename folder `%s' to `%s': Special folder"),
			     folder->full_name, newname);
		
		return -1;
	}
	
	p = new = g_strdup (newname);
	while (*p != '\0') {
		if (*p == '/')
			*p = folder->separator;
		p++;
	}
	
	utf7 = spruce_imap_utf8_utf7 (new);
	g_free (new);
	
	ic = spruce_imap_engine_queue (engine, NULL, "RENAME %F %S\r\n", folder, utf7);
	while ((id = spruce_imap_engine_iterate (engine)) < ic->id && id != -1)
		;
	
	if (id == -1 || ic->status != SPRUCE_IMAP_COMMAND_COMPLETE) {
		g_propagate_error (err, ic->err);
		ic->err = NULL;
		spruce_imap_command_unref (ic);
		g_free (utf7);
		return -1;
	}
	
	switch (ic->result) {
	case SPRUCE_IMAP_RESULT_OK:
		/* update the utf-7 name */
		g_free (imap_folder->utf7_name);
		imap_folder->utf7_name = utf7;
		utf7 = NULL;
		
		imap_move_cache (folder, newname);
		
		break;
	case SPRUCE_IMAP_RESULT_NO:
		/* FIXME: would be good to save the NO reason into the err message */
		g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_FOLDER_ILLEGAL_NAME,
			     _("Cannot rename folder `%s' to `%s': Invalid mailbox name"),
			     folder->full_name, newname);
		retval = -1;
		break;
	case SPRUCE_IMAP_RESULT_BAD:
		g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_FOLDER_ILLEGAL_NAME,
			     _("Cannot delete folder `%s' to `%s': Bad command"),
			     folder->full_name, newname);
		retval = -1;
		break;
	}
	
	spruce_imap_command_unref (ic);
	g_free (utf7);
	
	return retval;
}

static void
imap_newname (SpruceFolder *folder, const char *parent, const char *name)
{
	SpruceIMAPFolder *imap_folder = (SpruceIMAPFolder *) folder;
	char *path, *p;
	
	SPRUCE_FOLDER_CLASS (parent_class)->newname (folder, parent, name);
	
	p = path = g_strdup (folder->full_name);
	while (*p != '\0') {
		if (*p == '/')
			*p = folder->separator;
		p++;
	}
	
	/* update the utf7_name */
	g_free (imap_folder->utf7_name);
	imap_folder->utf7_name = spruce_imap_utf8_utf7 (path);
	g_free (path);
	
	/* FIXME: this might not be right... we probably only need to
	 * update the paths on the object - the content should already
	 * be moved, I think. */
	imap_move_cache (folder, folder->full_name);
}


static struct {
	const char *name;
	guint32 flag;
} imap_flags[] = {
	{ "\\Answered", SPRUCE_MESSAGE_ANSWERED  },
	{ "\\Deleted",  SPRUCE_MESSAGE_DELETED   },
	{ "\\Draft",    SPRUCE_MESSAGE_DRAFT     },
	{ "\\Flagged",  SPRUCE_MESSAGE_FLAGGED   },
	{ "\\Seen",     SPRUCE_MESSAGE_SEEN      },
	
	/* user-defined flags */
	{ "Forwarded",  SPRUCE_MESSAGE_FORWARDED },
	{ "NonJunk",    SPRUCE_MESSAGE_NOTJUNK   },
	{ "Junk",       SPRUCE_MESSAGE_JUNK      },
};

static int
imap_sync_flag (SpruceFolder *folder, GPtrArray *infos, char onoff, const char *flag, GError **err)
{
	SpruceIMAPEngine *engine = ((SpruceIMAPStore *) folder->store)->engine;
	SpruceIMAPCommand *ic;
	int i, id, retval = 0;
	char *set = NULL;
	
	for (i = 0; i < infos->len; ) {
		i += spruce_imap_get_uid_set (engine, folder->summary, infos, i, 30 + strlen (flag), &set);
		
		ic = spruce_imap_engine_queue (engine, folder, "UID STORE %s %cFLAGS.SILENT (%s)\r\n", set, onoff, flag);
		while ((id = spruce_imap_engine_iterate (engine)) < ic->id && id != -1)
			;
		
		g_free (set);
		
		if (id == -1 || ic->status != SPRUCE_IMAP_COMMAND_COMPLETE) {
			g_propagate_error (err, ic->err);
			ic->err = NULL;
			spruce_imap_command_unref (ic);
			
			return -1;
		}
		
		switch (ic->result) {
		case SPRUCE_IMAP_RESULT_NO:
			/* FIXME: would be good to save the NO reason into the err message */
			g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_FOLDER_ILLEGAL_NAME,
				     _("Cannot sync flags to folder `%s': Unknown"),
				     folder->full_name);
			retval = -1;
			break;
		case SPRUCE_IMAP_RESULT_BAD:
			g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_FOLDER_ILLEGAL_NAME,
				     _("Cannot sync flags to folder `%s': Bad command"),
				     folder->full_name);
			retval = -1;
			break;
		}
		
		spruce_imap_command_unref (ic);
		
		if (retval == -1)
			return -1;
	}
	
	return 0;
}

static int
imap_sync_changes (SpruceFolder *folder, GPtrArray *sync, GError **err)
{
	SpruceIMAPMessageInfo *iinfo;
	GPtrArray *on_set, *off_set;
	SpruceMessageInfo *info;
	flags_diff_t diff;
	int retval = 0;
	int i, j;
	
	on_set = g_ptr_array_new ();
	off_set = g_ptr_array_new ();
	
	/* construct commands to sync system and user flags */
	for (i = 0; i < G_N_ELEMENTS (imap_flags); i++) {
		if (!(imap_flags[i].flag & folder->permanent_flags))
			continue;
		
		for (j = 0; j < sync->len; j++) {
			info = (SpruceMessageInfo *) sync->pdata[j];
			iinfo = (SpruceIMAPMessageInfo *) info;
			
			spruce_imap_flags_diff (&diff, iinfo->server_flags, info->flags);
			if (diff.changed & imap_flags[i].flag) {
				if (diff.bits & imap_flags[i].flag) {
					g_ptr_array_add (on_set, info);
				} else {
					g_ptr_array_add (off_set, info);
				}
			}
		}
		
		if (on_set->len > 0) {
			if ((retval = imap_sync_flag (folder, on_set, '+', imap_flags[i].name, err)) == -1)
				break;
			
			g_ptr_array_set_size (on_set, 0);
		}
		
		if (off_set->len > 0) {
			if ((retval = imap_sync_flag (folder, off_set, '-', imap_flags[i].name, err)) == -1)
				break;
			
			g_ptr_array_set_size (off_set, 0);
		}
	}
	
	g_ptr_array_free (on_set, TRUE);
	g_ptr_array_free (off_set, TRUE);
	
	if (retval == -1)
		return-1;
	
	for (i = 0; i < sync->len; i++) {
		info = (SpruceMessageInfo *) sync->pdata[i];
		iinfo = (SpruceIMAPMessageInfo *) info;
		info->flags &= ~SPRUCE_MESSAGE_DIRTY;
		iinfo->server_flags = info->flags & folder->permanent_flags;
	}
	
	return 0;
}

static int
imap_expunge_all (SpruceFolder *folder, GError **err)
{
	SpruceIMAPEngine *engine = ((SpruceIMAPStore *) folder->store)->engine;
	SpruceIMAPCommand *ic;
	int retval = 0;
	int id;
	
	ic = spruce_imap_engine_queue (engine, folder, "EXPUNGE\r\n");
	while ((id = spruce_imap_engine_iterate (engine)) < ic->id && id != -1)
		;
	
	switch (ic->result) {
	case SPRUCE_IMAP_RESULT_OK:
		retval = spruce_imap_summary_flush_updates (folder->summary, err);
		break;
	case SPRUCE_IMAP_RESULT_NO:
		/* FIXME: would be good to save the NO reason into the err message */
		g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_FOLDER_ILLEGAL_NAME,
			     _("Cannot expunge folder `%s': Unknown"),
			     folder->full_name);
		retval = -1;
		break;
	case SPRUCE_IMAP_RESULT_BAD:
		g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_FOLDER_ILLEGAL_NAME,
			     _("Cannot expunge folder `%s': Bad command"),
			     folder->full_name);
		retval = -1;
		break;
	}
	
	spruce_imap_command_unref (ic);
	
	return retval;
}

static int
imap_sync (SpruceFolder *folder, gboolean expunge, GError **err)
{
	SpruceIMAPEngine *engine = ((SpruceIMAPStore *) folder->store)->engine;
	SpruceIMAPMessageInfo *iinfo;
	SpruceMessageInfo *info;
	SpruceIMAPCommand *ic;
	flags_diff_t diff;
	GPtrArray *sync;
	int id, retval;
	int count, i;
	
	/* gather a list of changes to sync to the server */
	count = spruce_folder_summary_count (folder->summary);
	sync = g_ptr_array_new ();
	
	for (i = 0; i < count; i++) {
		info = spruce_folder_summary_index (folder->summary, i);
		iinfo = (SpruceIMAPMessageInfo *) info;
		if (info->flags & SPRUCE_MESSAGE_DIRTY) {
			spruce_imap_flags_diff (&diff, iinfo->server_flags, info->flags);
			diff.changed &= folder->permanent_flags;
			
			/* weed out flag changes that we can't sync to the server */
			if (!diff.changed)
				spruce_folder_summary_info_unref (folder->summary, info);
			else
				g_ptr_array_add (sync, info);
		} else {
			spruce_folder_summary_info_unref (folder->summary, info);
		}
	}
	
	if (sync->len > 0) {
		retval = imap_sync_changes (folder, sync, err);
		
		for (i = 0; i < sync->len; i++)
			spruce_folder_summary_info_unref (folder->summary, sync->pdata[i]);
		
		g_ptr_array_free (sync, TRUE);
		
		if (retval == -1)
			return -1;
	} else {
		g_ptr_array_free (sync, TRUE);
	}
	
	if (expunge && (folder->mode & SPRUCE_FOLDER_MODE_WRITE)) {
		retval = imap_expunge_all (folder, err);
	} else {
		retval = spruce_imap_summary_flush_updates (folder->summary, err);
	}
	
	spruce_folder_summary_save (folder->summary);
	
	return retval;
}

static int
imap_expunge_uids_manual (SpruceFolder *folder, GPtrArray *uids, GError **err)
{
	SpruceIMAPEngine *engine = ((SpruceIMAPStore *) folder->store)->engine;
	SpruceMessageInfo *info;
	SpruceIMAPCommand *ic;
	GHashTable *uid_hash;
	GPtrArray *undelete;
	int id, retval;
	int count, i;
	
	uid_hash = g_hash_table_new (g_str_hash, g_str_equal);
	for (i = 0; i < uids->len; i++)
		g_hash_table_insert (uid_hash, uids->pdata[i], uids->pdata[i]);
	
	count = spruce_folder_summary_count (folder->summary);
	undelete = g_ptr_array_new ();
	
	for (i = 0; i < count; i++) {
		info = spruce_folder_summary_index (folder->summary, i);
		if ((info->flags & SPRUCE_MESSAGE_DELETED) &&
		    g_hash_table_lookup (uid_hash, info->uid)) {
			spruce_folder_summary_info_unref (folder->summary, info);
			continue;
		}
		
		/* temporarily undelete this message */
		info->flags &= ~SPRUCE_MESSAGE_DELETED;
		info->flags |= SPRUCE_MESSAGE_DIRTY;
		
		g_ptr_array_add (undelete, info);
	}
	
	if (undelete->len > 0)
		spruce_folder_summary_touch (folder->summary);
	
	retval = imap_sync (folder, TRUE, err);
	
	if (undelete->len > 0)
		spruce_folder_summary_touch (folder->summary);
	
	/* re-delete any temporarily undeleted messages */
	for (i = 0; i < undelete->len; i++) {
		info = (SpruceMessageInfo *) undelete->pdata[i];
		info->flags |= (SPRUCE_MESSAGE_DELETED | SPRUCE_MESSAGE_DIRTY);
		spruce_folder_summary_info_unref (folder->summary, info);
	}
	
	g_ptr_array_free (undelete, TRUE);
	
	return retval;
}

static int
imap_expunge_uids (SpruceFolder *folder, GPtrArray *uids, GError **err)
{
	SpruceIMAPEngine *engine = ((SpruceIMAPStore *) folder->store)->engine;
	SpruceMessageInfo *info;
	SpruceIMAPCommand *ic;
	GPtrArray *expunge;
	int id, retval = 0;
	char *set = NULL;
	int i;
	
	if (imap_sync (folder, FALSE, err) == -1)
		return -1;
	
	expunge = g_ptr_array_new ();
	for (i = 0; i < uids->len; i++) {
		info = spruce_folder_summary_uid (folder->summary, uids->pdata[i]);
		g_ptr_array_add (expunge, info);
	}
	
	for (i = 0; i < expunge->len; ) {
		i += spruce_imap_get_uid_set (engine, folder->summary, expunge, i, 14, &set);
		
		ic = spruce_imap_engine_queue (engine, folder, "UID EXPUNGE %s\r\n", set);
		while ((id = spruce_imap_engine_iterate (engine)) < ic->id && id != -1)
			;
		
		g_free (set);
		
		if (id == -1 || ic->status != SPRUCE_IMAP_COMMAND_COMPLETE) {
			g_propagate_error (err, ic->err);
			ic->err = NULL;
			spruce_imap_command_unref (ic);
			retval = -1;
			break;
		}
		
		switch (ic->result) {
		case SPRUCE_IMAP_RESULT_OK:
			retval = spruce_imap_summary_flush_updates (folder->summary, err);
			break;
		case SPRUCE_IMAP_RESULT_NO:
			/* FIXME: would be good to save the NO reason into the err message */
			g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_FOLDER_ILLEGAL_NAME,
				     _("Cannot expunge folder `%s': Unknown"),
				     folder->full_name);
			retval = -1;
			break;
		case SPRUCE_IMAP_RESULT_BAD:
			g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_FOLDER_ILLEGAL_NAME,
				     _("Cannot expunge folder `%s': Bad command"),
				     folder->full_name);
			retval = -1;
			break;
		}
		
		spruce_imap_command_unref (ic);
		
		if (retval == -1)
			break;
	}
	
	for (i = 0; i < uids->len; i++) {
		info = (SpruceMessageInfo *) expunge->pdata[i];
		spruce_folder_summary_info_unref (folder->summary, info);
	}
	
	g_ptr_array_free (expunge, TRUE);
	
	return retval;
}

static int
imap_expunge (SpruceFolder *folder, GPtrArray *uids, GError **err)
{
	SpruceIMAPEngine *engine = ((SpruceIMAPStore *) folder->store)->engine;
	
	if (SPRUCE_FOLDER_CLASS (parent_class)->expunge (folder, uids, err) == -1)
		return -1;
	
	if (!uids)
		return imap_expunge_all (folder, err);
	
	if (!(engine->capa & SPRUCE_IMAP_CAPABILITY_UIDPLUS))
		return imap_expunge_uids_manual (folder, uids, err);
	else
		return imap_expunge_uids (folder, uids, err);
}


static int
list_sort (const spruce_imap_list_t **list0, const spruce_imap_list_t **list1)
{
	return strcmp ((*list0)->name, (*list1)->name);
}

static void
list_remove_dups (GPtrArray *array)
{
	spruce_imap_list_t *list, *last;
	int i;
	
	last = array->pdata[0];
	for (i = 1; i < array->len; i++) {
		list = array->pdata[i];
		if (!strcmp (list->name, last->name)) {
			g_ptr_array_remove_index (array, i--);
			last->flags |= list->flags;
			g_free (list->name);
			g_free (list);
		}
	}
}

static GPtrArray *
imap_glob_match (SpruceFolder *folder, GPtrArray *array, const char *pattern)
{
	spruce_imap_list_t *list;
	SpruceFolder *subfolder;
	GPatternSpec *pspec;
	GString *match;
	GPtrArray *ls;
	int i;
	
	if (*folder->full_name != '\0') {
		match = g_string_new (((SpruceIMAPFolder *) folder)->utf7_name);
		g_string_append_c (match, folder->separator);
	} else {
		match = g_string_new ("");
	}
	g_string_append (match, pattern);
	
	pspec = g_pattern_spec_new (match->str);
	g_string_free (match, TRUE);
	
	ls = g_ptr_array_new ();
	for (i = 0; i < array->len; i++) {
		list = array->pdata[i];
		
		if (!g_pattern_match_string (pspec, list->name))
			continue;
		
		subfolder = spruce_imap_folder_new_list (folder, list);
		g_ptr_array_add (ls, subfolder);
	}
	
	g_pattern_spec_free (pspec);
	
	return ls;
}

static GPtrArray *
imap_list_or_lsub (SpruceFolder *folder, const char *cmd, const char *pattern, GError **err)
{
	SpruceIMAPEngine *engine = ((SpruceIMAPStore *) folder->store)->engine;
	GPtrArray *array, *ls = NULL;
	const char *start, *inptr;
	spruce_imap_list_t *list;
	SpruceIMAPCommand *ic;
	char *utf7_pattern;
	GString *match;
	int id, i;
	
	if (*folder->full_name != '\0') {
		match = g_string_new (((SpruceIMAPFolder *) folder)->utf7_name);
		g_string_append_c (match, folder->separator);
	} else {
		match = g_string_new ("");
	}
	
	/* roughly translate the supplied glob pattern into a
	 * non-recursive LIST wildcard pattern */
	start = inptr = utf7_pattern = spruce_imap_utf8_utf7 (pattern);
	while (*inptr != '\0') {
		while (*inptr != '\0' && *inptr != '?' && *inptr != '*')
			inptr++;
		
		g_string_append_len (match, start, inptr - start);
		g_string_append_c (match, '%');
		while (*inptr == '?' || *inptr == '*')
			inptr++;
		start = inptr;
	}
	
	g_string_append_len (match, start, inptr - start);
	
	ic = spruce_imap_engine_queue (engine, NULL, "%s \"\" %S\r\n", cmd, match);
	spruce_imap_command_register_untagged (ic, cmd, spruce_imap_untagged_list);
	ic->user_data = array = g_ptr_array_new ();
	g_string_free (match, TRUE);
	
	while ((id = spruce_imap_engine_iterate (engine)) < ic->id && id != -1)
		;
	
	if (id == -1 || ic->status != SPRUCE_IMAP_COMMAND_COMPLETE) {
		for (i = 0; i < array->len; i++) {
			list = array->pdata[i];
			g_free (list->name);
			g_free (list);
		}
		
		g_propagate_error (err, ic->err);
		ic->err = NULL;
		
		g_ptr_array_free (array, TRUE);
		spruce_imap_command_unref (ic);
		g_free (utf7_pattern);
		return NULL;
	}
	
	switch (ic->result) {
	case SPRUCE_IMAP_RESULT_OK:
		if (array->len > 0) {
			g_ptr_array_sort (array, (GCompareFunc) list_sort);
			list_remove_dups (array);
			
			ls = imap_glob_match (folder, array, utf7_pattern);
		} else {
			/* return empty list even if there are no
			 * matches when no exception occurred */
			ls = g_ptr_array_new ();
		}
		break;
	case SPRUCE_IMAP_RESULT_NO:
		/* FIXME: would be good to save the NO reason into the err message */
		g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_FOLDER_ILLEGAL_NAME,
			     _("Cannot list `%s' under folder `%s': Unknown"),
			     pattern, folder->full_name);
		break;
	case SPRUCE_IMAP_RESULT_BAD:
		g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_FOLDER_ILLEGAL_NAME,
			     _("Cannot list `%s' under folder `%s': Bad command"),
			     pattern, folder->full_name);
		break;
	}
	
	for (i = 0; i < array->len; i++) {
		list = array->pdata[0];
		g_free (list->name);
		g_free (list);
	}
	
	g_ptr_array_free (array, TRUE);
	g_free (utf7_pattern);
	
	spruce_imap_command_unref (ic);
	
	return ls;
}

static GPtrArray *
imap_list (SpruceFolder *folder, const char *pattern, GError **err)
{
	return imap_list_or_lsub (folder, "LIST", pattern, err);
}

static GPtrArray *
imap_lsub (SpruceFolder *folder, const char *pattern, GError **err)
{
	return imap_list_or_lsub (folder, "LSUB", pattern, err);
}

static int
imap_subscribe (SpruceFolder *folder, GError **err)
{
	SpruceIMAPEngine *engine = ((SpruceIMAPStore *) folder->store)->engine;
	SpruceIMAPCommand *ic;
	int id, retval = 0;
	
	ic = spruce_imap_engine_queue (engine, NULL, "SUBSCRIBE %F\r\n", folder);
	while ((id = spruce_imap_engine_iterate (engine)) < ic->id && id != -1)
		;
	
	if (id == -1 || ic->status != SPRUCE_IMAP_COMMAND_COMPLETE) {
		g_propagate_error (err, ic->err);
		ic->err = NULL;
		spruce_imap_command_unref (ic);
		return -1;
	}
	
	switch (ic->result) {
	case SPRUCE_IMAP_RESULT_OK:
		/* subscribed */
		break;
	case SPRUCE_IMAP_RESULT_NO:
		/* FIXME: would be good to save the NO reason into the err message */
		g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_FOLDER_ILLEGAL_NAME,
			     _("Cannot subscribe to folder `%s': Invalid mailbox name"),
			     folder->full_name);
		retval = -1;
		break;
	case SPRUCE_IMAP_RESULT_BAD:
		g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_FOLDER_ILLEGAL_NAME,
			     _("Cannot subscribe to folder `%s': Bad command"),
			     folder->full_name);
		retval = -1;
		break;
	}
	
	spruce_imap_command_unref (ic);
	
	return retval;
}

static int
imap_unsubscribe (SpruceFolder *folder, GError **err)
{
	SpruceIMAPEngine *engine = ((SpruceIMAPStore *) folder->store)->engine;
	SpruceIMAPCommand *ic;
	int id, retval = 0;
	
	ic = spruce_imap_engine_queue (engine, NULL, "UNSUBSCRIBE %F\r\n", folder);
	while ((id = spruce_imap_engine_iterate (engine)) < ic->id && id != -1)
		;
	
	if (id == -1 || ic->status != SPRUCE_IMAP_COMMAND_COMPLETE) {
		g_propagate_error (err, ic->err);
		ic->err = NULL;
		spruce_imap_command_unref (ic);
		return -1;
	}
	
	switch (ic->result) {
	case SPRUCE_IMAP_RESULT_OK:
		/* unsubscribed */
		break;
	case SPRUCE_IMAP_RESULT_NO:
		/* FIXME: would be good to save the NO reason into the err message */
		g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_FOLDER_ILLEGAL_NAME,
			     _("Cannot unsubscribe to folder `%s': Invalid mailbox name"),
			     folder->full_name);
		retval = -1;
		break;
	case SPRUCE_IMAP_RESULT_BAD:
		g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_FOLDER_ILLEGAL_NAME,
			     _("Cannot unsubscribe to folder `%s': Bad command"),
			     folder->full_name);
		retval = -1;
		break;
	}
	
	spruce_imap_command_unref (ic);
	
	return retval;
}

static int
untagged_fetch (SpruceIMAPEngine *engine, SpruceIMAPCommand *ic, guint32 index, spruce_imap_token_t *token, GError **err)
{
	SpruceFolderSummary *summary = ((SpruceFolder *) engine->folder)->summary;
	GMimeStream *fstream, *stream = ic->user_data;
	SpruceFolderChangeInfo *changes;
	SpruceIMAPMessageInfo *iinfo;
	SpruceMessageInfo *info;
	GMimeFilter *crlf;
	guint32 new_flags;
	guint32 flags;
	
	if (spruce_imap_engine_next_token (engine, token, err) == -1)
		return -1;
	
	/* parse the FETCH response list */
	if (token->token != '(') {
		spruce_imap_utils_set_unexpected_token_error (err, engine, token);
		return -1;
	}
	
	do {
		if (spruce_imap_engine_next_token (engine, token, err) == -1)
			goto exception;
		
		if (token->token == ')' || token->token == '\n')
			break;
		
		if (token->token != SPRUCE_IMAP_TOKEN_ATOM)
			goto unexpected;
		
		if (!strcmp (token->v.atom, "BODY[")) {
			if (spruce_imap_engine_next_token (engine, token, err) == -1)
				goto exception;
			
			if (token->token != ']')
				goto unexpected;
			
			if (spruce_imap_engine_next_token (engine, token, err) == -1)
				goto exception;
			
			if (token->token != SPRUCE_IMAP_TOKEN_LITERAL)
				goto unexpected;
			
			fstream = g_mime_stream_filter_new (stream);
			crlf = g_mime_filter_crlf_new (FALSE, FALSE);
			g_mime_stream_filter_add ((GMimeStreamFilter *) fstream, crlf);
			g_object_unref (crlf);
			
			g_mime_stream_write_to_stream ((GMimeStream *) engine->istream, fstream);
			g_mime_stream_flush (fstream);
			g_object_unref (fstream);
		} else if (!strcmp (token->v.atom, "UID")) {
			if (spruce_imap_engine_next_token (engine, token, err) == -1)
				goto exception;
			
			if (token->token != SPRUCE_IMAP_TOKEN_NUMBER || token->v.number == 0)
				goto unexpected;
		} else if (!strcmp (token->v.atom, "FLAGS")) {
			/* even though we didn't request this bit of information, it might be
			 * given to us if another client recently changed the flags... */
			if (spruce_imap_parse_flags_list (engine, &flags, err) == -1)
				goto exception;
			
			if ((info = spruce_folder_summary_index (summary, index - 1))) {
				iinfo = (SpruceIMAPMessageInfo *) info;
				new_flags = spruce_imap_merge_flags (iinfo->server_flags, info->flags, flags);
				iinfo->server_flags = flags;
				
				if (info->flags != new_flags) {
					info->flags = new_flags;
					changes = spruce_folder_change_info_new ();
					spruce_folder_change_info_change_uid (changes, info->uid);
					g_signal_emit_by_name (engine->folder, "folder-changed", changes);
					spruce_folder_change_info_free (changes);
				}
				
				spruce_folder_summary_info_unref (summary, info);
			}
		} else {
			/* wtf? */
			fprintf (stderr, "huh? %s?...\n", token->v.atom);
		}
	} while (1);
	
	if (token->token != ')') {
		fprintf (stderr, "expected ')' to close untagged FETCH response\n");
		goto unexpected;
	}
	
	return 0;
	
 unexpected:
	
	spruce_imap_utils_set_unexpected_token_error (err, engine, token);
	
 exception:
	
	return -1;
}

static GMimeMessage *
imap_get_message (SpruceFolder *folder, const char *uid, GError **err)
{
	SpruceIMAPEngine *engine = ((SpruceIMAPStore *) folder->store)->engine;
	SpruceCache *cache = ((SpruceIMAPFolder *) folder)->cache;
	GMimeMessage *message = NULL;
	SpruceIMAPCommand *ic;
	GMimeParser *parser;
	GMimeStream *stream;
	int commit = TRUE;
	int id;
	
	/* try getting the message from the cache first... */
	if ((stream = spruce_cache_get (cache, uid, NULL))) {
		parser = g_mime_parser_new_with_stream (stream);
		message = g_mime_parser_construct_message (parser);
		g_object_unref (parser);
		
		return message;
	}
	
	ic = spruce_imap_engine_queue (engine, folder, "UID FETCH %s BODY.PEEK[]\r\n", uid);
	spruce_imap_command_register_untagged (ic, "FETCH", untagged_fetch);
	if (!(ic->user_data = stream = spruce_cache_add (cache, uid, NULL))) {
		ic->user_data = stream = g_mime_stream_mem_new ();
		commit = FALSE;
	}
	
	while ((id = spruce_imap_engine_iterate (engine)) < ic->id && id != -1)
		;
	
	if (id == -1 || ic->status != SPRUCE_IMAP_COMMAND_COMPLETE) {
		g_propagate_error (err, ic->err);
		g_object_unref (ic->user_data);
		ic->err = NULL;
		spruce_imap_command_unref (ic);
		return NULL;
	}
	
	switch (ic->result) {
	case SPRUCE_IMAP_RESULT_OK:
		if (commit) {
			stream = spruce_cache_stream_commit (ic->user_data);
			g_object_unref (ic->user_data);
		}
		
		g_mime_stream_reset (stream);
		parser = g_mime_parser_new_with_stream (stream);
		message = g_mime_parser_construct_message (parser);
		g_object_unref (parser);
		break;
	case SPRUCE_IMAP_RESULT_NO:
		/* FIXME: would be good to save the NO reason into the err message */
		g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_FOLDER_NO_SUCH_MESSAGE,
			     _("Cannot get message %s from folder `%s': No such message"),
			     uid, folder->full_name);
		break;
	case SPRUCE_IMAP_RESULT_BAD:
		g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_GENERIC,
			     _("Cannot get message %s from folder `%s': Bad command"),
			     uid, folder->full_name);
		break;
	}
	
	spruce_imap_command_unref (ic);
	
	g_object_unref (stream);
	
	return message;
}

static void
imap_append_info (SpruceFolder *folder, GMimeMessage *message, SpruceMessageInfo *info, guint32 auid)
{
	SpruceIMAPMessageInfo *iinfo;
	SpruceMessageInfo *minfo;
	char uid[12];
	
	sprintf (uid, "%u", auid);
	
	if ((minfo = spruce_folder_summary_uid (folder->summary, uid))) {
		/* this should never happen but is here for safety sake */
		spruce_folder_summary_info_unref (folder->summary, minfo);
		return;
	}
	
	minfo = spruce_folder_summary_info_new_from_message (folder->summary, message);
	iinfo = (SpruceIMAPMessageInfo *) minfo;
	minfo->uid = g_strdup (uid);
	
	minfo->flags = info->flags;
	iinfo->server_flags = info->flags & folder->permanent_flags;
	
	spruce_tag_list_copy (&minfo->user_tags, &info->user_tags);
	spruce_flag_list_copy (&minfo->user_flags, &info->user_flags);
	
	spruce_folder_summary_add (folder->summary, minfo);
}

static char *tm_months[] = {
	"Jan", "Feb", "Mar", "Apr", "May", "Jun",
	"Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

static int
imap_append_message (SpruceFolder *folder, GMimeMessage *message, SpruceMessageInfo *info, GError **err)
{
	SpruceIMAPEngine *engine = ((SpruceIMAPStore *) folder->store)->engine;
	SpruceIMAPSummary *summary = (SpruceIMAPSummary *) folder->summary;
	SpruceIMAPRespCode *resp;
	SpruceIMAPCommand *ic;
	GError *lerr = NULL;
	char flags[100], *p;
	char date[50];
	struct tm tm;
	int retval;
	int id, i;
	
	/* construct the option flags list */
	if (info->flags & folder->permanent_flags) {
		p = g_stpcpy (flags, " (");
		
		for (i = 0; i < G_N_ELEMENTS (imap_flags); i++) {
			if ((info->flags & imap_flags[i].flag) & folder->permanent_flags) {
				p = g_stpcpy (p, imap_flags[i].name);
				*p++ = ' ';
			}
		}
		
		p[-1] = ')';
		*p = '\0';
	} else {
		flags[0] = '\0';
	}
	
	/* construct the optional date_time string */
	if (info->date_received != (time_t) -1) {
		int tzone;
		
#ifdef HAVE_LOCALTIME_R
		localtime_r (&info->date_received, &tm);
#else
		memcpy (&tm, localtime (&info->date_received), sizeof (tm));
#endif
		
#if defined (HAVE_TM_GMTOFF)
		tzone = -tm.tm_gmtoff;
#elif defined (HAVE_TIMEZONE)
		if (tm.tm_isdst > 0) {
#if defined (HAVE_ALTZONE)
			tzone = altzone;
#else /* !defined (HAVE_ALTZONE) */
			tzone = (timezone - 3600);
#endif
		} else {
			tzone = timezone;
		}
#else
#error Neither HAVE_TIMEZONE nor HAVE_TM_GMTOFF defined. Rerun autoheader, autoconf, etc.
#endif
		
		sprintf (date, " \"%02d-%s-%04d %02d:%02d:%02d %+05d\"",
			 tm.tm_mday, tm_months[tm.tm_mon], tm.tm_year + 1900,
			 tm.tm_hour, tm.tm_min, tm.tm_sec, tzone);
	} else {
		date[0] = '\0';
	}
	
 retry:
	
	ic = spruce_imap_engine_queue (engine, NULL, "APPEND %F%s%s %L\r\n",
				       folder, flags, date, message);
	while ((id = spruce_imap_engine_iterate (engine)) < ic->id && id != -1)
		;
	
	if (id == -1 || ic->status != SPRUCE_IMAP_COMMAND_COMPLETE) {
		g_propagate_error (err, ic->err);
		ic->err = NULL;
		spruce_imap_command_unref (ic);
		return -1;
	}
	
	switch (ic->result) {
	case SPRUCE_IMAP_RESULT_OK:
		retval = 0;
		if (!(engine->capa & SPRUCE_IMAP_CAPABILITY_UIDPLUS))
			break;
		
		if (!folder->summary->loaded)
			break;
		
		/* FIXME: don't bother with APPENDUID if we got a UIDNOTSTICKY RESP-CODE */
		for (i = 0; i < ic->resp_codes->len; i++) {
			resp = ic->resp_codes->pdata[i];
			if (resp->code == SPRUCE_IMAP_RESP_CODE_APPENDUID) {
				if (resp->v.appenduid.uidvalidity == summary->uidvalidity)
					imap_append_info (folder, message, info, resp->v.appenduid.uid);
				break;
			}
		}
		break;
	case SPRUCE_IMAP_RESULT_NO:
		/* FIXME: can we give the user any more information? */
		g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_FOLDER_NO_SUCH_MESSAGE,
			     _("Cannot append message to folder `%s': Unknown error"),
			     folder->full_name);
		
		for (i = 0; i < ic->resp_codes->len; i++) {
			resp = ic->resp_codes->pdata[i];
			if (resp->code == SPRUCE_IMAP_RESP_CODE_TRYCREATE) {
				if (spruce_folder_create (folder, SPRUCE_FOLDER_CAN_HOLD_MESSAGES, &lerr) == -1) {
					g_error_free (lerr);
					break;
				}
				
				spruce_imap_command_unref (ic);
				g_clear_error (err);
				goto retry;
			}
		}
		
		retval = -1;
		break;
	case SPRUCE_IMAP_RESULT_BAD:
		g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_GENERIC,
			     _("Cannot append message to folder `%s': Bad command"),
			     folder->full_name);
		retval = -1;
		break;
	default:
		g_assert_not_reached ();
		retval = -1;
	}
	
	spruce_imap_command_unref (ic);
	
	return retval;
}

static int
info_uid_sort (const SpruceMessageInfo **info0, const SpruceMessageInfo **info1)
{
	guint32 uid0, uid1;
	
	uid0 = strtoul ((*info0)->uid, NULL, 10);
	uid1 = strtoul ((*info1)->uid, NULL, 10);
	
	if (uid0 == uid1)
		return 0;
	
	return uid0 < uid1 ? -1 : 1;
}

static int
imap_xfer_messages (SpruceFolder *src, GPtrArray *uids, SpruceFolder *dest, gboolean move, GError **err)
{
	SpruceIMAPEngine *engine = ((SpruceIMAPStore *) src->store)->engine;
	int i, j, n, id, dest_namelen, retval = 0;
	SpruceMessageInfo *info;
	SpruceIMAPCommand *ic;
	GPtrArray *infos;
	char *set;
	
	infos = g_ptr_array_new ();
	for (i = 0; i < uids->len; i++) {
		if (!(info = spruce_folder_summary_uid (src->summary, uids->pdata[i])))
			continue;
		
		g_ptr_array_add (infos, info);
	}
	
	if (infos->len == 0) {
		g_ptr_array_free (infos, TRUE);
		return 0;
	}
	
	g_ptr_array_sort (infos, (GCompareFunc) info_uid_sort);
	
	dest_namelen = strlen (spruce_imap_folder_utf7_name ((SpruceIMAPFolder *) dest));
	
	for (i = 0; i < infos->len; i += n) {
		n = spruce_imap_get_uid_set (engine, src->summary, infos, i, 10 + dest_namelen, &set);
		
		if (move && (engine->capa & SPRUCE_IMAP_CAPABILITY_XGWMOVE))
			ic = spruce_imap_engine_queue (engine, src, "UID XGWMOVE %s %F\r\n", set, dest);
		else
			ic = spruce_imap_engine_queue (engine, src, "UID COPY %s %F\r\n", set, dest);
		
		while ((id = spruce_imap_engine_iterate (engine)) < ic->id && id != -1)
			;
		
		g_free (set);
		
		if (id == -1 || ic->status != SPRUCE_IMAP_COMMAND_COMPLETE) {
			g_propagate_error (err, ic->err);
			ic->err = NULL;
			spruce_imap_command_unref (ic);
			g_free (set);
			retval = -1;
			goto done;
		}
		
		switch (ic->result) {
		case SPRUCE_IMAP_RESULT_NO:
			/* FIXME: would be good to save the NO reason into the err message */
			if (move) {
				g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_FOLDER_ILLEGAL_NAME,
					     _("Cannot move messages from folder `%s' to folder `%s': Unknown"),
					     src->full_name, dest->full_name);
			} else {
				g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_FOLDER_ILLEGAL_NAME,
					     _("Cannot copy messages from folder `%s' to folder `%s': Unknown"),
					     src->full_name, dest->full_name);
			}
			
			retval = -1;
			goto done;
		case SPRUCE_IMAP_RESULT_BAD:
			if (move) {
				g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_FOLDER_ILLEGAL_NAME,
					     _("Cannot move messages from folder `%s' to folder `%s': Bad command"),
					     src->full_name, dest->full_name);
			} else {
				g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_FOLDER_ILLEGAL_NAME,
					     _("Cannot copy messages from folder `%s' to folder `%s': Bad command"),
					     src->full_name, dest->full_name);
			}
			
			retval = -1;
			goto done;
		}
		
		spruce_imap_command_unref (ic);
		
		/* FIXME: if the IMAP server supports UIDPLUS, we got
		 * a COPYUID RESP-CODE, we didn't get a UIDNOTSTICKY
		 * RESP-CODE, and dest->summary is loaded, add the
		 * appropriate infos to the dest summary */
		
		if (move && !(engine->capa & SPRUCE_IMAP_CAPABILITY_XGWMOVE)) {
			for (j = i; j < n; j++) {
				info = infos->pdata[j];
				info->flags |= SPRUCE_MESSAGE_DELETED | SPRUCE_MESSAGE_DIRTY;
			}
			
			spruce_folder_summary_touch (src->summary);
		}
	}
	
 done:
	
	for (i = 0; i < infos->len; i++)
		spruce_folder_summary_info_unref (src->summary, infos->pdata[i]);
	g_ptr_array_free (infos, TRUE);
	
	return retval;
}

static int
imap_copy_messages (SpruceFolder *src, GPtrArray *uids, SpruceFolder *dest, GError **err)
{
	return imap_xfer_messages (src, uids, dest, FALSE, err);
}

static int
imap_move_messages (SpruceFolder *src, GPtrArray *uids, SpruceFolder *dest, GError **err)
{
	return imap_xfer_messages (src, uids, dest, TRUE, err);
}

static GPtrArray *
imap_search (SpruceFolder *folder, GPtrArray *uids, const char *expression, GError **err)
{
	/* FIXME: implement me */
	
	return NULL;
}
