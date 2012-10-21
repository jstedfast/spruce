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

#include <spruce/spruce-error.h>
#include <spruce/spruce-cache.h>
#include <spruce/spruce-file-utils.h>
#include <spruce/spruce-cache-stream.h>


/* FIXME: if, at _new, we crawled the cache collecting size/atime
 * info, then whenever a new stream gets added to the cache, we could
 * (after stat'ing the streams created this session for size info)
 * enforce the cache size in "real time" without the need for our
 * consumer needing to periodically call
 * spruce_stream_cache_expire()... however, this may be seen as
 * unnecessary overhead? Perhaps it would be acceptable to just expire
 * the cache at _finalize() time? */


#define IS_HEX_DIGIT(x) (((x) >= '0' && (x) <= '9') || ((x) >= 'a' && (x) <= 'f'))


static void spruce_cache_class_init (SpruceCacheClass *klass);
static void spruce_cache_init (SpruceCache *cache, SpruceCacheClass *klass);
static void spruce_cache_finalize (GObject *object);

static GObjectClass *parent_class = NULL;


GType
spruce_cache_get_type (void)
{
	static GType type = 0;
	
	if (!type) {
		static const GTypeInfo info = {
			sizeof (SpruceCacheClass),
			NULL, /* base_class_init */
			NULL, /* base_class_finalize */
			(GClassInitFunc) spruce_cache_class_init,
			NULL, /* class_finalize */
			NULL, /* class_data */
			sizeof (SpruceCache),
			0,    /* n_preallocs */
			(GInstanceInitFunc) spruce_cache_init,
		};
		
		type = g_type_register_static (G_TYPE_OBJECT, "SpruceCache", &info, 0);
	}
	
	return type;
}


static void
spruce_cache_class_init (SpruceCacheClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	
	parent_class = g_type_class_ref (G_TYPE_OBJECT);
	
	object_class->finalize = spruce_cache_finalize;
}

static void
spruce_cache_init (SpruceCache *cache, SpruceCacheClass *klass)
{
	cache->cache_size = 0;
	cache->basedir = NULL;
}

static void
cache_clear_tmp (SpruceCache *cache)
{
	struct dirent *dent;
	struct stat st;
	GString *path;
	size_t len;
	DIR *dir;
	
	path = g_string_new (cache->basedir);
	g_string_append_c (path, G_DIR_SEPARATOR);
	g_string_append (path, "tmp");
	
	if (!(dir = opendir (path->str))) {
		g_string_free (path, TRUE);
		return;
	}
	
	g_string_append_c (path, G_DIR_SEPARATOR);
	len = path->len;
	
	while ((dent = readdir (dir))) {
		if (!strcmp (dent->d_name, ".") || !strcmp (dent->d_name, ".."))
			continue;
		
		g_string_truncate (path, len);
		g_string_append (path, dent->d_name);
		
		if (lstat (path->str, &st) == -1)
			continue;
		
		if (S_ISDIR (st.st_mode))
			spruce_rmdir (path->str);
		else
			unlink (path->str);
	}
	
	g_string_free (path, TRUE);
	closedir (dir);
}

static void
spruce_cache_finalize (GObject *object)
{
	SpruceCache *cache = (SpruceCache *) object;
	
	spruce_cache_expire (cache, NULL);
	cache_clear_tmp (cache);
	g_free (cache->basedir);
	
	G_OBJECT_CLASS (parent_class)->finalize (object);
}



SpruceCache *
spruce_cache_new (const char *basedir, guint64 cache_size)
{
	SpruceCache *cache;
	
	g_return_val_if_fail (basedir != NULL, NULL);
	
	cache = g_object_new (SPRUCE_TYPE_CACHE, NULL);
	cache->basedir = g_strdup (basedir);
	cache->cache_size = cache_size;
	
	cache_clear_tmp (cache);
	
	return cache;
}


