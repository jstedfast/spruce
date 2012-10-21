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


#ifndef __SPRUCE_IMAP_ENGINE_H__
#define __SPRUCE_IMAP_ENGINE_H__

#include <stdarg.h>
#include <gmime/gmime-stream.h>
#include <spruce/spruce-folder.h>
#include <spruce/spruce-service.h>
#include <spruce/spruce-session.h>
#include <spruce/spruce-list.h>

G_BEGIN_DECLS

#define SPRUCE_TYPE_IMAP_ENGINE            (spruce_imap_engine_get_type ())
#define SPRUCE_IMAP_ENGINE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), SPRUCE_TYPE_IMAP_ENGINE, SpruceIMAPEngine))
#define SPRUCE_IMAP_ENGINE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), SPRUCE_TYPE_IMAP_ENGINE, SpruceIMAPEngineClass))
#define SPRUCE_IS_IMAP_ENGINE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SPRUCE_TYPE_IMAP_ENGINE))
#define SPRUCE_IS_IMAP_ENGINE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), SPRUCE_TYPE_IMAP_ENGINE))
#define SPRUCE_IMAP_ENGINE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), SPRUCE_TYPE_IMAP_ENGINE, SpruceIMAPEngineClass))

typedef struct _SpruceIMAPEngine SpruceIMAPEngine;
typedef struct _SpruceIMAPEngineClass SpruceIMAPEngineClass;

struct _spruce_imap_token_t;
struct _SpruceIMAPCommand;
struct _SpruceIMAPFolder;
struct _SpruceIMAPStream;

typedef enum {
	SPRUCE_IMAP_ENGINE_DISCONNECTED,
	SPRUCE_IMAP_ENGINE_CONNECTED,
	SPRUCE_IMAP_ENGINE_PREAUTH,
	SPRUCE_IMAP_ENGINE_AUTHENTICATED,
	SPRUCE_IMAP_ENGINE_SELECTED,
	SPRUCE_IMAP_ENGINE_IDLE,
} spruce_imap_engine_t;

typedef enum {
	SPRUCE_IMAP_LEVEL_UNKNOWN,
	SPRUCE_IMAP_LEVEL_IMAP4,
	SPRUCE_IMAP_LEVEL_IMAP4REV1
} spruce_imap_level_t;

enum {
	SPRUCE_IMAP_CAPABILITY_IMAP4           = (1 << 0),
	SPRUCE_IMAP_CAPABILITY_IMAP4REV1       = (1 << 1),
	SPRUCE_IMAP_CAPABILITY_STATUS          = (1 << 2),
	SPRUCE_IMAP_CAPABILITY_NAMESPACE       = (1 << 3),
	SPRUCE_IMAP_CAPABILITY_UIDPLUS         = (1 << 4),
	SPRUCE_IMAP_CAPABILITY_LITERALPLUS     = (1 << 5),
	SPRUCE_IMAP_CAPABILITY_LOGINDISABLED   = (1 << 6),
	SPRUCE_IMAP_CAPABILITY_STARTTLS        = (1 << 7),
	SPRUCE_IMAP_CAPABILITY_UNSELECT        = (1 << 8),
	SPRUCE_IMAP_CAPABILITY_CONDSTORE       = (1 << 9),
	SPRUCE_IMAP_CAPABILITY_IDLE            = (1 << 10),
	
	/* Non-standard extensions */
	SPRUCE_IMAP_CAPABILITY_XGWEXTENSIONS   = (1 << 14),
	SPRUCE_IMAP_CAPABILITY_XGWMOVE         = (1 << 15),
	
	SPRUCE_IMAP_CAPABILITY_useful_lsub     = (1 << 20),
	SPRUCE_IMAP_CAPABILITY_utf8_search     = (1 << 21),
};

typedef enum {
	SPRUCE_IMAP_RESP_CODE_ALERT,
	SPRUCE_IMAP_RESP_CODE_BADCHARSET,
	SPRUCE_IMAP_RESP_CODE_CAPABILITY,
	SPRUCE_IMAP_RESP_CODE_PARSE,
	SPRUCE_IMAP_RESP_CODE_PERM_FLAGS,
	SPRUCE_IMAP_RESP_CODE_READONLY,
	SPRUCE_IMAP_RESP_CODE_READWRITE,
	SPRUCE_IMAP_RESP_CODE_TRYCREATE,
	SPRUCE_IMAP_RESP_CODE_UIDNEXT,
	SPRUCE_IMAP_RESP_CODE_UIDVALIDITY,
	SPRUCE_IMAP_RESP_CODE_UNSEEN,
	SPRUCE_IMAP_RESP_CODE_NEWNAME,
	SPRUCE_IMAP_RESP_CODE_APPENDUID,
	SPRUCE_IMAP_RESP_CODE_COPYUID,
	SPRUCE_IMAP_RESP_CODE_UIDNOTSTICKY,
	
	/* rfc4551 extension resp-codes */
	SPRUCE_IMAP_RESP_CODE_HIGHESTMODSEQ,
	SPRUCE_IMAP_RESP_CODE_NOMODSEQ,
	
	SPRUCE_IMAP_RESP_CODE_UNKNOWN,
} spruce_imap_resp_code_t;

