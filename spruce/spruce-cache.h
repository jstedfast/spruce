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


#ifndef __SPRUCE_CACHE_H__
#define __SPRUCE_CACHE_H__

#include <glib.h>
#include <gmime/gmime-stream.h>

G_BEGIN_DECLS

#define SPRUCE_TYPE_CACHE            (spruce_cache_get_type ())
#define SPRUCE_CACHE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), SPRUCE_TYPE_CACHE, SpruceCache))
#define SPRUCE_CACHE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), SPRUCE_TYPE_CACHE, SpruceCacheClass))
#define SPRUCE_IS_CACHE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SPRUCE_TYPE_CACHE))
#define SPRUCE_IS_CACHE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), SPRUCE_TYPE_CACHE))
#define SPRUCE_CACHE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), SPRUCE_TYPE_CACHE, SpruceCacheClass))

typedef struct _SpruceCache SpruceCache;
typedef struct _SpruceCacheClass SpruceCacheClass;

struct _SpruceCache {
	GObject parent_object;
	
	guint64 cache_size;
	char *basedir;
};

struct _SpruceCacheClass {
	GObjectClass parent_class;
	
};


GType spruce_cache_get_type (void);


SpruceCache *spruce_cache_new (const char *basedir, guint64 cache_size);

GMimeStream *spruce_cache_add (SpruceCache *cache, const char *key, GError **err);
GMimeStream *spruce_cache_get (SpruceCache *cache, const char *key, GError **err);

int spruce_cache_commit (SpruceCache *cache, const char *key, GError **err);

int spruce_cache_rekey (SpruceCache *cache, const char *key, const char *new_key, GError **err);

int spruce_cache_expire (SpruceCache *cache, GError **err);
int spruce_cache_expire_all (SpruceCache *cache, GError **err);
int spruce_cache_expire_key (SpruceCache *cache, const char *key, GError **err);

int spruce_cache_delete (SpruceCache *cache, GError **err);
int spruce_cache_rename (SpruceCache *cache, const char *new_path, GError **err);


/* utility function for returning a temporary disk-backed stream which
 * will be unlinked on finalize */
GMimeStream *spruce_cache_tmp_stream (void);

G_END_DECLS

#endif /* __SPRUCE_CACHE_H__ */
