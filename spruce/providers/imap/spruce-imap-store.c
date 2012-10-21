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
#include <ctype.h>

#include <glib.h>
#include <glib/gi18n.h>

#include <spruce/spruce-sasl.h>
#include <spruce/spruce-error.h>
#include <spruce/spruce-tcp-stream.h>
#include <spruce/spruce-tcp-stream-ssl.h>

#include "spruce-imap-store.h"
#include "spruce-imap-engine.h"
#include "spruce-imap-folder.h"
#include "spruce-imap-stream.h"
#include "spruce-imap-command.h"


static void spruce_imap_store_class_init (SpruceIMAPStoreClass *klass);
static void spruce_imap_store_init (SpruceIMAPStore *store, SpruceIMAPStoreClass *klass);
static void spruce_imap_store_finalize (GObject *object);

static int imap_connect (SpruceService *service, GError **err);
static int imap_reconnect (SpruceIMAPEngine *engine, GError **err);
static int imap_disconnect (SpruceService *service, gboolean clean, GError **err);
static GList *imap_query_auth_types (SpruceService *service, GError **err);

static SpruceFolder *imap_get_default_folder (SpruceStore *store, GError **err);
static SpruceFolder *imap_get_folder (SpruceStore *store, const char *name, GError **err);
static SpruceFolder *imap_get_folder_by_url (SpruceStore *store, SpruceURL *url, GError **err);
static GPtrArray *imap_get_personal_namespaces (SpruceStore *store, GError **err);
static GPtrArray *imap_get_shared_namespaces (SpruceStore *store, GError **err);
static GPtrArray *imap_get_other_namespaces (SpruceStore *store, GError **err);


static SpruceStoreClass *parent_class = NULL;


GType
spruce_imap_store_get_type (void)
{
	static GType type = 0;
	
	if (!type) {
		static const GTypeInfo info = {
			sizeof (SpruceIMAPStoreClass),
			NULL, /* base_class_init */
			NULL, /* base_class_finalize */
			(GClassInitFunc) spruce_imap_store_class_init,
			NULL, /* class_finalize */
			NULL, /* class_data */
			sizeof (SpruceIMAPStore),
			0,    /* n_preallocs */
			(GInstanceInitFunc) spruce_imap_store_init,
		};
		
		type = g_type_register_static (SPRUCE_TYPE_STORE, "SpruceIMAPStore", &info, 0);
	}
	
	return type;
}


static void
spruce_imap_store_class_init (SpruceIMAPStoreClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	SpruceStoreClass *store_class = SPRUCE_STORE_CLASS (klass);
	SpruceServiceClass *service_class = SPRUCE_SERVICE_CLASS (klass);
	
	parent_class = g_type_class_ref (SPRUCE_TYPE_STORE);
	
	object_class->finalize = spruce_imap_store_finalize;
	
	service_class->connect = imap_connect;
	service_class->disconnect = imap_disconnect;
	service_class->query_auth_types = imap_query_auth_types;
	
	store_class->get_default_folder = imap_get_default_folder;
	store_class->get_folder = imap_get_folder;
	store_class->get_folder_by_url = imap_get_folder_by_url;
	store_class->get_personal_namespaces = imap_get_personal_namespaces;
	store_class->get_shared_namespaces = imap_get_shared_namespaces;
	store_class->get_other_namespaces = imap_get_other_namespaces;
}

static void
spruce_imap_store_init (SpruceIMAPStore *store, SpruceIMAPStoreClass *klass)
{
	store->engine = NULL;
}

static void
spruce_imap_store_finalize (GObject *object)
{
	SpruceIMAPStore *store = (SpruceIMAPStore *) object;
	
	if (store->engine)
		g_object_unref (store->engine);
	
	G_OBJECT_CLASS (parent_class)->finalize (object);
}

enum {
	MODE_CLEAR,
	MODE_SSL,
	MODE_TLS
};

#define STARTTLS_FLAGS (SPRUCE_TCP_STREAM_SSL_ENABLE_TLS)
#define SSL_PORT_FLAGS (SPRUCE_TCP_STREAM_SSL_ENABLE_SSL3 | SPRUCE_TCP_STREAM_SSL_ENABLE_SSL_CONNECT)

