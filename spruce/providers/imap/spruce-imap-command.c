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

#include <errno.h>

#include <gmime/gmime-stream-null.h>
#include <gmime/gmime-stream-filter.h>
#include <gmime/gmime-filter-crlf.h>

#include <spruce/spruce-error.h>

#include "spruce-imap-stream.h"
#include "spruce-imap-engine.h"
#include "spruce-imap-folder.h"
#include "spruce-imap-specials.h"

#include "spruce-imap-command.h"


#define _(x) x
#define d(x) x


enum {
	IMAP_STRING_ATOM,
	IMAP_STRING_QSTRING,
	IMAP_STRING_LITERAL,
};

static int
imap_string_get_type (const char *str)
{
	int type = 0;
	
	while (*str) {
		if (!is_atom (*str)) {
			if (is_qsafe (*str))
				type = IMAP_STRING_QSTRING;
			else
				return IMAP_STRING_LITERAL;
		}
		str++;
	}
	
	return type;
}

#if 0
static gboolean
imap_string_is_atom_safe (const char *str)
{
	while (is_atom (*str))
		str++;
	
	return *str == '\0';
}

static gboolean
imap_string_is_quote_safe (const char *str)
{
	while (is_qsafe (*str))
		str++;
	
	return *str == '\0';
}
#endif

static size_t
spruce_imap_literal_length (SpruceIMAPLiteral *literal)
{
	GMimeStream *stream, *null;
	GMimeFilter *crlf;
	size_t len;
	
	if (literal->type == SPRUCE_IMAP_LITERAL_STRING)
		return strlen (literal->literal.string);
	
	null = g_mime_stream_null_new ();
	crlf = g_mime_filter_crlf_new (TRUE, FALSE);
	stream = g_mime_stream_filter_new (null);
	g_mime_stream_filter_add ((GMimeStreamFilter *) stream, crlf);
	g_object_unref (crlf);
	
	switch (literal->type) {
	case SPRUCE_IMAP_LITERAL_STREAM:
		g_mime_stream_write_to_stream (literal->literal.stream, stream);
		g_mime_stream_reset (literal->literal.stream);
		break;
	case SPRUCE_IMAP_LITERAL_OBJECT:
		g_mime_object_write_to_stream (literal->literal.object, stream);
		break;
	case SPRUCE_IMAP_LITERAL_WRAPPER:
		g_mime_data_wrapper_write_to_stream (literal->literal.wrapper, stream);
		break;
	default:
		g_assert_not_reached ();
		break;
	}
	
	len = ((GMimeStreamNull *) null)->written;
	
	g_object_unref (stream);
	g_object_unref (null);
	
	return len;
}

static SpruceIMAPCommandPart *
command_part_new (void)
{
	SpruceIMAPCommandPart *part;
	
	part = g_new (SpruceIMAPCommandPart, 1);
	part->next = NULL;
	part->buffer = NULL;
	part->buflen = 0;
	part->literal = NULL;
	
	return part;
}

static void
imap_command_append_string (SpruceIMAPEngine *engine, SpruceIMAPCommandPart **tail, GString *str, const char *string)
{
	SpruceIMAPCommandPart *part;
	SpruceIMAPLiteral *literal;
	register const char *inptr;
	const char *start;
	
	switch (imap_string_get_type (string)) {
	case IMAP_STRING_ATOM:
		/* string is safe as it is... */
		g_string_append (str, string);
		break;
	case IMAP_STRING_QSTRING:
		/* we need to quote the string */
		g_string_append_c (str, '"');
		
		inptr = string;
		while (*inptr) {
			start = string;
			while (*inptr && *inptr != '\\' && *inptr != '"')
				inptr++;
			
			if (inptr > start)
				g_string_append_len (str, start, inptr - start);
			
			if (*inptr != '\0') {
				g_string_append_c (str, '\\');
				g_string_append_c (str, *inptr);
				inptr++;
			}
		}
		
		g_string_append_c (str, '"');
		break;
	case IMAP_STRING_LITERAL:
		if (engine->capa & SPRUCE_IMAP_CAPABILITY_LITERALPLUS) {
			/* we have to send a literal, but the server supports LITERAL+ so use that */
			g_string_append_printf (str, "{%zu+}\r\n%s", strlen (string), string);
		} else {
			/* we have to make it a literal */
			literal = g_new (SpruceIMAPLiteral, 1);
			literal->type = SPRUCE_IMAP_LITERAL_STRING;
			literal->literal.string = g_strdup (string);
			
			g_string_append_printf (str, "{%zu}\r\n", strlen (string));
			
			(*tail)->buffer = g_strdup (str->str);
			(*tail)->buflen = str->len;
			(*tail)->literal = literal;
			
			part = command_part_new ();
			(*tail)->next = part;
			(*tail) = part;
			
			g_string_truncate (str, 0);
		}
		break;
	}
}

