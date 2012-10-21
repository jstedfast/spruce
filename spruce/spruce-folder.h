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


#ifndef __SPRUCE_FOLDER_H__
#define __SPRUCE_FOLDER_H__

#include <glib.h>

#include <gmime/gmime-message.h>

#include <spruce/spruce-url.h>
#include <spruce/spruce-folder-summary.h>

G_BEGIN_DECLS

#define SPRUCE_TYPE_FOLDER            (spruce_folder_get_type ())
#define SPRUCE_FOLDER(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), SPRUCE_TYPE_FOLDER, SpruceFolder))
#define SPRUCE_FOLDER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), SPRUCE_TYPE_FOLDER, SpruceFolderClass))
#define SPRUCE_IS_FOLDER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SPRUCE_TYPE_FOLDER))
#define SPRUCE_IS_FOLDER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), SPRUCE_TYPE_FOLDER))
#define SPRUCE_FOLDER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), SPRUCE_TYPE_FOLDER, SpruceFolderClass))

typedef struct _SpruceFolder SpruceFolder;
typedef struct _SpruceFolderClass SpruceFolderClass;


enum {
	SPRUCE_FOLDER_MODE_NONE         = 0,
	SPRUCE_FOLDER_MODE_READ         = (1 << 0),
	SPRUCE_FOLDER_MODE_WRITE        = (1 << 1), /* this isn't actually a valid mode for a folder */
	SPRUCE_FOLDER_MODE_READ_WRITE   = (1 << 0) | (1 << 1)
};

enum {
	SPRUCE_FOLDER_CAN_HOLD_FOLDERS  = (1 << 0),
	SPRUCE_FOLDER_CAN_HOLD_MESSAGES = (1 << 1),
	SPRUCE_FOLDER_CAN_HOLD_ANYTHING = (1 << 0) | (1 << 1)
};

typedef struct _SpruceFolderChangeInfo {
	struct _SpruceFolderChangeInfoPrivate *priv;
	GPtrArray *added_uids;
	GPtrArray *changed_uids;
	GPtrArray *removed_uids;
} SpruceFolderChangeInfo;

struct _SpruceFolder {
	GObject parent_object;
	
	struct _SpruceStore *store;
	SpruceFolder *parent;
	
	SpruceFolderSummary *summary;
	
	guint32 type:2; /* can hold folders/messages */
	guint32 supports_searches:1;
	guint32 supports_subscriptions:1;
	
	/* state */
	guint32 mode:2;
	guint32 exists:1;
	guint32 is_subscribed:1;
	guint32 has_new_messages:1;
	
	guint32 freeze_count:23;
	guint32 open_count;
	
	char separator;
	
	char *name;
	char *full_name;
	
	guint32 permanent_flags;
};

struct _SpruceFolderClass {
	GObjectClass parent_class;
	
	/* public signals */
	void           (* deleted) (SpruceFolder *folder);
	void           (* renamed) (SpruceFolder *folder, const char *oldname, const char *newname);
	void           (* subscribed) (SpruceFolder *folder);
	void           (* unsubscribed) (SpruceFolder *folder);
	
	void           (* folder_changed) (SpruceFolder *folder, SpruceFolderChangeInfo *changes);
	
	/* private signals */
	void           (* deleting) (SpruceFolder *folder);
	
	/* virtual methods */
	gboolean       (* can_hold_folders) (SpruceFolder *folder);
	gboolean       (* can_hold_messages) (SpruceFolder *folder);
	
	gboolean       (* has_new_messages) (SpruceFolder *folder);
	
	gboolean       (* supports_subscriptions) (SpruceFolder *folder);
	gboolean       (* supports_searches) (SpruceFolder *folder);
	
	guint32        (* get_mode) (SpruceFolder *folder);
	
	const char *   (* get_name) (SpruceFolder *folder);
	const char *   (* get_full_name) (SpruceFolder *folder);
	
