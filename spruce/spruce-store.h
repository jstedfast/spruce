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


#ifndef __SPRUCE_STORE_H__
#define __SPRUCE_STORE_H__

#include <glib.h>

#include <spruce/spruce-folder.h>
#include <spruce/spruce-service.h>

G_BEGIN_DECLS

#define SPRUCE_TYPE_STORE            (spruce_store_get_type ())
#define SPRUCE_STORE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), SPRUCE_TYPE_STORE, SpruceStore))
#define SPRUCE_STORE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), SPRUCE_TYPE_STORE, SpruceStoreClass))
#define SPRUCE_IS_STORE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SPRUCE_TYPE_STORE))
#define SPRUCE_IS_STORE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), SPRUCE_TYPE_STORE))
#define SPRUCE_STORE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), SPRUCE_TYPE_STORE, SpruceStoreClass))

typedef struct _SpruceStore SpruceStore;
typedef struct _SpruceStoreClass SpruceStoreClass;

struct _SpruceStore {
	SpruceService parent_object;
	
	struct _SpruceStorePrivate *priv;
};

struct _SpruceStoreClass {
	SpruceServiceClass parent_class;
	
	SpruceFolder * (* get_default_folder) (SpruceStore *store, GError **err);
	SpruceFolder * (* get_folder)         (SpruceStore *store, const char *name, GError **err);
	SpruceFolder * (* get_folder_by_url)  (SpruceStore *store, SpruceURL *url, GError **err);
	
	GPtrArray * (* get_personal_namespaces) (SpruceStore *store, GError **err);
	GPtrArray * (* get_shared_namespaces) (SpruceStore *store, GError **err);
	GPtrArray * (* get_other_namespaces) (SpruceStore *store, GError **err);
	
	int (* noop) (SpruceStore *store, GError **err);
};


GType spruce_store_get_type (void);

SpruceFolder *spruce_store_get_default_folder (SpruceStore *store, GError **err);
SpruceFolder *spruce_store_get_folder (SpruceStore *store, const char *name, GError **err);
SpruceFolder *spruce_store_get_folder_by_url (SpruceStore *store, SpruceURL *url, GError **err);

GPtrArray *spruce_store_get_personal_namespaces (SpruceStore *store, GError **err);
GPtrArray *spruce_store_get_shared_namespaces (SpruceStore *store, GError **err);
GPtrArray *spruce_store_get_other_namespaces (SpruceStore *store, GError **err);

int spruce_store_noop (SpruceStore *store, GError **err);

G_END_DECLS

#endif /* __SPRUCE_STORE_H__ */
