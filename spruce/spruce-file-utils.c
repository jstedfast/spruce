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
#include <sys/types.h>
#include <sys/poll.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include "spruce-file-utils.h"


int
spruce_file_util_encode_int8 (GMimeStream *out, gint8 value)
{
	if (g_mime_stream_write (out, (char *) &value, 1) == -1)
		return -1;
	
	return 0;
}


int
spruce_file_util_decode_int8 (GMimeStream *in, gint8 *value)
{
	if (g_mime_stream_read (in, (char *) value, 1) == -1)
		return -1;
	
	return 0;
}


int
spruce_file_util_encode_uint8 (GMimeStream *out, guint8 value)
{
	if (g_mime_stream_write (out, (char *) &value, 1) == -1)
		return -1;
	
	return 0;
}


int
spruce_file_util_decode_uint8 (GMimeStream *in, guint8 *value)
{
	if (g_mime_stream_read (in, (char *) value, 1) == -1)
		return -1;
	
	return 0;
}


int
spruce_file_util_encode_int16 (GMimeStream *out, gint16 value)
{
	gint16 save;
	
	save = GINT16_TO_BE (value);
	if (g_mime_stream_write (out, (char *) &save, 2) == -1)
		return -1;
	
	return 0;
}


int
spruce_file_util_decode_int16 (GMimeStream *in, gint16 *value)
{
	gint16 load;
	
	if (g_mime_stream_read (in, (char *) &load, 2) == -1)
		return -1;
	
	*value = GINT16_FROM_BE (load);
	
	return 0;
}


int
spruce_file_util_encode_uint16 (GMimeStream *out, guint16 value)
{
	guint16 save;
	
	save = GUINT16_TO_BE (value);
	if (g_mime_stream_write (out, (char *) &save, 2) == -1)
		return -1;
	
	return 0;
}


int
spruce_file_util_decode_uint16 (GMimeStream *in, guint16 *value)
{
	guint16 load;
	
	if (g_mime_stream_read (in, (char *) &load, 2) == -1)
		return -1;
	
	*value = GUINT16_FROM_BE (load);
	
	return 0;
}


int
spruce_file_util_encode_int32 (GMimeStream *out, gint32 value)
{
	gint32 save;
	
	save = GINT32_TO_BE (value);
	if (g_mime_stream_write (out, (char *) &save, 4) == -1)
		return -1;
	
	return 0;
}


int
spruce_file_util_decode_int32 (GMimeStream *in, gint32 *value)
{
	gint32 load;
	
	if (g_mime_stream_read (in, (char *) &load, 4) == -1)
		return -1;
	
	*value = GINT32_FROM_BE (load);
	
	return 0;
}


int
spruce_file_util_encode_uint32 (GMimeStream *out, guint32 value)
{
	guint32 save;
	
	save = GUINT32_TO_BE (value);
	if (g_mime_stream_write (out, (char *) &save, 4) == -1)
		return -1;
	
	return 0;
}


int
spruce_file_util_decode_uint32 (GMimeStream *in, guint32 *value)
{
	guint32 load;
	
	if (g_mime_stream_read (in, (char *) &load, 4) == -1)
		return -1;
	
	*value = GUINT32_FROM_BE (load);
	
	return 0;
}


int
spruce_file_util_encode_int64 (GMimeStream *out, gint64 value)
{
	gint64 save;
	
	save = GINT64_TO_BE (value);
	if (g_mime_stream_write (out, (char *) &save, 4) == -1)
		return -1;
	
	return 0;
}


int
spruce_file_util_decode_int64 (GMimeStream *in, gint64 *value)
{
	gint64 load;
	
	if (g_mime_stream_read (in, (char *) &load, 4) == -1)
		return -1;
	
	*value = GINT64_FROM_BE (load);
	
	return 0;
}


int
spruce_file_util_encode_uint64 (GMimeStream *out, guint64 value)
{
	guint64 save;
	
	save = GUINT64_TO_BE (value);
	if (g_mime_stream_write (out, (char *) &save, 4) == -1)
		return -1;
	
	return 0;
}


int
spruce_file_util_decode_uint64 (GMimeStream *in, guint64 *value)
{
	guint64 load;
	
	if (g_mime_stream_read (in, (char *) &load, 4) == -1)
		return -1;
	
	*value = GUINT64_FROM_BE (load);
	
	return 0;
}


int
spruce_file_util_encode_size_t (GMimeStream *out, size_t value)
{
	return spruce_file_util_encode_uint32 (out, (guint32) value);
}


int
spruce_file_util_decode_size_t (GMimeStream *in, size_t *value)
{
	guint32 load;
	
	if (spruce_file_util_decode_uint32 (in, &load) == -1)
		return -1;
	
	*value = (size_t) load;
	
	return 0;
}


int
spruce_file_util_encode_time_t (GMimeStream *out, time_t value)
{
	return spruce_file_util_encode_uint32 (out, (guint32) value);
}


int
spruce_file_util_decode_time_t (GMimeStream *in, time_t *value)
{
	guint32 load;
	
	if (spruce_file_util_decode_uint32 (in, &load) == -1)
		return -1;
	
	*value = (time_t) load;
	
	return 0;
}


