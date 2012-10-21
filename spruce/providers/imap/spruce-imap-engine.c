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

#include <string.h>
#include <limits.h>
#include <ctype.h>
#include <errno.h>

#include <glib.h>
#include <glib/gi18n.h>

#include <spruce/spruce-sasl.h>
#include <spruce/spruce-error.h>

#include "spruce-imap-summary.h"
#include "spruce-imap-command.h"
#include "spruce-imap-stream.h"
#include "spruce-imap-folder.h"
#include "spruce-imap-utils.h"

#include "spruce-imap-engine.h"

#define d(x) x


static void spruce_imap_engine_class_init (SpruceIMAPEngineClass *klass);
static void spruce_imap_engine_init (SpruceIMAPEngine *engine, SpruceIMAPEngineClass *klass);
static void spruce_imap_engine_finalize (GObject *object);

static int parse_xgwextensions (SpruceIMAPEngine *engine, SpruceIMAPCommand *ic, guint32 index,
				spruce_imap_token_t *token, GError **err);


static GObjectClass *parent_class = NULL;


GType
spruce_imap_engine_get_type (void)
{
	static GType type = 0;
	
	if (!type) {
		static const GTypeInfo info = {
			sizeof (SpruceIMAPEngineClass),
			NULL, /* base_class_init */
			NULL, /* base_class_finalize */
			(GClassInitFunc) spruce_imap_engine_class_init,
			NULL, /* class_finalize */
			NULL, /* class_data */
			sizeof (SpruceIMAPEngine),
			0,    /* n_preallocs */
			(GInstanceInitFunc) spruce_imap_engine_init,
		};
		
		type = g_type_register_static (G_TYPE_OBJECT, "SpruceIMAPEngine", &info, 0);
	}
	
	return type;
}

static void
spruce_imap_engine_class_init (SpruceIMAPEngineClass *klass)
{
	GObjectClass *object_class = (GObjectClass *) klass;
	
	parent_class = g_type_class_ref (G_TYPE_OBJECT);
	
	klass->tagprefix = 'A';
	
	object_class->finalize = spruce_imap_engine_finalize;
}

static void
spruce_imap_engine_init (SpruceIMAPEngine *engine, SpruceIMAPEngineClass *klass)
{
	engine->state = SPRUCE_IMAP_ENGINE_DISCONNECTED;
	engine->level = SPRUCE_IMAP_LEVEL_UNKNOWN;
	
	engine->session = NULL;
	engine->service = NULL;
	engine->url = NULL;
	
	engine->istream = NULL;
	engine->ostream = NULL;
	
	engine->authtypes = g_hash_table_new (g_str_hash, g_str_equal);
	
	engine->capa = 0;
	
	/* this is the suggested default, impacts the max command line length we'll send */
	engine->maxlentype = SPRUCE_IMAP_ENGINE_MAXLEN_LINE;
	engine->maxlen = 1000;
	
	engine->namespaces.personal = NULL;
	engine->namespaces.other = NULL;
	engine->namespaces.shared = NULL;
	
	if (klass->tagprefix > 'Z')
		klass->tagprefix = 'A';
	
	engine->tagprefix = klass->tagprefix++;
	engine->tag = 0;
	
	engine->nextid = 1;
	
	engine->folder = NULL;
	
	spruce_list_init (&engine->queue);
}

static void
imap_namespace_clear (SpruceIMAPNamespace **namespace)
{
	SpruceIMAPNamespace *node, *next;
	
	node = *namespace;
	while (node != NULL) {
		next = node->next;
		g_free (node->path);
		g_free (node);
		node = next;
	}
	
	*namespace = NULL;
}

static void
spruce_imap_engine_finalize (GObject *object)
{
	SpruceIMAPEngine *engine = (SpruceIMAPEngine *) object;
	SpruceListNode *node;
	
	if (engine->istream)
		g_object_unref (engine->istream);
	
	if (engine->ostream)
		g_object_unref (engine->ostream);
	
	g_hash_table_foreach (engine->authtypes, (GHFunc) g_free, NULL);
	g_hash_table_destroy (engine->authtypes);
	
	imap_namespace_clear (&engine->namespaces.personal);
	imap_namespace_clear (&engine->namespaces.other);
	imap_namespace_clear (&engine->namespaces.shared);
	
	if (engine->folder)
		g_object_unref (engine->folder);
	
	while ((node = spruce_list_unlink_head (&engine->queue))) {
		node->next = NULL;
		node->prev = NULL;
		
		spruce_imap_command_unref ((SpruceIMAPCommand *) node);
	}
}


/**
 * spruce_imap_engine_new:
 * @service: IMAP service
 * @reconnect: reconnect callback function
 *
 * Returns a new imap engine
 **/
SpruceIMAPEngine *
spruce_imap_engine_new (SpruceService *service, SpruceIMAPReconnectFunc reconnect)
{
	SpruceIMAPEngine *engine;
	
	g_return_val_if_fail (SPRUCE_IS_SERVICE (service), NULL);
	
	engine = (SpruceIMAPEngine *) g_object_new (SPRUCE_TYPE_IMAP_ENGINE, NULL);
	engine->session = service->session;
	engine->reconnect = reconnect;
	engine->url = service->url;
	engine->service = service;
	
	return engine;
}


/**
 * spruce_imap_engine_take_stream:
 * @engine: imap engine
 * @stream: tcp stream
 * @err: GError
 *
 * Gives ownership of @stream to @engine and reads the greeting from
 * the stream.
 *
 * Returns %0 on success or %-1 on fail.
 *
 * Note: on error, @stream will be unref'd.
 **/
int
spruce_imap_engine_take_stream (SpruceIMAPEngine *engine, GMimeStream *stream, GError **err)
{
	spruce_imap_token_t token;
	int code;
	
	g_return_val_if_fail (SPRUCE_IS_IMAP_ENGINE (engine), -1);
	g_return_val_if_fail (GMIME_IS_STREAM (stream), -1);
	
	if (engine->istream)
		g_object_unref (engine->istream);
	
	if (engine->ostream)
		g_object_unref (engine->ostream);
	
	engine->istream = (SpruceIMAPStream *) spruce_imap_stream_new (stream);
	engine->ostream = g_mime_stream_buffer_new (stream, GMIME_STREAM_BUFFER_BLOCK_WRITE);
	engine->state = SPRUCE_IMAP_ENGINE_CONNECTED;
	g_object_unref (stream);
	
	if (spruce_imap_engine_next_token (engine, &token, err) == -1)
		goto exception;
	
	if (token.token != '*') {
		spruce_imap_utils_set_unexpected_token_error (err, engine, &token);
		goto exception;
	}
	
	if ((code = spruce_imap_engine_handle_untagged_1 (engine, &token, err)) == -1) {
		goto exception;
	} else if (code != SPRUCE_IMAP_UNTAGGED_OK && code != SPRUCE_IMAP_UNTAGGED_PREAUTH) {
		/* FIXME: set an error? */
		goto exception;
	}
	
	return 0;
	
 exception:
	
	spruce_imap_engine_disconnect (engine);
	
	return -1;
}


/**
 * spruce_imap_engine_disconnect:
 * @engine: IMAP engine
 *
 * Closes the engine's connection to the IMAP server and sets the
 * engine's state to #SPRUCE_IMAP_ENGINE_DISCONNECTED.
 **/
void
spruce_imap_engine_disconnect (SpruceIMAPEngine *engine)
{
	engine->state = SPRUCE_IMAP_ENGINE_DISCONNECTED;
	
	if (engine->istream) {
		g_object_unref (engine->istream);
		engine->istream = NULL;
	}
	
	if (engine->ostream) {
		g_object_unref (engine->ostream);
		engine->ostream = NULL;
	}
}


