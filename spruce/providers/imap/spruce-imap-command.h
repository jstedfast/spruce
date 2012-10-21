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


#ifndef __SPRUCE_IMAP_COMMAND_H__
#define __SPRUCE_IMAP_COMMAND_H__

#include <stdarg.h>
#include <gmime/gmime.h>
#include <spruce/spruce-list.h>

G_BEGIN_DECLS

struct _SpruceIMAPEngine;
struct _SpruceIMAPFolder;
struct _spruce_imap_token_t;

typedef struct _SpruceIMAPCommand SpruceIMAPCommand;
typedef struct _SpruceIMAPLiteral SpruceIMAPLiteral;

typedef int (* SpruceIMAPPlusCallback) (struct _SpruceIMAPEngine *engine,
					SpruceIMAPCommand *ic,
					const unsigned char *linebuf,
					size_t linelen, GError **err);

typedef int (* SpruceIMAPUntaggedCallback) (struct _SpruceIMAPEngine *engine,
					    SpruceIMAPCommand *ic,
					    guint32 index,
					    struct _spruce_imap_token_t *token,
					    GError **err);

typedef void (* SpruceIMAPCommandReset) (SpruceIMAPCommand *ic, void *user_data);

enum {
	SPRUCE_IMAP_LITERAL_STRING,
	SPRUCE_IMAP_LITERAL_STREAM,
	SPRUCE_IMAP_LITERAL_OBJECT,
	SPRUCE_IMAP_LITERAL_WRAPPER,
};

struct _SpruceIMAPLiteral {
	int type;
	union {
		char *string;
		GMimeStream *stream;
		GMimeObject *object;
		GMimeDataWrapper *wrapper;
	} literal;
};

typedef struct _SpruceIMAPCommandPart {
	struct _SpruceIMAPCommandPart *next;
	size_t buflen;
	char *buffer;
	
	SpruceIMAPLiteral *literal;
} SpruceIMAPCommandPart;

enum {
	SPRUCE_IMAP_COMMAND_QUEUED,
	SPRUCE_IMAP_COMMAND_ACTIVE,
	SPRUCE_IMAP_COMMAND_COMPLETE,
	SPRUCE_IMAP_COMMAND_ERROR,
};

enum {
	SPRUCE_IMAP_RESULT_NONE,
	SPRUCE_IMAP_RESULT_OK,
	SPRUCE_IMAP_RESULT_NO,
	SPRUCE_IMAP_RESULT_BAD,
};

struct _SpruceIMAPCommand {
	SpruceListNode node;
	
	struct _SpruceIMAPEngine *engine;
	
	unsigned int ref_count:26;
	unsigned int status:3;
	unsigned int result:3;
	int id;
	
	char *tag;
	
	GPtrArray *resp_codes;
	
	struct _SpruceIMAPFolder *folder;
	GError *err;
	
	/* command parts - logical breaks in the overall command based on literals */
	SpruceIMAPCommandPart *parts;
	
	/* current part */
	SpruceIMAPCommandPart *part;
	
	/* untagged handlers */
	GHashTable *untagged;
	
	/* '+' callback/data */
	SpruceIMAPPlusCallback plus;
	
	SpruceIMAPCommandReset reset;
	void *user_data;
};

SpruceIMAPCommand *spruce_imap_command_new (struct _SpruceIMAPEngine *engine, struct _SpruceIMAPFolder *folder,
					    const char *format, ...);
SpruceIMAPCommand *spruce_imap_command_newv (struct _SpruceIMAPEngine *engine, struct _SpruceIMAPFolder *folder,
					     const char *format, va_list args);

void spruce_imap_command_register_untagged (SpruceIMAPCommand *ic, const char *atom, SpruceIMAPUntaggedCallback untagged);

void spruce_imap_command_ref (SpruceIMAPCommand *ic);
void spruce_imap_command_unref (SpruceIMAPCommand *ic);

/* returns 1 when complete, 0 if there is more to do, or -1 on error */
int spruce_imap_command_step (SpruceIMAPCommand *ic);

void spruce_imap_command_reset (SpruceIMAPCommand *ic);

G_END_DECLS

#endif /* __SPRUCE_IMAP_COMMAND_H__ */
