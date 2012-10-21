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


#ifndef __SPRUCE_POP_STREAM_H__
#define __SPRUCE_POP_STREAM_H__

#include <gmime/gmime-stream.h>

G_BEGIN_DECLS

#define SPRUCE_TYPE_POP_STREAM            (spruce_pop_stream_get_type ())
#define SPRUCE_POP_STREAM(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), SPRUCE_TYPE_POP_STREAM, SprucePOPStream))
#define SPRUCE_POP_STREAM_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), SPRUCE_TYPE_POP_STREAM, SprucePOPStreamClass))
#define SPRUCE_IS_POP_STREAM(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SPRUCE_TYPE_POP_STREAM))
#define SPRUCE_IS_POP_STREAM_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), SPRUCE_TYPE_POP_STREAM))
#define SPRUCE_POP_STREAM_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), SPRUCE_TYPE_POP_STREAM, SprucePOPStreamClass))

typedef struct _SprucePOPStream SprucePOPStream;
typedef struct _SprucePOPStreamClass SprucePOPStreamClass;

typedef enum {
	SPRUCE_POP_STREAM_LINE,
	SPRUCE_POP_STREAM_DATA,
} spruce_pop_stream_mode_t;

#define POP_SCAN_HEAD 128
#define POP_SCAN_BUF 4096

struct _SprucePOPStream {
	GMimeStream parent_object;
	
	GMimeStream *stream;
	
	spruce_pop_stream_mode_t mode;
	int disconnected:1;
	int midline:1;
	int eod:1;
	
	char realbuf[POP_SCAN_HEAD + POP_SCAN_BUF + 1];
	char *inbuf;
	char *inptr;
	char *inend;
};

struct _SprucePOPStreamClass {
	GMimeStreamClass parent_class;
	
};


GType spruce_pop_stream_get_type (void);

GMimeStream *spruce_pop_stream_new (GMimeStream *stream);

int spruce_pop_stream_line (SprucePOPStream *stream, char **line, size_t *len);
void spruce_pop_stream_set_mode (SprucePOPStream *stream, spruce_pop_stream_mode_t mode);
void spruce_pop_stream_unset_eod (SprucePOPStream *stream);

G_END_DECLS

#endif /* __SPRUCE_POP_STREAM_H__ */