/**
 * spruce_imap_engine_capability:
 * @engine: IMAP engine
 * @err: GError
 *
 * Forces the IMAP engine to query the IMAP server for a list of capabilities.
 *
 * Returns %0 on success or %-1 on fail.
 **/
int
spruce_imap_engine_capability (SpruceIMAPEngine *engine, GError **err)
{
	SpruceIMAPCommand *ic;
	int id, retval = 0;
	
	ic = spruce_imap_engine_prequeue (engine, NULL, "CAPABILITY\r\n");
	while ((id = spruce_imap_engine_iterate (engine)) < ic->id && id != -1)
		;
	
	if (id == -1 || ic->status != SPRUCE_IMAP_COMMAND_COMPLETE) {
		g_propagate_error (err, ic->err);
		ic->err = NULL;
		retval = -1;
	}
	
	spruce_imap_command_unref (ic);
	
	if (retval == -1 || !(engine->capa & SPRUCE_IMAP_CAPABILITY_XGWEXTENSIONS))
		return retval;
	
	ic = spruce_imap_engine_prequeue (engine, NULL, "XGWEXTENSIONS\r\n");
	spruce_imap_command_register_untagged (ic, "XGWEXTENSIONS", parse_xgwextensions);
	
	while ((id = spruce_imap_engine_iterate (engine)) < ic->id && id != -1)
		;
	
	if (id == -1 || ic->status != SPRUCE_IMAP_COMMAND_COMPLETE) {
		g_propagate_error (err, ic->err);
		ic->err = NULL;
		retval = -1;
	}
	
	return retval;
}


/**
 * spruce_imap_engine_namespace:
 * @engine: IMAP engine
 * @err: GError
 *
 * Forces the IMAP engine to query the IMAP server for a list of namespaces.
 *
 * Returns %0 on success or %-1 on fail.
 **/
int
spruce_imap_engine_namespace (SpruceIMAPEngine *engine, GError **err)
{
	spruce_imap_list_t *list;
	GPtrArray *array = NULL;
	SpruceIMAPCommand *ic;
	int id, i;
	
	if (engine->capa & SPRUCE_IMAP_CAPABILITY_NAMESPACE) {
		ic = spruce_imap_engine_prequeue (engine, NULL, "NAMESPACE\r\n");
	} else {
		ic = spruce_imap_engine_prequeue (engine, NULL, "LIST \"\" \"\"\r\n");
		spruce_imap_command_register_untagged (ic, "LIST", spruce_imap_untagged_list);
		ic->user_data = array = g_ptr_array_new ();
	}
	
	while ((id = spruce_imap_engine_iterate (engine)) < ic->id && id != -1)
		;
	
	if (id == -1 || ic->status != SPRUCE_IMAP_COMMAND_COMPLETE) {
		g_propagate_error (err, ic->err);
		ic->err = NULL;
		spruce_imap_command_unref (ic);
		return -1;
	}
	
	if (array != NULL) {
		if (ic->result == SPRUCE_IMAP_RESULT_OK) {
			SpruceIMAPNamespace *namespace;
			
			g_assert (array->len == 1);
			list = array->pdata[0];
			
			namespace = g_new (SpruceIMAPNamespace, 1);
			namespace->next = NULL;
			namespace->path = g_strdup ("");
			namespace->sep = list->delim;
			
			engine->namespaces.personal = namespace;
		} else {
			/* should never *ever* happen */
		}
		
		for (i = 0; i < array->len; i++) {
			list = array->pdata[i];
			g_free (list->name);
			g_free (list);
		}
		
		g_ptr_array_free (array, TRUE);
	}
	
	spruce_imap_command_unref (ic);
	
	return 0;
}


int
spruce_imap_engine_select_folder (SpruceIMAPEngine *engine, SpruceFolder *folder, GError **err)
{
	SpruceIMAPRespCode *resp;
	SpruceIMAPCommand *ic;
	int id, retval = 0;
	int i;
	
	g_return_val_if_fail (SPRUCE_IS_IMAP_ENGINE (engine), -1);
	g_return_val_if_fail (SPRUCE_IS_IMAP_FOLDER (folder), -1);
	
	/* POSSIBLE FIXME: if the folder to be selected will already
	 * be selected by the time the queue is emptied, simply
	 * no-op? */
	
	ic = spruce_imap_engine_queue (engine, folder, "SELECT %F\r\n", folder);
	while ((id = spruce_imap_engine_iterate (engine)) < ic->id && id != -1)
		;
	
	if (id == -1 || ic->status != SPRUCE_IMAP_COMMAND_COMPLETE) {
		g_propagate_error (err, ic->err);
		ic->err = NULL;
		spruce_imap_command_unref (ic);
		return -1;
	}
	
	switch (ic->result) {
	case SPRUCE_IMAP_RESULT_OK:
		folder->mode = 0;
		for (i = 0; i < ic->resp_codes->len; i++) {
			resp = ic->resp_codes->pdata[i];
			switch (resp->code) {
			case SPRUCE_IMAP_RESP_CODE_PERM_FLAGS:
				folder->permanent_flags = resp->v.flags;
				break;
			case SPRUCE_IMAP_RESP_CODE_READONLY:
				folder->mode = SPRUCE_FOLDER_MODE_READ;
				break;
			case SPRUCE_IMAP_RESP_CODE_READWRITE:
				folder->mode = SPRUCE_FOLDER_MODE_READ_WRITE;
				break;
			case SPRUCE_IMAP_RESP_CODE_UIDNEXT:
				spruce_imap_summary_set_uidnext (folder->summary, resp->v.uidnext);
				break;
			case SPRUCE_IMAP_RESP_CODE_UIDVALIDITY:
				spruce_imap_summary_set_uidvalidity (folder->summary, resp->v.uidvalidity);
				break;
			case SPRUCE_IMAP_RESP_CODE_UNSEEN:
				spruce_imap_summary_set_unseen (folder->summary, resp->v.unseen);
				break;
			default:
				break;
			}
		}
		
		if (folder->mode == 0) {
			folder->mode = SPRUCE_FOLDER_MODE_READ;
			g_warning ("Expected to find [READ-ONLY] or [READ-WRITE] in SELECT response");
		}
		
		break;
	case SPRUCE_IMAP_RESULT_NO:
		/* FIXME: would be good to save the NO reason into the err message */
		g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_FOLDER_ILLEGAL_NAME,
			     _("Cannot select folder `%s': Invalid mailbox name"),
			     folder->full_name);
		retval = -1;
		break;
	case SPRUCE_IMAP_RESULT_BAD:
		g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_FOLDER_ILLEGAL_NAME,
			     _("Cannot select folder `%s': Bad command"),
			     folder->full_name);
		retval = -1;
		break;
	default:
		g_assert_not_reached ();
		retval = -1;
	}
	
	spruce_imap_command_unref (ic);
	
	return retval;
}