SpruceIMAPCommand *
spruce_imap_command_newv (SpruceIMAPEngine *engine, SpruceIMAPFolder *imap_folder, const char *format, va_list args)
{
	SpruceIMAPCommandPart *parts, *part, *tail;
	SpruceIMAPCommand *ic;
	const char *start;
	GString *str;
	
	tail = parts = command_part_new ();
	
	str = g_string_new ("");
	start = format;
	
	while (*format) {
		register char ch = *format++;
		
		if (ch == '%') {
			SpruceIMAPLiteral *literal;
			SpruceIMAPFolder *folder;
			unsigned int u;
			char *string;
			size_t len;
			void *obj;
			int c, d;
			
			g_string_append_len (str, start, format - start - 1);
			
			switch (*format) {
			case '%':
				/* literal % */
				g_string_append_c (str, '%');
				break;
			case 'c':
				/* character */
				c = va_arg (args, int);
				g_string_append_c (str, c);
				break;
			case 'd':
				/* integer */
				d = va_arg (args, int);
				g_string_append_printf (str, "%d", d);
				break;
			case 'u':
				/* unsigned integer */
				u = va_arg (args, unsigned int);
				g_string_append_printf (str, "%u", u);
				break;
			case 'F':
				/* SpruceIMAPFolder */
				folder = va_arg (args, SpruceIMAPFolder *);
				string = (char *) spruce_imap_folder_utf7_name (folder);
				imap_command_append_string (engine, &tail, str, string);
				break;
			case 'L':
				/* Literal */
				obj = va_arg (args, void *);
				
				literal = g_new (SpruceIMAPLiteral, 1);
				if (GMIME_IS_DATA_WRAPPER (obj)) {
					literal->type = SPRUCE_IMAP_LITERAL_WRAPPER;
					literal->literal.wrapper = obj;
				} else if (GMIME_IS_OBJECT (obj)) {
					literal->type = SPRUCE_IMAP_LITERAL_OBJECT;
					literal->literal.object = obj;
				} else if (GMIME_IS_STREAM (obj)) {
					literal->type = SPRUCE_IMAP_LITERAL_STREAM;
					literal->literal.stream = obj;
				} else {
					g_assert_not_reached ();
				}
				
				g_object_ref (obj);
				
				/* FIXME: take advantage of LITERAL+? */
				len = spruce_imap_literal_length (literal);
				g_string_append_printf (str, "{%zu}\r\n", len);
				
				tail->buffer = g_strdup (str->str);
				tail->buflen = str->len;
				tail->literal = literal;
				
				part = command_part_new ();
				tail->next = part;
				tail = part;
				
				g_string_truncate (str, 0);
				
				break;
			case 'S':
				/* string which may need to be quoted or made into a literal */
				string = va_arg (args, char *);
				imap_command_append_string (engine, &tail, str, string);
				break;
			case 's':
				/* safe atom string */
				string = va_arg (args, char *);
				g_string_append (str, string);
				break;
			default:
				g_warning ("unknown formatter %%%c", *format);
				g_string_append_c (str, '%');
				g_string_append_c (str, *format);
				break;
			}
			
			format++;
			
			start = format;
		}
	}
	
	g_string_append (str, start);
	tail->buffer = str->str;
	tail->buflen = str->len;
	tail->literal = NULL;
	g_string_free (str, FALSE);
	
	ic = g_new0 (SpruceIMAPCommand, 1);
	((SpruceListNode *) ic)->next = NULL;
	((SpruceListNode *) ic)->prev = NULL;
	ic->untagged = g_hash_table_new (g_str_hash, g_str_equal);
	ic->status = SPRUCE_IMAP_COMMAND_QUEUED;
	ic->result = SPRUCE_IMAP_RESULT_NONE;
	ic->resp_codes = g_ptr_array_new ();
	ic->engine = engine;
	ic->ref_count = 1;
	ic->parts = parts;
	ic->part = parts;
	ic->plus = NULL;
	ic->tag = NULL;
	ic->err = NULL;
	ic->id = -1;
	
	ic->user_data = NULL;
	ic->reset = NULL;
	
	if (imap_folder) {
		g_object_ref (imap_folder);
		ic->folder = imap_folder;
	} else
		ic->folder = NULL;
	
	return ic;
}

