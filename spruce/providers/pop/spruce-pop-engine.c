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
#include <errno.h>
#include <ctype.h>

#include <glib.h>
#include <glib/gi18n.h>

#include <spruce/spruce-sasl.h>
#include <spruce/spruce-service.h>

#include "spruce-pop-stream.h"
#include "spruce-pop-engine.h"

#define d(x) x


static void spruce_pop_engine_class_init (SprucePOPEngineClass *klass);
static void spruce_pop_engine_init (SprucePOPEngine *engine, SprucePOPEngineClass *klass);
static void spruce_pop_engine_finalize (GObject *object);


static GObjectClass *parent_class = NULL;


GType
spruce_pop_engine_get_type (void)
{
	static GType type = 0;
	
	if (!type) {
		static const GTypeInfo info = {
			sizeof (SprucePOPEngineClass),
			NULL, /* base_class_init */
			NULL, /* base_class_finalize */
			(GClassInitFunc) spruce_pop_engine_class_init,
			NULL, /* class_finalize */
			NULL, /* class_data */
			sizeof (SprucePOPEngine),
			0,    /* n_preallocs */
			(GInstanceInitFunc) spruce_pop_engine_init,
		};
		
		type = g_type_register_static (G_TYPE_OBJECT, "SprucePOPEngine", &info, 0);
	}
	
	return type;
}


static void
spruce_pop_engine_class_init (SprucePOPEngineClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	
	parent_class = g_type_class_ref (G_TYPE_OBJECT);
	
	object_class->finalize = spruce_pop_engine_finalize;
}

static void
spruce_pop_engine_init (SprucePOPEngine *engine, SprucePOPEngineClass *klass)
{
	engine->stream = NULL;
	engine->authtypes = g_hash_table_new (g_str_hash, g_str_equal);
	engine->login_delay = 0;
	engine->apop = NULL;
	engine->capa = SPRUCE_POP_CAPA_USER;
	engine->state = 0;
	engine->nextid = 1;
	
	list_init (&engine->queue);
}

static void
spruce_pop_engine_finalize (GObject *object)
{
	SprucePOPEngine *engine = (SprucePOPEngine *) object;
	
	if (engine->stream)
		g_object_unref (engine->stream);
	
	g_free (engine->apop);
	
	g_hash_table_destroy (engine->authtypes);
	
	G_OBJECT_CLASS (parent_class)->finalize (object);
}



static int
pop_read_line (SprucePOPStream *pop_stream, char **line, size_t *len, GByteArray **outbuf)
{
	GByteArray *buf = NULL;
	int errnosav = 0;
	int ret;
	
	/* read the response */
	while ((ret = spruce_pop_stream_line (pop_stream, line, len)) > 0) {
		if (buf == NULL)
			buf = g_byte_array_new ();
		g_byte_array_append (buf, (unsigned char *) *line, *len);
	}
	
	if (ret == -1)
		errnosav = errno ? errno : -1;
	
	if (buf != NULL) {
		g_byte_array_append (buf, (unsigned char *) *line, *len);
		*line = (char *) buf->data;
		*len = buf->len - 1;
	}
	
	d(fprintf (stderr, "received: %s\r\n", *line));
	
	if (ret == -1) {
		if (buf != NULL)
			g_byte_array_free (buf, TRUE);
		
		*outbuf = NULL;
		
		return errnosav;
	}
	
	*outbuf = buf;
	
	return 0;
}


/**
 * spruce_pop_engine_get_line:
 * @engine: POP engine
 * @line: line pointer
 * @len: len pointer
 *
 * Reads a single line and points @line at it. Also sets @len to the
 * length of the line buffer.
 *
 * Note: @line must be free'd by the caller.
 *
 * Returns %0 on success, %-1 on unknown error, or errno.
 **/
int
spruce_pop_engine_get_line (SprucePOPEngine *engine, char **line, size_t *len)
{
	GByteArray *buf = NULL;
	int ret;
	
	if ((ret = pop_read_line (engine->stream, line, len, &buf)) != 0)
		return ret;
	
	if (buf == NULL) {
		buf = g_byte_array_new ();
		g_byte_array_append (buf, (unsigned char *) *line, *len + 1);
		*line = (char *) buf->data;
	}
	
	g_byte_array_free (buf, FALSE);
	
	return 0;
}