static struct {
	const char *name;
	guint32 flag;
} imap_capabilities[] = {
	{ "IMAP4",         SPRUCE_IMAP_CAPABILITY_IMAP4         }, /* rfc1730 */
	{ "IMAP4REV1",     SPRUCE_IMAP_CAPABILITY_IMAP4REV1     }, /* rfc3501 */
	{ "STATUS",        SPRUCE_IMAP_CAPABILITY_STATUS        },
	{ "NAMESPACE",     SPRUCE_IMAP_CAPABILITY_NAMESPACE     }, /* rfc2342 */
	{ "UIDPLUS",       SPRUCE_IMAP_CAPABILITY_UIDPLUS       }, /* rfc4315 */
	{ "LITERAL+",      SPRUCE_IMAP_CAPABILITY_LITERALPLUS   }, /* rfc2088 */
	{ "LOGINDISABLED", SPRUCE_IMAP_CAPABILITY_LOGINDISABLED }, /* rfc3501 */
	{ "STARTTLS",      SPRUCE_IMAP_CAPABILITY_STARTTLS      }, /* rfc3501 */
	{ "UNSELECT",      SPRUCE_IMAP_CAPABILITY_UNSELECT      }, /* rfc3691 */
	{ "CONDSTORE",     SPRUCE_IMAP_CAPABILITY_CONDSTORE     }, /* rfc4551 */
	{ "IDLE",          SPRUCE_IMAP_CAPABILITY_IDLE          }, /* rfc2177 */
	{ "XGWEXTENSIONS", SPRUCE_IMAP_CAPABILITY_XGWEXTENSIONS }, /* GroupWise extensions */
};

static struct {
	const char *name;
	guint32 flag;
} imap_xgwextensions[] = {
	{ "XGWMOVE",       SPRUCE_IMAP_CAPABILITY_XGWMOVE       }, /* GroupWise MOVE command */
};

static int
parse_xgwextensions (SpruceIMAPEngine *engine, SpruceIMAPCommand *ic, guint32 index, spruce_imap_token_t *token, GError **err)
{
	int i;
	
	if (spruce_imap_engine_next_token (engine, token, err) == -1)
		return -1;
	
	while (token->token == SPRUCE_IMAP_TOKEN_ATOM) {
		for (i = 0; i < G_N_ELEMENTS (imap_xgwextensions); i++) {
			if (!g_ascii_strcasecmp (imap_xgwextensions[i].name, token->v.atom))
				engine->capa |= imap_xgwextensions[i].flag;
		}
		
		if (spruce_imap_engine_next_token (engine, token, err) == -1)
			return -1;
	}
	
	if (token->token != '\n') {
		d(fprintf (stderr, "expected '\\n' at the end of the XGWEXTENSIONS response\n"));
		goto unexpected;
	}
	
	return 0;
	
 unexpected:
	
	spruce_imap_utils_set_unexpected_token_error (err, engine, token);
	
	return -1;
}

static gboolean
auth_free (gpointer key, gpointer value, gpointer user_data)
{
	g_free (key);
	return TRUE;
}

static int
engine_parse_capability (SpruceIMAPEngine *engine, int sentinel, GError **err)
{
	spruce_imap_token_t token;
	size_t i;
	
	engine->capa = SPRUCE_IMAP_CAPABILITY_utf8_search;
	engine->level = 0;
	
	g_hash_table_foreach_remove (engine->authtypes, (GHRFunc) auth_free, NULL);
	
	if (spruce_imap_engine_next_token (engine, &token, err) == -1)
		return -1;
	
	while (token.token == SPRUCE_IMAP_TOKEN_ATOM) {
		if (!g_ascii_strncasecmp ("AUTH=", token.v.atom, 5)) {
			SpruceServiceAuthType *auth;
			
			if ((auth = spruce_sasl_authtype (token.v.atom + 5)) != NULL)
				g_hash_table_insert (engine->authtypes, g_strdup (token.v.atom + 5), auth);
		} else {
			for (i = 0; i < G_N_ELEMENTS (imap_capabilities); i++) {
				if (!g_ascii_strcasecmp (imap_capabilities[i].name, token.v.atom))
					engine->capa |= imap_capabilities[i].flag;
			}
		}
		
		if (spruce_imap_engine_next_token (engine, &token, err) == -1)
			return -1;
	}
	
	if (token.token != sentinel) {
		spruce_imap_utils_set_unexpected_token_error (err, engine, &token);
		return -1;
	}
	
	/* unget our sentinel token */
	spruce_imap_stream_unget_token (engine->istream, &token);
	
	/* figure out which version of IMAP we are dealing with */
	if (engine->capa & SPRUCE_IMAP_CAPABILITY_IMAP4REV1) {
		engine->level = SPRUCE_IMAP_LEVEL_IMAP4REV1;
		engine->capa |= SPRUCE_IMAP_CAPABILITY_STATUS;
	} else if (engine->capa & SPRUCE_IMAP_CAPABILITY_IMAP4) {
		engine->level = SPRUCE_IMAP_LEVEL_IMAP4;
	} else {
		engine->level = SPRUCE_IMAP_LEVEL_UNKNOWN;
	}
	
	return 0;
}

static int
engine_parse_flags_list (SpruceIMAPEngine *engine, SpruceIMAPRespCode *resp, int perm, GError **err)
{
	guint32 flags = 0;
	
	if (spruce_imap_parse_flags_list (engine, &flags, err) == -1)
		return-1;
	
	if (resp != NULL)
		resp->v.flags = flags;
	
	if (engine->current && engine->current->folder) {
		if (perm)
			((SpruceFolder *) engine->current->folder)->permanent_flags = flags;
		/*else
		  ((SpruceFolder *) engine->current->folder)->folder_flags = flags;*/
	} else if (engine->folder) {
		if (perm)
			((SpruceFolder *) engine->folder)->permanent_flags = flags;
		/*else
		  ((SpruceFolder *) engine->folder)->folder_flags = flags;*/
	} else {
		fprintf (stderr, "We seem to be in a bit of a pickle. we've just parsed an untagged %s\n"
			 "response for a folder, yet we do not currently have a folder selected?\n",
			 perm ? "PERMANENTFLAGS" : "FLAGS");
	}
	
	return 0;
}

static int
engine_parse_flags (SpruceIMAPEngine *engine, GError **err)
{
	spruce_imap_token_t token;
	
	if (engine_parse_flags_list (engine, NULL, FALSE, err) == -1)
		return -1;
	
	if (spruce_imap_engine_next_token (engine, &token, err) == -1)
		return -1;
	
	if (token.token != '\n') {
		d(fprintf (stderr, "Expected to find a '\\n' token after the FLAGS response\n"));
		spruce_imap_utils_set_unexpected_token_error (err, engine, &token);
		return -1;
	}
	
	return 0;
}

