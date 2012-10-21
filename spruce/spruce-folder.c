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

#include <string.h>

#include <glib/gi18n.h>

#include <spruce/spruce-store.h>

#include "spruce-error.h"
#include "spruce-folder.h"
#include "spruce-marshal.h"


enum {
	DELETING,
	DELETED,
	RENAMED,
	SUBSCRIBED,
	UNSUBSCRIBED,
	FOLDER_CHANGED,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };


static void spruce_folder_class_init (SpruceFolderClass *klass);
static void spruce_folder_init (SpruceFolder *folder, SpruceFolderClass *klass);
static void spruce_folder_finalize (GObject *object);

static gboolean folder_can_hold_folders (SpruceFolder *folder);
static gboolean folder_can_hold_messages (SpruceFolder *folder);
static gboolean folder_has_new_messages (SpruceFolder *folder);
static gboolean folder_supports_subscriptions (SpruceFolder *folder);
static gboolean folder_supports_searches (SpruceFolder *folder);
static guint32 folder_get_mode (SpruceFolder *folder);
static const char *folder_get_name (SpruceFolder *folder);
static const char *folder_get_full_name (SpruceFolder *folder);
static SpruceURL *folder_get_url (SpruceFolder *folder);
static char folder_get_separator (SpruceFolder *folder);
static guint32 folder_get_permanent_flags (SpruceFolder *folder);
static SpruceStore *folder_get_store (SpruceFolder *folder);
static SpruceFolder *folder_get_parent (SpruceFolder *folder);
static gboolean folder_is_open (SpruceFolder *folder);
static gboolean folder_is_subscribed (SpruceFolder *folder);
static gboolean folder_exists (SpruceFolder *folder);
static int folder_get_message_count (SpruceFolder *folder);
static int folder_get_unread_message_count (SpruceFolder *folder);
static int folder_open (SpruceFolder *folder, GError **err);
static int folder_close (SpruceFolder *folder, gboolean expunge, GError **err);
static int folder_create (SpruceFolder *folder, int type, GError **err);
static int folder_delete (SpruceFolder *folder, GError **err);
static int folder_rename (SpruceFolder *folder, const char *newname, GError **err);
static void folder_newname (SpruceFolder *folder, const char *parent, const char *name);
static int folder_sync (SpruceFolder *folder, gboolean expunge, GError **err);
static int folder_expunge (SpruceFolder *folder, GPtrArray *uids, GError **err);
static GPtrArray *folder_list (SpruceFolder *folder, const char *pattern, GError **err);
static GPtrArray *folder_lsub (SpruceFolder *folder, const char *pattern, GError **err);
static int folder_subscribe (SpruceFolder *folder, GError **err);
static int folder_unsubscribe (SpruceFolder *folder, GError **err);
static GPtrArray *folder_get_uids (SpruceFolder *folder);
static void folder_free_uids (SpruceFolder *folder, GPtrArray *uids);
static SpruceMessageInfo *folder_get_message_info (SpruceFolder *folder, const char *uid);
static void folder_free_message_info (SpruceFolder *folder, SpruceMessageInfo *info);
static guint32 folder_get_message_flags (SpruceFolder *folder, const char *uid);
static int folder_set_message_flags (SpruceFolder *folder, const char *uid, guint32 flags, guint32 set);
static GMimeMessage *folder_get_message (SpruceFolder *folder, const char *uid, GError **err);
static int folder_append_message (SpruceFolder *folder, GMimeMessage *message,
				  SpruceMessageInfo *info, GError **err);
static int folder_copy_messages (SpruceFolder *src, GPtrArray *uids, SpruceFolder *dest, GError **err);
static int folder_move_messages (SpruceFolder *src, GPtrArray *uids, SpruceFolder *dest, GError **err);
static GPtrArray *folder_search (SpruceFolder *folder, GPtrArray *uids, const char *expression, GError **err);
static void folder_freeze (SpruceFolder *folder);
static void folder_thaw (SpruceFolder *folder);
static gboolean folder_is_frozen (SpruceFolder *folder);


static void spruce_folder_newname (SpruceFolder *folder, const char *parent, const char *name);


static GObjectClass *parent_class = NULL;


GType
spruce_folder_get_type (void)
{
	static GType type = 0;
	
	if (!type) {
		static const GTypeInfo info = {
			sizeof (SpruceFolderClass),
			NULL, /* base_class_init */
			NULL, /* base_class_finalize */
			(GClassInitFunc) spruce_folder_class_init,
			NULL, /* class_finalize */
			NULL, /* class_data */
			sizeof (SpruceFolder),
			0,    /* n_preallocs */
			(GInstanceInitFunc) spruce_folder_init,
		};
		
		type = g_type_register_static (G_TYPE_OBJECT, "SpruceFolder", &info, 0);
	}
	
	return type;
}