static int
pop_process_cmd (SprucePOPEngine *engine, SprucePOPCommand *pc)
{
	GByteArray *buf = NULL;
	char *line;
	size_t len;
	
	if (!strncmp (pc->cmd, "PASS ", 5))
		d(fprintf (stderr, "PASS xxx\r\n"));
	else
		d(fprintf (stderr, "sending : %s", pc->cmd));
	
	if (g_mime_stream_write ((GMimeStream *) engine->stream, pc->cmd, strlen (pc->cmd)) == -1) {
		pc->error = errno ? errno : -1;
		return -1;
	}
	
	/* read server response line */
	if (pop_read_line (engine->stream, &line, &len, &buf) != 0) {
		pc->status = SPRUCE_POP_COMMAND_PROTOCOL_ERROR;
		pc->error = errno ? errno : pc->status;
		return -1;
	}
	
	/* check the response code */
	if (!strncmp (line, "+OK", 3) && (line[3] == '\0' || isspace ((unsigned char) line[3]))) {
		pc->status = SPRUCE_POP_COMMAND_OK;
		line += 3;
	} else if (!strncmp (line, "-ERR", 4) && (line[3] == '\0' || isspace ((unsigned char) line[3]))) {
		pc->status = SPRUCE_POP_COMMAND_ERR;
		line += 4;
	} else if (line[0] == '+' && (line[1] == ' ' || line[1] == '\0') && !strncmp (pc->cmd, "AUTH", 4)) {
		/* SASL AUTH command continuation request */
		pc->status = SPRUCE_POP_COMMAND_CONTINUE;
	} else {
		/* huh? this should never happen */
		pc->status = SPRUCE_POP_COMMAND_PROTOCOL_ERROR;
		
		if (buf != NULL)
			g_byte_array_free (buf, TRUE);
		
		return -1;
	}
	
	if (pc->handler)
		pc->retval = pc->handler (engine, pc, line, pc->user_data);
	
	return 0;
}


/**
 * spruce_pop_engine_iterate:
 * @engine: POP engine
 *
 * Processes the first command in the queue.
 *
 * Returns the id of the processed command, %0 if there were no
 * commands to process, or %-1 on error.
 *
 * Note: more details on the error will be held on the
 * #SprucePOPCommand that failed.
 **/
int
spruce_pop_engine_iterate (SprucePOPEngine *engine)
{
	SprucePOPCommand *pc = NULL;
	
	if (list_is_empty (&engine->queue))
		return 0;
	
	pc = (SprucePOPCommand *) list_unlink_head (&engine->queue);
	pc->status = SPRUCE_POP_COMMAND_ACTIVE;
	
	if (pop_process_cmd (engine, pc) == -1)
		return -1;
	
	return pc->id;
}


static SprucePOPCommand *
pop_queue_cmd (SprucePOPEngine *pop, char *cmd, SprucePOPCommandHandler handler, void *user_data)
{
	SprucePOPCommand *pc;
	
	pc = g_new (SprucePOPCommand, 1);
	pc->user_data = user_data;
	pc->handler = handler;
	pc->cmd = cmd;
	pc->id = pop->nextid++;
	pc->status = SPRUCE_POP_COMMAND_QUEUED;
	pc->error = 0;
	pc->retval = 0;
	
	list_append (&pop->queue, (ListNode *) pc);
	
	return pc;
}


/**
 * spruce_pop_engine_queue:
 * @engine: POP engine
 * @handler: command handler callback
 * @user_data: user data to pass to @handler
 * @fmt: POP command format string
 * @Varargs: varargs to use to fill in @fmt
 *
 * Queues a command on the POP engine @engine.
 *
 * Returns a #SprucePOPCommand context.
 **/