static int
engine_parse_namespace (SpruceIMAPEngine *engine, GError **err)
{
	SpruceIMAPNamespace *namespaces[3], *node, *tail;
	spruce_imap_token_t token;
	int i, n = 0;
	
	imap_namespace_clear (&engine->namespaces.personal);
	imap_namespace_clear (&engine->namespaces.other);
	imap_namespace_clear (&engine->namespaces.shared);
	
	if (spruce_imap_engine_next_token (engine, &token, err) == -1)
		return -1;
	
	do {
		namespaces[n] = NULL;
		tail = (SpruceIMAPNamespace *) &namespaces[n];
		
		if (token.token == '(') {
			/* decode the list of namespace pairs */
			if (spruce_imap_engine_next_token (engine, &token, err) == -1)
				goto exception;
			
			while (token.token == '(') {
				/* decode a namespace pair */
				
				/* get the path name token */
				if (spruce_imap_engine_next_token (engine, &token, err) == -1)
					goto exception;
				
				if (token.token != SPRUCE_IMAP_TOKEN_QSTRING) {
					d(fprintf (stderr, "Expected to find a qstring token as first element in NAMESPACE pair\n"));
					spruce_imap_utils_set_unexpected_token_error (err, engine, &token);
					goto exception;
				}
				
				node = g_new (SpruceIMAPNamespace, 1);
				node->next = NULL;
				node->path = g_strdup (token.v.qstring);
				
				/* get the path delimiter token */
				if (spruce_imap_engine_next_token (engine, &token, err) == -1) {
					g_free (node->path);
					g_free (node);
					
					goto exception;
				}
				
				if (token.token != SPRUCE_IMAP_TOKEN_QSTRING || strlen (token.v.qstring) > 1) {
					d(fprintf (stderr, "Expected to find a qstring token as second element in NAMESPACE pair\n"));
					spruce_imap_utils_set_unexpected_token_error (err, engine, &token);
					g_free (node->path);
					g_free (node);
					
					goto exception;
				}
				
				node->sep = *token.v.qstring;
				tail->next = node;
				tail = node;
				
				/* canonicalise the namespace path */
				if (node->path[strlen (node->path) - 1] == node->sep)
					node->path[strlen (node->path) - 1] = '\0';
				
				/* get the closing ')' for this namespace pair */
				if (spruce_imap_engine_next_token (engine, &token, err) == -1)
					goto exception;
				
				if (token.token != ')') {
					d(fprintf (stderr, "Expected to find a ')' token to close the current namespace pair\n"));
					spruce_imap_utils_set_unexpected_token_error (err, engine, &token);
					
					goto exception;
				}
				
				/* get the next token (should be either '(' or ')') */
				if (spruce_imap_engine_next_token (engine, &token, err) == -1)
					goto exception;
			}
			
			if (token.token != ')') {
				d(fprintf (stderr, "Expected to find a ')' to close the current namespace list\n"));
				spruce_imap_utils_set_unexpected_token_error (err, engine, &token);
				goto exception;
			}
		} else if (token.token == SPRUCE_IMAP_TOKEN_NIL) {
			/* namespace list is NIL */
			namespaces[n] = NULL;
		} else {
			d(fprintf (stderr, "Expected to find either NIL or '(' token in untagged NAMESPACE response\n"));
			spruce_imap_utils_set_unexpected_token_error (err, engine, &token);
			goto exception;
		}
		
		/* get the next token (should be either '(', NIL, or '\n') */
		if (spruce_imap_engine_next_token (engine, &token, err) == -1)
			goto exception;
		
		n++;
	} while (n < 3);
	
	engine->namespaces.personal = namespaces[0];
	engine->namespaces.other = namespaces[1];
	engine->namespaces.shared = namespaces[2];
	
	return 0;
	
 exception:
	
	for (i = 0; i <= n; i++)
		imap_namespace_clear (&namespaces[i]);
	
	return -1;
}


/**
 * 
 * resp-text-code  = "ALERT" /
 *                   "BADCHARSET" [SP "(" astring *(SP astring) ")" ] /
 *                   capability-data / "PARSE" /
 *                   "PERMANENTFLAGS" SP "(" [flag-perm *(SP flag-perm)] ")" /
 *                   "READ-ONLY" / "READ-WRITE" / "TRYCREATE" /
 *                   "UIDNEXT" SP nz-number / "UIDVALIDITY" SP nz-number /
 *                   "UNSEEN" SP nz-number /
 *                   atom [SP 1*<any TEXT-CHAR except "]">]
 **/

static struct {
	const char *name;
	spruce_imap_resp_code_t code;
	int save;
} imap_resp_codes[] = {
	{ "ALERT",          SPRUCE_IMAP_RESP_CODE_ALERT,         0 },
	{ "BADCHARSET",     SPRUCE_IMAP_RESP_CODE_BADCHARSET,    0 },
	{ "CAPABILITY",     SPRUCE_IMAP_RESP_CODE_CAPABILITY,    0 },
	{ "PARSE",          SPRUCE_IMAP_RESP_CODE_PARSE,         1 },
	{ "PERMANENTFLAGS", SPRUCE_IMAP_RESP_CODE_PERM_FLAGS,    1 },
	{ "READ-ONLY",      SPRUCE_IMAP_RESP_CODE_READONLY,      1 },
	{ "READ-WRITE",     SPRUCE_IMAP_RESP_CODE_READWRITE,     1 },
	{ "TRYCREATE",      SPRUCE_IMAP_RESP_CODE_TRYCREATE,     1 },
	{ "UIDNEXT",        SPRUCE_IMAP_RESP_CODE_UIDNEXT,       1 },
	{ "UIDVALIDITY",    SPRUCE_IMAP_RESP_CODE_UIDVALIDITY,   1 },
	{ "UNSEEN",         SPRUCE_IMAP_RESP_CODE_UNSEEN,        1 },
	{ "NEWNAME",        SPRUCE_IMAP_RESP_CODE_NEWNAME,       1 },
	
	/* rfc4315 (UIDPLUS extension) */
	{ "APPENDUID",      SPRUCE_IMAP_RESP_CODE_APPENDUID,     1 },
	{ "COPYUID",        SPRUCE_IMAP_RESP_CODE_COPYUID,       1 },
	{ "UIDNOTSTICKY",   SPRUCE_IMAP_RESP_CODE_UIDNOTSTICKY,  1 },
	
	/* rfc4551 (currently a draft) extension resp-code */
	{ "HIGHESTMODSEQ",  SPRUCE_IMAP_RESP_CODE_HIGHESTMODSEQ, 1 },
	{ "NOMODSEQ",       SPRUCE_IMAP_RESP_CODE_NOMODSEQ,      1 },
};