SpruceIMAPCommand *
spruce_imap_command_new (SpruceIMAPEngine *engine, SpruceIMAPFolder *folder, const char *format, ...)
{
	SpruceIMAPCommand *command;
	va_list args;
	
	va_start (args, format);
	command = spruce_imap_command_newv (engine, folder, format, args);
	va_end (args);
	
	return command;
}

void
spruce_imap_command_register_untagged (SpruceIMAPCommand *ic, const char *atom, SpruceIMAPUntaggedCallback untagged)
{
	g_hash_table_insert (ic->untagged, g_strdup (atom), untagged);
}

void
spruce_imap_command_ref (SpruceIMAPCommand *ic)
{
	ic->ref_count++;
}

void
spruce_imap_command_unref (SpruceIMAPCommand *ic)
{
	SpruceIMAPCommandPart *part, *next;
	int i;
	
	if (ic == NULL)
		return;
	
	ic->ref_count--;
	if (ic->ref_count == 0) {
		if (ic->folder)
			g_object_unref (ic->folder);
		
		g_free (ic->tag);
		
		for (i = 0; i < ic->resp_codes->len; i++) {
			SpruceIMAPRespCode *resp_code;
			
			resp_code = ic->resp_codes->pdata[i];
			spruce_imap_resp_code_free (resp_code);
		}
		g_ptr_array_free (ic->resp_codes, TRUE);
		
		g_hash_table_foreach (ic->untagged, (GHFunc) g_free, NULL);
		g_hash_table_destroy (ic->untagged);
		
		if (ic->err)
			g_error_free (ic->err);
		
		part = ic->parts;
		while (part != NULL) {
			g_free (part->buffer);
			if (part->literal) {
				switch (part->literal->type) {
				case SPRUCE_IMAP_LITERAL_STRING:
					g_free (part->literal->literal.string);
					break;
				case SPRUCE_IMAP_LITERAL_STREAM:
					g_object_unref (part->literal->literal.stream);
					break;
				case SPRUCE_IMAP_LITERAL_OBJECT:
					g_object_unref (part->literal->literal.object);
					break;
				case SPRUCE_IMAP_LITERAL_WRAPPER:
					g_object_unref (part->literal->literal.wrapper);
					break;
				}
				
				g_free (part->literal);
			}
			
			next = part->next;
			g_free (part);
			part = next;
		}
		
		g_free (ic);
	}
}


static int
imap_literal_write_to_stream (SpruceIMAPLiteral *literal, GMimeStream *stream)
{
	GMimeStream *istream, *ostream = NULL;
	GMimeDataWrapper *wrapper;
	GMimeObject *object;
	GMimeFilter *crlf;
	char *string;
	
	if (literal->type == SPRUCE_IMAP_LITERAL_STRING) {
		string = literal->literal.string;
		if (g_mime_stream_write (stream, string, strlen (string)) == -1)
			return -1;
		
		return 0;
	}
	
	crlf = g_mime_filter_crlf_new (TRUE, FALSE);
	stream = g_mime_stream_filter_new (stream);
	g_mime_stream_filter_add ((GMimeStreamFilter *) ostream, crlf);
	g_object_unref (crlf);
	
	/* write the literal */
	switch (literal->type) {
	case SPRUCE_IMAP_LITERAL_STREAM:
		istream = literal->literal.stream;
		if (g_mime_stream_write_to_stream (istream, ostream) == -1)
			goto exception;
		break;
	case SPRUCE_IMAP_LITERAL_OBJECT:
		object = literal->literal.object;
		if (g_mime_object_write_to_stream (object, ostream) == -1)
			goto exception;
		break;
	case SPRUCE_IMAP_LITERAL_WRAPPER:
		wrapper = literal->literal.wrapper;
		if (g_mime_data_wrapper_write_to_stream (wrapper, ostream) == -1)
			goto exception;
		break;
	}
	
	g_object_unref (ostream);
	ostream = NULL;
	
#if 0
	if (g_mime_stream_write (stream, "\r\n", 2) == -1)
		return -1;
#endif
	
	return 0;
	
 exception:
	
	g_object_unref (ostream);
	
	return -1;
}


