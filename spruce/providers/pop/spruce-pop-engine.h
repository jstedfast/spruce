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


#ifndef __SPRUCE_POP_ENGINE_H__
#define __SPRUCE_POP_ENGINE_H__

#include <glib.h>
#include <glib-object.h>

#include <util/list.h>
#include <gmime/gmime-stream.h>

G_BEGIN_DECLS

#define SPRUCE_TYPE_POP_ENGINE            (spruce_pop_engine_get_type ())
#define SPRUCE_POP_ENGINE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), SPRUCE_TYPE_POP_ENGINE, SprucePOPEngine))
#define SPRUCE_POP_ENGINE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), SPRUCE_TYPE_POP_ENGINE, SprucePOPEngineClass))
#define SPRUCE_IS_POP_ENGINE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SPRUCE_TYPE_POP_ENGINE))
#define SPRUCE_IS_POP_ENGINE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), SPRUCE_TYPE_POP_ENGINE))
#define SPRUCE_POP_ENGINE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), SPRUCE_TYPE_POP_ENGINE, SprucePOPEngineClass))

typedef struct _SprucePOPEngine SprucePOPEngine;
typedef struct _SprucePOPEngineClass SprucePOPEngineClass;

typedef struct _SprucePOPCommand SprucePOPCommand;

typedef int (* SprucePOPCommandHandler) (SprucePOPEngine *engine, SprucePOPCommand *pc, const char *line, void *user_data);

enum {
	SPRUCE_POP_COMMAND_QUEUED          = -5,
	SPRUCE_POP_COMMAND_ACTIVE          = -4,
	SPRUCE_POP_COMMAND_CONTINUE        = -3,
	SPRUCE_POP_COMMAND_PROTOCOL_ERROR  = -2,
	SPRUCE_POP_COMMAND_ERR             = -1,
	SPRUCE_POP_COMMAND_OK              =  0
};

struct _SprucePOPCommand {
	struct _SprucePOPCommand *next;
	struct _SprucePOPCommand *prev;
	
	SprucePOPCommandHandler handler;
	void *user_data;
	char *cmd;
	int id;
	
	/* output */
	int status; /* QUEUED, ACTIVE, ERR, OK, ... */
	int error;  /* 0: success; -1: disconnected; >0: errno */
	int retval; /* return code from the handler func */
};

/* POP states */
enum {
	SPRUCE_POP_STATE_CONNECT,
	SPRUCE_POP_STATE_AUTH,
	SPRUCE_POP_STATE_TRANSACTION,
	SPRUCE_POP_STATE_UPDATE,
};

/* POP capabilities we care about */
enum {
	SPRUCE_POP_CAPA_APOP        = (1 << 0),
	SPRUCE_POP_CAPA_LOGIN_DELAY = (1 << 1),
	SPRUCE_POP_CAPA_PIPELINING  = (1 << 2),
	SPRUCE_POP_CAPA_RESP_CODES  = (1 << 3),
	SPRUCE_POP_CAPA_SASL        = (1 << 4),
	SPRUCE_POP_CAPA_STLS        = (1 << 5),
	SPRUCE_POP_CAPA_TOP         = (1 << 6),
	SPRUCE_POP_CAPA_UIDL        = (1 << 7),
	SPRUCE_POP_CAPA_USER        = (1 << 8),
	
	/* manually probed */
	SPRUCE_POP_CAPA_PROBED_TOP  = (1 << 9),
	SPRUCE_POP_CAPA_PROBED_UIDL = (1 << 10),
	SPRUCE_POP_CAPA_PROBED_USER = (1 << 11),
};

/* mask for capabilities returned by the CAPA command */
#define SPRUCE_POP_CAPA_MASK (0x01fe)

struct _SprucePOPEngine {
	GObject parent_object;
	
	struct _SprucePOPStream *stream;
	GHashTable *authtypes;
	guint login_delay;
	guint32 capa;
	char *apop;
	int state;
	
	int nextid;
	
	List queue;
};

struct _SprucePOPEngineClass {
	GObjectClass parent_class;
	
};


GType spruce_pop_engine_get_type (void);

SprucePOPEngine *spruce_pop_engine_new (void);

/* returns OK, ERR, or PROTOCOL_ERROR */
int spruce_pop_engine_take_stream (SprucePOPEngine *engine, struct _SprucePOPStream *stream);

/* returns OK, ERR, or PROTOCOL_ERROR */
int spruce_pop_engine_capa (SprucePOPEngine *engine);

/* queue a POP command */
SprucePOPCommand *spruce_pop_engine_queue (SprucePOPEngine *engine, SprucePOPCommandHandler handler,
					   void *user_data, const char *fmt, ...);
void spruce_pop_command_free (SprucePOPEngine *engine, SprucePOPCommand *pc);

int spruce_pop_engine_iterate (SprucePOPEngine *engine);


/* utility functions */
int spruce_pop_engine_get_line (SprucePOPEngine *engine, char **line, size_t *len);

G_END_DECLS

#endif /* __SPRUCE_POP_ENGINE_H__ */