static void
spruce_folder_class_init (SpruceFolderClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	
	parent_class = g_type_class_ref (G_TYPE_OBJECT);
	
	object_class->finalize = spruce_folder_finalize;
	
	/* virtual methods */
	klass->can_hold_folders = folder_can_hold_folders;
	klass->can_hold_messages = folder_can_hold_messages;
	klass->has_new_messages = folder_has_new_messages;
	klass->supports_subscriptions = folder_supports_subscriptions;
	klass->supports_searches = folder_supports_searches;
	klass->get_mode = folder_get_mode;
	klass->get_name = folder_get_name;
	klass->get_full_name = folder_get_full_name;
	klass->get_url = folder_get_url;
	klass->get_separator = folder_get_separator;
	klass->get_permanent_flags = folder_get_permanent_flags;
	klass->get_store = folder_get_store;
	klass->get_parent = folder_get_parent;
	klass->is_open = folder_is_open;
	klass->is_subscribed = folder_is_subscribed;
	klass->exists = folder_exists;
	klass->get_message_count = folder_get_message_count;
	klass->get_unread_message_count = folder_get_unread_message_count;
	klass->open = folder_open;
	klass->close = folder_close;
	klass->create = folder_create;
	klass->delete = folder_delete;
	klass->rename = folder_rename;
	klass->newname = folder_newname;
	klass->sync = folder_sync;
	klass->expunge = folder_expunge;
	klass->list = folder_list;
	klass->lsub = folder_lsub;
	klass->subscribe = folder_subscribe;
	klass->unsubscribe = folder_unsubscribe;
	klass->get_uids = folder_get_uids;
	klass->free_uids = folder_free_uids;
	klass->get_message_info = folder_get_message_info;
	klass->free_message_info = folder_free_message_info;
	klass->get_message_flags = folder_get_message_flags;
	klass->set_message_flags = folder_set_message_flags;
	klass->get_message = folder_get_message;
	klass->append_message = folder_append_message;
	klass->copy_messages = folder_copy_messages;
	klass->move_messages = folder_move_messages;
	klass->search = folder_search;
	klass->freeze = folder_freeze;
	klass->thaw = folder_thaw;
	klass->is_frozen = folder_is_frozen;
	
	/* signals */
	signals[DELETED] =
		g_signal_new ("deleted",
			      SPRUCE_TYPE_FOLDER,
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (SpruceFolderClass, deleted),
			      NULL,
			      NULL,
			      spruce_marshal_VOID__NONE,
			      G_TYPE_NONE, 0);
	
	signals[RENAMED] =
		g_signal_new ("renamed",
			      SPRUCE_TYPE_FOLDER,
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (SpruceFolderClass, renamed),
			      NULL,
			      NULL,
			      spruce_marshal_VOID__STRING_STRING,
			      G_TYPE_NONE, 2,
			      G_TYPE_STRING,
			      G_TYPE_STRING);
	
	signals[SUBSCRIBED] =
		g_signal_new ("subscribed",
			      SPRUCE_TYPE_FOLDER,
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (SpruceFolderClass, subscribed),
			      NULL,
			      NULL,
			      spruce_marshal_VOID__NONE,
			      G_TYPE_NONE, 0);
	
	signals[UNSUBSCRIBED] =
		g_signal_new ("unsubscribed",
			      SPRUCE_TYPE_FOLDER,
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (SpruceFolderClass, unsubscribed),
			      NULL,
			      NULL,
			      spruce_marshal_VOID__NONE,
			      G_TYPE_NONE, 0);
	
	signals[FOLDER_CHANGED] =
		g_signal_new ("folder-changed",
			      SPRUCE_TYPE_FOLDER,
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (SpruceFolderClass, folder_changed),
			      NULL,
			      NULL,
			      spruce_marshal_VOID__POINTER,
			      G_TYPE_NONE, 1,
			      G_TYPE_POINTER);
	
	/* private signals */
	signals[DELETING] =
		g_signal_new ("deleting",
			      SPRUCE_TYPE_FOLDER,
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (SpruceFolderClass, deleting),
			      NULL,
			      NULL,
			      spruce_marshal_VOID__NONE,
			      G_TYPE_NONE, 0);
}

static void
spruce_folder_init (SpruceFolder *folder, SpruceFolderClass *klass)
{
	folder->store = NULL;
	folder->parent = NULL;
	folder->summary = NULL;
	folder->type = 0;
	folder->supports_subscriptions = FALSE;
	folder->supports_searches = FALSE;
	folder->exists = FALSE;
	folder->is_subscribed = FALSE;
	folder->has_new_messages = FALSE;
	folder->freeze_count = 0;
	folder->open_count = 0;
	folder->mode = 0;
	folder->name = NULL;
	folder->full_name = NULL;
}

static void
spruce_folder_finalize (GObject *object)
{
	SpruceFolder *folder = (SpruceFolder *) object;
	
	if (folder->store)
		g_object_unref (folder->store);
	
	if (folder->parent)
		g_object_unref (folder->parent);
	
	if (folder->summary)
		g_object_unref (folder->summary);
	
	g_free (folder->name);
	g_free (folder->full_name);
	
	G_OBJECT_CLASS (parent_class)->finalize (object);
}


static void
deleting_folder (SpruceFolder *parent, SpruceFolder *child)
{
	spruce_folder_delete (child, NULL);
}

static void
renamed_folder (SpruceFolder *parent, const char *oldname, const char *newname, SpruceFolder *child)
{
	char *name;
	
	name = g_alloca (strlen (child->name) + 1);
	strcpy (name, child->name);
	
	spruce_folder_newname (child, newname, name);
}


/**
 * spruce_folder_construct:
 * @folder: a #SpruceFolder
 * @store: a #SpruceStore
 * @parent: a #SpruceFolder
 * @name: folder's name
 * @full_name: folder's full name
 *
 * Initialize @folder.
 **/
void
spruce_folder_construct (SpruceFolder *folder, SpruceStore *store, SpruceFolder *parent, const char *name, const char *full_name)
{
	g_return_if_fail (SPRUCE_IS_FOLDER (folder));
	g_return_if_fail (SPRUCE_IS_STORE (store));
	
	g_object_ref (store);
	folder->store = store;
	
	if (parent) {
		g_object_ref (parent);
		folder->parent = parent;
		g_signal_connect (parent, "deleting", G_CALLBACK (deleting_folder), folder);
		g_signal_connect (parent, "renamed", G_CALLBACK (renamed_folder), folder);
	}
	
	folder->name = g_strdup (name);
	folder->full_name = g_strdup (full_name);
}


static gboolean
folder_can_hold_folders (SpruceFolder *folder)
{
	return folder->type & SPRUCE_FOLDER_CAN_HOLD_FOLDERS;
}


/**
 * spruce_folder_can_hold_folders:
 * @folder: a #SpruceFolder
 *
 * Gets whether or not the folder can contain other folders.
 *
 * Returns: %TRUE if @folder can contain folders or %FALSE otherwise.
 **/
gboolean
spruce_folder_can_hold_folders (SpruceFolder *folder)
{
	g_return_val_if_fail (SPRUCE_IS_FOLDER (folder), FALSE);
	
	return SPRUCE_FOLDER_GET_CLASS (folder)->can_hold_folders (folder);
}


static gboolean
folder_can_hold_messages (SpruceFolder *folder)
{
	return folder->type & SPRUCE_FOLDER_CAN_HOLD_MESSAGES;
}


/**
 * spruce_folder_can_hold_messages:
 * @folder: a #SpruceFolder
 *
 * Gets whether or not the folder can contain messages.
 *
 * Returns: %TRUE if @folder can contain messages or %FALSE otherwise.
 **/
gboolean
spruce_folder_can_hold_messages (SpruceFolder *folder)
{
	g_return_val_if_fail (SPRUCE_IS_FOLDER (folder), FALSE);
	
	return SPRUCE_FOLDER_GET_CLASS (folder)->can_hold_messages (folder);
}


static gboolean
folder_has_new_messages (SpruceFolder *folder)
{
	return folder->has_new_messages;
}


/**
 * spruce_folder_has_new_messages:
 * @folder: a #SpruceFolder
 *
 * Gets whether or not the folder has new messages.
 *
 * Returns: %TRUE if @folder has new messages or %FALSE otherwise.
 **/