SprucePOPCommand *
spruce_pop_engine_queue (SprucePOPEngine *engine, SprucePOPCommandHandler handler, void *user_data,
			 const char *fmt, ...)
{
	va_list args;
	char *cmd;
	
	va_start (args, fmt);
	cmd = g_strdup_vprintf (fmt, args);
	va_end (args);
	
	return pop_queue_cmd (engine, cmd, handler, user_data);
}


/**
 * spruce_pop_command_free:
 * @engine: POP engine
 * @pc: POP command
 *
 * Frees @pc and de-queues it if necessary.
 **/
void
spruce_pop_command_free (SprucePOPEngine *engine, SprucePOPCommand *pc)
{
	if (pc->status == SPRUCE_POP_COMMAND_QUEUED)
		list_unlink ((ListNode *) pc);
	
	g_free (pc->cmd);
	g_free (pc);
}


typedef void (* CapaParser) (SprucePOPEngine *engine, char *line);

static void
login_delay_parser (SprucePOPEngine *engine, char *line)
{
	register unsigned char *inptr = (unsigned char *) line;
	
	while (isspace (*inptr))
		inptr++;
	
	engine->login_delay = strtoul ((char *) inptr, NULL, 10);
}

static void
sasl_parser (SprucePOPEngine *engine, char *line)
{
	register unsigned char *mech, *inptr;
	SpruceServiceAuthType *auth;
	int done = 0;
	
	inptr = (unsigned char *) line + 4;
	do {
		mech = inptr + 1;
		while (isspace (*mech))
			mech++;
		
		inptr = mech;
		while (*inptr && !isspace (*inptr))
			inptr++;
		
		done = *inptr == '\0';
		*inptr = '\0';
		
		if ((auth = spruce_sasl_authtype ((char *) mech)) != NULL)
			g_hash_table_insert (engine->authtypes, auth->authproto, auth);
	} while (!done);
}

static struct {
	const char *name;
	int n;
	guint32 flag;
	CapaParser parser;
} capa_list[] = {
	{ "LOGIN-DELAY", 11, SPRUCE_POP_CAPA_LOGIN_DELAY, login_delay_parser },
	{ "PIPELINING",  10, SPRUCE_POP_CAPA_PIPELINING,  NULL               },
	{ "RESP-CODES",  10, SPRUCE_POP_CAPA_RESP_CODES,  NULL               },
	{ "SASL",         4, SPRUCE_POP_CAPA_SASL,        sasl_parser        },
	{ "STLS",         4, SPRUCE_POP_CAPA_STLS,        NULL               },
	{ "TOP",          3, SPRUCE_POP_CAPA_TOP,         NULL               },
	{ "UIDL",         4, SPRUCE_POP_CAPA_UIDL,        NULL               },
	{ "USER",         4, SPRUCE_POP_CAPA_USER,        NULL               },
};


static int
pop_capa_cb (SprucePOPEngine *engine, SprucePOPCommand *pc, const char *line, void *user_data)
{
	GByteArray *buf = NULL;
	char *linebuf;
	size_t len;
	guint i;
	int err;
	
	if (pc->status != SPRUCE_POP_COMMAND_OK)
		return -1;
	
	/* clear all CAPA response bits */
	engine->capa &= ~SPRUCE_POP_CAPA_MASK;
	
	do {
		if (buf != NULL) {
			g_byte_array_free (buf, TRUE);
			buf = NULL;
		}
		
		/* read a capability */
		if ((err = pop_read_line (engine->stream, &linebuf, &len, &buf)) != 0) {
			pc->error = err;
			return -1;
		}
		
		for (i = 0; i < G_N_ELEMENTS (capa_list); i++) {
			if (!strncmp (capa_list[i].name, linebuf, capa_list[i].n)) {
				engine->capa |= capa_list[i].flag;
				if (capa_list[i].parser)
					capa_list->parser (engine, linebuf);
				break;
			}
		}
	} while (strcmp (linebuf, ".") != 0);
	
	if (buf != NULL) {
		g_byte_array_free (buf, TRUE);
		buf = NULL;
	}
	
	return 0;
}


