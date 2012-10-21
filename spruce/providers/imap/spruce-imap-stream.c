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
#include <limits.h>
#include <errno.h>
#include <ctype.h>

#include "spruce-imap-specials.h"

#include "spruce-imap-stream.h"

#define d(x) x

#ifndef ULLONG_MAX
#define ULLONG_MAX G_MAXUINT64
#endif

#define ustrchr(s,c) (unsigned char *) strchr ((char *) (s), (c))
#define ustrcmp(s1,s2) strcmp ((char *) (s1), (char *) (s2))

#define IMAP_TOKEN_LEN  128

static void spruce_imap_stream_class_init (SpruceIMAPStreamClass *klass);
static void spruce_imap_stream_init (SpruceIMAPStream *stream, SpruceIMAPStreamClass *klass);
static void spruce_imap_stream_finalize (GObject *object);

static ssize_t stream_read (GMimeStream *stream, char *buffer, size_t n);
static ssize_t stream_write (GMimeStream *stream, const char *buffer, size_t n);
static int stream_flush  (GMimeStream *stream);
static int stream_close  (GMimeStream *stream);
static gboolean stream_eos (GMimeStream *stream);


static GMimeStreamClass *parent_class = NULL;


GType
spruce_imap_stream_get_type (void)
{
	static GType type = 0;
	
	if (!type) {
		static const GTypeInfo info = {
			sizeof (SpruceIMAPStreamClass),
			NULL, /* base_class_init */
			NULL, /* base_class_finalize */
			(GClassInitFunc) spruce_imap_stream_class_init,
			NULL, /* class_finalize */
			NULL, /* class_data */
			sizeof (SpruceIMAPStream),
			0,    /* n_preallocs */
			(GInstanceInitFunc) spruce_imap_stream_init,
		};
		
		type = g_type_register_static (GMIME_TYPE_STREAM, "SpruceIMAPStream", &info, 0);
	}
	
	return type;
}

static void
spruce_imap_stream_class_init (SpruceIMAPStreamClass *klass)
{
	GMimeStreamClass *stream_class = GMIME_STREAM_CLASS (klass);
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	
	parent_class = g_type_class_ref (GMIME_TYPE_STREAM);
	
	object_class->finalize = spruce_imap_stream_finalize;
	
	/* virtual method overload */
	stream_class->read = stream_read;
	stream_class->write = stream_write;
	stream_class->flush = stream_flush;
	stream_class->close = stream_close;
	stream_class->eos = stream_eos;
}

static void
spruce_imap_stream_init (SpruceIMAPStream *imap, SpruceIMAPStreamClass *klass)
{
	imap->stream = NULL;
	
	imap->mode = SPRUCE_IMAP_STREAM_MODE_TOKEN;
	imap->disconnected = FALSE;
	imap->have_unget = FALSE;
	imap->eol = FALSE;
	
	imap->literal = 0;
	
	imap->inbuf = imap->realbuf + IMAP_READ_PRELEN;
	imap->inptr = imap->inbuf;
	imap->inend = imap->inbuf;
	
	imap->tokenbuf = g_malloc (IMAP_TOKEN_LEN);
	imap->tokenptr = imap->tokenbuf;
	imap->tokenleft = IMAP_TOKEN_LEN;
	
	((GMimeStream *) imap)->bound_end = -1;
}

static void
spruce_imap_stream_finalize (GObject *object)
{
	SpruceIMAPStream *imap = (SpruceIMAPStream *) object;
	
	if (imap->stream)
		g_object_unref (imap->stream);
	
	g_free (imap->tokenbuf);
	
	G_OBJECT_CLASS (parent_class)->finalize (object);
}