gboolean
spruce_folder_has_new_messages (SpruceFolder *folder)
{
	g_return_val_if_fail (SPRUCE_IS_FOLDER (folder), FALSE);
	
	if (folder->exists && (folder->type & SPRUCE_FOLDER_CAN_HOLD_MESSAGES))
		return SPRUCE_FOLDER_GET_CLASS (folder)->has_new_messages (folder);
	
	return FALSE;
}


static gboolean
folder_supports_subscriptions (SpruceFolder *folder)
{
	return folder->supports_subscriptions;
}


/**
 * spruce_folder_supports_subscriptions:
 * @folder: a #SpruceFolder
 *
 * Gets whether or not the folder supports subscriptions.
 *
 * Returns: %TRUE if @folder supports subscriptions or %FALSE
 * otherwise.
 **/
gboolean
spruce_folder_supports_subscriptions (SpruceFolder *folder)
{
	g_return_val_if_fail (SPRUCE_IS_FOLDER (folder), FALSE);
	
	return SPRUCE_FOLDER_GET_CLASS (folder)->supports_subscriptions (folder);
}


static gboolean
folder_supports_searches (SpruceFolder *folder)
{
	return folder->supports_searches;
}


/**
 * spruce_folder_supports_searches:
 * @folder: a #SpruceFolder
 *
 * Gets whether or not the folder supports searches.
 *
 * Returns: %TRUE if @folder supports searches or %FALSE otherwise.
 **/
gboolean
spruce_folder_supports_searches (SpruceFolder *folder)
{
	g_return_val_if_fail (SPRUCE_IS_FOLDER (folder), FALSE);
	
	return SPRUCE_FOLDER_GET_CLASS (folder)->supports_searches (folder);
}


static guint32
folder_get_mode (SpruceFolder *folder)
{
	return folder->mode;
}


/**
 * spruce_folder_get_mode:
 * @folder: a #SpruceFolder
 *
 * Gets the mode the folder has been opened with.
 *
 * Returns: the mode of the folder.
 **/
guint32
spruce_folder_get_mode (SpruceFolder *folder)
{
	g_return_val_if_fail (SPRUCE_IS_FOLDER (folder), 0);
	
	return SPRUCE_FOLDER_GET_CLASS (folder)->get_mode (folder);
}


static const char *
folder_get_name (SpruceFolder *folder)
{
	return folder->name;
}


/**
 * spruce_folder_get_name:
 * @folder: a #SpruceFolder
 *
 * Gets the name of the folder.
 *
 * Returns: the name of the folder.
 **/
const char *
spruce_folder_get_name (SpruceFolder *folder)
{
	g_return_val_if_fail (SPRUCE_IS_FOLDER (folder), NULL);
	
	return SPRUCE_FOLDER_GET_CLASS (folder)->get_name (folder);
}


static const char *
folder_get_full_name (SpruceFolder *folder)
{
	return folder->full_name;
}


/**
 * spruce_folder_get_full_name:
 * @folder: a #SpruceFolder
 *
 * Gets the full name of the folder.
 *
 * Returns: the full name of the folder.
 **/
const char *
spruce_folder_get_full_name (SpruceFolder *folder)
{
	g_return_val_if_fail (SPRUCE_IS_FOLDER (folder), NULL);
	
	return SPRUCE_FOLDER_GET_CLASS (folder)->get_full_name (folder);
}


static SpruceURL *
folder_get_url (SpruceFolder *folder)
{
	SpruceURL *url;
	
	url = spruce_url_copy (((SpruceService *) folder->store)->url);
	spruce_url_set_fragment (url, folder->full_name);
	
	return url;
}


/**
 * spruce_folder_get_url:
 * @folder: a #SpruceFolder
 *
 * Gets the URL of the folder.
 *
 * Returns: a newly allocated #SpruceURL for @folder.
 **/
SpruceURL *
spruce_folder_get_url (SpruceFolder *folder)
{
	g_return_val_if_fail (SPRUCE_IS_FOLDER (folder), NULL);
	
	return SPRUCE_FOLDER_GET_CLASS (folder)->get_url (folder);
}


/**
 * spruce_folder_get_uri:
 * @folder: a #SpruceFolder
 *
 * Gets the URI of the folder.
 *
 * Returns: a newly string containing the URI of the folder.
 **/
char *
spruce_folder_get_uri (SpruceFolder *folder)
{
	SpruceURL *url;
	char *uri;
	
	url = spruce_folder_get_url (folder);
	uri = spruce_url_to_string (url, 0);
	g_object_unref (url);
	
	return uri;
}


static char
folder_get_separator (SpruceFolder *folder)
{
	return folder->separator;
}


/**
 * spruce_folder_get_separator:
 * @folder: a #SpruceFolder
 *
 * Gets the path separator for the specified folder.
 *
 * Returns: the path separator for the specified folder or %0 if
 * unknown.
 **/
char
spruce_folder_get_separator (SpruceFolder *folder)
{
	g_return_val_if_fail (SPRUCE_IS_FOLDER (folder), '\0');
	
	return SPRUCE_FOLDER_GET_CLASS (folder)->get_separator (folder);
}


static guint32
folder_get_permanent_flags (SpruceFolder *folder)
{
	return folder->permanent_flags;
}


/**
 * spruce_folder_get_permanent_flags:
 * @folder: a #SpruceFolder
 *
 * Gets a bitmask of the flags with persistent state on the folder.
 *
 * Returns: a bitmask of the flags with persistent state on the
 * folder.
 **/
guint32
spruce_folder_get_permanent_flags (SpruceFolder *folder)
{
	g_return_val_if_fail (SPRUCE_IS_FOLDER (folder), 0);
	
	return SPRUCE_FOLDER_GET_CLASS (folder)->get_permanent_flags (folder);
}


static SpruceStore *
folder_get_store (SpruceFolder *folder)
{
	return folder->store;
}


/**
 * spruce_folder_get_store:
 * @folder: a #SpruceFolder
 *
 * Gets the store that the specified folder belongs to.
 *
 * Returns: the parent #SpruceStore.
 **/
SpruceStore *
spruce_folder_get_store (SpruceFolder *folder)
{
	g_return_val_if_fail (SPRUCE_IS_FOLDER (folder), NULL);
	
	return SPRUCE_FOLDER_GET_CLASS (folder)->get_store (folder);
}


static SpruceFolder *
folder_get_parent (SpruceFolder *folder)
{
	return folder->parent;
}


/**
 * spruce_folder_get_parent:
 * @folder: a #SpruceFolder
 *
 * Gets the parent folder for the specified folder.
 *
 * Returns: the parent #SpruceFolder or %NULL if the specified folder
 * is the toplevel folder.
 **/