static char *
cache_path (SpruceCache *cache, const char *key, int create)
{
	char hashstr[16], *dir, *path;
	guint32 hash;
	
	hash = g_str_hash (key) & ((1 << 6) - 1);
	snprintf (hashstr, sizeof (hashstr), "%02x", hash);
	dir = g_build_filename (cache->basedir, hashstr, NULL);
	
	if (create)
		spruce_mkdir (dir, 0777);
	
	path = g_build_filename (dir, key, NULL);
	g_free (dir);
	
	return path;
}


/**
 * spruce_cache_add:
 * @cache: a #SpruceCache object
 * @key: stream key
 * @err: a #GError
 *
 * Creates a new #GMimeStream in the cache, referenced by @key.
 *
 * Note: stream might not always be backed by the disk if a filesystem
 * error occurs.
 *
 * Returns a #GMimeStream or %NULL on error.
 **/
GMimeStream *
spruce_cache_add (SpruceCache *cache, const char *key, GError **err)
{
	GMimeStream *stream;
	char *path, *tmp;
	int fd;
	
	tmp = g_build_filename (cache->basedir, "tmp", NULL);
	path = g_build_filename (tmp, key, NULL);
	spruce_mkdir (tmp, 0777);
	g_free (tmp);
	
	if ((fd = open (path, O_CREAT | O_EXCL | O_LARGEFILE | O_RDWR, 0666)) == -1) {
		if (errno == EEXIST) {
			g_set_error (err, SPRUCE_ERROR, errno,
				     _("Cannot create item `%s' in cache: %s."),
				     key, g_strerror (errno));
			g_free (path);
			return NULL;
		}
		
		g_free (path);
		
		if (!(stream = spruce_cache_tmp_stream ())) {
			g_set_error (err, SPRUCE_ERROR, errno,
				     _("Cannot create item `%s' in cache: %s."),
				     key, g_strerror (errno));
			return NULL;
		}
	}
	
	stream = spruce_cache_stream_new (cache, key, path, fd, err);
	
	g_free (path);
	
	return stream;
}


/**
 * spruce_cache_get:
 * @cache: a #SpruceCache object
 * @key: stream key
 * @err: a #GError
 *
 * Gets the #GMimeStream referenced by @key.
 *
 * Returns a read-only #GMimeStream or %NULL on error.
 **/
GMimeStream *
spruce_cache_get (SpruceCache *cache, const char *key, GError **err)
{
	char *path;
	int fd;
	
	g_return_val_if_fail (SPRUCE_IS_CACHE (cache), NULL);
	g_return_val_if_fail (key != NULL, NULL);
	
	path = cache_path (cache, key, FALSE);
	fd = open (path, O_RDONLY);
	g_free (path);
	
	if (fd == -1) {
		g_set_error (err, SPRUCE_ERROR, errno,
			     _("Cannot get cached item `%s': %s."),
			     key, g_strerror (errno));
		return NULL;
	}
	
	return g_mime_stream_fs_new (fd);
}


/**
 * spruce_cache_commit:
 * @cache: a #SpruceCache object
 * @key: key of the stream to commit
 * @err: a #GError
 *
 * Commits the stream referenced by @key to the cache.
 *
 * Returns %0 on success or %-1 on fail.
 **/
int
spruce_cache_commit (SpruceCache *cache, const char *key, GError **err)
{
	char *path, *tmp;
	int rv;
	
	g_return_val_if_fail (SPRUCE_IS_CACHE (cache), -1);
	g_return_val_if_fail (key != NULL, -1);
	
	tmp = g_build_filename (cache->basedir, "tmp", key, NULL);
	path = cache_path (cache, key, TRUE);
	
	if ((rv = rename (tmp, path)) == -1) {
		g_set_error (err, SPRUCE_ERROR, errno,
			     _("Cannot commit item `%s' to cache: %s."),
			     key, g_strerror (errno));
		unlink (tmp);
	}
	
	g_free (path);
	g_free (tmp);
	
	return rv;
}


/**
 * spruce_cache_rekey:
 * @cache: a #SpruceCache object
 * @key: stream key
 * @new_key: new stream key
 * @err: a #GError
 *
 * Re-key's a cached item from @key to @new_key. If successful, the
 * stream must be requested using @new_key as @key will no longer
 * reference the stream.
 *
 * Returns %0 on success or %-1 on error.
 **/