/**
 * spruce_pop_engine_capa:
 * @engine: POP engine
 *
 * Forces the POP engine to query the POP server for a list of capabilities.
 *
 * Returns OK on success, ERR on POP error, PROTOCOL_ERROR on unknown error, or errno.
 **/
int
spruce_pop_engine_capa (SprucePOPEngine *engine)
{
	SprucePOPCommand *pc;
	int id, retval = 0;
	
	pc = spruce_pop_engine_queue (engine, pop_capa_cb, NULL, "CAPA\r\n");
	while ((id = spruce_pop_engine_iterate (engine)) < pc->id && id != -1)
		;
	
	if (id == -1 || pc->retval == -1)
		retval = pc->error;
	else
		retval = pc->status;
	
	spruce_pop_command_free (engine, pc);
	
	return retval;
}


SprucePOPEngine *
spruce_pop_engine_new (void)
{
	return g_object_new (SPRUCE_TYPE_POP_ENGINE, NULL);
}

static gboolean
auth_free (gpointer key, gpointer value, gpointer user_data)
{
	return TRUE;
}


/**
 * spruce_pop_engine_take_stream:
 * @engine: POP engine
 * @stream: POP stream
 *
 * Gives ownership of @stream to @engine and reads the greeting from
 * the stream.
 *
 * Returns #SPRUCE_POP_COMMAND_OK on success, #SPRUCE_POP_COMMAND_ERR
 * on POP -ERR, #SPRUCE_POP_COMMAND_PROTOCOL_ERROR on unknown error,
 * or errno.
 *
 * Note: on error, @stream will be unref'd.
 **/
int
spruce_pop_engine_take_stream (SprucePOPEngine *engine, SprucePOPStream *stream)
{
	GByteArray *buf = NULL;
	register char *inptr;
	char *line;
	size_t len;
	int err;
	
	g_return_val_if_fail (SPRUCE_IS_POP_ENGINE (engine), SPRUCE_POP_COMMAND_PROTOCOL_ERROR);
	g_return_val_if_fail (engine->stream != stream, SPRUCE_POP_COMMAND_OK);
	
	if (engine->stream) {
		g_object_unref (engine->stream);
		engine->stream = NULL;
	}
	
	g_free (engine->apop);
	engine->apop = NULL;
	
	g_hash_table_foreach_remove (engine->authtypes, auth_free, NULL);
	list_init (&engine->queue);
	engine->state = 0;
	engine->capa = SPRUCE_POP_CAPA_USER;
	
	/* read the POP server greeting */
	if ((err = pop_read_line (stream, &line, &len, &buf)) != 0) {
		if (buf != NULL)
			g_byte_array_free (buf, TRUE);
		
		g_object_unref (stream);
		
		return err != -1 ? err : SPRUCE_POP_COMMAND_PROTOCOL_ERROR;
	}
	
	/* check that the greeting from the server is +OK */
	if (!(strncmp (line, "+OK", 3) == 0 && (isspace ((unsigned char) line[3]) || line[3] == '\0'))) {
		/* this should never happen... but is here for completeness */
		int retval;
		
		if (!strncmp (line, "-ERR", 4))
			retval = SPRUCE_POP_COMMAND_ERR;
		else
			retval = SPRUCE_POP_COMMAND_PROTOCOL_ERROR;
		
		if (buf != NULL)
			g_byte_array_free (buf, TRUE);
		
		g_object_unref (stream);
		
		return retval;
	}
	
	/* look for an APOP timestamp */
	inptr = line;
	while (*inptr && *inptr != '<')
		inptr++;
	
	if (*inptr == '<') {
		char *start = inptr;
		
		while (*inptr && *inptr != '>')
			inptr++;
		
		if (*inptr == '>') {
			inptr++;
			engine->apop = g_strndup (start, inptr - start);
			engine->capa |= SPRUCE_POP_CAPA_APOP;
		}
	}
	
	if (buf != NULL) {
		g_byte_array_free (buf, TRUE);
		buf = NULL;
	}
	
	engine->stream = stream;
	engine->state = SPRUCE_POP_STATE_AUTH;
	
	return SPRUCE_POP_COMMAND_OK;
}