SpruceFolder *
spruce_folder_get_parent (SpruceFolder *folder)
{
	g_return_val_if_fail (SPRUCE_IS_FOLDER (folder), NULL);
	
	return SPRUCE_FOLDER_GET_CLASS (folder)->get_parent (folder);
}


static gboolean
folder_is_open (SpruceFolder *folder)
{
	return folder->open_count > 0;
}


/**
 * spruce_folder_is_open:
 * @folder: a #SpruceFolder
 *
 * Gets whether or not the folder is open.
 *
 * Returns: %TRUE if the folder is open or %FALSE otherwise.
 **/
gboolean
spruce_folder_is_open (SpruceFolder *folder)
{
	g_return_val_if_fail (SPRUCE_IS_FOLDER (folder), FALSE);
	
	return SPRUCE_FOLDER_GET_CLASS (folder)->is_open (folder);
}


static gboolean
folder_is_subscribed (SpruceFolder *folder)
{
	return folder->is_subscribed;
}


/**
 * spruce_folder_is_subscribed:
 * @folder: a #SpruceFolder
 *
 * Gets whether or not the folder is subscribed.
 *
 * Returns: %TRUE if the folder is subscribed or %FALSE otherwise.
 **/
gboolean
spruce_folder_is_subscribed (SpruceFolder *folder)
{
	g_return_val_if_fail (SPRUCE_IS_FOLDER (folder), FALSE);
	
	return SPRUCE_FOLDER_GET_CLASS (folder)->is_subscribed (folder);
}


static gboolean
folder_exists (SpruceFolder *folder)
{
	return folder->exists;
}


/**
 * spruce_folder_exists:
 * @folder: a #SpruceFolder
 *
 * Gets whether or not the folder exists.
 *
 * Returns: %TRUE if the folder exists or %FALSE otherwise.
 **/
gboolean
spruce_folder_exists (SpruceFolder *folder)
{
	g_return_val_if_fail (SPRUCE_IS_FOLDER (folder), FALSE);
	
	return SPRUCE_FOLDER_GET_CLASS (folder)->exists (folder);
}


static int
folder_get_message_count (SpruceFolder *folder)
{
	if (folder->summary == NULL)
		return -1;
	
	return spruce_folder_summary_count (folder->summary);
}


/**
 * spruce_folder_get_message_count:
 * @folder: a #SpruceFolder
 *
 * Gets the number of messages in the specified folder.
 *
 * Returns: the number of messages in the folder or %-1 on error.
 **/
int
spruce_folder_get_message_count (SpruceFolder *folder)
{
	g_return_val_if_fail (SPRUCE_IS_FOLDER (folder), -1);
	
	return SPRUCE_FOLDER_GET_CLASS (folder)->get_message_count (folder);
}


static int
folder_get_unread_message_count (SpruceFolder *folder)
{
	SpruceMessageInfo *info;
	int i, count = 0;
	
	if (folder->summary == NULL)
		return -1;
	
	if (!folder->summary->loaded)
		return folder->summary->unread;
	
	for (i = 0; i < folder->summary->messages->len; i++) {
		info = folder->summary->messages->pdata[i];
		
		if (!(info->flags & SPRUCE_MESSAGE_SEEN))
			count++;
	}
	
	return count;
}


/**
 * spruce_folder_get_unread_message_count:
 * @folder: a #SpruceFolder
 *
 * Gets the number of unread messages in the specified folder.
 *
 * Returns: the number of unread messages in the folder or %-1 on
 * error.
 **/
int
spruce_folder_get_unread_message_count (SpruceFolder *folder)
{
	g_return_val_if_fail (SPRUCE_IS_FOLDER (folder), -1);
	
	return SPRUCE_FOLDER_GET_CLASS (folder)->get_unread_message_count (folder);
}


static int
folder_open (SpruceFolder *folder, GError **err)
{
	return 0;
}


/**
 * spruce_folder_open:
 * @folder: a #SpruceFolder
 * @err: a #GError
 *
 * Opens a folder, incrementing an 'open count' which will not allow
 * the folder to be closed until an equal number of calls to
 * spruce_folder_close() have been made.
 *
 * Returns: %0 on success or %-1 on error.
 **/
int
spruce_folder_open (SpruceFolder *folder, GError **err)
{
	int retval;
	
	g_return_val_if_fail (SPRUCE_IS_FOLDER (folder), -1);
	
	if (folder->open_count > 0) {
		folder->open_count++;
		return 0;
	}
	
	if ((retval = SPRUCE_FOLDER_GET_CLASS (folder)->open (folder, err)) == 0)
		folder->open_count = 1;
	
	return retval;
}


static int
folder_close (SpruceFolder *folder, gboolean expunge, GError **err)
{
	if (SPRUCE_FOLDER_GET_CLASS (folder)->sync (folder, expunge, err) == -1)
		return -1;
	
	if (folder->summary) {
		spruce_folder_summary_save (folder->summary);
		spruce_folder_summary_unload (folder->summary);
	}
	
	folder->mode = 0;
	
	return 0;
}


/**
 * spruce_folder_open:
 * @folder: a #SpruceFolder
 * @expunge: whether or not the folder should be expunged
 * @err: a #GError
 *
 * Opens a folder, incrementing an 'open count' which will not allow
 * the folder to be closed until an equal number of calls to
 * spruce_folder_close() have been made.
 *
 * Returns: %0 on success or %-1 on error.
 **/
int
spruce_folder_close (SpruceFolder *folder, gboolean expunge, GError **err)
{
	int retval;
	
	g_return_val_if_fail (SPRUCE_IS_FOLDER (folder), -1);
	g_return_val_if_fail (folder->open_count > 0, -1);
	
	if (folder->open_count > 1) {
		/* FIXME: expunge the folder? or re-think the API? */
		folder->open_count--;
		return 0;
	}
	
	if ((retval = SPRUCE_FOLDER_GET_CLASS (folder)->close (folder, expunge, err)) == 0)
		folder->open_count = 0;
	
	return retval;
}


static int
folder_create (SpruceFolder *folder, int type, GError **err)
{
	return -1;
}


/**
 * spruce_folder_create:
 * @folder: a #SpruceFolder
 * @type: folder type
 * @err: a #GError
 *
 * Creates the folder on the store.
 *
 * Returns: %0 on success or %-1 on error.
 **/
int
spruce_folder_create (SpruceFolder *folder, int type, GError **err)
{
	int retval;
	
	g_return_val_if_fail (SPRUCE_IS_FOLDER (folder), -1);
	
	/* shortcut this operation if we already exist */
	if (folder->exists) {
		g_set_error (err, SPRUCE_ERROR, EEXIST,
			     _("Cannot create folder `%s': folder exists"), folder->full_name);
		
		return -1;
	}
	
	/* make sure our parent folder exists and can hold folders */
	if (folder->parent && !spruce_folder_exists (folder->parent)) {
		if (spruce_folder_create (folder->parent, SPRUCE_FOLDER_CAN_HOLD_FOLDERS, err) == -1)
			return -1;
	}
	
	if ((retval = SPRUCE_FOLDER_GET_CLASS (folder)->create (folder, type, err)) == 0)
		folder->exists = TRUE;
	
	return retval;
}


