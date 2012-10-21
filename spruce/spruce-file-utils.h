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


#ifndef __SPRUCE_FILE_UTILS_H__
#define __SPRUCE_FILE_UTILS_H__

#include <glib.h>

#include <sys/types.h>
#include <time.h>

#include <gmime/gmime-stream.h>

G_BEGIN_DECLS

int spruce_file_util_encode_int8 (GMimeStream *out, gint8 value);
int spruce_file_util_decode_int8 (GMimeStream *in, gint8 *value);
int spruce_file_util_encode_uint8 (GMimeStream *out, guint8 value);
int spruce_file_util_decode_uint8 (GMimeStream *in, guint8 *value);
int spruce_file_util_encode_int16 (GMimeStream *out, gint16 value);
int spruce_file_util_decode_int16 (GMimeStream *in, gint16 *value);
int spruce_file_util_encode_uint16 (GMimeStream *out, guint16 value);
int spruce_file_util_decode_uint16 (GMimeStream *in, guint16 *value);
int spruce_file_util_encode_int32 (GMimeStream *out, gint32 value);
int spruce_file_util_decode_int32 (GMimeStream *in, gint32 *value);
int spruce_file_util_encode_uint32 (GMimeStream *out, guint32 value);
int spruce_file_util_decode_uint32 (GMimeStream *in, guint32 *value);
int spruce_file_util_encode_int64 (GMimeStream *out, gint64 value);
int spruce_file_util_decode_int64 (GMimeStream *in, gint64 *value);
int spruce_file_util_encode_uint64 (GMimeStream *out, guint64 value);
int spruce_file_util_decode_uint64 (GMimeStream *in, guint64 *value);
int spruce_file_util_encode_size_t (GMimeStream *out, size_t value);
int spruce_file_util_decode_size_t (GMimeStream *in, size_t *value);
int spruce_file_util_encode_time_t (GMimeStream *out, time_t value);
int spruce_file_util_decode_time_t (GMimeStream *in, time_t *value);
int spruce_file_util_encode_string (GMimeStream *out, const char *value);
int spruce_file_util_decode_string (GMimeStream *in, char **value);

ssize_t spruce_read (int fd, char *buf, size_t n);
ssize_t spruce_write (int fd, const char *buf, size_t n);

int spruce_mkdir (const char *path, mode_t mode);
int spruce_rmdir (const char *path);

G_END_DECLS

#endif /* __SPRUCE_FILE_UTILS_H__ */
