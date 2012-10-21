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

#include "spruce-store.h"


struct _SpruceStorePrivate {
	GHashTable *folder_hash;
};

static void spruce_store_class_init (SpruceStoreClass *klass);
static void spruce_store_init (SpruceStore *store, SpruceStoreClass *klass);
static void spruce_store_finalize (GObject *object);

static SpruceFolder *store_get_default_folder (SpruceStore *store, GError **err);
static SpruceFolder *store_get_folder (SpruceStore *store, const char *name, GError **err);
static SpruceFolder *store_get_folder_by_url (SpruceStore *store, SpruceURL *url, GError **err);
static GPtrArray *store_get_personal_namespaces (SpruceStore *store, GError **err);
static GPtrArray *store_get_shared_namespaces (SpruceStore *store, GError **err);
static GPtrArray *store_get_other_namespaces (SpruceStore *store, GError **err);
static int store_noop (SpruceStore *store, GError **err);


static SpruceService *parent_class = NULL;


GType
spruce_store_get_type (void)
{
	static GType type = 0;
	
	if (!type) {
		static const GTypeInfo info = {
			sizeof (SpruceStoreClass),
			NULL, /* base_class_init */
			NULL, /* base_class_finalize */
			(GClassInitFunc) spruce_store_class_init,
			NULL, /* class_finalize */
			NULL, /* class_data */
			sizeof (SpruceStore),
			0,    /* n_preallocs */
			(GInstanceInitFunc) spruce_store_init,
		};
		
		type = g_type_register_static (SPRUCE_TYPE_SERVICE, "SpruceStore", &info, 0);
	}
	
	return type;
}


static void
spruce_store_class_init (SpruceStoreClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	
	parent_class = g_type_class_ref (SPRUCE_TYPE_SERVICE);
	
	object_class->finalize = spruce_store_finalize;
	
	klass->get_default_folder = store_get_default_folder;
	klass->get_folder = store_get_folder;
	klass->get_folder_by_url = store_get_folder_by_url;
	klass->get_personal_namespaces = store_get_personal_namespaces;
	klass->get_shared_namespaces = store_get_shared_namespaces;
	klass->get_other_namespaces = store_get_other_namespaces;
	klass->noop = store_noop;
}

static void
spruce_store_init (SpruceStore *store, SpruceStoreClass *klass)
{
	store->priv = g_new0 (struct _SpruceStorePrivate, 1);
	store->priv->folder_hash = g_hash_table_new (g_str_hash, g_str_equal);
}

static void
spruce_store_finalize (GObject *object)
{
	SpruceStore *store = (SpruceStore *) object;
	
	g_hash_table_destroy (store->priv->folder_hash);
	g_free (store->priv);
	
	G_OBJECT_CLASS (parent_class)->finalize (object);
}


static SpruceFolder *
store_get_default_folder (SpruceStore *store, GError **err)
{
	return NULL;
}


SpruceFolder *
spruce_store_get_default_folder (SpruceStore *store, GError **err)
{
	g_return_val_if_fail (SPRUCE_IS_STORE (store), NULL);
	
	return SPRUCE_STORE_GET_CLASS (store)->get_default_folder (store, err);
}


static SpruceFolder *
store_get_folder (SpruceStore *store, const char *name, GError **err)
{
	return NULL;
}


static void
folder_destroyed (SpruceStore *store, SpruceFolder *folder)
{
	struct _SpruceStorePrivate *priv = store->priv;
	gpointer key, val;
	
	g_hash_table_lookup_extended (priv->folder_hash, folder->full_name, &key, &val);
	g_hash_table_remove (priv->folder_hash, folder->full_name);
	g_free (key);
}

static void
folder_renamed (SpruceFolder *folder, const char *oldname, const char *newname, SpruceStore *store)
{
	struct _SpruceStorePrivate *priv = store->priv;
	gpointer key, val;
	
	/* remove it from the hash under the old name */
	g_hash_table_lookup_extended (priv->folder_hash, oldname, &key, &val);
	g_hash_table_remove (priv->folder_hash, oldname);
	g_free (key);
	
	/* re-add it under the new name */
	g_hash_table_insert (priv->folder_hash, g_strdup (newname), folder);
}

SpruceFolder *
spruce_store_get_folder (SpruceStore *store, const char *name, GError **err)
{
	struct _SpruceStorePrivate *priv;
	SpruceFolder *folder;
	
	g_return_val_if_fail (SPRUCE_IS_STORE (store), NULL);
	
	priv = store->priv;
	if ((folder = g_hash_table_lookup (priv->folder_hash, name))) {
		g_object_ref (folder);
	} else if ((folder = SPRUCE_STORE_GET_CLASS (store)->get_folder (store, name, err))) {
		g_hash_table_insert (priv->folder_hash, g_strdup (name), folder);
		g_object_weak_ref ((GObject *) folder, (GWeakNotify) folder_destroyed, store);
		g_signal_connect (folder, "renamed", G_CALLBACK (folder_renamed), store);
	}
	
	return folder;
}


static SpruceFolder *
store_get_folder_by_url (SpruceStore *store, SpruceURL *url, GError **err)
{
	return NULL;
}


SpruceFolder *
spruce_store_get_folder_by_url (SpruceStore *store, SpruceURL *url, GError **err)
{
	SpruceService *service = (SpruceService *) store;
	
	g_return_val_if_fail (SPRUCE_IS_STORE (store), NULL);
	g_return_val_if_fail (SPRUCE_IS_URL (url), NULL);
	
	g_return_val_if_fail (strcmp (service->url->protocol, url->protocol) == 0, NULL);
	
	return SPRUCE_STORE_GET_CLASS (store)->get_folder_by_url (store, url, err);
}


static GPtrArray *
store_get_personal_namespaces (SpruceStore *store, GError **err)
{
	return NULL;
}


GPtrArray *
spruce_store_get_personal_namespaces (SpruceStore *store, GError **err)
{
	g_return_val_if_fail (SPRUCE_IS_STORE (store), NULL);
	
	return SPRUCE_STORE_GET_CLASS (store)->get_personal_namespaces (store, err);
}


static GPtrArray *
store_get_shared_namespaces (SpruceStore *store, GError **err)
{
	return NULL;
}


GPtrArray *
spruce_store_get_shared_namespaces (SpruceStore *store, GError **err)
{
	g_return_val_if_fail (SPRUCE_IS_STORE (store), NULL);
	
	return SPRUCE_STORE_GET_CLASS (store)->get_shared_namespaces (store, err);
}


static GPtrArray *
store_get_other_namespaces (SpruceStore *store, GError **err)
{
	return NULL;
}


GPtrArray *
spruce_store_get_other_namespaces (SpruceStore *store, GError **err)
{
	g_return_val_if_fail (SPRUCE_IS_STORE (store), NULL);
	
	return SPRUCE_STORE_GET_CLASS (store)->get_other_namespaces (store, err);
}


static int
store_noop (SpruceStore *store, GError **err)
{
	return 0;
}


int
spruce_store_noop (SpruceStore *store, GError **err)
{
	g_return_val_if_fail (SPRUCE_IS_STORE (store), -1);
	
	return SPRUCE_STORE_GET_CLASS (store)->noop (store, err);
}