static int
folder_delete (SpruceFolder *folder, GError **err)
{
	return 0;
}


/**
 * spruce_folder_delete:
 * @folder: a #SpruceFolder
 * @err: a #GError
 *
 * Deletes the folder from the store.
 *
 * Returns: %0 on success or %-1 on error.
 **/
int
spruce_folder_delete (SpruceFolder *folder, GError **err)
{
	int retval;
	
	g_return_val_if_fail (SPRUCE_IS_FOLDER (folder), -1);
	
	if (!folder->exists)
		return 0;
	
	g_signal_emit (folder, signals[DELETING], 0);
	if ((retval = SPRUCE_FOLDER_GET_CLASS (folder)->delete (folder, err)) == 0) {
		g_signal_emit (folder, signals[DELETED], 0);
		folder->exists = FALSE;
	}
	
	return retval;
}


static int
folder_rename (SpruceFolder *folder, const char *newname, GError **err)
{
	return 0;
}


/**
 * spruce_folder_rename:
 * @folder: a #SpruceFolder
 * @newname: new full-name of the folder
 * @err: a #GError
 *
 * Renames the folder on the store to @newname. The new name should be
 * the full path name.
 *
 * Returns: %0 on success or %-1 on error.
 **/
int
spruce_folder_rename (SpruceFolder *folder, const char *newname, GError **err)
{
	const char *name;
	char *oldname;
	int retval;
	
	g_return_val_if_fail (SPRUCE_IS_FOLDER (folder), -1);
	
	oldname = g_alloca (strlen (folder->full_name) + 1);
	strcpy (oldname, folder->full_name);
	
	if ((retval = SPRUCE_FOLDER_GET_CLASS (folder)->rename (folder, newname, err)) == 0) {
		g_free (folder->full_name);
		g_free (folder->name);
		
		folder->full_name = g_strdup (newname);
		
		if (!(name = strrchr (newname, '/')))
			name = newname;
		else
			name++;
		
		folder->name = g_strdup (name);
		
		g_signal_emit (folder, signals[RENAMED], 0, oldname, newname);
	}
	
	return retval;
}


static void
folder_newname (SpruceFolder *folder, const char *parent, const char *name)
{
	char *full_name;
	
	full_name = folder->full_name;
	
	if (parent)
		folder->full_name = g_strdup_printf ("%s/%s", parent, name);
	else
		folder->full_name = g_strdup (name);
	
	g_free (folder->name);
	folder->name = g_strdup (name);
	
	g_signal_emit (folder, signals[RENAMED], 0, full_name, folder->full_name);
	
	g_free (full_name);
}


static void
spruce_folder_newname (SpruceFolder *folder, const char *parent, const char *name)
{
	SPRUCE_FOLDER_GET_CLASS (folder)->newname (folder, parent, name);
}


static int
folder_sync (SpruceFolder *folder, gboolean expunge, GError **err)
{
	if (expunge && SPRUCE_FOLDER_GET_CLASS (folder)->expunge (folder, NULL, err) == -1)
		return -1;
	
	if (folder->summary)
		spruce_folder_summary_save (folder->summary);
	
	return 0;
}


/**
 * spruce_folder_sync:
 * @folder: a #SpruceFolder
 * @expunge: %TRUE if the folder should be expunged
 * @err: a #GError
 *
 * Syncs the folder with the backing store, expunging deleted messages
 * if requested.
 *
 * Returns: %0 on success or %-1 on error.
 **/
int
spruce_folder_sync (SpruceFolder *folder, gboolean expunge, GError **err)
{
	g_return_val_if_fail (SPRUCE_IS_FOLDER (folder), -1);
	
	return SPRUCE_FOLDER_GET_CLASS (folder)->sync (folder, expunge, err);
}


static int
folder_expunge (SpruceFolder *folder, GPtrArray *uids, GError **err)
{
	if (!(folder->mode & SPRUCE_FOLDER_MODE_WRITE)) {
		g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_FOLDER_READ_ONLY,
			     _("Cannot expunge folder `%s': folder in read-only mode"),
			     folder->full_name);
		
		return -1;
	}
	
	return 0;
}


/**
 * spruce_folder_expunge:
 * @folder: a #SpruceFolder
 * @uids: subset of UIDs to expunge or %NULL for all
 * @err: a #GError
 *
 * Expunges all messages marked for deletion in the specified list
 * @uids.
 *
 * Returns: %0 on success or %-1 on fail.
 **/
int
spruce_folder_expunge (SpruceFolder *folder, GPtrArray *uids, GError **err)
{
	g_return_val_if_fail (SPRUCE_IS_FOLDER (folder), -1);
	
	if (uids && uids->len == 0)
		return 0;
	
	if (!(folder->type & SPRUCE_FOLDER_CAN_HOLD_MESSAGES))
		return 0;
	
	return SPRUCE_FOLDER_GET_CLASS (folder)->expunge (folder, uids, err);
}


static GPtrArray *
folder_list (SpruceFolder *folder, const char *pattern, GError **err)
{
	return NULL;
}


/**
 * spruce_folder_list:
 * @folder: a #SpruceFolder
 * @pattern: glob pattern
 * @err: a #GError
 *
 * Lists the subfolders of @folder matching the glob pattern specified
 * by @pattern.
 *
 * Returns: a #GPtrArray of #SpruceFolder objects or %NULL on error.
 **/
GPtrArray *
spruce_folder_list (SpruceFolder *folder, const char *pattern, GError **err)
{
	g_return_val_if_fail (SPRUCE_IS_FOLDER (folder), NULL);
	
	/* can't list subfolders if we can't hold any ;-) */
	if (!(folder->type & SPRUCE_FOLDER_CAN_HOLD_FOLDERS))
		return g_ptr_array_new ();
	
	return SPRUCE_FOLDER_GET_CLASS (folder)->list (folder, pattern, err);
}


static GPtrArray *
folder_lsub (SpruceFolder *folder, const char *pattern, GError **err)
{
	return NULL;
}


/**
 * spruce_folder_lsub:
 * @folder: a #SpruceFolder
 * @pattern: glob pattern
 * @err: a #GError
 *
 * Lists the subscribed subfolders of @folder matching the glob
 * pattern specified by @pattern.
 *
 * Returns: a #GPtrArray of #SpruceFolder objects or %NULL on error.
 **/