int
spruce_cache_rekey (SpruceCache *cache, const char *key, const char *new_key, GError **err)
{
	char *oldpath, *newpath, *realpath = NULL;
	struct stat st;
	
	g_return_val_if_fail (SPRUCE_IS_CACHE (cache), -1);
	g_return_val_if_fail (new_key != NULL, -1);
	g_return_val_if_fail (key != NULL, -1);
	
	oldpath = cache_path (cache, key, FALSE);
	
	/* Note: this is a quick check to see if the old file even
	 * exists... if not, no sense getting the new key (which will
	 * `mkdir -p` the cache dir if it doesn't already exist). We
	 * also later use @st to check if this is a symlink. */
	if (lstat (oldpath, &st) == -1) {
		g_set_error (err, SPRUCE_ERROR, errno,
			     _("Cannot rekey cached item `%s': %s."),
			     key, g_strerror (errno));
		g_free (oldpath);
		return -1;
	}
	
	/* Note: if oldpath is a symlink, we want to link back to the
	 * original path, not the symlink. */
	if (S_ISLNK (st.st_mode) && !(realpath = g_file_read_link (oldpath, err))) {
		g_free (oldpath);
		return -1;
	}
	
	newpath = cache_path (cache, new_key, TRUE);
	
	/* we cannot simply use rename(2) here, because we don't want
	 * to overwrite newpath if it already exists... we want to
	 * error out in that case. Preserve symlinkness. */
	if ((realpath ? symlink (realpath, newpath) : link (oldpath, newpath)) == -1) {
		g_set_error (err, SPRUCE_ERROR, errno,
			     _("Cannot rekey cached item `%s': %s."),
			     key, g_strerror (errno));
		
		g_free (realpath);
		g_free (oldpath);
		g_free (newpath);
		
		return -1;
	}
	
	unlink (oldpath);
	
	g_free (realpath);
	g_free (oldpath);
	g_free (newpath);
	
	return 0;
}


struct CacheInfo {
	char *filename;
	time_t atime;
	size_t size;
};

static guint64
cache_stat (const char *dirname, GPtrArray *stats)
{
	struct CacheInfo *info;
	struct dirent *dent;
	guint64 size = 0;
	char *filename;
	struct stat st;
	DIR *dir;
	
	if (!(dir = opendir (dirname)))
		return 0;
	
	while ((dent = readdir (dir))) {
		if (!strcmp (dent->d_name, ".") || !strcmp (dent->d_name, ".."))
			continue;
		
		filename = g_build_filename (dirname, dent->d_name, NULL);
		
		/* Note: we ignore symlinks, allowing the user to
		 * manually bypass expiratory rules... useful if, say,
		 * a particular cached item is particularly large and
		 * the user wants to avoid having the client ever have
		 * to re-fetch the item. */
		
		if (lstat (filename, &st) == 0 && !S_ISLNK (st.st_mode)) {
			info = g_new (struct CacheInfo, 1);
			g_ptr_array_add (stats, info);
			info->filename = filename;
			info->atime = st.st_atime;
			info->size = st.st_size;
			size += st.st_size;
		} else
			g_free (filename);
	}
	
	closedir (dir);
	
	return size;
}

static int
cache_info_cmp (const void *v1, const void *v2)
{
	struct CacheInfo *info1 = *((struct CacheInfo **) v1);
	struct CacheInfo *info2 = *((struct CacheInfo **) v2);
	
	return info1->atime - info2->atime;
}


/**
 * spruce_cache_expire:
 * @cache: a #SpruceCache object
 * @err: a #GError
 *
 * Expires old streams in the cache if the cache size exceeds the size
 * limit.
 *
 * Returns %0 on success or %-1 on fail.
 **/