	SpruceURL *    (* get_url) (SpruceFolder *folder);
	
	char           (* get_separator) (SpruceFolder *folder);
	
	guint32        (* get_permanent_flags) (SpruceFolder *folder);
	
	struct _SpruceStore *  (* get_store)  (SpruceFolder *folder);
	SpruceFolder * (* get_parent) (SpruceFolder *folder);
	
	gboolean       (* is_open) (SpruceFolder *folder);
	gboolean       (* is_subscribed) (SpruceFolder *folder);
	
	gboolean       (* exists) (SpruceFolder *folder);
	
	int            (* get_message_count) (SpruceFolder *folder);
	int            (* get_unread_message_count) (SpruceFolder *folder);
	
	int            (* open) (SpruceFolder *folder, GError **err);
	int            (* close) (SpruceFolder *folder, gboolean expunge, GError **err);
	int            (* create) (SpruceFolder *folder, int type, GError **err);
	int            (* delete) (SpruceFolder *folder, GError **err);
	int            (* rename) (SpruceFolder *folder, const char *newname, GError **err);
	void           (* newname) (SpruceFolder *folder, const char *parent, const char *name);
	
	int            (* sync) (SpruceFolder *folder, gboolean expunge, GError **err);
	
	int            (* expunge) (SpruceFolder *folder, GPtrArray *uids, GError **err);
	
	GPtrArray *    (* list) (SpruceFolder *folder, const char *pattern, GError **err);
	GPtrArray *    (* lsub) (SpruceFolder *folder, const char *pattern, GError **err);
	
	int            (* subscribe) (SpruceFolder *folder, GError **err);
	int            (* unsubscribe) (SpruceFolder *folder, GError **err);
	
	GPtrArray *    (* get_uids) (SpruceFolder *folder);
	void           (* free_uids) (SpruceFolder *folder, GPtrArray *uids);
	
	SpruceMessageInfo * (* get_message_info) (SpruceFolder *folder, const char *uid);
	void           (* free_message_info) (SpruceFolder *folder, SpruceMessageInfo *info);
	
	guint32        (* get_message_flags) (SpruceFolder *folder, const char *uid);
	int            (* set_message_flags) (SpruceFolder *folder, const char *uid,
					      guint32 flags, guint32 set);
	
	GMimeMessage * (* get_message) (SpruceFolder *folder, const char *uid, GError **err);
	
	int            (* append_message) (SpruceFolder *folder, GMimeMessage *message,
					   SpruceMessageInfo *info, GError **err);
	
	int            (* copy_messages) (SpruceFolder *src, GPtrArray *uids, SpruceFolder *dest, GError **err);
	int            (* move_messages) (SpruceFolder *src, GPtrArray *uids, SpruceFolder *dest, GError **err);
	
	GPtrArray *    (* search) (SpruceFolder *folder, GPtrArray *uids, const char *expression, GError **err);
	
	void           (* freeze) (SpruceFolder *folder);
	void           (* thaw) (SpruceFolder *folder);
	gboolean       (* is_frozen) (SpruceFolder *folder);
};


GType spruce_folder_get_type (void);

void spruce_folder_construct (SpruceFolder *folder, struct _SpruceStore *store, SpruceFolder *parent,
			      const char *name, const char *full_name);


gboolean spruce_folder_can_hold_folders (SpruceFolder *folder);
gboolean spruce_folder_can_hold_messages (SpruceFolder *folder);

gboolean spruce_folder_has_new_messages (SpruceFolder *folder);

gboolean spruce_folder_supports_subscriptions (SpruceFolder *folder);
gboolean spruce_folder_supports_searches (SpruceFolder *folder);

guint32 spruce_folder_get_mode (SpruceFolder *folder);

const char *spruce_folder_get_name (SpruceFolder *folder);
const char *spruce_folder_get_full_name (SpruceFolder *folder);