GPtrArray *
spruce_folder_lsub (SpruceFolder *folder, const char *pattern, GError **err)
{
	g_return_val_if_fail (SPRUCE_IS_FOLDER (folder), NULL);
	
	/* can't list subfolders if we can't hold any ;-) */
	if (!(folder->type & SPRUCE_FOLDER_CAN_HOLD_FOLDERS))
		return g_ptr_array_new ();
	
	if (folder->supports_subscriptions)
		return SPRUCE_FOLDER_GET_CLASS (folder)->lsub (folder, pattern, err);
	else
		return SPRUCE_FOLDER_GET_CLASS (folder)->list (folder, pattern, err);
}


static int
folder_subscribe (SpruceFolder *folder, GError **err)
{
	return 0;
}


/**
 * spruce_folder_subscribe:
 * @folder: a #SpruceFolder
 * @err: a #GError
 *
 * Subscribes to the folder.
 *
 * Returns: %0 on success or %-1 on error.
 **/
int
spruce_folder_subscribe (SpruceFolder *folder, GError **err)
{
	int retval;
	
	g_return_val_if_fail (SPRUCE_IS_FOLDER (folder), -1);
	
	/* FIXME: should we shortcut if we are already subscribed? or
	 * is it better to let the subclass's implementation
	 * subscribe in case another client unsubscribed? */
	
	if ((retval = SPRUCE_FOLDER_GET_CLASS (folder)->subscribe (folder, err)) == 0) {
		folder->is_subscribed = TRUE;
		
		g_signal_emit (folder, signals[SUBSCRIBED], 0);
	}
	
	return retval;
}


static int
folder_unsubscribe (SpruceFolder *folder, GError **err)
{
	return 0;
}


/**
 * spruce_folder_unsubscribe:
 * @folder: a #SpruceFolder
 * @err: a #GError
 *
 * Unsubscribes from the folder.
 *
 * Returns: %0 on success or %-1 on error.
 **/
int
spruce_folder_unsubscribe (SpruceFolder *folder, GError **err)
{
	int retval;
	
	g_return_val_if_fail (SPRUCE_IS_FOLDER (folder), -1);
	
	/* FIXME: should we shortcut if we are already unsubscribed? or
           is it better to let the subclass's implementation
           unsubscribe in case another client re-subscribed? */
	
	if ((retval = SPRUCE_FOLDER_GET_CLASS (folder)->unsubscribe (folder, err)) == 0) {
		folder->is_subscribed = FALSE;
		
		g_signal_emit (folder, signals[UNSUBSCRIBED], 0);
	}
	
	return retval;
}


static GPtrArray *
folder_get_uids (SpruceFolder *folder)
{
	SpruceMessageInfo *info;
	GPtrArray *uids;
	int i, max;
	
	if (!folder->summary)
		return NULL;
	
	if ((max = spruce_folder_summary_count (folder->summary)) == 0)
		return NULL;
	
	uids = g_ptr_array_sized_new (max);
	
	for (i = 0; i < max; i++) {
		info = spruce_folder_summary_index (folder->summary, i);
		g_ptr_array_add (uids, g_strdup (info->uid));
		spruce_folder_summary_info_unref (folder->summary, info);
	}
	
	return uids;
}


/**
 * spruce_folder_get_uids:
 * @folder: a #SpruceFolder
 *
 * Gets an array containing the uids of all messages in the folder.
 *
 * Returns: a newly allocated #GPtrArray of uid strings or %NULL on
 * error.
 **/
GPtrArray *
spruce_folder_get_uids (SpruceFolder *folder)
{
	g_return_val_if_fail (SPRUCE_IS_FOLDER (folder), NULL);
	
	return SPRUCE_FOLDER_GET_CLASS (folder)->get_uids (folder);
}


static void
folder_free_uids (SpruceFolder *folder, GPtrArray *uids)
{
	int i;
	
	for (i = 0; i < uids->len; i++)
		g_free (uids->pdata[i]);
	
	g_ptr_array_free (uids, TRUE);
}


/**
 * spruce_folder_free_uids:
 * @folder: a #SpruceFolder
 * @uids: a #GPtrArray of uids.
 *
 * Frees an array of uids as returned from spruce_folder_get_uids().
 **/
void
spruce_folder_free_uids (SpruceFolder *folder, GPtrArray *uids)
{
	g_return_if_fail (SPRUCE_IS_FOLDER (folder));
	
	if (uids)
		SPRUCE_FOLDER_GET_CLASS (folder)->free_uids (folder, uids);
}


static SpruceMessageInfo *
folder_get_message_info (SpruceFolder *folder, const char *uid)
{
	SpruceMessageInfo *info = NULL;
	
	if (folder->summary)
		info = spruce_folder_summary_uid (folder->summary, uid);
	
	return info;
}


/**
 * spruce_folder_get_message_info:
 * @folder: a #SpruceFolder
 * @uid: a message uid
 *
 * Gets the #SpruceMessageInfo for the message with the specified uid.
 *
 * Returns: a reference to the #SpruceMessageInfo for the specified
 * uid or %NULL if no such message exists.
 **/
SpruceMessageInfo *
spruce_folder_get_message_info (SpruceFolder *folder, const char *uid)
{
	g_return_val_if_fail (SPRUCE_IS_FOLDER (folder), NULL);
	g_return_val_if_fail (uid != NULL, NULL);
	
	if (!(folder->type & SPRUCE_FOLDER_CAN_HOLD_MESSAGES))
		return NULL;
	
	return SPRUCE_FOLDER_GET_CLASS (folder)->get_message_info (folder, uid);
}


static void
folder_free_message_info (SpruceFolder *folder, SpruceMessageInfo *info)
{
	spruce_folder_summary_info_unref (folder->summary, info);
}


/**
 * spruce_folder_free_message_info:
 * @folder: a #SpruceFolder
 * @info: a #SpruceMessageInfo
 *
 * Frees a #SpruceMessageInfo as returned by
 * spruce_folder_get_message_info().
 **/
void
spruce_folder_free_message_info (SpruceFolder *folder, SpruceMessageInfo *info)
{
	g_return_if_fail (SPRUCE_IS_FOLDER (folder));
	
	if (info)
		SPRUCE_FOLDER_GET_CLASS (folder)->free_message_info (folder, info);
}


static guint32
folder_get_message_flags (SpruceFolder *folder, const char *uid)
{
	SpruceMessageInfo *info = NULL;
	
	if (folder->summary) {
		if ((info = spruce_folder_summary_uid (folder->summary, uid)))
			return info->flags & ~SPRUCE_MESSAGE_DIRTY;
	}
	
	return 0;
}