static ssize_t
imap_fill (SpruceIMAPStream *imap)
{
	unsigned char *inbuf, *inptr, *inend;
	ssize_t nread;
	size_t inlen;
	
	if (imap->disconnected) {
		errno = EINVAL;
		return -1;
	}
	
	inbuf = imap->inbuf;
	inptr = imap->inptr;
	inend = imap->inend;
	inlen = inend - inptr;
	
	g_assert (inptr <= inend);
	
	/* attempt to align 'inend' with realbuf + SCAN_HEAD */
	if (inptr >= inbuf) {
		inbuf -= inlen < IMAP_READ_PRELEN ? inlen : IMAP_READ_PRELEN;
		memmove (inbuf, inptr, inlen);
		inptr = inbuf;
		inbuf += inlen;
	} else if (inptr > imap->realbuf) {
		size_t shift;
		
		shift = MIN (inptr - imap->realbuf, inend - inbuf);
		memmove (inptr - shift, inptr, inlen);
		inptr -= shift;
		inbuf = inptr + inlen;
	} else {
		/* we can't shift... */
		inbuf = inend;
	}
	
	imap->inptr = inptr;
	imap->inend = inbuf;
	inend = imap->realbuf + IMAP_READ_PRELEN + IMAP_READ_BUFLEN - 1;
	
	if ((nread = g_mime_stream_read (imap->stream, (char *) inbuf, inend - inbuf)) == -1)
		return -1;
	else if (nread == 0)
		imap->disconnected = TRUE;
	
	imap->inend += nread;
	
	return imap->inend - imap->inptr;
}

static ssize_t
stream_read (GMimeStream *stream, char *buffer, size_t n)
{
	SpruceIMAPStream *imap = (SpruceIMAPStream *) stream;
	ssize_t len, nread = 0;
	
	if (imap->mode == SPRUCE_IMAP_STREAM_MODE_LITERAL) {
		/* don't let our caller read past the end of the literal */
		n = MIN (n, imap->literal);
	}
	
	if (imap->inptr < imap->inend) {
		len = MIN (n, imap->inend - imap->inptr);
		memcpy (buffer, imap->inptr, len);
		imap->inptr += len;
		nread = len;
	}
	
	if (nread < n) {
		if ((len = g_mime_stream_read (imap->stream, buffer + nread, n - nread)) == 0)
			imap->disconnected = TRUE;
		else if (len == -1)
			return -1;
		
		nread += len;
	}
	
	if (imap->mode == SPRUCE_IMAP_STREAM_MODE_LITERAL) {
		imap->literal -= nread;
		
		if (imap->literal == 0) {
			imap->mode = SPRUCE_IMAP_STREAM_MODE_TOKEN;
			imap->eol = TRUE;
		}
	}
	
	return nread;
}

static ssize_t
stream_write (GMimeStream *stream, const char *buffer, size_t n)
{
	SpruceIMAPStream *imap = (SpruceIMAPStream *) stream;
	ssize_t nwritten;
	
	if (imap->disconnected) {
		errno = EINVAL;
		return -1;
	}
	
	if ((nwritten = g_mime_stream_write (imap->stream, buffer, n)) == 0)
		imap->disconnected = TRUE;
	
	return nwritten;
}

static int
stream_flush (GMimeStream *stream)
{
	SpruceIMAPStream *imap = (SpruceIMAPStream *) stream;
	
	return g_mime_stream_flush (imap->stream);
}

static int
stream_close (GMimeStream *stream)
{
	SpruceIMAPStream *imap = (SpruceIMAPStream *) stream;
	
	if (g_mime_stream_close (imap->stream) == -1)
		return -1;
	
	g_object_unref (imap->stream);
	imap->stream = NULL;
	
	imap->disconnected = TRUE;
	
	return 0;
}

static gboolean
stream_eos (GMimeStream *stream)
{
	SpruceIMAPStream *imap = (SpruceIMAPStream *) stream;
	
	if (imap->eol)
		return TRUE;
	
	if (imap->disconnected && imap->inptr == imap->inend)
		return TRUE;
	
	if (g_mime_stream_eos (imap->stream))
		return TRUE;
	
	return FALSE;
}


/**
 * spruce_imap_stream_new:
 * @stream: tcp stream
 *
 * Returns a new imap stream
 **/
GMimeStream *
spruce_imap_stream_new (GMimeStream *stream)
{
	SpruceIMAPStream *imap;
	
	g_return_val_if_fail (GMIME_IS_STREAM (stream), NULL);
	
	imap = g_object_new (SPRUCE_TYPE_IMAP_STREAM, NULL);
	
	imap->stream = stream;
	g_object_ref (stream);
	
	return (GMimeStream *) imap;
}

static inline size_t
next_alloc_size (size_t n)
{
	return ((n + 63) / 64) * 64;
}

