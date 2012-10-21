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
#include <string.h>
#include <errno.h>

#include "spruce-pop-stream.h"

#define d(x) x

static void spruce_pop_stream_class_init (SprucePOPStreamClass *klass);
static void spruce_pop_stream_init (SprucePOPStream *stream, SprucePOPStreamClass *klass);
static void spruce_pop_stream_finalize (GObject *object);

static ssize_t stream_read (GMimeStream *stream, char *buf, size_t n);
static ssize_t stream_write (GMimeStream *stream, const char *buf, size_t n);
static int stream_flush (GMimeStream *stream);
static int stream_close (GMimeStream *stream);
static gboolean stream_eos (GMimeStream *stream);
static int stream_reset (GMimeStream *stream);


static GMimeStreamClass *parent_class = NULL;


GType
spruce_pop_stream_get_type (void)
{
	static GType type = 0;
	
	if (!type) {
		static const GTypeInfo info = {
			sizeof (SprucePOPStreamClass),
			NULL, /* base_class_init */
			NULL, /* base_class_finalize */
			(GClassInitFunc) spruce_pop_stream_class_init,
			NULL, /* class_finalize */
			NULL, /* class_data */
			sizeof (SprucePOPStream),
			0,    /* n_preallocs */
			(GInstanceInitFunc) spruce_pop_stream_init,
		};
		
		type = g_type_register_static (GMIME_TYPE_STREAM, "SprucePOPStream", &info, 0);
	}
	
	return type;
}

static void
spruce_pop_stream_class_init (SprucePOPStreamClass *klass)
{
	GMimeStreamClass *stream_class = GMIME_STREAM_CLASS (klass);
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	
	parent_class = g_type_class_ref (GMIME_TYPE_STREAM);
	
	object_class->finalize = spruce_pop_stream_finalize;
	
	stream_class->read = stream_read;
	stream_class->write = stream_write;
	stream_class->flush = stream_flush;
	stream_class->close = stream_close;
	stream_class->eos = stream_eos;
	stream_class->reset = stream_reset;
}

static void
spruce_pop_stream_init (SprucePOPStream *stream, SprucePOPStreamClass *klass)
{
	stream->stream = NULL;
	
	stream->mode = SPRUCE_POP_STREAM_LINE;
	stream->disconnected = FALSE;
	stream->midline = FALSE;
	stream->eod = FALSE;
	
	stream->inbuf = stream->realbuf + POP_SCAN_HEAD;
	stream->inptr = stream->inbuf;
	stream->inend = stream->inbuf;
}

