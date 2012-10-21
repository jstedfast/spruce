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

#include <spruce/spruce-offline-folder.h>


static void spruce_offline_folder_class_init (SpruceOfflineFolderClass *klass);
static void spruce_offline_folder_init (SpruceOfflineFolder *folder, SpruceOfflineFolderClass *klass);
static void spruce_offline_folder_finalize (GObject *object);

static void offline_folder_downsync (SpruceOfflineFolder *folder, const char *expression, GError **err);


static SpruceFolderClass *parent_class = NULL;


GType
spruce_offline_folder_get_type (void)
{
	static GType type = 0;
	
	if (!type) {
		static const GTypeInfo info = {
			sizeof (SpruceOfflineFolderClass),
			NULL, /* base_class_init */
			NULL, /* base_class_finalize */
			(GClassInitFunc) spruce_offline_folder_class_init,
			NULL, /* class_finalize */
			NULL, /* class_data */
			sizeof (SpruceOfflineFolder),
			0,    /* n_preallocs */
			(GInstanceInitFunc) spruce_offline_folder_init,
		};
		
		type = g_type_register_static (SPRUCE_TYPE_FOLDER, "SpruceOfflineFolder", &info, 0);
	}
	
	return type;
}


static void
spruce_offline_folder_class_init (SpruceOfflineFolderClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	
	parent_class = g_type_class_ref (SPRUCE_TYPE_FOLDER);
	
	object_class->finalize = spruce_offline_folder_finalize;
	
	klass->downsync = offline_folder_downsync;
}

static void
spruce_offline_folder_init (SpruceOfflineFolder *folder, SpruceOfflineFolderClass *klass)
{
	folder->sync_offline = FALSE;
}

static void
spruce_offline_folder_finalize (GObject *object)
{
	G_OBJECT_CLASS (parent_class)->finalize (object);
}


static void
offline_folder_downsync (SpruceOfflineFolder *offline, const char *expression, GError **err)
{
	SpruceFolder *folder = (SpruceFolder *) offline;
	GMimeMessage *message;
	GPtrArray *uids;
	int i;
	
	if (expression)
		uids = spruce_folder_search (folder, NULL, expression, err);
	else
		uids = spruce_folder_get_uids (folder);
	
	if (!uids)
		return;
	
	for (i = 0; i < uids->len; i++) {
		/* loading the message will force the provider to
		 * cache it locally (if not already cached) */
		if (!(message = spruce_folder_get_message (folder, uids->pdata[i], err)))
			break;
		
		g_object_unref (message);
	}
	
	if (expression)
		spruce_folder_search_free (folder, uids);
	else
		spruce_folder_free_uids (folder, uids);
}


/**
 * spruce_offline_folder_downsync:
 * @offline: a #SpruceOfflineFolder object
 * @expression: search expression describing which set of messages to downsync (%NULL for all)
 * @err: a #GError
 *
 * Syncs messages in @offline described by the search @expression to
 * the local machine for offline availability.
 **/
void
spruce_offline_folder_downsync (SpruceOfflineFolder *offline, const char *expression, GError **err)
{
	g_return_if_fail (SPRUCE_IS_OFFLINE_FOLDER (offline));
	
	SPRUCE_OFFLINE_FOLDER_GET_CLASS (offline)->downsync (offline, expression, err);
}
