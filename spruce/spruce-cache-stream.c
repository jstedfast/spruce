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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <errno.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>

#include <gmime/gmime-stream-fs.h>
#include <gmime/gmime-stream-mem.h>

#include <spruce/spruce-error.h>
#include <spruce/spruce-cache.h>
#include <spruce/spruce-file-utils.h>
#include <spruce/spruce-cache-stream.h>


typedef enum {
	CACHE_STATE_NONE,
	CACHE_STATE_ABORTED,
	CACHE_STATE_COMMITTED,
} cache_state_t;

struct _SpruceCacheStreamPrivate {
	GMimeStream *overflow;
	GMimeStream *backing;
	cache_state_t state;
	SpruceCache *cache;
	char *path;
	char *key;
};


static void spruce_cache_stream_class_init (SpruceCacheStreamClass *klass);
static void spruce_cache_stream_init (SpruceCacheStream *stream, SpruceCacheStreamClass *klass);
static void spruce_cache_stream_finalize (GObject *object);

static GMimeStream *cache_stream_commit (SpruceCacheStream *stream);


static GMimeStreamCatClass *parent_class = NULL;


GType
spruce_cache_stream_get_type (void)
{
	static GType type = 0;
	
	if (!type) {
		static const GTypeInfo info = {
			sizeof (SpruceCacheStreamClass),
			NULL, /* base_class_init */
			NULL, /* base_class_finalize */
			(GClassInitFunc) spruce_cache_stream_class_init,
			NULL, /* class_finalize */
			NULL, /* class_data */
			sizeof (SpruceCacheStream),
			0,    /* n_preallocs */
			(GInstanceInitFunc) spruce_cache_stream_init,
		};
		
		type = g_type_register_static (GMIME_TYPE_STREAM_CAT, "SpruceCacheStream", &info, 0);
	}
	
	return type;
}


static void
spruce_cache_stream_class_init (SpruceCacheStreamClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	
	parent_class = g_type_class_ref (GMIME_TYPE_STREAM_CAT);
	
	object_class->finalize = spruce_cache_stream_finalize;
	
	klass->commit = cache_stream_commit;
}

static void
spruce_cache_stream_init (SpruceCacheStream *stream, SpruceCacheStreamClass *klass)
{
	stream->priv = g_new (struct _SpruceCacheStreamPrivate, 1);
	stream->priv->state = CACHE_STATE_NONE;
	stream->priv->overflow = NULL;
	stream->priv->backing = NULL;
	stream->priv->cache = NULL;
	stream->priv->path = NULL;
	stream->priv->key = NULL;
}

static void
spruce_cache_stream_finalize (GObject *object)
{
	SpruceCacheStream *stream = (SpruceCacheStream *) object;
	struct _SpruceCacheStreamPrivate *priv = stream->priv;
	
	if (!priv->state)
		priv->state = CACHE_STATE_ABORTED;
	
	if (priv->state == CACHE_STATE_ABORTED && priv->path)
		unlink (priv->path);
	
	g_object_unref (priv->overflow);
	g_object_unref (priv->backing);
	
	if (priv->cache)
		g_object_unref (priv->cache);
	
	g_free (priv->path);
	g_free (priv->key);
	
	G_OBJECT_CLASS (parent_class)->finalize (object);
}


void
spruce_cache_stream_abort (SpruceCacheStream *stream)
{
	g_return_if_fail (SPRUCE_IS_CACHE_STREAM (stream));
	
	if (stream->priv->state != CACHE_STATE_NONE)
		return;
	
	stream->priv->state = CACHE_STATE_ABORTED;
}


static GMimeStream *
cache_stream_commit (SpruceCacheStream *cstream)
{
	struct _SpruceCacheStreamPrivate *priv = cstream->priv;
	GByteArray *buf;
	
	/* can't commit if there was any overflow */
	if ((buf = GMIME_STREAM_MEM (priv->overflow)->buffer) && buf->len)
		return NULL;
	
	/* can't commit if we failed to sync to disk */
	if (g_mime_stream_flush (priv->backing) == -1)
		return NULL;
	
	if (spruce_cache_commit (priv->cache, priv->key, NULL) == -1)
		return NULL;
	
	return spruce_cache_get (priv->cache, priv->key, NULL);
}


GMimeStream *
spruce_cache_stream_commit (SpruceCacheStream *stream)
{
	GMimeStream *str;
	
	g_return_val_if_fail (SPRUCE_IS_CACHE_STREAM (stream), NULL);
	
	if (stream->priv->state != CACHE_STATE_NONE)
		goto exception;
	
	if (!(str = SPRUCE_CACHE_STREAM_GET_CLASS (stream)->commit (stream)))
		goto exception;
	
	stream->priv->state = CACHE_STATE_COMMITTED;
	
	return str;
	
 exception:
	
	stream->priv->state = CACHE_STATE_ABORTED;
	
	return g_mime_stream_substream ((GMimeStream *) stream, 0, -1);
}


/**
 * spruce_cache_stream_new:
 * @cache: a #SpruceCache object
 * @key: key for this stream in the cache
 * @path: file path that this stream should represent
 * @fd: UNIX file descritor that this stream should represent
 * @err: a #GError
 *
 * Creates a new #SpruceCacheStream which can be committed (using
 * spruce_cache_stream_commit()) to @cache if both @cache and @key are
 * non-%NULL.
 *
 * If @cache and/or @key are %NULL, then calls to
 * spruce_cache_stream_commit() will do nothing with the returned
 * stream.
 *
 * If @path is non-%NULL, calls to spruce_cache_stream_abort() will
 * queue an unlink() of the @path when the returned stream is
 * finalized.
 *
 * Returns a #SpruceCacheStream.
 **/
GMimeStream *
spruce_cache_stream_new (SpruceCache *cache, const char *key, const char *path, int fd, GError **err)
{
	struct _SpruceCacheStreamPrivate *priv;
	SpruceCacheStream *cstream;
	
	g_return_val_if_fail (path != NULL, NULL);
	g_return_val_if_fail (fd != -1, NULL);
	
	cstream = g_object_new (SPRUCE_TYPE_CACHE_STREAM, NULL);
	g_mime_stream_construct (GMIME_STREAM (cstream), 0, -1);
	priv = cstream->priv;
	
	if (cache && key) {
		priv->key = g_strdup (key);
		g_object_ref (cache);
		priv->cache = cache;
	} else {
		/* can't commit this stream */
		priv->state = CACHE_STATE_ABORTED;
	}
	
	priv->path = g_strdup (path);
	
	priv->backing = g_mime_stream_fs_new (fd);
	g_mime_stream_cat_add_source ((GMimeStreamCat *) cstream, priv->backing);
	
	priv->overflow = g_mime_stream_mem_new ();
	g_mime_stream_cat_add_source ((GMimeStreamCat *) cstream, priv->overflow);
	
	return (GMimeStream *) cstream;
}