#define token_save(imap, start, len) G_STMT_START {                         \
	if (imap->tokenleft <= len) {                                       \
		size_t tlen, toff;                                          \
		                                                            \
		toff = imap->tokenptr - imap->tokenbuf;                     \
		tlen = next_alloc_size (toff + len + 1);                    \
		                                                            \
		while (tlen < toff + len)                                   \
			tlen <<= 1;                                         \
		                                                            \
		imap->tokenbuf = g_realloc (imap->tokenbuf, tlen);          \
		imap->tokenptr = imap->tokenbuf + toff;                     \
		imap->tokenleft = (tlen - 1) - toff;                        \
	}                                                                   \
	                                                                    \
	memcpy (imap->tokenptr, start, len);                                \
	imap->tokenptr += len;                                              \
	imap->tokenleft -= len;                                             \
} G_STMT_END

#define token_clear(imap) G_STMT_START {                                    \
	imap->tokenleft += imap->tokenptr - imap->tokenbuf;                 \
	imap->tokenptr = imap->tokenbuf;                                    \
	imap->literal = 0;                                                  \
} G_STMT_END


/**
 * spruce_imap_stream_next_token:
 * @stream: imap stream
 * @token: imap token
 *
 * Reads the next token from the imap stream and saves it in @token.
 *
 * Returns %0 on success or %-1 on fail.
 **/