static gboolean
connect_to_server (SpruceService *service, struct addrinfo *ai, int mode, GError **err)
{
	SpruceIMAPStore *store = (SpruceIMAPStore *) service;
	SpruceIMAPEngine *engine = store->engine;
	GMimeStream *tcp_stream;
	SpruceIMAPCommand *ic;
	GError *lerr = NULL;
	int id, ret;
	
	if (mode != MODE_CLEAR) {
#ifdef HAVE_SSL
		if (mode == MODE_TLS) {
			tcp_stream = spruce_tcp_stream_ssl_new (service->session, service->url->host, STARTTLS_FLAGS);
		} else {
			tcp_stream = spruce_tcp_stream_ssl_new (service->session, service->url->host, SSL_PORT_FLAGS);
		}
#else
		g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_SERVICE_UNAVAILABLE,
			     _("Could not connect to %s: %s"),
			     service->url->host,
			     _("SSL unavailable"));
		
		return FALSE;
#endif /* HAVE_SSL */
	} else {
		tcp_stream = spruce_tcp_stream_new ();
	}
	
	fprintf (stderr, "connecting to %s\n", service->url->host);
	if ((ret = spruce_tcp_stream_connect ((SpruceTcpStream *) tcp_stream, ai)) == -1) {
		g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_SERVICE_UNAVAILABLE,
			     _("Could not connect to %s: %s"),
			     service->url->host,
			     g_strerror (errno));
		
		g_object_unref (tcp_stream);
		
		return FALSE;
	}
	
	if (spruce_imap_engine_take_stream (engine, tcp_stream, err) == -1) {
		fprintf (stderr, "take_stream failed\n");
		return FALSE;
	}
	
	if (spruce_imap_engine_capability (engine, err) == -1) {
		fprintf (stderr, "CAPABILITY failed\n");
		return FALSE;
	}
	
	if (mode != MODE_TLS) {
		/* we're done */
		return TRUE;
	}
	
#ifdef HAVE_SSL
	if (!(engine->capa & SPRUCE_IMAP_CAPABILITY_STARTTLS)) {
		g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_SERVICE_UNAVAILABLE,
			     _("Failed to connect to IMAP server %s in secure mode: "
			       "Server does not support STARTTLS"),
			     service->url->host);
		
		spruce_imap_engine_disconnect (engine);
		
		return FALSE;
	}
	
	ic = spruce_imap_engine_prequeue (engine, NULL, "STARTTLS\r\n");
	while ((id = spruce_imap_engine_iterate (engine)) < ic->id && id != -1)
		;
	
	if (id == -1 || ic->result != SPRUCE_IMAP_RESULT_OK) {
		if (ic->result != SPRUCE_IMAP_RESULT_OK) {
			g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_GENERIC,
				     _("Failed to connect to IMAP server %s in secure mode: %s"),
				     service->url->host, _("Unknown error"));
		} else {
			g_propagate_error (err, ic->err);
			ic->err = NULL;
		}
		
		spruce_imap_command_unref (ic);
		
		return FALSE;
	}
	
	spruce_imap_command_unref (ic);
	
	if (spruce_tcp_stream_ssl_enable_ssl ((SpruceTcpStreamSSL *) tcp_stream, &lerr) == -1) {
		g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_GENERIC,
			     _("Failed to connect to IMAP server %s in secure mode: %s"),
			     service->url->host, lerr->message);
		spruce_imap_engine_disconnect (engine);
		g_error_free (lerr);
		return FALSE;
	}
	
	if (spruce_imap_engine_capability (engine, err) == -1) {
		fprintf (stderr, "CAPABILITY failed\n");
		return FALSE;
	}
	
	return TRUE;
#endif /* HAVE_SSL */
	g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_GENERIC,
		     _("Failed to connect to IMAP server %s in secure mode: %s"),
		     service->url->host, _("SSL is not available in this build"));
	
	spruce_imap_engine_disconnect (engine);
	
	return FALSE;
}

static gboolean
connect_to_server_wrapper (SpruceIMAPEngine *engine, GError **err)
{
	SpruceService *service = engine->service;
	const char *starttls, *port, *serv;
	struct addrinfo hints, *ai;
	char servbuf[16];
	int mode, ret;
	
	serv = service->url->protocol;
	if (strcmp (serv, "imaps") != 0) {
		mode = MODE_CLEAR;
		port = "143";
		
		if ((starttls = spruce_url_get_param (service->url, "starttls"))) {
			if (!strcmp (starttls, "yes") || !strcmp (starttls, "true"))
				mode = MODE_TLS;
		}
	} else {
		mode = MODE_SSL;
		port = "993";
	}
	
	if (service->url->port != 0) {
		sprintf (servbuf, "%d", service->url->port);
		serv = servbuf;
		port = NULL;
	}
	
	memset (&hints, 0, sizeof (hints));
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_family = PF_UNSPEC;
	
	if (!(ai = spruce_getaddrinfo (service->url->host, serv, port, &hints, err)))
		return FALSE;
	
	ret = connect_to_server (service, ai, mode, err);
	
	spruce_freeaddrinfo (ai);
	
	return ret;
}