SpruceURL *spruce_folder_get_url (SpruceFolder *folder);
char      *spruce_folder_get_uri (SpruceFolder *folder);

char spruce_folder_get_separator (SpruceFolder *folder);

guint32 spruce_folder_get_permanent_flags (SpruceFolder *folder);

struct _SpruceStore *spruce_folder_get_store (SpruceFolder *folder);
SpruceFolder *spruce_folder_get_parent (SpruceFolder *folder);

gboolean spruce_folder_is_open (SpruceFolder *folder);
gboolean spruce_folder_is_subscribed (SpruceFolder *folder);

gboolean spruce_folder_exists (SpruceFolder *folder);

int spruce_folder_get_message_count (SpruceFolder *folder);
int spruce_folder_get_unread_message_count (SpruceFolder *folder);

int spruce_folder_open (SpruceFolder *folder, GError **err);
int spruce_folder_close (SpruceFolder *folder, gboolean expunge, GError **err);
int spruce_folder_create (SpruceFolder *folder, int type, GError **err);
int spruce_folder_delete (SpruceFolder *folder, GError **err);
int spruce_folder_rename (SpruceFolder *folder, const char *newname, GError **err);

int spruce_folder_sync (SpruceFolder *folder, gboolean expunge, GError **err);

int spruce_folder_expunge (SpruceFolder *folder, GPtrArray *uids, GError **err);

GPtrArray *spruce_folder_list (SpruceFolder *folder, const char *pattern, GError **err);
GPtrArray *spruce_folder_lsub (SpruceFolder *folder, const char *pattern, GError **err);

int spruce_folder_subscribe (SpruceFolder *folder, GError **err);
int spruce_folder_unsubscribe (SpruceFolder *folder, GError **err);

GPtrArray *spruce_folder_get_uids (SpruceFolder *folder);
void      spruce_folder_free_uids (SpruceFolder *folder, GPtrArray *uids);

SpruceMessageInfo *spruce_folder_get_message_info (SpruceFolder *folder, const char *uid);
void              spruce_folder_free_message_info (SpruceFolder *folder, SpruceMessageInfo *info);

guint32 spruce_folder_get_message_flags (SpruceFolder *folder, const char *uid);
int     spruce_folder_set_message_flags (SpruceFolder *folder, const char *uid, guint32 flags, guint32 set);

GMimeMessage *spruce_folder_get_message (SpruceFolder *folder, const char *uid, GError **err);

int spruce_folder_append_message (SpruceFolder *folder, GMimeMessage *message,
				  SpruceMessageInfo *info, GError **err);

int spruce_folder_copy_messages (SpruceFolder *src, GPtrArray *uids, SpruceFolder *dest, GError **err);
int spruce_folder_move_messages (SpruceFolder *src, GPtrArray *uids, SpruceFolder *dest, GError **err);

GPtrArray *spruce_folder_search (SpruceFolder *folder, GPtrArray *uids, const char *expression, GError **err);
void spruce_folder_search_free (SpruceFolder *folder, GPtrArray *results);


SpruceFolderChangeInfo *spruce_folder_change_info_new (void);
void spruce_folder_change_info_clear (SpruceFolderChangeInfo *changes);
void spruce_folder_change_info_free (SpruceFolderChangeInfo *changes);

gboolean spruce_folder_change_info_changed (SpruceFolderChangeInfo *changes);

void spruce_folder_change_info_add_uid (SpruceFolderChangeInfo *changes, const char *uid);
void spruce_folder_change_info_change_uid (SpruceFolderChangeInfo *changes, const char *uid);
void spruce_folder_change_info_remove_uid (SpruceFolderChangeInfo *changes, const char *uid);

void spruce_folder_freeze (SpruceFolder *folder);
void spruce_folder_thaw (SpruceFolder *folder);
gboolean spruce_folder_is_frozen (SpruceFolder *folder);

G_END_DECLS

#endif /* __SPRUCE_FOLDER_H__ */
