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

#include <spruce/spruce-offline-store.h>
#include <spruce/spruce-offline-folder.h>


static void spruce_offline_store_class_init (SpruceOfflineStoreClass *klass);
static void spruce_offline_store_init (SpruceOfflineStore *store, SpruceOfflineStoreClass *klass);
static void spruce_offline_store_finalize (GObject *object);


static SpruceStoreClass *parent_class = NULL;


GType
spruce_offline_store_get_type (void)
{
	static GType type = 0;
	
	if (!type) {
		static const GTypeInfo info = {
			sizeof (SpruceOfflineStoreClass),
			NULL, /* base_class_init */
			NULL, /* base_class_finalize */
			(GClassInitFunc) spruce_offline_store_class_init,
			NULL, /* class_finalize */
			NULL, /* class_data */
			sizeof (SpruceOfflineStore),
			0,    /* n_preallocs */
			(GInstanceInitFunc) spruce_offline_store_init,
		};
		
		type = g_type_register_static (SPRUCE_TYPE_STORE, "SpruceOfflineStore", &info, 0);
	}
	
	return type;
}


static void
spruce_offline_store_class_init (SpruceOfflineStoreClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	
	parent_class = g_type_class_ref (SPRUCE_TYPE_STORE);
	
	object_class->finalize = spruce_offline_store_finalize;
}

static void
spruce_offline_store_init (SpruceOfflineStore *store, SpruceOfflineStoreClass *klass)
{
	store->state = SPRUCE_OFFLINE_STORE_NETWORK_AVAIL;
}

static void
spruce_offline_store_finalize (GObject *object)
{
	G_OBJECT_CLASS (parent_class)->finalize (object);
}


/**
 * FIXME: this is rather hackish...
 **/

struct _SpruceStorePrivate {
	GHashTable *folder_hash;
};

static void
get_folders_cb (const char *name, SpruceFolder *folder, GPtrArray *folders)
{
	g_ptr_array_add (folders, folder);
	g_object_ref (folder);
}

static GPtrArray *
spruce_object_bag_list (GHashTable *hash)
{
	GPtrArray *folders;
	
	folders = g_ptr_array_new ();
	g_hash_table_foreach (hash, (GHFunc) get_folders_cb, folders);
	
	return folders;
}

/**
 * spruce_offline_store_set_network_state:
 * @store: a #SpruceOfflineStore object
 * @state: the network state
 * @err: a #GError
 *
 * Set the network state to either #SPRUCE_OFFLINE_STORE_NETWORK_AVAIL
 * or #SPRUCE_OFFLINE_STORE_NETWORK_UNAVAIL.
 **/
void
spruce_offline_store_set_network_state (SpruceOfflineStore *store, int state, GError **err)
{
	SpruceService *service = SPRUCE_SERVICE (store);
	gboolean network_state, sync;
	GError *lerr = NULL;
	const char *opt;
	
	network_state = spruce_session_get_network_state (service->session);
	
	if (store->state == state)
		return;
	
	if (store->state == SPRUCE_OFFLINE_STORE_NETWORK_AVAIL) {
		/* network available -> network unavailable */
		if (network_state) {
			opt = spruce_url_get_param (((SpruceService *) store)->url, "sync_offline");
			sync = opt && (!opt[0] || !strcmp (opt, "true") || !strcmp (opt, "yes"));
			
			if (g_hash_table_size (((SpruceStore *) store)->priv->folder_hash) > 0) {
				SpruceFolder *folder;
				GPtrArray *folders;
				int i;
				
				folders = spruce_object_bag_list (((SpruceStore *) store)->priv->folder_hash);
				for (i = 0; i < folders->len; i++) {
					folder = folders->pdata[i];
					
					if (G_TYPE_CHECK_INSTANCE_TYPE (folder, SPRUCE_TYPE_OFFLINE_FOLDER)
					    && (sync || ((SpruceOfflineFolder *) folder)->sync_offline)) {
						spruce_offline_folder_downsync ((SpruceOfflineFolder *) folder, NULL, &lerr);
						g_error_free (lerr);
						lerr = NULL;
					}
					
					g_object_unref (folder);
				}
				
				g_ptr_array_free (folders, TRUE);
			}
			
			/*spruce_store_sync (SPRUCE_STORE (store), FALSE, &lerr);*/
			g_error_free (lerr);
			lerr = NULL;
		}
		
		if (!spruce_service_disconnect (SPRUCE_SERVICE (store), network_state, err))
			return;
	} else {
		/* network unavailable -> network available */
		if (!spruce_service_connect (SPRUCE_SERVICE (store), err))
			return;
	}
	
	store->state = state;
}