int
spruce_imap_engine_parse_resp_code (SpruceIMAPEngine *engine, GError **err)
{
	SpruceIMAPRespCode *resp = NULL;
	spruce_imap_resp_code_t code;
	spruce_imap_token_t token;
	unsigned char *linebuf;
	size_t len;
	
	if (spruce_imap_engine_next_token (engine, &token, err) == -1)
		return -1;
	
	if (token.token != '[') {
		d(fprintf (stderr, "Expected a '[' token (followed by a RESP-CODE)\n"));
		spruce_imap_utils_set_unexpected_token_error (err, engine, &token);
		return -1;
	}
	
	if (spruce_imap_engine_next_token (engine, &token, err) == -1)
		return -1;
	
	if (token.token != SPRUCE_IMAP_TOKEN_ATOM) {
		d(fprintf (stderr, "Expected an atom token containing a RESP-CODE\n"));
		spruce_imap_utils_set_unexpected_token_error (err, engine, &token);
		return -1;
	}
	
	for (code = 0; code < G_N_ELEMENTS (imap_resp_codes); code++) {
		if (!strcmp (imap_resp_codes[code].name, token.v.atom)) {
			if (engine->current && imap_resp_codes[code].save) {
				resp = g_new0 (SpruceIMAPRespCode, 1);
				resp->code = code;
			}
			break;
		}
	}
	
	switch (code) {
	case SPRUCE_IMAP_RESP_CODE_BADCHARSET:
		/* apparently we don't support UTF-8 afterall */
		engine->capa &= ~SPRUCE_IMAP_CAPABILITY_utf8_search;
		break;
	case SPRUCE_IMAP_RESP_CODE_CAPABILITY:
		/* capability list follows */
		if (engine_parse_capability (engine, ']', err) == -1)
			goto exception;
		break;
	case SPRUCE_IMAP_RESP_CODE_PERM_FLAGS:
		/* flag list follows */
		if (engine_parse_flags_list (engine, resp, TRUE, err) == -1)
			goto exception;
		break;
	case SPRUCE_IMAP_RESP_CODE_READONLY:
		break;
	case SPRUCE_IMAP_RESP_CODE_READWRITE:
		break;
	case SPRUCE_IMAP_RESP_CODE_TRYCREATE:
		break;
	case SPRUCE_IMAP_RESP_CODE_UIDNEXT:
		if (spruce_imap_engine_next_token (engine, &token, err) == -1)
			goto exception;
		
		if (token.token != SPRUCE_IMAP_TOKEN_NUMBER) {
			d(fprintf (stderr, "Expected an nz_number token as an argument to the UIDNEXT RESP-CODE\n"));
			spruce_imap_utils_set_unexpected_token_error (err, engine, &token);
			goto exception;
		}
		
		if (resp != NULL)
			resp->v.uidnext = token.v.number;
		
		break;
	case SPRUCE_IMAP_RESP_CODE_UIDVALIDITY:
		if (spruce_imap_engine_next_token (engine, &token, err) == -1)
			goto exception;
		
		if (token.token != SPRUCE_IMAP_TOKEN_NUMBER) {
			d(fprintf (stderr, "Expected an nz_number token as an argument to the UIDVALIDITY RESP-CODE\n"));
			spruce_imap_utils_set_unexpected_token_error (err, engine, &token);
			goto exception;
		}
		
		if (resp != NULL)
			resp->v.uidvalidity = token.v.number;
		
		break;
	case SPRUCE_IMAP_RESP_CODE_UNSEEN:
		if (spruce_imap_engine_next_token (engine, &token, err) == -1)
			return -1;
		
		if (token.token != SPRUCE_IMAP_TOKEN_NUMBER) {
			d(fprintf (stderr, "Expected an nz_number token as an argument to the UNSEEN RESP-CODE\n"));
			spruce_imap_utils_set_unexpected_token_error (err, engine, &token);
			goto exception;
		}
		
		if (resp != NULL)
			resp->v.unseen = token.v.number;
		
		break;
	case SPRUCE_IMAP_RESP_CODE_NEWNAME:
		/* this RESP-CODE may actually be removed - see here:
		 * http://www.washington.edu/imap/listarch/2001/msg00058.html */
		
		if (spruce_imap_engine_next_token (engine, &token, err) == -1)
			return -1;
		
		if (token.token != SPRUCE_IMAP_TOKEN_ATOM && token.token != SPRUCE_IMAP_TOKEN_QSTRING) {
			d(fprintf (stderr, "Expected an atom or qstring token as the first argument to the NEWNAME RESP-CODE\n"));
			spruce_imap_utils_set_unexpected_token_error (err, engine, &token);
			goto exception;
		}
		
		if (resp != NULL)
			resp->v.newname[0] = g_strdup (token.v.atom);
		
		if (token.token != SPRUCE_IMAP_TOKEN_ATOM && token.token != SPRUCE_IMAP_TOKEN_QSTRING) {
			d(fprintf (stderr, "Expected an atom or qstring token as the second argument to the NEWNAME RESP-CODE\n"));
			spruce_imap_utils_set_unexpected_token_error (err, engine, &token);
			goto exception;
		}
		
		if (resp != NULL)
			resp->v.newname[1] = g_strdup (token.v.atom);
		
		break;
	case SPRUCE_IMAP_RESP_CODE_APPENDUID:
		if (spruce_imap_engine_next_token (engine, &token, err) == -1)
			return -1;
		
		if (token.token != SPRUCE_IMAP_TOKEN_NUMBER) {
			d(fprintf (stderr, "Expected an nz_number token as the first argument to the APPENDUID RESP-CODE\n"));
			spruce_imap_utils_set_unexpected_token_error (err, engine, &token);
			goto exception;
		}
		
		if (resp != NULL)
			resp->v.appenduid.uidvalidity = token.v.number;
		
		if (spruce_imap_engine_next_token (engine, &token, err) == -1)
			return -1;
		
		if (token.token != SPRUCE_IMAP_TOKEN_NUMBER) {
			d(fprintf (stderr, "Expected an nz_number token as the second argument to the APPENDUID RESP-CODE\n"));
			spruce_imap_utils_set_unexpected_token_error (err, engine, &token);
			goto exception;
		}
		
		if (resp != NULL)
			resp->v.appenduid.uid = token.v.number;
		
		break;
	case SPRUCE_IMAP_RESP_CODE_COPYUID:
		if (spruce_imap_engine_next_token (engine, &token, err) == -1)
			return -1;
		
		if (token.token != SPRUCE_IMAP_TOKEN_NUMBER) {
			d(fprintf (stderr, "Expected an nz_number token as the first argument to the COPYUID RESP-CODE\n"));
			spruce_imap_utils_set_unexpected_token_error (err, engine, &token);
			goto exception;
		}
		
		if (resp != NULL)
			resp->v.copyuid.uidvalidity = token.v.number;
		
		if (spruce_imap_engine_next_token (engine, &token, err) == -1)
			return -1;
		
		if (token.token != SPRUCE_IMAP_TOKEN_ATOM) {
			d(fprintf (stderr, "Expected an atom token as the second argument to the COPYUID RESP-CODE\n"));
			spruce_imap_utils_set_unexpected_token_error (err, engine, &token);
			goto exception;
		}
		
		if (resp != NULL)
			resp->v.copyuid.srcset = g_strdup (token.v.atom);
		
		if (spruce_imap_engine_next_token (engine, &token, err) == -1)
			return -1;
		
		if (token.token != SPRUCE_IMAP_TOKEN_ATOM) {
			d(fprintf (stderr, "Expected an atom token as the third argument to the APPENDUID RESP-CODE\n"));
			spruce_imap_utils_set_unexpected_token_error (err, engine, &token);
			goto exception;
		}
		
		if (resp != NULL)
			resp->v.copyuid.destset = g_strdup (token.v.atom);
		
		break;
	case SPRUCE_IMAP_RESP_CODE_UIDNOTSTICKY:
		/* nothing to parse */
		break;
	case SPRUCE_IMAP_RESP_CODE_HIGHESTMODSEQ:
		if (spruce_imap_engine_next_token (engine, &token, err) == -1)
			goto exception;
		
		if (token.token != SPRUCE_IMAP_TOKEN_NUMBER) {
			d(fprintf (stderr, "Expected an nz_number token as an argument to the HIGHESTMODSEQ RESP-CODE\n"));
			spruce_imap_utils_set_unexpected_token_error (err, engine, &token);
			goto exception;
		}
		
		if (resp != NULL)
			resp->v.highestmodseq = token.v.number;
		
		break;
	case SPRUCE_IMAP_RESP_CODE_NOMODSEQ:
		/* nothing to parse */
		break;
	default:
		d(fprintf (stderr, "Unknown RESP-CODE encountered: %s\n", token.v.atom));
		
		/* extensions are of the form: "[" atom [SPACE 1*<any TEXT_CHAR except "]">] "]" */
		
		/* eat up the TEXT_CHARs */
		while (token.token != ']' && token.token != '\n') {
			if (spruce_imap_engine_next_token (engine, &token, err) == -1)
				goto exception;
		}
		
		break;
	}
	
	while (token.token != ']' && token.token != '\n') {
		if (spruce_imap_engine_next_token (engine, &token, err) == -1)
			goto exception;
	}
	
	if (token.token != ']') {
		spruce_imap_utils_set_unexpected_token_error (err, engine, &token);
		d(fprintf (stderr, "Expected to find a ']' token after the RESP-CODE\n"));
		return -1;
	}
	
	if (code == SPRUCE_IMAP_RESP_CODE_ALERT) {
		if (spruce_imap_engine_line (engine, &linebuf, &len, err) == -1)
			goto exception;
		
		spruce_session_alert_user (engine->session, (const char *) linebuf);
		g_free (linebuf);
	} else if (resp != NULL && code == SPRUCE_IMAP_RESP_CODE_PARSE) {
		if (spruce_imap_engine_line (engine, &linebuf, &len, err) == -1)
			goto exception;
		
		resp->v.parse = (char *) linebuf;
	} else {
		/* eat up the rest of the response */
		if (spruce_imap_engine_line (engine, NULL, NULL, err) == -1)
			goto exception;
	}
	
	if (resp != NULL)
		g_ptr_array_add (engine->current->resp_codes, resp);
	
	return 0;
	
 exception:
	
	if (resp != NULL)
		spruce_imap_resp_code_free (resp);
	
	return -1;
}