typedef struct _SpruceIMAPRespCode {
	spruce_imap_resp_code_t code;
	union {
		guint32 flags;
		char *parse;
		guint32 uidnext;
		guint32 uidvalidity;
		guint32 unseen;
		char *newname[2];
		struct {
			guint32 uidvalidity;
			guint32 uid;
		} appenduid;
		struct {
			guint32 uidvalidity;
			char *srcset;
			char *destset;
		} copyuid;
		guint64 highestmodseq;
	} v;
} SpruceIMAPRespCode;

enum {
	SPRUCE_IMAP_UNTAGGED_ERROR = -1,
	SPRUCE_IMAP_UNTAGGED_OK,
	SPRUCE_IMAP_UNTAGGED_NO,
	SPRUCE_IMAP_UNTAGGED_BAD,
	SPRUCE_IMAP_UNTAGGED_PREAUTH,
	SPRUCE_IMAP_UNTAGGED_HANDLED,
};

typedef struct _SpruceIMAPNamespace {
	struct _SpruceIMAPNamespace *next;
	char *path;
	char sep;
} SpruceIMAPNamespace;

typedef struct _SpruceIMAPNamespaceList {
	SpruceIMAPNamespace *personal;
	SpruceIMAPNamespace *other;
	SpruceIMAPNamespace *shared;
} SpruceIMAPNamespaceList;

enum {
	SPRUCE_IMAP_ENGINE_MAXLEN_LINE,
	SPRUCE_IMAP_ENGINE_MAXLEN_TOKEN
};

typedef int (* SpruceIMAPReconnectFunc) (SpruceIMAPEngine *engine, GError **err);

struct _SpruceIMAPEngine {
	GObject parent_object;
	
	SpruceIMAPReconnectFunc reconnect;
	gboolean reconnecting;
	
	SpruceSession *session;
	SpruceService *service;
	SpruceURL *url;
	
	spruce_imap_engine_t state;
	spruce_imap_level_t level;
	guint32 capa;
	
	guint32 maxlen:31;
	guint32 maxlentype:1;
	
	SpruceIMAPNamespaceList namespaces;
	GHashTable *authtypes;               /* supported authtypes */
	
	struct _SpruceIMAPStream *istream;
	GMimeStream *ostream;
	
	unsigned char tagprefix;             /* 'A'..'Z' */
	unsigned int tag;                    /* next command tag */
	int nextid;
	
	struct _SpruceIMAPFolder *folder;    /* currently selected folder */
	
	SpruceList queue;                    /* queue of waiting commands */
	struct _SpruceIMAPCommand *current;
};

struct _SpruceIMAPEngineClass {
	GObjectClass parent_class;
	
	unsigned char tagprefix;
};


GType spruce_imap_engine_get_type (void);

SpruceIMAPEngine *spruce_imap_engine_new (SpruceService *service, SpruceIMAPReconnectFunc reconnect);

/* returns 0 on success or -1 on error */
int spruce_imap_engine_take_stream (SpruceIMAPEngine *engine, GMimeStream *stream, GError **err);

void spruce_imap_engine_disconnect (SpruceIMAPEngine *engine);

int spruce_imap_engine_capability (SpruceIMAPEngine *engine, GError **err);
int spruce_imap_engine_namespace (SpruceIMAPEngine *engine, GError **err);

int spruce_imap_engine_select_folder (SpruceIMAPEngine *engine, SpruceFolder *folder, GError **err);

struct _SpruceIMAPCommand *spruce_imap_engine_queue (SpruceIMAPEngine *engine,
						     SpruceFolder *folder,
						     const char *format, ...);
struct _SpruceIMAPCommand *spruce_imap_engine_prequeue (SpruceIMAPEngine *engine,
							SpruceFolder *folder,
							const char *format, ...);

void spruce_imap_engine_queue_command (SpruceIMAPEngine *engine, struct _SpruceIMAPCommand *ic);
void spruce_imap_engine_prequeue_command (SpruceIMAPEngine *engine, struct _SpruceIMAPCommand *ic);

void spruce_imap_engine_dequeue (SpruceIMAPEngine *engine, struct _SpruceIMAPCommand *ic);

int spruce_imap_engine_iterate (SpruceIMAPEngine *engine);


/* untagged response utility functions */
int spruce_imap_engine_handle_untagged_1 (SpruceIMAPEngine *engine, struct _spruce_imap_token_t *token, GError **err);
void spruce_imap_engine_handle_untagged (SpruceIMAPEngine *engine, GError **err);

/* stream wrapper utility functions */
int spruce_imap_engine_next_token (SpruceIMAPEngine *engine, struct _spruce_imap_token_t *token, GError **err);
int spruce_imap_engine_line (SpruceIMAPEngine *engine, unsigned char **line, size_t *len, GError **err);
int spruce_imap_engine_literal (SpruceIMAPEngine *engine, unsigned char **literal, size_t *len, GError **err);
int spruce_imap_engine_eat_line (SpruceIMAPEngine *engine, GError **err);


/* response code stuff */
int spruce_imap_engine_parse_resp_code (SpruceIMAPEngine *engine, GError **err);
void spruce_imap_resp_code_free (SpruceIMAPRespCode *rcode);

G_END_DECLS

#endif /* __SPRUCE_IMAP_ENGINE_H__ */