/**
 * spruce_folder_get_message_flags:
 * @folder: a #SpruceFolder
 * @uid: a message uid
 *
 * Gets the flags for the message with the specified uid.
 *
 * Returns: the flags for the message with the specified uid or %0 if
 * it doesn't exist.
 *
 * Note: %0 may also be returned if the message has no flags set.
 **/
guint32
spruce_folder_get_message_flags (SpruceFolder *folder, const char *uid)
{
	g_return_val_if_fail (SPRUCE_IS_FOLDER (folder), 0);
	g_return_val_if_fail (uid != NULL, 0);
	
	if (!(folder->type & SPRUCE_FOLDER_CAN_HOLD_MESSAGES))
		return 0;
	
	return SPRUCE_FOLDER_GET_CLASS (folder)->get_message_flags (folder, uid);
}


static int
folder_set_message_flags (SpruceFolder *folder, const char *uid, guint32 flags, guint32 set)
{
	SpruceMessageInfo *info = NULL;
	guint32 new;
	
	if (folder->summary) {
		if ((info = spruce_folder_summary_uid (folder->summary, uid))) {
			new = (info->flags & ~flags) | (set & flags);
			
			if (info->flags != new) {
				info->flags = new | SPRUCE_MESSAGE_DIRTY;
				spruce_folder_summary_touch (folder->summary);
			}
			
			return 0;
		}
	}
	
	return -1;
}


int
spruce_folder_set_message_flags (SpruceFolder *folder, const char *uid, guint32 flags, guint32 set)
{
	g_return_val_if_fail (SPRUCE_IS_FOLDER (folder), -1);
	g_return_val_if_fail (uid != NULL, -1);
	
	if (!(folder->type & SPRUCE_FOLDER_CAN_HOLD_MESSAGES))
		return -1;
	
	return SPRUCE_FOLDER_GET_CLASS (folder)->set_message_flags (folder, uid, flags, set);
}


static GMimeMessage *
folder_get_message (SpruceFolder *folder, const char *uid, GError **err)
{
	return NULL;
}


GMimeMessage *
spruce_folder_get_message (SpruceFolder *folder, const char *uid, GError **err)
{
	g_return_val_if_fail (SPRUCE_IS_FOLDER (folder), NULL);
	g_return_val_if_fail (uid != NULL, NULL);
	
	if (!(folder->type & SPRUCE_FOLDER_CAN_HOLD_MESSAGES)) {
		/* FIXME: set an error */
		return NULL;
	}
	
	return SPRUCE_FOLDER_GET_CLASS (folder)->get_message (folder, uid, err);
}


static int
folder_append_message (SpruceFolder *folder, GMimeMessage *message, SpruceMessageInfo *info, GError **err)
{
	return -1;
}


int
spruce_folder_append_message (SpruceFolder *folder, GMimeMessage *message, SpruceMessageInfo *info, GError **err)
{
	g_return_val_if_fail (SPRUCE_IS_FOLDER (folder), -1);
	g_return_val_if_fail (GMIME_IS_MESSAGE (message), -1);
	g_return_val_if_fail (info != NULL, -1);
	
	if (!(folder->type & SPRUCE_FOLDER_CAN_HOLD_MESSAGES)) {
		/* FIXME: set an error */
		return -1;
	}
	
	if (!(folder->mode & SPRUCE_FOLDER_MODE_WRITE)) {
		/* FIXME: set an error */
		return -1;
	}
	
	return SPRUCE_FOLDER_GET_CLASS (folder)->append_message (folder, message, info, err);
}


static int
folder_copy_messages (SpruceFolder *src, GPtrArray *uids, SpruceFolder *dest, GError **err)
{
	SpruceMessageInfo *info;
	GMimeMessage *message;
	int i;
	
	for (i = 0; i < uids->len; i++) {
		if (!(info = SPRUCE_FOLDER_GET_CLASS (src)->get_message_info (src, uids->pdata[i])))
			continue;
		
		if (!(message = SPRUCE_FOLDER_GET_CLASS (src)->get_message (src, uids->pdata[i], err))) {
			spruce_folder_free_message_info (src, info);
			continue;
		}
		
		if (spruce_folder_append_message (dest, message, info, err) == -1) {
			spruce_folder_free_message_info (src, info);
			g_object_unref (message);
			return -1;
		}
		
		spruce_folder_free_message_info (src, info);
		g_object_unref (message);
	}
	
	return 0;
}


int
spruce_folder_copy_messages (SpruceFolder *src, GPtrArray *uids, SpruceFolder *dest, GError **err)
{
	g_return_val_if_fail (SPRUCE_IS_FOLDER (src), -1);
	g_return_val_if_fail (SPRUCE_IS_FOLDER (dest), -1);
	g_return_val_if_fail (uids != NULL, -1);
	
	if (!(src->type & SPRUCE_FOLDER_CAN_HOLD_MESSAGES)) {
		/* FIXME: set an error */
		return -1;
	}
	
	return SPRUCE_FOLDER_GET_CLASS (src)->copy_messages (src, uids, dest, err);
}


static int
folder_move_messages (SpruceFolder *src, GPtrArray *uids, SpruceFolder *dest, GError **err)
{
	SpruceMessageInfo *info;
	GMimeMessage *message;
	guint32 flag;
	int i;
	
	flag = SPRUCE_MESSAGE_DELETED;
	
	for (i = 0; i < uids->len; i++) {
		if (!(info = SPRUCE_FOLDER_GET_CLASS (src)->get_message_info (src, uids->pdata[i])))
			continue;
		
		if (!(message = SPRUCE_FOLDER_GET_CLASS (src)->get_message (src, uids->pdata[i], err))) {
			spruce_folder_free_message_info (src, info);
			continue;
		}
		
		if (spruce_folder_append_message (dest, message, info, err) == -1) {
			spruce_folder_free_message_info (src, info);
			g_object_unref (message);
			return -1;
		}
		
		if (SPRUCE_FOLDER_GET_CLASS (src)->set_message_flags (src, uids->pdata[i], flag, flag) == -1) {
			spruce_folder_free_message_info (src, info);
			g_object_unref (message);
			return -1;
		}
		
		spruce_folder_free_message_info (src, info);
		g_object_unref (message);
	}
	
	return 0;
}