/* returns -1 on error, or one of SPRUCE_IMAP_UNTAGGED_[OK,NO,BAD,PREAUTH,HANDLED] on success */
int
spruce_imap_engine_handle_untagged_1 (SpruceIMAPEngine *engine, spruce_imap_token_t *token, GError **err)
{
	int code = SPRUCE_IMAP_UNTAGGED_HANDLED;
	SpruceIMAPCommand *ic = engine->current;
	SpruceIMAPUntaggedCallback untagged;
	SpruceFolder *folder;
	unsigned int v;
	
	if (spruce_imap_engine_next_token (engine, token, err) == -1)
		return -1;
	
	if (token->token == SPRUCE_IMAP_TOKEN_ATOM) {
		if (!strcmp ("BYE", token->v.atom)) {
			/* we don't care if we fail here, either way we've been disconnected */
			if (spruce_imap_engine_next_token (engine, token, NULL) == 0) {
				if (token->token == '[') {
					spruce_imap_stream_unget_token (engine->istream, token);
					spruce_imap_engine_parse_resp_code (engine, NULL);
				} else {
					spruce_imap_engine_line (engine, NULL, NULL, NULL);
				}
			}
			
			engine->state = SPRUCE_IMAP_ENGINE_DISCONNECTED;
			
			/* only return error if we didn't request a disconnect */
			if (ic && strcmp (ic->parts->buffer, "LOGOUT\r\n") != 0) {
				g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_GENERIC,
					     _("IMAP server %s unexpectedly disconnected: %s"),
					     engine->url->host, _("Got BYE response"));
				
				return -1;
			}
		} else if (!strcmp ("CAPABILITY", token->v.atom)) {
			/* capability tokens follow */
			if (engine_parse_capability (engine, '\n', err) == -1)
				return -1;
			
			/* find the eoln token */
			if (spruce_imap_engine_next_token (engine, token, err) == -1)
				return -1;
			
			if (token->token != '\n') {
				spruce_imap_utils_set_unexpected_token_error (err, engine, token);
				return -1;
			}
		} else if (!strcmp ("FLAGS", token->v.atom)) {
			/* flags list follows */
			if (engine_parse_flags (engine, err) == -1)
				return -1;
		} else if (!strcmp ("NAMESPACE", token->v.atom)) {
			if (engine_parse_namespace (engine, err) == -1)
				return -1;
		} else if (!strcmp ("NO", token->v.atom) || !strcmp ("BAD", token->v.atom)) {
			code = !strcmp ("NO", token->v.atom) ? SPRUCE_IMAP_UNTAGGED_NO : SPRUCE_IMAP_UNTAGGED_BAD;
			
			/* our command has been rejected */
			if (spruce_imap_engine_next_token (engine, token, err) == -1)
				return -1;
			
			if (token->token == '[') {
				/* we have a resp code */
				spruce_imap_stream_unget_token (engine->istream, token);
				if (spruce_imap_engine_parse_resp_code (engine, err) == -1)
					return -1;
			} else if (token->token != '\n') {
				/* we just have resp text */
				if (spruce_imap_engine_line (engine, NULL, NULL, err) == -1)
					return -1;
			}
		} else if (!strcmp ("OK", token->v.atom)) {
			code = SPRUCE_IMAP_UNTAGGED_OK;
			
			if (engine->state == SPRUCE_IMAP_ENGINE_CONNECTED) {
				/* initial server greeting */
				engine->state = SPRUCE_IMAP_ENGINE_PREAUTH;
			}
			
			if (spruce_imap_engine_next_token (engine, token, err) == -1)
				return -1;
			
			if (token->token == '[') {
				/* we have a resp code */
				spruce_imap_stream_unget_token (engine->istream, token);
				if (spruce_imap_engine_parse_resp_code (engine, err) == -1)
					return -1;
			} else {
				/* we just have resp text */
				if (spruce_imap_engine_line (engine, NULL, NULL, err) == -1)
					return -1;
			}
		} else if (!strcmp ("PREAUTH", token->v.atom)) {
			code = SPRUCE_IMAP_UNTAGGED_PREAUTH;
			
			if (engine->state == SPRUCE_IMAP_ENGINE_CONNECTED)
				engine->state = SPRUCE_IMAP_ENGINE_AUTHENTICATED;
			
			if (spruce_imap_engine_parse_resp_code (engine, err) == -1)
				return -1;
		} else if (ic && (untagged = g_hash_table_lookup (ic->untagged, token->v.atom))) {
			/* registered untagged handler for imap command */
			if (untagged (engine, ic, 0, token, err) == -1)
				return -1;
		} else {
			d(fprintf (stderr, "Unhandled atom token in untagged response: %s", token->v.atom));
			
			if (spruce_imap_engine_eat_line (engine, err) == -1)
				return -1;
		}
	} else if (token->token == SPRUCE_IMAP_TOKEN_NUMBER) {
		/* we probably have something like "* 1 EXISTS" */
		v = token->v.number;
		
		if (spruce_imap_engine_next_token (engine, token, err) == -1)
			return -1;
		
		if (token->token != SPRUCE_IMAP_TOKEN_ATOM) {
			spruce_imap_utils_set_unexpected_token_error (err, engine, token);
			
			return -1;
		}
		
		/* which folder is this EXISTS/EXPUNGE/RECENT acting on? */
		if (engine->current && engine->current->folder)
			folder = (SpruceFolder *) engine->current->folder;
		else if (engine->folder)
			folder = (SpruceFolder *) engine->folder;
		else
			folder = NULL;
		
		/* NOTE: these can be over-ridden by a registered untagged response handler */
		if (!strcmp ("EXISTS", token->v.atom)) {
			spruce_imap_summary_set_exists (folder->summary, v);
		} else if (!strcmp ("EXPUNGE", token->v.atom) || !strcmp ("XGWMOVE", token->v.atom)) {
			spruce_imap_summary_expunge (folder->summary, (int) v);
		} else if (!strcmp ("RECENT", token->v.atom)) {
			spruce_imap_summary_set_recent (folder->summary, v);
		} else if (ic && (untagged = g_hash_table_lookup (ic->untagged, token->v.atom))) {
			/* registered untagged handler for imap command */
			if (untagged (engine, ic, v, token, err) == -1)
				return -1;
		} else {
			d(fprintf (stderr, "Unrecognized untagged response: * %u %s\n",
				   v, token->v.atom));
		}
		
		/* find the eoln token */
		if (spruce_imap_engine_eat_line (engine, err) == -1)
			return -1;
	} else {
		spruce_imap_utils_set_unexpected_token_error (err, engine, token);
		
		return -1;
	}
	
	return code;
}


void
spruce_imap_engine_handle_untagged (SpruceIMAPEngine *engine, GError **err)
{
	spruce_imap_token_t token;
	
	g_return_if_fail (SPRUCE_IS_IMAP_ENGINE (engine));
	
	do {
		if (spruce_imap_engine_next_token (engine, &token, err) == -1)
			goto exception;
		
		if (token.token != '*')
			break;
		
		if (spruce_imap_engine_handle_untagged_1 (engine, &token, err) == -1)
			goto exception;
	} while (1);
	
	spruce_imap_stream_unget_token (engine->istream, &token);
	
	return;
	
 exception:
	
	engine->state = SPRUCE_IMAP_ENGINE_DISCONNECTED;
}