int
spruce_cache_expire (SpruceCache *cache, GError **err)
{
	GString *path, *files = NULL;
	struct CacheInfo *info;
	struct dirent *dent;
	GPtrArray *stats;
	guint64 size = 0;
	struct stat st;
	DIR *dir;
	int i;
	
	g_return_val_if_fail (SPRUCE_IS_CACHE (cache), -1);
	
	if (!(dir = opendir (cache->basedir))) {
		if (errno == ENOENT)
			return 0;
		
		g_set_error (err, SPRUCE_ERROR, errno,
			     _("Cannot open directory `%s': %s."),
			     cache->basedir, g_strerror (errno));
		
		return -1;
	}
	
	path = g_string_new (cache->basedir);
	g_string_append_c (path, G_DIR_SEPARATOR);
	g_string_append_len (path, "ff", 2);
	
	stats = g_ptr_array_new ();
	
	while ((dent = readdir (dir))) {
		/* Note: ignore files/directories that aren't a hash dir. */
		if (!(IS_HEX_DIGIT (dent->d_name[0]) &&
		      IS_HEX_DIGIT (dent->d_name[1]) &&
		      dent->d_name[2] == '\0'))
			continue;
		
		strcpy (path->str + path->len - 3, dent->d_name);
		
		/* Note: by not following symlinks, we allow the user
		 * to manually prevent blocks of the cache from being
		 * expired. */
		if (lstat (path->str, &st) == -1)
			continue;
		
		if (S_ISDIR (st.st_mode))
			size += cache_stat (path->str, stats);
	}
	
	g_string_free (path, TRUE);
	closedir (dir);
	
	if (size > cache->cache_size) {
		/* sort our cached files by access time, oldest first */
		qsort (stats->pdata, stats->len, sizeof (void *), cache_info_cmp);
		
		for (i = 0; i < stats->len && size > cache->cache_size; i++) {
			info = stats->pdata[i];
			if (unlink (info->filename) == -1 && errno != ENOENT) {
				if (files == NULL)
					files = g_string_new ("");
				g_string_append_c (files, '\n');
				g_string_append (files, info->filename);
			}
			
			if (info->size < size)
				size -= info->size;
			else
				size = 0;
		}
	}
	
	for (i = 0; i < stats->len; i++) {
		info = stats->pdata[i];
		g_free (info->filename);
		g_free (info);
	}
	
	g_ptr_array_free (stats, TRUE);
	
	if (files != NULL) {
		g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_GENERIC,
			     _("Could not delete these files from the cache: %s"),
			     files->str);
		g_string_free (files, TRUE);
	}
	
	return files == NULL ? 0 : -1;
}


static int
uncache_all (GString *path, GError **err)
{
	struct dirent *dent;
	int errnosav = 0;
	int nerrors = 0;
	struct stat st;
	size_t len;
	DIR *dir;
	
	if (!(dir = opendir (path->str))) {
		if (errno == ENOENT)
			return 0;
		
		g_set_error (err, SPRUCE_ERROR, errno,
			     _("Cannot uncache all items in `%s': %s."),
			     path->str, g_strerror (errno));
		
		return -1;
	}
	
	g_string_append_c (path, G_DIR_SEPARATOR);
	len = path->len;
	
	while ((dent = readdir (dir))) {
		if (!strcmp (dent->d_name, ".") ||
		    !strcmp (dent->d_name, "..") ||
		    !strcmp (dent->d_name, "tmp"))
			continue;
		
		g_string_truncate (path, len);
		g_string_append (path, dent->d_name);
		if (lstat (path->str, &st) == -1)
			continue;
		
		if (S_ISDIR (st.st_mode)) {
			uncache_all (path, NULL);
		} else if (unlink (path->str) == -1 && errno != ENOENT) {
			if (nerrors == 0) {
				errnosav = errno;
				g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_GENERIC,
					     _("Cannot uncache `%s': %s."),
					     path->str, g_strerror (errno));
			} else if (errnosav != errno)
				errnosav = -1;
			nerrors++;
		}
	}
	
	g_string_truncate (path, len - 1);
	
	closedir (dir);
	
	if (nerrors > 1 && err != NULL) {
		g_clear_error (err);
		if (errnosav == -1) {
			g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_GENERIC,
				     _("Cannot uncache %d items in `%s' for various reasons."),
				     nerrors, path->str);
		} else {
			g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_GENERIC,
				     _("Cannot uncache %d items in `%s': unlink(2) failed: %s."),
				     nerrors, path->str, g_strerror (errnosav));
		}
	}
	
	return nerrors > 0 ? -1 : 0;
}