int
spruce_file_util_encode_string (GMimeStream *out, const char *value)
{
	size_t len;
	
	/* FIXME: should we treat NULL and "" differently? */
	
	if (value == NULL || *value == '\0')
		return spruce_file_util_encode_size_t (out, 0);
	
	len = strlen (value);
	if (spruce_file_util_encode_size_t (out, len) == -1)
		return -1;
	
	if (g_mime_stream_write (out, value, len) == -1)
		return -1;
	
	return 0;
}


int
spruce_file_util_decode_string (GMimeStream *in, char **value)
{
	size_t len;
	char *buf;
	
	if (spruce_file_util_decode_size_t (in, &len) == -1) {
		*value = NULL;
		return -1;
	}
	
	if (!(buf = g_try_malloc (len + 1)))
		return -1;
	
	if (len > 0 && g_mime_stream_read (in, buf, len) == -1) {
		*value = NULL;
		g_free (buf);
		return -1;
	}
	
	buf[len] = '\0';
	*value = buf;
	
	return 0;
}


ssize_t
spruce_read (int fd, char *buf, size_t n)
{
	ssize_t nread;
	int cancel_fd;
	
#if 0
	if (spruce_operation_cancel_check (NULL)) {
		errno = EINTR;
		return -1;
	}
	
	cancel_fd = spruce_operation_cancel_fd (NULL);
#else
	cancel_fd = -1;
#endif
	
	if (cancel_fd == -1) {
		do {
			nread = read (fd, buf, n);
		} while (nread == -1 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK));
	} else {
		struct pollfd ufds[2];
		int errnosav, flags;
		
		flags = fcntl (fd, F_GETFL);
		fcntl (fd, F_SETFL, flags | O_NONBLOCK);
		
		ufds[0].fd = fd;
		ufds[0].events = POLLIN;
		
		ufds[1].fd = cancel_fd;
		ufds[1].events = POLLIN;
		
		do {
			ufds[0].revents = 0;
			ufds[1].revents = 0;
			
			if (poll (ufds, 2, -1) != -1) {
				if (ufds[1].revents != 0) {
					/* user cancelled */
					errno = EINTR;
					nread = -1;
					break;
				}
				
				if (ufds[0].revents & POLLIN) {
					do {
						nread = read (fd, buf, n);
					} while (nread == -1 && errno == EINTR);
				} else {
					nread = -1;
				}
			} else {
				if (errno == EINTR)
					errno = EAGAIN;
				
				nread = -1;
			}
		} while (nread == -1 && (errno == EAGAIN || errno == EWOULDBLOCK));
		
		errnosav = errno;
		fcntl (fd, F_SETFL, flags);
		errno = errnosav;
	}
	
	return nread;
}


ssize_t
spruce_write (int fd, const char *buf, size_t n)
{
	ssize_t w, written = 0;
	int cancel_fd;
	
#if 0
	if (spruce_operation_cancel_check (NULL)) {
		errno = EINTR;
		return -1;
	}
	
	cancel_fd = spruce_operation_cancel_fd (NULL);
#else
	cancel_fd = -1;
#endif
	
	if (cancel_fd == -1) {
		do {
			do {
				w = write (fd, buf + written, n - written);
			} while (w == -1 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK));
			
			if (w > 0)
				written += w;
		} while (w != -1 && written < n);
	} else {
		struct pollfd ufds[2];
		int errnosav, flags;
		
		flags = fcntl (fd, F_GETFL);
		fcntl (fd, F_SETFL, flags | O_NONBLOCK);
		
		ufds[0].fd = fd;
		ufds[0].events = POLLOUT;
		
		ufds[1].fd = cancel_fd;
		ufds[1].events = POLLIN;
		
		do {
			ufds[0].revents = 0;
			ufds[1].revents = 0;
			
			if (poll (ufds, 2, -1) != -1) {
				if (ufds[1].revents != 0) {
					/* user cancelled */
					errno = EINTR;
					w = -1;
					break;
				}
				
				do {
					w = write (fd, buf + written, n - written);
				} while (w == -1 && errno == EINTR);
				
				if (w == -1) {
					if (errno == EAGAIN || errno == EWOULDBLOCK) {
						/* try again */
						w = 0;
					}
				} else
					written += w;
			} else if (errno == EINTR) {
				w = 0;
			} else {
				/* irrecoverable poll() error */
				w = -1;
			}
		} while (w != -1 && written < n);
		
		errnosav = errno;
		fcntl (fd, F_SETFL, flags);
		errno = errnosav;
	}
	
	if (w == -1)
		return -1;
	
	return written;
}


int
spruce_mkdir (const char *path, mode_t mode)
{
	return g_mkdir_with_parents (path, mode);
}


static int
spruce_rmdir_real (GString *path)
{
	struct dirent *dent;
	struct stat st;
	size_t len;
	DIR *dir;
	
	if (!(dir = opendir (path->str)))
		return -1;
	
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
			spruce_rmdir_real (path);
		else
			unlink (path->str);
	}
	
	closedir (dir);
	
	g_string_truncate (path, len - 1);
	
	return rmdir (path->str);
}

int
spruce_rmdir (const char *path)
{
	GString *full_path;
	int rv;
	
	full_path = g_string_new (path);
	rv = spruce_rmdir_real (full_path);
	g_string_free (full_path, TRUE);
	
	return rv;
}
