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


#ifndef __SPRUCE_CACHE_STREAM_H__
#define __SPRUCE_CACHE_STREAM_H__

#include <gmime/gmime-stream.h>
#include <gmime/gmime-stream-cat.h>

G_BEGIN_DECLS

#define SPRUCE_TYPE_CACHE_STREAM            (spruce_cache_stream_get_type ())
#define SPRUCE_CACHE_STREAM(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), SPRUCE_TYPE_CACHE_STREAM, SpruceCacheStream))
#define SPRUCE_CACHE_STREAM_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), SPRUCE_TYPE_CACHE_STREAM, SpruceCacheStreamClass))
#define SPRUCE_IS_CACHE_STREAM(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SPRUCE_TYPE_CACHE_STREAM))
#define SPRUCE_IS_CACHE_STREAM_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), SPRUCE_TYPE_CACHE_STREAM))
#define SPRUCE_CACHE_STREAM_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), SPRUCE_TYPE_CACHE_STREAM, SpruceCacheStreamClass))

typedef struct _SpruceCacheStream SpruceCacheStream;
typedef struct _SpruceCacheStreamClass SpruceCacheStreamClass;

struct _SpruceCache;

struct _SpruceCacheStream {
	GMimeStreamCat parent_object;
	
	struct _SpruceCacheStreamPrivate *priv;
};

struct _SpruceCacheStreamClass {
	GMimeStreamCatClass parent_class;
	
	/* Virtual methods */
	GMimeStream * (* commit) (SpruceCacheStream *stream);
};


GType spruce_cache_stream_get_type (void);


GMimeStream *spruce_cache_stream_new (struct _SpruceCache *cache, const char *key,
				      const char *path, int fd, GError **err);

void spruce_cache_stream_abort (SpruceCacheStream *stream);
GMimeStream *spruce_cache_stream_commit (SpruceCacheStream *stream);

G_END_DECLS

#endif /* __SPRUCE_CACHE_STREAM_H__ */
