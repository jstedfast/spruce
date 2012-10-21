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


#ifndef __SPRUCE_PROVIDER_H__
#define __SPRUCE_PROVIDER_H__

#include <spruce/spruce-url.h>

G_BEGIN_DECLS

#define SPRUCE_TYPE_PROVIDER            (spruce_provider_get_type ())
#define SPRUCE_PROVIDER(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), SPRUCE_TYPE_PROVIDER, SpruceProvider))
#define SPRUCE_PROVIDER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), SPRUCE_TYPE_PROVIDER, SpruceProviderClass))
#define SPRUCE_IS_PROVIDER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SPRUCE_TYPE_PROVIDER))
#define SPRUCE_IS_PROVIDER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), SPRUCE_TYPE_PROVIDER))
#define SPRUCE_PROVIDER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), SPRUCE_TYPE_PROVIDER, SpruceProviderClass))

typedef struct _SpruceProvider SpruceProvider;
typedef struct _SpruceProviderClass SpruceProviderClass;

enum {
	SPRUCE_PROVIDER_TYPE_STORE,
	SPRUCE_PROVIDER_TYPE_TRANSPORT,
	SPRUCE_NUM_PROVIDER_TYPES
};

struct _SpruceProvider {
	GObject parent_object;
	
	const char *protocol;
	const char *name;
	const char *description;
	
	GType object_types[SPRUCE_NUM_PROVIDER_TYPES];
	
	GList *authtypes;
	
	GHashFunc url_hash;
	GCompareFunc url_equal;
	
	GHashTable *service_hash[SPRUCE_NUM_PROVIDER_TYPES];
};

struct _SpruceProviderClass {
	GObjectClass parent_class;
	
};


GType spruce_provider_get_type (void);

/* methods defined by each plugin module */
void spruce_provider_module_init (void);

/* methods to be used by the provider plugin */
void spruce_provider_register (SpruceProvider *provider);

/* methods to be used by the application */
SpruceProvider *spruce_provider_lookup (const char *protocol, GError **err);
GList *spruce_provider_list (gboolean load);

struct _SpruceService *spruce_provider_lookup_service (SpruceProvider *provider, SpruceURL *url, int type);
void spruce_provider_insert_service (SpruceProvider *provider, struct _SpruceService *service, int type);

/* internal methods */
G_GNUC_INTERNAL void spruce_provider_scan_modules (void);
G_GNUC_INTERNAL void spruce_provider_shutdown (void);

G_END_DECLS

#endif /* __SPRUCE_PROVIDER_H__ */
