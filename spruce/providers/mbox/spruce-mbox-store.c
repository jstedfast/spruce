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

#include "spruce-mbox-store.h"
#include "spruce-mbox-folder.h"

static void spruce_mbox_store_class_init (SpruceMboxStoreClass *klass);
static void spruce_mbox_store_init (SpruceMboxStore *store, SpruceMboxStoreClass *klass);
static void spruce_mbox_store_finalize (GObject *object);

static SpruceFolder *mbox_get_default_folder (SpruceStore *store, GError **err);
static SpruceFolder *mbox_get_folder (SpruceStore *store, const char *name, GError **err);
static SpruceFolder *mbox_get_folder_by_url (SpruceStore *store, SpruceURL *url, GError **err);


static SpruceStoreClass *parent_class = NULL;


GType
spruce_mbox_store_get_type (void)
{
	static GType type = 0;
	
	if (!type) {
		static const GTypeInfo info = {
			sizeof (SpruceMboxStoreClass),
			NULL, /* base_class_init */
			NULL, /* base_class_finalize */
			(GClassInitFunc) spruce_mbox_store_class_init,
			NULL, /* class_finalize */
			NULL, /* class_data */
			sizeof (SpruceMboxStore),
			0,    /* n_preallocs */
			(GInstanceInitFunc) spruce_mbox_store_init,
		};
		
		type = g_type_register_static (SPRUCE_TYPE_STORE, "SpruceMboxStore", &info, 0);
	}
	
	return type;
}


static void
spruce_mbox_store_class_init (SpruceMboxStoreClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	SpruceStoreClass *store_class = SPRUCE_STORE_CLASS (klass);
	
	parent_class = g_type_class_ref (SPRUCE_TYPE_STORE);
	
	object_class->finalize = spruce_mbox_store_finalize;
	
	store_class->get_default_folder = mbox_get_default_folder;
	store_class->get_folder = mbox_get_folder;
	store_class->get_folder_by_url = mbox_get_folder_by_url;
}

static void
spruce_mbox_store_init (SpruceMboxStore *store, SpruceMboxStoreClass *klass)
{
	;
}

static void
spruce_mbox_store_finalize (GObject *object)
{
	G_OBJECT_CLASS (parent_class)->finalize (object);
}


static SpruceFolder *
mbox_get_default_folder (SpruceStore *store, GError **err)
{
	return mbox_get_folder (store, "Inbox", err);
}

static SpruceFolder *
mbox_get_folder (SpruceStore *store, const char *name, GError **err)
{
	return spruce_mbox_folder_new (store, name, err);
}

static SpruceFolder *
mbox_get_folder_by_url (SpruceStore *store, SpruceURL *url, GError **err)
{
	SpruceService *service = (SpruceService *) store;
	const char *name = "Inbox";
	
	g_return_val_if_fail (strcmp (service->url->path, url->path) == 0, NULL);
	
	if (url->fragment != NULL)
		name = url->fragment;
	
	return mbox_get_folder (store, name, err);
}