static void
spruce_pop_stream_finalize (GObject *object)
{
	SprucePOPStream *pop = (SprucePOPStream *) object;
	
	if (pop->stream)
		g_object_unref (pop->stream);
	
	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static ssize_t
pop_fill (SprucePOPStream *pop)
{
	char *inbuf, *inptr, *inend;
	ssize_t nread;
	size_t inlen;
	
	if (pop->disconnected) {
		errno = EINVAL;
		return -1;
	}
	
	inbuf = pop->inbuf;
	inptr = pop->inptr;
	inend = pop->inend;
	inlen = inend - inptr;
	
	g_assert (inptr <= inend);
	
	/* attempt to align 'inend' with realbuf + POP_SCAN_HEAD */
	if (inptr >= inbuf) {
		inbuf -= inlen < POP_SCAN_HEAD ? inlen : POP_SCAN_HEAD;
		memmove (inbuf, inptr, inlen);
		inptr = inbuf;
		inbuf += inlen;
	} else if (inptr > pop->realbuf) {
		size_t shift;
		
		shift = MIN (inptr - pop->realbuf, inend - inbuf);
		memmove (inptr - shift, inptr, inlen);
		inptr -= shift;
		inbuf = inptr + inlen;
	} else {
		/* we can't shift... */
		inbuf = inend;
	}
	
	pop->inptr = inptr;
	pop->inend = inbuf;
	inend = pop->realbuf + POP_SCAN_HEAD + POP_SCAN_BUF;
	
	if ((nread = g_mime_stream_read (pop->stream, inbuf, inend - inbuf)) == 0)
		pop->disconnected = TRUE;
	else if (nread == -1)
		return -1;
	
	pop->inend += nread;
	
	return pop->inend - pop->inptr;
}

static ssize_t
stream_read (GMimeStream *stream, char *buf, size_t n)
{
	SprucePOPStream *pop = (SprucePOPStream *) stream;
	register char *inptr, *outptr;
	char *inend, *outbuf, *outend;
	ssize_t left, nread = -1;
	
	left = pop->inend - pop->inptr;
	
	switch (pop->mode) {
	case SPRUCE_POP_STREAM_LINE:
		/* 3 comes from ".\r\n" */
		if (!pop->midline && left < 3) {
			if ((left = pop_fill (pop)) == -1 && pop->inptr == pop->inend)
				return -1;
			else
				left = pop->inend - pop->inptr;
		}
		
		nread = n;
		nread = MIN (nread, left);
		
		memcpy (buf, pop->inptr, nread);
		
		pop->inptr += nread;
		
		/* keep midline state */
		if (nread > 0 && pop->inptr[-1] == '\n')
			pop->midline = FALSE;
		else
			pop->midline = TRUE;
		
		break;
	case SPRUCE_POP_STREAM_DATA:
		if (left < POP_SCAN_HEAD) {
			/* keep our buffer full to the optimal size */
			if ((left = pop_fill (pop)) == -1 && pop->inptr == pop->inend)
				return -1;
		}
		
		outbuf = buf;
		outptr = outbuf;
		outend = outbuf + n;
		
		inptr = pop->inptr;
		inend = pop->inend;
		inend[0] = '\r';
		
		do {
			/* read until eoln */
			while (pop->midline && nread < (ssize_t) n) {
				while (outptr < outend && *inptr != '\r' && *inptr != '\n')
					*outptr++ = *inptr++;
				
				if (outptr == outend) {
					/* we're done. we got everything our caller asked for */
					pop->inptr = inptr;
					
					return outptr - outbuf;
				}
				
				/* convert CRLF to LF */
				if (inptr[0] == '\r') {
					if ((inptr + 1) < inend) {
						if (inptr[1] == '\n') {
							pop->midline = FALSE;
							*outptr++ = '\n';
							inptr += 2;
						} else {
							/* \r in the middle of a line? odd... */
							*outptr++ = *inptr++;
						}
					} else {
						/* not enough buffered data to process eoln */
						pop->midline = TRUE;
						pop->inptr = inptr;
						
						return outptr - outbuf;
					}
				} else {
					/* broken POP server? encountered non-CRLF encoded eoln */
					pop->midline = FALSE;
					*outptr++ = '\n';
					inptr++;
				}
			}
			
			if (inptr == inend) {
				/* out of buffered data */
				pop->midline = TRUE;
				pop->inptr = inptr;
				
				return outptr - outbuf;
			}
			
			if (inptr[0] != '.') {
				/* no special processing required... */
				pop->midline = TRUE;
				continue;
			}
			
			/* line begins with '.', special processing required */
			
			if ((inptr + 1) == inend) {
				/* stop here. we don't have enough buffered to continue */
				pop->inptr = inptr;
				
				return outptr - outbuf;
			}
			
			if (inptr[1] == '\r') {
				if ((inptr + 2) < inend) {
					if (inptr[2] == '\n') {
						/* end of multi-line response */
						pop->inptr = inptr + 2;
						pop->eod = TRUE;
						
						return outptr - outbuf;
					}
				} else {
					/* stop here. we don't have enough buffered to continue */
					pop->inptr = inptr;
					
					return outptr - outbuf;
				}
			} else if (inptr[1] == '\n') {
				/* improper multi-line termination? */
#if d(!)0
				{
					const char *eoln = "\"...";
					char *dstart = inptr + 2;
					char *dend = dstart;
					
					while (dend < inend && dend[0] != '\n')
						dend++;
					
					if (dend[0] == '\n') {
						if (dend[-1] == '\r') {
							eoln = "\\r\\n\"";
							dend -= 2;
						} else {
							eoln = "\\n\"";
							dend--;
						}
					} else if (dend[0] == '\r') {
						eoln = "\\r\"";
						dend--;
					}
					
					fprintf (stderr, "\n*** Possible broken multi-line termination encountered.\n");
					if (dend > dstart)
						fprintf (stderr, "inptr = \".\\n%.*s%s\n", (int) (dend - dstart), dstart, eoln);
					else
						fprintf (stderr, "inptr = \".\\n\"...\n");
				}
#endif /* debug */
				pop->inptr = inptr;
				
				/* try looking ahead to the next line and seeing if it looks like a POP response? */
				inptr++;
				
				if (inptr == inend && pop->disconnected) {
					/* yea, I think it's safe to assume EOD */
					pop->inptr = inend;
					pop->eod = TRUE;
					
					d(fprintf (stderr, "assuming EOD (disconnected)\n\n"));
					
					return outptr - outbuf;
				}
				
				if ((inend - inptr) > 3) {
					if (!strncmp (inptr, "+OK", 3) && (inptr[3] == '\r' || inptr[3] == '\n')) {
						/* looks like yes */
						pop->eod = TRUE;
						
						d(fprintf (stderr, "assuming EOD (next line is +OK)\n\n"));
						
						return outptr - outbuf;
					}
				}
				
				if ((inend - inptr) > 4) {
					if (!strncmp (inptr, "-ERR", 4) && (inptr[4] == '\r' || inptr[4] == '\n')) {
						/* looks like yes */
						pop->eod = TRUE;
						
						d(fprintf (stderr, "assuming EOD (next line is -ERR)\n\n"));
						
						return outptr - outbuf;
					}
				}
				
				/* no choice but to wait for the next read */
				d(fprintf (stderr, "not enough data to make a decision, waiting for next read\n\n"));
				
				return outptr - outbuf;
			} else if (inptr[1] == '.') {
				/* unescape ".." to "." */
				inptr++;
			}
			
			/* we might not technically be midline, but any
			 * BOL processing that needed to be done has
			 * been done so for all intents and purposes,
			 * we are midline. */
			pop->midline = TRUE;
		} while (outptr < outend);
		
		pop->inptr = inptr;
		nread = outptr - outbuf;
		
		break;
	}
	
	return nread;
}

static ssize_t
stream_write (GMimeStream *stream, const char *buf, size_t n)
{
	SprucePOPStream *pop = (SprucePOPStream *) stream;
	ssize_t nwritten;
	
	if (pop->disconnected) {
		errno = EINVAL;
		return -1;
	}
	
	/* sending a [new] command so EOD becomes FALSE auto-magically for the next read */
	pop->eod = FALSE;
	
	if ((nwritten = g_mime_stream_write (pop->stream, buf, n)) == 0)
		pop->disconnected = TRUE;
	
	return nwritten;
}

static int
stream_flush (GMimeStream *stream)
{
	SprucePOPStream *pop = (SprucePOPStream *) stream;
	
	return g_mime_stream_flush (pop->stream);
}

static int
stream_close (GMimeStream *stream)
{
	SprucePOPStream *pop = (SprucePOPStream *) stream;
	
	g_return_val_if_fail (GMIME_IS_STREAM (pop->stream), -1);
	
	if (g_mime_stream_close (pop->stream) == -1)
		return -1;
	
	g_object_unref (pop->stream);
	pop->stream = NULL;
	
	pop->disconnected = TRUE;
	
	return 0;
}

static gboolean
stream_eos (GMimeStream *stream)
{
	SprucePOPStream *pop = (SprucePOPStream *) stream;
	
	if (pop->mode == SPRUCE_POP_STREAM_DATA && pop->eod)
		return TRUE;
	
	if (pop->disconnected && pop->inptr == pop->inend)
		return TRUE;
	
	if (g_mime_stream_eos (pop->stream))
		return TRUE;
	
	return FALSE;
}

static int
stream_reset (GMimeStream *stream)
{
	SprucePOPStream *pop = (SprucePOPStream *) stream;
	
	if (g_mime_stream_reset (pop->stream) == -1)
		return -1;
	
	pop->mode = SPRUCE_POP_STREAM_LINE;
	pop->inbuf = pop->realbuf + POP_SCAN_HEAD;
	pop->inptr = pop->inbuf;
	pop->inend = pop->inptr;
	
	return 0;
}


/**
 * spruce_pop_stream_new:
 * @stream: source stream
 *
 * Creates a new SprucePOPStream object around @stream.
 *
 * Returns a new SprucePOPStream.
 **/
GMimeStream *
spruce_pop_stream_new (GMimeStream *stream)
{
	SprucePOPStream *pop;
	
	g_return_val_if_fail (GMIME_IS_STREAM (stream), NULL);
	
	pop = g_object_new (SPRUCE_TYPE_POP_STREAM, NULL);
	g_object_ref (stream);
	pop->stream = stream;
	
	g_mime_stream_construct ((GMimeStream *) pop, 0, -1);
	
	return (GMimeStream *) pop;
}


/**
 * spruce_pop_stream_line:
 * @stream: POP stream
 * @line: 
 * @len:
 *
 * Reads a single line from the pop stream and points @line at an
 * internal buffer containing the line read and sets @len to the
 * length of the line buffer.
 *
 * Returns %-1 on error, %0 if the line read is complete, or %1 if the
 * read is incomplete.
 **/
int
spruce_pop_stream_line (SprucePOPStream *stream, char **line, size_t *len)
{
	register char *inptr;
	char *inend;
	
	g_return_val_if_fail (SPRUCE_IS_POP_STREAM (stream), -1);
	g_return_val_if_fail (line != NULL, -1);
	g_return_val_if_fail (len != NULL, -1);
	
	if ((stream->inend - stream->inptr) < 3) {
		/* keep our buffer full to the optimal size */
		if (pop_fill (stream) == -1 && stream->inptr == stream->inend)
			return -1;
	}
	
	*line = stream->inptr;
	inptr = stream->inptr;
	inend = stream->inend;
	inend[0] = '\n';
	
	while (*inptr != '\n')
		inptr++;
	
	*len = (inptr - stream->inptr);
	if (inptr < inend) {
		/* got the eoln */
		stream->midline = FALSE;
		if (inptr > stream->inptr && inptr[-1] == '\r')
			inptr[-1] = '\0';
		else
			inptr[0] = '\0';
		
		stream->inptr = inptr + 1;
		*len += 1;
		
		return 0;
	}
	
	stream->midline = TRUE;
	stream->inptr = inptr;
	
	return 1;
}


/**
 * spruce_pop_stream_set_mode:
 * @stream: POP stream
 * @mode: LINE vs DATA mode
 *
 * Sets the read mode on the stream.
 **/
void
spruce_pop_stream_set_mode (SprucePOPStream *stream, spruce_pop_stream_mode_t mode)
{
	g_return_if_fail (SPRUCE_IS_POP_STREAM (stream));
	
	stream->mode = mode;
}


/**
 * spruce_pop_stream_unset_eod:
 * @stream: POP stream
 *
 * Unsets the EOD marker for a POP stream (ie. just queued another RETR command?)
 **/
void
spruce_pop_stream_unset_eod (SprucePOPStream *stream)
{
	g_return_if_fail (SPRUCE_IS_POP_STREAM (stream));
	
	stream->eod = FALSE;
}
