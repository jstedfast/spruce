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


#ifndef __SPRUCE_OFFLINE_STORE_H__
#define __SPRUCE_OFFLINE_STORE_H__

#include <spruce/spruce-store.h>

G_BEGIN_DECLS

#define SPRUCE_TYPE_OFFLINE_STORE            (spruce_offline_store_get_type ())
#define SPRUCE_OFFLINE_STORE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), SPRUCE_TYPE_OFFLINE_STORE, SpruceOfflineStore))
#define SPRUCE_OFFLINE_STORE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), SPRUCE_TYPE_OFFLINE_STORE, SpruceOfflineStoreClass))
#define SPRUCE_IS_OFFLINE_STORE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SPRUCE_TYPE_OFFLINE_STORE))
#define SPRUCE_IS_OFFLINE_STORE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), SPRUCE_TYPE_OFFLINE_STORE))
#define SPRUCE_OFFLINE_STORE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), SPRUCE_TYPE_OFFLINE_STORE, SpruceOfflineStoreClass))

typedef struct _SpruceOfflineStore SpruceOfflineStore;
typedef struct _SpruceOfflineStoreClass SpruceOfflineStoreClass;

enum {
	SPRUCE_OFFLINE_STORE_NETWORK_AVAIL,
	SPRUCE_OFFLINE_STORE_NETWORK_UNAVAIL,
};

struct _SpruceOfflineStore {
	SpruceStore parent_object;
	
	int state;
};

struct _SpruceOfflineStoreClass {
	SpruceStoreClass parent_class;
	
	void (* set_network_state) (SpruceOfflineStore *store, int state, GError **err);
};


GType spruce_offline_store_get_type (void);


void spruce_offline_store_set_network_state (SpruceOfflineStore *store, int state, GError **err);

G_END_DECLS

#endif /* __SPRUCE_OFFLINE_STORE_H__ */