static int
sasl_auth (SpruceIMAPEngine *engine, SpruceIMAPCommand *ic, const unsigned char *linebuf, size_t linelen, GError **err)
{
	/* Perform a single challenge iteration */
	SpruceSASL *sasl = ic->user_data;
	char *challenge;
	
	if (spruce_sasl_authenticated (sasl)) {
		g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_SERVICE_CANT_AUTHENTICATE,
			     _("Cannot authenticate to IMAP server %s using the %s authentication mechanism"),
			     engine->url->host, engine->url->auth);
		return -1;
	}
	
	while (isspace (*linebuf))
		linebuf++;
	
	if (*linebuf == '\0')
		linebuf = NULL;
	
	if (!(challenge = spruce_sasl_challenge_base64 (sasl, (const char *) linebuf, err)))
		return -1;
	
	fprintf (stderr, "sending : %s\r\n", challenge);
	
	if (g_mime_stream_printf (engine->ostream, "%s\r\n", challenge) == -1) {
		g_free (challenge);
		return -1;
	}
	
	g_free (challenge);
	
	if (g_mime_stream_flush (engine->ostream) == -1)
		return -1;
	
	return 0;
}

static int
imap_try_authenticate (SpruceService *service, const char *key, gboolean reprompt, const char *errmsg, GError **err)
{
	SpruceIMAPStore *store = (SpruceIMAPStore *) service;
	SpruceSession *session = service->session;
	SpruceSASL *sasl = NULL;
	SpruceIMAPCommand *ic;
	int id;
	
	if (!service->url->passwd) {
		guint32 flags = SPRUCE_SESSION_PASSWORD_SECRET;
		char *prompt;
		
		if (reprompt)
			flags |= SPRUCE_SESSION_PASSWORD_REPROMPT;
		
		prompt = g_strdup_printf (_("%s%sPlease enter the IMAP password for %s on host %s"),
					  errmsg ? errmsg : "",
					  errmsg ? "\n" : "",
					  service->url->user,
					  service->url->host);
		
		service->url->passwd = spruce_session_request_passwd (session, prompt, key, flags, err);
		
		g_free (prompt);
		
		if (!service->url->passwd)
			return FALSE;
	}
	
	if (service->url->auth) {
		SpruceServiceAuthType *mech;
		
		mech = g_hash_table_lookup (store->engine->authtypes, service->url->auth);
		sasl = spruce_sasl_new ("imap", mech->authproto, service);
		
		ic = spruce_imap_engine_prequeue (store->engine, NULL, "AUTHENTICATE %s\r\n",
						  service->url->auth);
		ic->plus = sasl_auth;
		ic->user_data = sasl;
	} else {
		ic = spruce_imap_engine_prequeue (store->engine, NULL, "LOGIN %S %S\r\n",
						  service->url->user, service->url->passwd);
	}
	
	while ((id = spruce_imap_engine_iterate (store->engine)) < ic->id && id != -1)
		;
	
	if (sasl != NULL)
		g_object_unref (sasl);
	
	if (id == -1 || ic->status == SPRUCE_IMAP_COMMAND_ERROR) {
		/* unrecoverable error */
		g_propagate_error (err, ic->err);
		ic->err = NULL;
		spruce_imap_command_unref (ic);
		
		return FALSE;
	}
	
	if (ic->result != SPRUCE_IMAP_RESULT_OK) {
		g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_SERVICE_CANT_AUTHENTICATE,
			     _("Cannot authenticate to IMAP server %s: Bad password"), service->url->host);
		spruce_imap_command_unref (ic);
		
		/* try again */
		
		return TRUE;
	}
	
	spruce_imap_command_unref (ic);
	
	return FALSE;
}

