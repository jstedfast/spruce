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


#ifndef __SPRUCE_MAILDIR_STORE_H__
#define __SPRUCE_MAILDIR_STORE_H__

#include <spruce/spruce-store.h>

G_BEGIN_DECLS

#define SPRUCE_TYPE_MAILDIR_STORE            (spruce_maildir_store_get_type ())
#define SPRUCE_MAILDIR_STORE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), SPRUCE_TYPE_MAILDIR_STORE, SpruceMaildirStore))
#define SPRUCE_MAILDIR_STORE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), SPRUCE_TYPE_MAILDIR_STORE, SpruceMaildirStoreClass))
#define SPRUCE_IS_MAILDIR_STORE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SPRUCE_TYPE_MAILDIR_STORE))
#define SPRUCE_IS_MAILDIR_STORE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), SPRUCE_TYPE_MAILDIR_STORE))
#define SPRUCE_MAILDIR_STORE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), SPRUCE_TYPE_MAILDIR_STORE, SpruceMaildirStoreClass))

typedef struct _SpruceMaildirStore SpruceMaildirStore;
typedef struct _SpruceMaildirStoreClass SpruceMaildirStoreClass;

struct _SpruceMaildirStore {
	SpruceStore parent_object;
	
};

struct _SpruceMaildirStoreClass {
	SpruceStoreClass parent_class;
	
};


GType spruce_maildir_store_get_type (void);

G_END_DECLS

#endif /* __SPRUCE_MAILDIR_STORE_H__ */