static void
unexpected_token (spruce_imap_token_t *token)
{
	switch (token->token) {
	case SPRUCE_IMAP_TOKEN_NO_DATA:
		fprintf (stderr, "*** NO DATA ***");
		break;
	case SPRUCE_IMAP_TOKEN_ERROR:
		fprintf (stderr, "*** ERROR ***");
		break;
	case SPRUCE_IMAP_TOKEN_NIL:
		fprintf (stderr, "NIL");
		break;
	case SPRUCE_IMAP_TOKEN_ATOM:
	        fprintf (stderr, "%s", token->v.atom);
		break;
	case SPRUCE_IMAP_TOKEN_QSTRING:
	        fprintf (stderr, "\"%s\"", token->v.qstring);
		break;
	case SPRUCE_IMAP_TOKEN_LITERAL:
		fprintf (stderr, "{%zu}", token->v.literal);
		break;
	default:
		fprintf (stderr, "%c", (unsigned char) (token->token & 0xff));
		break;
	}
}

int
spruce_imap_command_step (SpruceIMAPCommand *ic)
{
	SpruceIMAPEngine *engine = ic->engine;
	int result = SPRUCE_IMAP_RESULT_NONE;
	SpruceIMAPLiteral *literal;
	spruce_imap_token_t token;
	unsigned char *linebuf;
	ssize_t nwritten;
	size_t len;
	
	g_assert (ic->part != NULL);
	
	if (ic->part == ic->parts) {
		ic->tag = g_strdup_printf ("%c%.5u", engine->tagprefix, engine->tag++);
		g_mime_stream_printf (engine->ostream, "%s ", ic->tag);
		d(fprintf (stderr, "sending : %s ", ic->tag));
	}
	
#if d(!)0
	{
		int sending = ic->part != ic->parts;
		unsigned char *eoln, *eob;
		
		linebuf = (unsigned char *) ic->part->buffer;
		eob = linebuf + ic->part->buflen;
		
		do {
			eoln = linebuf;
			while (eoln < eob && *eoln != '\n')
				eoln++;
			
			if (eoln < eob)
				eoln++;
			
			if (sending)
				fwrite ("sending : ", 1, 10, stderr);
			fwrite (linebuf, 1, eoln - linebuf, stderr);
			
			linebuf = eoln + 1;
			sending = 1;
		} while (linebuf < eob);
	}
#endif
	
	if ((nwritten = g_mime_stream_write (engine->ostream, ic->part->buffer, ic->part->buflen)) == -1)
		goto exception;
	
	if (g_mime_stream_flush (engine->ostream) == -1)
		goto exception;
	
	/* now we need to read the response(s) from the IMAP server */
	
	do {
		if (spruce_imap_engine_next_token (engine, &token, &ic->err) == -1)
			goto exception;
		
		if (token.token == '+') {
			/* we got a continuation response from the server */
			fprintf (stderr, "got + response\n");
			literal = ic->part->literal;
			
			if (spruce_imap_engine_line (engine, &linebuf, &len, &ic->err) == -1)
				goto exception;
			
			if (literal) {
				if (imap_literal_write_to_stream (literal, engine->ostream) == -1)
					goto exception;
				
				g_free (linebuf);
				linebuf = NULL;
				
				break;
			} else if (ic->plus) {
				/* command expected a '+' response - probably AUTHENTICATE? */
				if (ic->plus (engine, ic, linebuf, len, &ic->err) == -1) {
					g_free (linebuf);
					return -1;
				}
				
				/* now we need to wait for a "<tag> OK/NO/BAD" response */
			} else {
				/* FIXME: error?? */
				g_assert_not_reached ();
			}
			
			g_free (linebuf);
			linebuf = NULL;
		} else if (token.token == '*') {
			/* we got an untagged response, let the engine handle this */
			if (spruce_imap_engine_handle_untagged_1 (engine, &token, &ic->err) == -1)
				goto exception;
		} else if (token.token == SPRUCE_IMAP_TOKEN_ATOM && !strcmp (token.v.atom, ic->tag)) {
			/* we got "<tag> OK/NO/BAD" */
			fprintf (stderr, "got %s response\n", token.v.atom);
			
			if (spruce_imap_engine_next_token (engine, &token, &ic->err) == -1)
				goto exception;
			
			if (token.token == SPRUCE_IMAP_TOKEN_ATOM) {
				if (!strcmp (token.v.atom, "OK"))
					result = SPRUCE_IMAP_RESULT_OK;
				else if (!strcmp (token.v.atom, "NO"))
					result = SPRUCE_IMAP_RESULT_NO;
				else if (!strcmp (token.v.atom, "BAD"))
					result = SPRUCE_IMAP_RESULT_BAD;
				
				if (result == SPRUCE_IMAP_RESULT_NONE) {
					fprintf (stderr, "expected OK/NO/BAD but got %s\n", token.v.atom);
					goto unexpected;
				}
				
				if (spruce_imap_engine_next_token (engine, &token, &ic->err) == -1)
					goto exception;
				
				if (token.token == '[') {
					/* we have a response code */
					spruce_imap_stream_unget_token (engine->istream, &token);
					if (spruce_imap_engine_parse_resp_code (engine, &ic->err) == -1)
						goto exception;
				} else if (token.token != '\n') {
					/* just gobble up the rest of the line */
					if (spruce_imap_engine_line (engine, NULL, NULL, &ic->err) == -1)
						goto exception;
				}
			} else {
				fprintf (stderr, "expected anything but this: ");
				unexpected_token (&token);
				fprintf (stderr, "\n");
				
				goto unexpected;
			}
			
			break;
		} else {
			fprintf (stderr, "wtf is this: ");
			unexpected_token (&token);
			fprintf (stderr, "\n");
			
		unexpected:
			
			/* no fucking clue what we got... */
			if (spruce_imap_engine_line (engine, &linebuf, &len, &ic->err) == -1)
				goto exception;
			
			g_set_error (&ic->err, SPRUCE_ERROR, SPRUCE_ERROR_GENERIC,
				     _("Unexpected response from IMAP server %s: %s"),
				     engine->url->host, linebuf);
			
			g_free (linebuf);
			
			goto exception;
		}
	} while (1);
	
	/* status should always be ACTIVE here... */
	if (ic->status == SPRUCE_IMAP_COMMAND_ACTIVE) {
		ic->part = ic->part->next;
		if (ic->part == NULL || result) {
			ic->status = SPRUCE_IMAP_COMMAND_COMPLETE;
			ic->result = result;
			return 1;
		}
	}
	
	return 0;
	
 exception:
	
	ic->status = SPRUCE_IMAP_COMMAND_ERROR;
	
	return -1;
}


void
spruce_imap_command_reset (SpruceIMAPCommand *ic)
{
	int i;
	
	for (i = 0; i < ic->resp_codes->len; i++)
		spruce_imap_resp_code_free (ic->resp_codes->pdata[i]);
	g_ptr_array_set_size (ic->resp_codes, 0);
	
	if (ic->reset && ic->user_data)
		ic->reset (ic, ic->user_data);
	
	ic->status = SPRUCE_IMAP_COMMAND_QUEUED;
	ic->result = SPRUCE_IMAP_RESULT_NONE;
	ic->part = ic->parts;
	g_free (ic->tag);
	ic->tag = NULL;
	
	if (ic->err) {
		g_error_free (ic->err);
		ic->err = NULL;
	}
}