static int
imap_process_command (SpruceIMAPEngine *engine, SpruceIMAPCommand *ic)
{
	int retval;
	
	while ((retval = spruce_imap_command_step (ic)) == 0)
		;
	
	if (retval == -1) {
		engine->state = SPRUCE_IMAP_ENGINE_DISCONNECTED;
		return -1;
	}
	
	return 0;
}


static void
engine_prequeue_folder_select (SpruceIMAPEngine *engine)
{
	SpruceIMAPCommand *ic;
	const char *cmd;
	
	ic = (SpruceIMAPCommand *) engine->queue.head;
	cmd = (const char *) ic->parts->buffer;
	
	if (!ic->folder || (!strncmp (cmd, "SELECT ", 7) || !strncmp (cmd, "EXAMINE ", 8)) ||
	    (engine->state == SPRUCE_IMAP_ENGINE_SELECTED && (ic->folder == engine->folder))) {
		/* no need to pre-queue a SELECT */
		return;
	}
	
	/* we need to pre-queue a SELECT */
	ic = spruce_imap_engine_prequeue (engine, (SpruceFolder *) ic->folder, "SELECT %F\r\n", ic->folder);
	ic->user_data = engine;
	
	spruce_imap_command_unref (ic);
}


static int
engine_state_change (SpruceIMAPEngine *engine, SpruceIMAPCommand *ic)
{
	const char *cmd;
	int retval = 0;
	
	cmd = ic->parts->buffer;
	if (!strncmp (cmd, "SELECT ", 7) || !strncmp (cmd, "EXAMINE ", 8)) {
		if (ic->result == SPRUCE_IMAP_RESULT_OK) {
			/* Update the selected folder */
			g_object_ref (ic->folder);
			if (engine->folder)
				g_object_unref (engine->folder);
			engine->folder = ic->folder;
			
			engine->state = SPRUCE_IMAP_ENGINE_SELECTED;
		} else if (ic->user_data == engine) {
			/* the engine pre-queued this SELECT command */
			retval = -1;
		}
	} else if (!strncmp (cmd, "UNSELECT", 8) || !strncmp (cmd, "CLOSE", 5)) {
		if (ic->result == SPRUCE_IMAP_RESULT_OK) {
			engine->state = SPRUCE_IMAP_ENGINE_AUTHENTICATED;
			if (engine->folder) {
				g_object_unref (engine->folder);
				engine->folder = NULL;
			}
		}
	} else if (!strncmp (cmd, "LOGOUT", 6)) {
		engine->state = SPRUCE_IMAP_ENGINE_DISCONNECTED;
		if (engine->folder) {
			g_object_unref (engine->folder);
			engine->folder = NULL;
		}
	}
	
	return retval;
}

/**
 * spruce_imap_engine_iterate:
 * @engine: IMAP engine
 *
 * Processes the first command in the queue.
 *
 * Returns the id of the processed command, %0 if there were no
 * commands to process, or %-1 on error.
 *
 * Note: more details on the error will be held on the
 * #SpruceIMAPCommand that failed.
 **/
int
spruce_imap_engine_iterate (SpruceIMAPEngine *engine)
{
	SpruceIMAPCommand *ic, *nic;
	GPtrArray *resp_codes;
	GError *err = NULL;
	int retries = 0;
	int retval;
	
	if (spruce_list_is_empty (&engine->queue))
		return 0;
	
 retry:
	/* FIXME: it would be nicer if we didn't have to check the stream's disconnected status */
	if ((engine->state == SPRUCE_IMAP_ENGINE_DISCONNECTED || engine->istream->disconnected)
	    && !engine->reconnecting) {
		engine->reconnecting = TRUE;
		retval = engine->reconnect (engine, &err);
		engine->reconnecting = FALSE;
		
		if (retval == -1) {
			/* pop the first command and act as tho it failed (which, technically, it did...) */
			ic = (SpruceIMAPCommand *) spruce_list_unlink_head (&engine->queue);
			ic->status = SPRUCE_IMAP_COMMAND_ERROR;
			g_propagate_error (&ic->err, err);
			spruce_imap_command_unref (ic);
			return -1;
		}
	}
	
	/* check to see if we need to pre-queue a SELECT, if so do it */
	engine_prequeue_folder_select (engine);
	
	engine->current = ic = (SpruceIMAPCommand *) spruce_list_unlink_head (&engine->queue);
	ic->status = SPRUCE_IMAP_COMMAND_ACTIVE;
	
	if ((retval = imap_process_command (engine, ic)) != -1) {
		if (engine_state_change (engine, ic) == -1) {
			/* This can ONLY happen if @ic was the pre-queued SELECT command
			 * and it got a NO or BAD response.
			 *
			 * We have to pop the next imap command or we'll get into an
			 * infinite loop. In order to provide @nic's owner with as much
			 * information as possible, we move all @ic status information
			 * over to @nic and pretend we just processed @nic.
			 **/
			
			nic = (SpruceIMAPCommand *) spruce_list_unlink_head (&engine->queue);
			
			nic->status = ic->status;
			nic->result = ic->result;
			resp_codes = nic->resp_codes;
			nic->resp_codes = ic->resp_codes;
			ic->resp_codes = resp_codes;
			g_propagate_error (&nic->err, ic->err);
			ic->err = NULL;
			
			spruce_imap_command_unref (ic);
			ic = nic;
		}
		
		retval = ic->id;
	} else if (!engine->reconnecting && retries < 3) {
		/* put @ic back in the queue and retry */
		spruce_list_prepend (&engine->queue, (SpruceListNode *) ic);
		spruce_imap_command_reset (ic);
		retries++;
		goto retry;
	} else {
		g_set_error (&ic->err, SPRUCE_ERROR, SPRUCE_ERROR_SERVICE_UNAVAILABLE,
			     _("Failed to send command to IMAP server %s: %s"),
			     engine->url->host, errno ? g_strerror (errno) :
			     _("service unavailable"));
	}
	
	spruce_imap_command_unref (ic);
	
	return retval;
}


/**
 * spruce_imap_engine_queue:
 * @engine: IMAP engine
 * @folder: IMAP folder that the command will affect (or %NULL if it doesn't matter)
 * @format: command format
 * @Varargs: arguments
 *
 * Basically the same as #spruce_imap_command_new() except that this
 * function also places the command in the engine queue.
 *
 * Returns the #SpruceIMAPCommand.
 **/
SpruceIMAPCommand *
spruce_imap_engine_queue (SpruceIMAPEngine *engine, SpruceFolder *folder, const char *format, ...)
{
	SpruceIMAPCommand *ic;
	va_list args;
	
	g_return_val_if_fail (SPRUCE_IS_IMAP_ENGINE (engine), NULL);
	g_return_val_if_fail (format != NULL, NULL);
	
	va_start (args, format);
	ic = spruce_imap_command_newv (engine, (SpruceIMAPFolder *) folder, format, args);
	va_end (args);
	
	spruce_imap_engine_queue_command (engine, ic);
	
	return ic;
}


/**
 * spruce_imap_engine_queue_command:
 * @engine: IMAP engine
 * @ic: IMAP command to queue
 *
 * Appends @ic to the queue of pending IMAP commands.
 **/