int
spruce_folder_move_messages (SpruceFolder *src, GPtrArray *uids, SpruceFolder *dest, GError **err)
{
	g_return_val_if_fail (SPRUCE_IS_FOLDER (src), -1);
	g_return_val_if_fail (SPRUCE_IS_FOLDER (dest), -1);
	g_return_val_if_fail (uids != NULL, -1);
	
	if (!(src->type & SPRUCE_FOLDER_CAN_HOLD_MESSAGES)) {
		/* FIXME: set an error */
		return -1;
	}
	
	return SPRUCE_FOLDER_GET_CLASS (src)->move_messages (src, uids, dest, err);
}


static GPtrArray *
folder_search (SpruceFolder *folder, GPtrArray *uids, const char *expression, GError **err)
{
	return NULL;
}


GPtrArray *
spruce_folder_search (SpruceFolder *folder, GPtrArray *uids, const char *expression, GError **err)
{
	g_return_val_if_fail (SPRUCE_IS_FOLDER (folder), NULL);
	
	if (!(folder->type & SPRUCE_FOLDER_CAN_HOLD_MESSAGES))
		return NULL;
	
	return SPRUCE_FOLDER_GET_CLASS (folder)->search (folder, uids, expression, err);
}


void
spruce_folder_search_free (SpruceFolder *folder, GPtrArray *results)
{
	int i;
	
	g_return_if_fail (results != NULL);
	
	for (i = 0; i < results->len; i++)
		g_free (results->pdata[i]);
	
	g_ptr_array_free (results, TRUE);
}


struct _SpruceFolderChangeInfoPrivate {
	GHashTable *uid_hash;
};

SpruceFolderChangeInfo *
spruce_folder_change_info_new (void)
{
	SpruceFolderChangeInfo *changes;
	
	changes = g_new (SpruceFolderChangeInfo, 1);
	changes->priv = g_new (struct _SpruceFolderChangeInfoPrivate, 1);
	changes->priv->uid_hash = g_hash_table_new (g_str_hash, g_str_equal);
	
	changes->added_uids = g_ptr_array_new ();
	changes->changed_uids = g_ptr_array_new ();
	changes->removed_uids = g_ptr_array_new ();
	
	return changes;
}

static gboolean
uid_hash_remove (gpointer key, gpointer value, gpointer user_data)
{
	return TRUE;
}

void
spruce_folder_change_info_clear (SpruceFolderChangeInfo *changes)
{
	int i;
	
	g_hash_table_foreach_remove (changes->priv->uid_hash, uid_hash_remove, NULL);
	
	for (i = 0; i < changes->added_uids->len; i++)
		g_free (changes->added_uids->pdata[i]);
	g_ptr_array_set_size (changes->added_uids, 0);
	
	for (i = 0; i < changes->changed_uids->len; i++)
		g_free (changes->changed_uids->pdata[i]);
	g_ptr_array_set_size (changes->changed_uids, 0);
	
	for (i = 0; i < changes->removed_uids->len; i++)
		g_free (changes->removed_uids->pdata[i]);
	g_ptr_array_set_size (changes->removed_uids, 0);
}


void
spruce_folder_change_info_free (SpruceFolderChangeInfo *changes)
{
	int i;
	
	g_hash_table_destroy (changes->priv->uid_hash);
	g_free (changes->priv);
	
	for (i = 0; i < changes->added_uids->len; i++)
		g_free (changes->added_uids->pdata[i]);
	g_ptr_array_free (changes->added_uids, TRUE);
	
	for (i = 0; i < changes->changed_uids->len; i++)
		g_free (changes->changed_uids->pdata[i]);
	g_ptr_array_free (changes->changed_uids, TRUE);
	
	for (i = 0; i < changes->removed_uids->len; i++)
		g_free (changes->removed_uids->pdata[i]);
	g_ptr_array_free (changes->removed_uids, TRUE);
	
	g_free (changes);
}


gboolean
spruce_folder_change_info_changed (SpruceFolderChangeInfo *changes)
{
	return changes->added_uids->len || changes->changed_uids->len || changes->removed_uids->len;
}


void
spruce_folder_change_info_add_uid (SpruceFolderChangeInfo *changes, const char *uid)
{
	gpointer key, val;
	
	if (g_hash_table_lookup_extended (changes->priv->uid_hash, uid, &key, &val)) {
		if (val == changes->removed_uids) {
			g_ptr_array_remove_fast (val, key);
			g_ptr_array_add (changes->changed_uids, key);
			g_hash_table_insert (changes->priv->uid_hash, key, changes->changed_uids);
		}
	} else {
		key = g_strdup (uid);
		g_ptr_array_add (changes->added_uids, key);
		g_hash_table_insert (changes->priv->uid_hash, key, changes->added_uids);
	}
}


void
spruce_folder_change_info_change_uid (SpruceFolderChangeInfo *changes, const char *uid)
{
	gpointer key, val;
	
	if (!g_hash_table_lookup_extended (changes->priv->uid_hash, uid, &key, &val)) {
		key = g_strdup (uid);
		g_ptr_array_add (changes->added_uids, key);
		g_hash_table_insert (changes->priv->uid_hash, key, changes->added_uids);
	}
}


void
spruce_folder_change_info_remove_uid (SpruceFolderChangeInfo *changes, const char *uid)
{
	gpointer key, val;
	
	if (g_hash_table_lookup_extended (changes->priv->uid_hash, uid, &key, &val)) {
		if (val != changes->removed_uids) {
			g_ptr_array_remove_fast (val, key);
			g_ptr_array_add (changes->changed_uids, key);
			g_hash_table_insert (changes->priv->uid_hash, key, changes->changed_uids);
		}
	} else {
		key = g_strdup (uid);
		g_ptr_array_add (changes->added_uids, key);
		g_hash_table_insert (changes->priv->uid_hash, key, changes->added_uids);
	}
}


static void
folder_freeze (SpruceFolder *folder)
{
	folder->freeze_count++;
}

void
spruce_folder_freeze (SpruceFolder *folder)
{
	g_return_if_fail (SPRUCE_IS_FOLDER (folder));
	
	SPRUCE_FOLDER_GET_CLASS (folder)->freeze (folder);
}


static void
folder_thaw (SpruceFolder *folder)
{
	if (folder->freeze_count > 0)
		folder->freeze_count--;
}


void
spruce_folder_thaw (SpruceFolder *folder)
{
	g_return_if_fail (SPRUCE_IS_FOLDER (folder));
	
	SPRUCE_FOLDER_GET_CLASS (folder)->thaw (folder);
}


static gboolean
folder_is_frozen (SpruceFolder *folder)
{
	return folder->freeze_count > 0;
}


gboolean
spruce_folder_is_frozen (SpruceFolder *folder)
{
	g_return_val_if_fail (SPRUCE_IS_FOLDER (folder), FALSE);
	
	return SPRUCE_FOLDER_GET_CLASS (folder)->is_frozen (folder);
}