/**
 * spruce_cache_expire_all:
 * @cache: a #SpruceCache object
 * @err: a #GError
 *
 * Expires all streams.
 *
 * Returns %0 on success or %-1 on fail.
 **/
int
spruce_cache_expire_all (SpruceCache *cache, GError **err)
{
	GString *path;
	int rv;
	
	g_return_val_if_fail (SPRUCE_IS_CACHE (cache), -1);
	
	path = g_string_new (cache->basedir);
	rv = uncache_all (path, err);
	g_string_free (path, TRUE);
	
	return rv;
}


/**
 * spruce_cache_expire_key:
 * @cache: a #SpruceCache object
 * @key: a stream key
 * @err: a #GError
 *
 * Expires the stream referenced by @key.
 *
 * Returns %0 on success or %-1 on fail.
 **/
int
spruce_cache_expire_key (SpruceCache *cache, const char *key, GError **err)
{
	char *path;
	int rv;
	
	g_return_val_if_fail (SPRUCE_IS_CACHE (cache), -1);
	g_return_val_if_fail (key != NULL, -1);
	
	path = cache_path (cache, key, FALSE);
	rv = unlink (path);
	g_free (path);
	
	if (rv == -1 && errno != ENOENT)
		g_set_error (err, SPRUCE_ERROR, errno,
			     _("Cannot uncache item `%s': %s."),
			     key, g_strerror (errno));
	else
		rv = 0;
	
	return rv;
}


/**
 * spruce_cache_delete:
 * @cache: a #SpruceCache object
 * @err: a #GError
 *
 * Deletes a cache.
 *
 * Returns %0 on success or %-1 on fail.
 **/
int
spruce_cache_delete (SpruceCache *cache, GError **err)
{
	g_return_val_if_fail (SPRUCE_IS_CACHE (cache), -1);
	
	if (spruce_rmdir (cache->basedir) == -1) {
		g_set_error (err, SPRUCE_ERROR, errno,
			     _("Cannot delete cache `%s': %s."),
			     cache->basedir, g_strerror (errno));
		return -1;
	}
	
	return 0;
}


/**
 * spruce_cache_rename:
 * @cache: a #SpruceCache object
 * @new_path: new base path for the cache
 * @err: a #GError
 *
 * Moves the cache to a new location on the file system.
 *
 * Returns %0 on success or %-1 on fail.
 **/
int
spruce_cache_rename (SpruceCache *cache, const char *new_path, GError **err)
{
	g_return_val_if_fail (SPRUCE_IS_CACHE (cache), -1);
	g_return_val_if_fail (new_path != NULL, -1);
	
	if (rename (cache->basedir, new_path) == -1) {
		g_set_error (err, SPRUCE_ERROR, errno,
			     _("Cannot rename cache `%s' to `%s': %s."),
			     cache->basedir, new_path, g_strerror (errno));
		return -1;
	}
	
	g_free (cache->basedir);
	cache->basedir = g_strdup (new_path);
	
	return 0;
}


/**
 * spruce_cache_tmp_stream:
 *
 * Creates a temporary stream, attempting to have it be a disk-backed
 * stream located in the user's preferred tmp directory. Failing that,
 * a memory stream is created instead. When the stream is destroyed,
 * it will be automatically unlinked from the filesystem.
 *
 * Returns a #GMimestream.
 **/
GMimeStream *
spruce_cache_tmp_stream (void)
{
	GMimeStream *stream;
	char *path;
	int fd;
	
	if ((fd = g_file_open_tmp (NULL, &path, NULL)) == -1)
		return NULL;
	
	stream = spruce_cache_stream_new (NULL, NULL, path, fd, NULL);
	
	g_free (path);
	
	return stream;
}
