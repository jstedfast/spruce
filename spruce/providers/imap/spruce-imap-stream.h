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


#ifndef __SPRUCE_IMAP_STREAM_H__
#define __SPRUCE_IMAP_STREAM_H__

#include <gmime/gmime-stream.h>

G_BEGIN_DECLS

#define SPRUCE_TYPE_IMAP_STREAM     (spruce_imap_stream_get_type ())
#define SPRUCE_IMAP_STREAM(obj)     (G_TYPE_CHECK_INSTANCE_CAST ((obj), SPRUCE_TYPE_IMAP_STREAM, SpruceIMAPStream))
#define SPRUCE_IMAP_STREAM_CLASS(k) (G_TYPE_CHECK_CLASS_CAST ((k), SPRUCE_TYPE_IMAP_STREAM, SpruceIMAPStreamClass))
#define SPRUCE_IS_IMAP_STREAM(o)    (G_TYPE_CHECK_INSTANCE_TYPE ((o), SPRUCE_TYPE_IMAP_STREAM))

typedef struct _SpruceIMAPStream SpruceIMAPStream;
typedef struct _SpruceIMAPStreamClass SpruceIMAPStreamClass;

#define IMAP_READ_PRELEN   128
#define IMAP_READ_BUFLEN   4096

enum {
	SPRUCE_IMAP_TOKEN_NO_DATA       = -9,
	SPRUCE_IMAP_TOKEN_ERROR         = -8,
	SPRUCE_IMAP_TOKEN_NIL           = -7,
	SPRUCE_IMAP_TOKEN_ATOM          = -6,
	SPRUCE_IMAP_TOKEN_FLAG          = -5,
	SPRUCE_IMAP_TOKEN_NUMBER        = -4,
	SPRUCE_IMAP_TOKEN_NUMBER64      = -3,
	SPRUCE_IMAP_TOKEN_QSTRING       = -2,
	SPRUCE_IMAP_TOKEN_LITERAL       = -1,
	/* SPRUCE_IMAP_TOKEN_CHAR would just be the char we got */
	SPRUCE_IMAP_TOKEN_EOLN          = '\n',
	SPRUCE_IMAP_TOKEN_LPAREN        = '(',
	SPRUCE_IMAP_TOKEN_RPAREN        = ')',
	SPRUCE_IMAP_TOKEN_ASTERISK      = '*',
	SPRUCE_IMAP_TOKEN_PLUS          = '+',
	SPRUCE_IMAP_TOKEN_LBRACKET      = '[',
	SPRUCE_IMAP_TOKEN_RBRACKET      = ']',
};

typedef struct _spruce_imap_token_t {
	int token;
	union {
		char *atom;
		char *flag;
		char *qstring;
		size_t literal;
		guint32 number;
		guint64 number64;
	} v;
} spruce_imap_token_t;

enum {
	SPRUCE_IMAP_STREAM_MODE_TOKEN   = 0,
	SPRUCE_IMAP_STREAM_MODE_LITERAL = 1,
};

struct _SpruceIMAPStream {
	GMimeStream parent_object;
	
	GMimeStream *stream;
	
	guint disconnected:1;  /* disconnected state */
	guint have_unget:1;    /* we have an unget token */
	guint mode:1;          /* TOKEN vs LITERAL */
	guint eol:1;           /* end-of-literal */
	
	size_t literal;
	
	/* i/o buffers */
	unsigned char realbuf[IMAP_READ_PRELEN + IMAP_READ_BUFLEN + 1];
	unsigned char *inbuf;
	unsigned char *inptr;
	unsigned char *inend;
	
	/* token buffers */
	unsigned char *tokenbuf;
	unsigned char *tokenptr;
	unsigned int tokenleft;
	
	spruce_imap_token_t unget;
};

struct _SpruceIMAPStreamClass {
	GMimeStreamClass parent_class;
	
	/* Virtual methods */
};


/* Standard Spruce function */
GType spruce_imap_stream_get_type (void);

GMimeStream *spruce_imap_stream_new (GMimeStream *stream);

int spruce_imap_stream_next_token (SpruceIMAPStream *stream, spruce_imap_token_t *token);
int spruce_imap_stream_unget_token (SpruceIMAPStream *stream, spruce_imap_token_t *token);

int spruce_imap_stream_line (SpruceIMAPStream *stream, unsigned char **line, size_t *len);
int spruce_imap_stream_literal (SpruceIMAPStream *stream, unsigned char **literal, size_t *len);

G_END_DECLS

#endif /* __SPRUCE_IMAP_STREAM_H__ */