static int
imap_reconnect (SpruceIMAPEngine *engine, GError **err)
{
	SpruceIMAPStore *store = (SpruceIMAPStore *) engine->service;
	SpruceService *service = engine->service;
	SpruceServiceAuthType *mech;
	gboolean reprompt = FALSE;
	char *key, *errmsg = NULL;
	GError *lerr = NULL;
	
	if (SPRUCE_SERVICE_CLASS (parent_class)->connect (service, err) == -1)
		return -1;
	
	if (!connect_to_server_wrapper (engine, err))
		return -1;
	
	if (engine->state != SPRUCE_IMAP_ENGINE_AUTHENTICATED) {
#define CANT_USE_AUTHMECH (!(mech = g_hash_table_lookup (store->engine->authtypes, service->url->auth)))
		if (service->url->auth && CANT_USE_AUTHMECH) {
			/* Oops. We can't AUTH using the requested mechanism */
			g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_SERVICE_CANT_AUTHENTICATE,
				     _("Cannot authenticate to IMAP server %s using %s"),
				     service->url->host, service->url->auth);
			
			return -1;
		}
		
		key = spruce_url_to_string (service->url, SPRUCE_URL_HIDE_ALL);
		while (imap_try_authenticate (service, key, reprompt, errmsg, &lerr)) {
			g_free (service->url->passwd);
			service->url->passwd = NULL;
			g_free (errmsg);
			errmsg = g_strdup (lerr->message);
			g_clear_error (&lerr);
			reprompt = TRUE;
		}
		g_free (errmsg);
		g_free (key);
	
		if (lerr != NULL) {
			g_propagate_error (err, lerr);
			return -1;
		}
	}
	
	if (spruce_imap_engine_namespace (store->engine, err) == -1)
		return -1;
	
	return 0;
}

static int
imap_connect (SpruceService *service, GError **err)
{
	SpruceIMAPStore *store = (SpruceIMAPStore *) service;
	
	if (!store->engine)
		store->engine = spruce_imap_engine_new (service, imap_reconnect);
	
	return imap_reconnect (store->engine, err);
}

static int
imap_disconnect (SpruceService *service, gboolean clean, GError **err)
{
	SpruceIMAPStore *store = (SpruceIMAPStore *) service;
	SpruceIMAPCommand *ic;
	int id;
	
	if (clean && store->engine && store->engine->istream && !store->engine->istream->disconnected) {
		ic = spruce_imap_engine_queue (store->engine, NULL, "LOGOUT\r\n");
		while ((id = spruce_imap_engine_iterate (store->engine)) < ic->id && id != -1)
			;
		
		spruce_imap_command_unref (ic);
	}
	
	g_object_unref (store->engine);
	store->engine = NULL;
	
	return 0;
}

extern SpruceServiceAuthType spruce_imap_password_authtype;

static GList *
imap_query_auth_types (SpruceService *service, GError **err)
{
	SpruceIMAPStore *store = (SpruceIMAPStore *) service;
	GList *node, *next, *authtypes = NULL;
	SpruceServiceAuthType *mech;
	
	if (store->engine) {
		node = authtypes = spruce_sasl_authtype_list ();
		while (node != NULL) {
			next = node->next;
			
			mech = node->data;
			if (!g_hash_table_lookup (store->engine->authtypes, mech->authproto))
				authtypes = g_list_delete_link (authtypes, node);
			
			node = next;
		}
		
		if (!(store->engine->capa & SPRUCE_IMAP_CAPABILITY_LOGINDISABLED))
			authtypes = g_list_prepend (authtypes, &spruce_imap_password_authtype);
	} else {
		authtypes = spruce_sasl_authtype_list ();
		authtypes = g_list_prepend (authtypes, &spruce_imap_password_authtype);
	}
	
	return authtypes;
}

static SpruceFolder *
imap_get_default_folder (SpruceStore *store, GError **err)
{
	return imap_get_folder (store, "INBOX", err);
}

static SpruceFolder *
imap_get_folder (SpruceStore *store, const char *name, GError **err)
{
	if (!g_ascii_strcasecmp ("INBOX", name))
		name = "INBOX";
	
	return spruce_imap_folder_new (store, name, TRUE, err);
}

static SpruceFolder *
imap_get_folder_by_url (SpruceStore *store, SpruceURL *url, GError **err)
{
	const char *name = "INBOX";
	
	if (url->fragment != NULL)
		name = url->fragment;
	
	return imap_get_folder (store, name, err);
}

static GPtrArray *
imap_get_personal_namespaces (SpruceStore *store, GError **err)
{
	/* FIXME: check the Engine - also always add INBOX? */
	return NULL;
}

static GPtrArray *
imap_get_shared_namespaces (SpruceStore *store, GError **err)
{
	/* FIXME: check the Engine */
	return NULL;
}

static GPtrArray *
imap_get_other_namespaces (SpruceStore *store, GError **err)
{
	/* FIXME: check the Engine */
	return NULL;
}