int
spruce_imap_stream_next_token (SpruceIMAPStream *stream, spruce_imap_token_t *token)
{
	register unsigned char *inptr;
	unsigned char *inend, *start, *p;
	gboolean escaped = FALSE;
	size_t literal = 0;
	int digit, ret;
	guint32 n32;
	guint64 n64;
	
	g_return_val_if_fail (SPRUCE_IS_IMAP_STREAM (stream), -1);
	g_return_val_if_fail (stream->mode != SPRUCE_IMAP_STREAM_MODE_LITERAL, -1);
	g_return_val_if_fail (token != NULL, -1);
	
	if (stream->have_unget) {
		memcpy (token, &stream->unget, sizeof (spruce_imap_token_t));
		stream->have_unget = FALSE;
		return 0;
	}
	
	token_clear (stream);
	
	inptr = stream->inptr;
	inend = stream->inend;
	*inend = '\0';
	
	do {
		if (inptr == inend) {
			if ((ret = imap_fill (stream)) < 0) {
				token->token = SPRUCE_IMAP_TOKEN_ERROR;
				return -1;
			} else if (ret == 0) {
				token->token = SPRUCE_IMAP_TOKEN_NO_DATA;
				return 0;
			}
			
			inptr = stream->inptr;
			inend = stream->inend;
			*inend = '\0';
		}
		
		while (*inptr == ' ' || *inptr == '\r')
			inptr++;
	} while (inptr == inend);
	
	do {
		if (inptr < inend) {
			if (*inptr == '"') {
				/* qstring token */
				escaped = FALSE;
				start = inptr;
				
				/* eat the beginning " */
				inptr++;
				
				p = inptr;
				while (inptr < inend) {
					if (*inptr == '"' && !escaped)
						break;
					
					if (*inptr == '\\' && !escaped) {
						token_save (stream, p, inptr - p);
						escaped = TRUE;
						inptr++;
						p = inptr;
					} else {
						inptr++;
						escaped = FALSE;
					}
				}
				
				token_save (stream, p, inptr - p);
				
				if (inptr == inend) {
					stream->inptr = start;
					goto refill;
				}
				
				/* eat the ending " */
				inptr++;
				
				/* nul-terminate the atom token */
				token_save (stream, "", 1);
				
				token->token = SPRUCE_IMAP_TOKEN_QSTRING;
				token->v.qstring = (char *) stream->tokenbuf;
				
				d(fprintf (stderr, "token: \"%s\"\n", token->v.qstring));
				
				break;
			} else if (strchr ("+*()[]\n", *inptr)) {
				/* special character token */
				token->token = *inptr++;
#if d(!)0
				if (token->token != '\n')
					fprintf (stderr, "token: %c\n", token->token);
				else
					fprintf (stderr, "token: \\n\n");
#endif
				break;
			} else if (*inptr == '{') {
				/* literal identifier token */
				start = inptr;
				if ((p = ustrchr (inptr, '}')) && ustrchr (p, '\n')) {
					inptr++;
					
					while (isdigit ((int) *inptr) && literal < UINT_MAX / 10)
						literal = (literal * 10) + (*inptr++ - '0');
					
					if (*inptr != '}') {
						if (isdigit ((int) *inptr))
							g_warning ("illegal literal identifier: literal too large");
						else if (*inptr != '+')
							g_warning ("illegal literal identifier: garbage following size");
						
						while (*inptr != '}')
							inptr++;
					}
					
					/* skip over '}' */
					inptr++;
					
					/* skip over any trailing whitespace */
					while (*inptr == ' ' || *inptr == '\r')
						inptr++;
					
					if (*inptr != '\n') {
						g_warning ("illegal token following literal identifier: %s", inptr);
						
						/* skip ahead to the eoln */
						if (!(inptr = ustrchr (inptr, '\n'))) {
							stream->inptr = start;
							goto refill;
						}
					}
					
					/* skip over '\n' */
					inptr++;
					
					token->token = SPRUCE_IMAP_TOKEN_LITERAL;
					token->v.literal = literal;
					
					d(fprintf (stderr, "token: {%zu}\n", literal));
					
					stream->mode = SPRUCE_IMAP_STREAM_MODE_LITERAL;
					stream->literal = literal;
					stream->eol = FALSE;
					
					break;
				} else {
					stream->inptr = inptr;
					goto refill;
				}
			} else if (*inptr >= '0' && *inptr <= '9') {
				/* numeric token */
				*inend = '\0';
				
				n32 = 0;
				while (*inptr >= '0' && *inptr <= '9') {
					digit = (*inptr - '0');
					if (n32 > (ULONG_MAX / 10))
						goto uint64;
					else if (n32 == (ULONG_MAX / 10) && digit > (ULONG_MAX % 10))
						goto uint64;
					else
						n32 = (n32 * 10) + digit;
					
					inptr++;
				}
				
				if (inptr == inend)
					goto refill;
				
				if (*inptr == ':' || *inptr == ',') {
					/* workaround for 'set' tokens (APPENDUID / COPYUID) */
					goto atom_token;
				}
				
				token->token = SPRUCE_IMAP_TOKEN_NUMBER;
				token->v.number = n32;
				
				d(fprintf (stderr, "token: %u\n", n32));
				
				break;
				
			uint64:
				/* probably a MODSEQ value */
				
				n64 = n32;
				while (*inptr >= '0' && *inptr <= '9') {
					digit = (*inptr - '0');
					if (n64 > (ULLONG_MAX / 10))
						break;
					else if (n64 == (ULLONG_MAX / 10) && digit > (ULLONG_MAX % 10))
						break;
					else
						n64 = (n64 * 10) + digit;
					
					inptr++;
				}
				
				if (inptr == inend)
					goto refill;
				
				if (*inptr >= '0' || *inptr <= '9') {
					/* integer overflow */
					token->token = SPRUCE_IMAP_TOKEN_ERROR;
					d(fprintf (stderr, "token: Error: integer overflow\n"));
					return -1;
				}
				
				token->token = SPRUCE_IMAP_TOKEN_NUMBER64;
				token->v.number64 = n64;
				
				d(fprintf (stderr, "token: %" G_GUINT64_FORMAT "\n", n64));
				
				break;
			} else if (is_atom (*inptr)) {
			atom_token:
				/* simple atom token */
				start = inptr;
				
				while (inptr < inend && is_atom (*inptr))
					inptr++;
				
				if (inptr == inend) {
					stream->inptr = start;
					goto refill;
				}
				
				token_save (stream, start, inptr - start);
				
				/* nul-terminate the atom token */
				token_save (stream, "", 1);
				
				if (!ustrcmp (stream->tokenbuf, "NIL")) {
					/* special atom token */
					token->token = SPRUCE_IMAP_TOKEN_NIL;
					d(fprintf (stderr, "token: NIL\n"));
				} else {
					token->token = SPRUCE_IMAP_TOKEN_ATOM;
					token->v.atom = (char *) stream->tokenbuf;
					d(fprintf (stderr, "token: %s\n", token->v.atom));
				}
				
				break;
			} else if (*inptr == '\\') {
				/* possible flag token ("\" atom) */
				start = inptr++;
				
				while (inptr < inend && is_atom (*inptr))
					inptr++;
				
				if (inptr == inend) {
					stream->inptr = start;
					goto refill;
				}
				
				/* handle the \* case */
				if ((inptr - start) == 1 && *inptr == '*')
					inptr++;
				
				if ((inptr - start) > 1) {
					token_save (stream, start, inptr - start);
					
					/* nul-terminate the flag token */
					token_save (stream, "", 1);
					
					token->token = SPRUCE_IMAP_TOKEN_FLAG;
					token->v.atom = (char *) stream->tokenbuf;
					d(fprintf (stderr, "token: %s\n", token->v.atom));
				} else {
					token->token = '\\';
					d(fprintf (stderr, "token: %c\n", token->token));
				}
				break;
			} else if (is_lwsp (*inptr)) {
				inptr++;
			} else {
				/* unknown character token? */
				token->token = *inptr++;
				d(fprintf (stderr, "token: %c\n", token->token));
				break;
			}
		} else {
		refill:
			token_clear (stream);
			
			if (imap_fill (stream) <= 0) {
				token->token = SPRUCE_IMAP_TOKEN_ERROR;
				return -1;
			}
			
			inptr = stream->inptr;
			inend = stream->inend;
			*inend = '\0';
		}
	} while (inptr < inend);
	
	stream->inptr = inptr;
	
	return 0;
}