void
spruce_imap_engine_queue_command (SpruceIMAPEngine *engine, SpruceIMAPCommand *ic)
{
	g_return_if_fail (SPRUCE_IS_IMAP_ENGINE (engine));
	g_return_if_fail (ic != NULL);
	
	if (engine->nextid == INT_MAX) {
		ic->id = engine->nextid;
		engine->nextid = 1;
	} else {
		ic->id = engine->nextid++;
	}
	
	spruce_list_append (&engine->queue, (SpruceListNode *) ic);
	spruce_imap_command_ref (ic);
}


/**
 * spruce_imap_engine_prequeue:
 * @engine: IMAP engine
 * @folder: IMAP folder that the command will affect (or %NULL if it doesn't matter)
 * @format: command format
 * @Varargs: arguments
 *
 * Basically the same as #spruce_imap_command_new() except that this
 * function also places the command at the beginning of the engine
 * queue.
 *
 * Returns the #SpruceIMAPCommand.
 **/
SpruceIMAPCommand *
spruce_imap_engine_prequeue (SpruceIMAPEngine *engine, SpruceFolder *folder, const char *format, ...)
{
	SpruceIMAPCommand *ic;
	va_list args;
	
	g_return_val_if_fail (SPRUCE_IS_IMAP_ENGINE (engine), NULL);
	g_return_val_if_fail (format != NULL, NULL);
	
	va_start (args, format);
	ic = spruce_imap_command_newv (engine, (SpruceIMAPFolder *) folder, format, args);
	va_end (args);
	
	spruce_imap_engine_prequeue_command (engine, ic);
	
	return ic;
}


/**
 * spruce_imap_engine_prequeue_command:
 * @engine: IMAP engine
 * @ic: IMAP command to pre-queue
 *
 * Places @ic at the head of the queue of pending IMAP commands.
 **/
void
spruce_imap_engine_prequeue_command (SpruceIMAPEngine *engine, SpruceIMAPCommand *ic)
{
	SpruceIMAPCommand *nic;
	SpruceListNode *node;
	int nextid;
	
	g_return_if_fail (SPRUCE_IS_IMAP_ENGINE (engine));
	g_return_if_fail (ic != NULL);
	
	if (spruce_list_is_empty (&engine->queue)) {
		spruce_imap_engine_queue_command (engine, ic);
		return;
	}
	
	spruce_imap_command_ref (ic);
	
	node = (SpruceListNode *) ic;
	spruce_list_prepend (&engine->queue, node);
	nic = (SpruceIMAPCommand *) node->next;
	ic->id = nic->id - 1;
	
	if (ic->id == 0) {
		/* increment all command ids */
		nextid = 1;
		node = engine->queue.head;
		while (node->next) {
			nic = (SpruceIMAPCommand *) node;
			node = node->next;
			nic->id = nextid++;
		}
		
		engine->nextid = nextid;
	}
}


void
spruce_imap_engine_dequeue (SpruceIMAPEngine *engine, SpruceIMAPCommand *ic)
{
	SpruceListNode *node = (SpruceListNode *) ic;
	
	if (node->next == NULL && node->prev == NULL)
		return;
	
	spruce_list_unlink (node);
	node->next = NULL;
	node->prev = NULL;
	
	spruce_imap_command_unref (ic);
}


int
spruce_imap_engine_next_token (SpruceIMAPEngine *engine, spruce_imap_token_t *token, GError **err)
{
	if (spruce_imap_stream_next_token (engine->istream, token) == -1) {
		g_set_error (err, SPRUCE_ERROR, errno ? errno : SPRUCE_ERROR_GENERIC,
			     _("IMAP server %s unexpectedly disconnected: %s"),
			     engine->url->host, errno ? g_strerror (errno) : _("Unknown"));
		return -1;
	}
	
	return 0;
}


int
spruce_imap_engine_eat_line (SpruceIMAPEngine *engine, GError **err)
{
	spruce_imap_token_t token;
	unsigned char *literal;
	int retval;
	size_t n;
	
	do {
		if (spruce_imap_engine_next_token (engine, &token, err) == -1)
			return -1;
		
		if (token.token == SPRUCE_IMAP_TOKEN_LITERAL) {
			while ((retval = spruce_imap_stream_literal (engine->istream, &literal, &n)) == 1)
				;
			
			if (retval == -1) {
				g_set_error (err, SPRUCE_ERROR, errno ? errno : SPRUCE_ERROR_GENERIC,
					     _("IMAP server %s unexpectedly disconnected: %s"),
					     engine->url->host, errno ? g_strerror (errno) : _("Unknown"));
				
				return -1;
			}
		}
	} while (token.token != '\n');
	
	return 0;
}


int
spruce_imap_engine_line (SpruceIMAPEngine *engine, unsigned char **line, size_t *len, GError **err)
{
	GByteArray *linebuf = NULL;
	unsigned char *buf;
	size_t buflen;
	int retval;
	
	if (line != NULL)
		linebuf = g_byte_array_new ();
	
	while ((retval = spruce_imap_stream_line (engine->istream, &buf, &buflen)) > 0) {
		if (line != NULL)
			g_byte_array_append (linebuf, buf, buflen);
	}
	
	if (retval == -1) {
		g_set_error (err, SPRUCE_ERROR, errno ? errno : SPRUCE_ERROR_GENERIC,
			     _("IMAP server %s unexpectedly disconnected: %s"),
			     engine->url->host, errno ? g_strerror (errno) : _("Unknown"));
		
		if (linebuf != NULL)
			g_byte_array_free (linebuf, TRUE);
		
		return -1;
	}
	
	if (linebuf != NULL) {
		g_byte_array_append (linebuf, buf, buflen);
		
		*line = linebuf->data;
		*len = linebuf->len;
		
		g_byte_array_free (linebuf, FALSE);
	}
	
	return 0;
}


int
spruce_imap_engine_literal (SpruceIMAPEngine *engine, unsigned char **literal, size_t *len, GError **err)
{
	GByteArray *literalbuf = NULL;
	unsigned char *buf;
	size_t buflen;
	int retval;
	
	if (literal != NULL)
		literalbuf = g_byte_array_new ();
	
	while ((retval = spruce_imap_stream_literal (engine->istream, &buf, &buflen)) > 0) {
		if (literalbuf != NULL)
			g_byte_array_append (literalbuf, buf, buflen);
	}
	
	if (retval == -1) {
		g_set_error (err, SPRUCE_ERROR, errno ? errno : SPRUCE_ERROR_GENERIC,
			     _("IMAP server %s unexpectedly disconnected: %s"),
			     engine->url->host, errno ? g_strerror (errno) : _("Unknown"));
		
		if (literalbuf != NULL)
			g_byte_array_free (literalbuf, TRUE);
		
		return -1;
	}
	
	if (literalbuf != NULL) {
		g_byte_array_append (literalbuf, buf, buflen);
		g_byte_array_append (literalbuf, (unsigned char *) "", 1);
		
		*literal = literalbuf->data;
		*len = literalbuf->len - 1;
		
		g_byte_array_free (literalbuf, FALSE);
	}
	
	return 0;
}


void
spruce_imap_resp_code_free (SpruceIMAPRespCode *rcode)
{
	switch (rcode->code) {
	case SPRUCE_IMAP_RESP_CODE_PARSE:
		g_free (rcode->v.parse);
		break;
	case SPRUCE_IMAP_RESP_CODE_NEWNAME:
		g_free (rcode->v.newname[0]);
		g_free (rcode->v.newname[1]);
		break;
	case SPRUCE_IMAP_RESP_CODE_COPYUID:
		g_free (rcode->v.copyuid.srcset);
		g_free (rcode->v.copyuid.destset);
		break;
	default:
		break;
	}
	
	g_free (rcode);
}