/**
 * spruce_imap_stream_unget_token:
 * @stream: imap stream
 * @token: token to 'unget'
 *
 * Ungets an imap token (as in ungetc()).
 *
 * Note: you may *ONLY* unget a single token. Trying to unget another
 * token will fail.
 *
 * Returns %0 on success or %-1 on fail.
 **/
int
spruce_imap_stream_unget_token (SpruceIMAPStream *stream, spruce_imap_token_t *token)
{
	if (stream->have_unget)
		return -1;
	
	if (token->token != SPRUCE_IMAP_TOKEN_NO_DATA) {
		memcpy (&stream->unget, token, sizeof (spruce_imap_token_t));
		stream->have_unget = TRUE;
	}
	
	return 0;
}


/**
 * spruce_imap_stream_readline:
 * @stream: imap stream
 * @line: line pointer
 * @len: line length
 *
 * Reads a single line from the imap stream and points @line at an
 * internal buffer containing the line read and sets @len to the
 * length of the line buffer.
 *
 * Returns %-1 on error, %0 if the line read is complete, or %1 if the
 * read is incomplete.
 **/
int
spruce_imap_stream_line (SpruceIMAPStream *stream, unsigned char **line, size_t *len)
{
	register unsigned char *inptr;
	unsigned char *inend;
	
	g_return_val_if_fail (SPRUCE_IS_IMAP_STREAM (stream), -1);
	g_return_val_if_fail (stream->mode != SPRUCE_IMAP_STREAM_MODE_LITERAL, -1);
	g_return_val_if_fail (line != NULL, -1);
	g_return_val_if_fail (len != NULL, -1);
	
	*stream->inend = '\0';
	if ((stream->inend - stream->inptr) < 2 && *stream->inptr != '\n') {
		if (imap_fill (stream) == -1 && stream->inptr == stream->inend)
			return -1;
	}
	
	*line = stream->inptr;
	inptr = stream->inptr;
	inend = stream->inend;
	*inend = '\n';
	
	while (*inptr != '\n')
		inptr++;
	
	*len = (inptr - stream->inptr);
	if (inptr < inend) {
		/* got the eoln */
		if (inptr > stream->inptr && inptr[-1] == '\r')
			inptr[-1] = '\0';
		else
			inptr[0] = '\0';
		
		stream->inptr = inptr + 1;
		*len += 1;
		
		return 0;
	}
	
	stream->inptr = inptr;
	
	return 1;
}


int
spruce_imap_stream_literal (SpruceIMAPStream *stream, unsigned char **literal, size_t *len)
{
	unsigned char *inptr, *inend;
	size_t nread;
	
	g_return_val_if_fail (SPRUCE_IS_IMAP_STREAM (stream), -1);
	g_return_val_if_fail (stream->mode == SPRUCE_IMAP_STREAM_MODE_LITERAL, -1);
	g_return_val_if_fail (literal != NULL, -1);
	g_return_val_if_fail (len != NULL, -1);
	
	if (stream->eol) {
		*len = 0;
		return 0;
	}
	
	if ((stream->inend - stream->inptr) < 1) {
		/* keep our buffer full to the optimal size */
		if (imap_fill (stream) == -1 && stream->inptr == stream->inend)
			return -1;
	}
	
	*literal = inptr = stream->inptr;
	inend = stream->inend;
	if ((inend - inptr) > stream->literal)
		inend = inptr + stream->literal;
	else
		inend = stream->inend;
	
	*len = nread = inend - inptr;
	
	stream->literal -= nread;
	stream->inptr += nread;
	
	if (stream->literal == 0) {
		stream->mode = SPRUCE_IMAP_STREAM_MODE_TOKEN;
		stream->eol = TRUE;
		return 0;
	}
	
	return 1;
}
