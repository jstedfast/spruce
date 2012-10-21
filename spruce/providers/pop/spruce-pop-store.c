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

#include <ctype.h>

#include <glib.h>
#include <glib/gi18n.h>

#include <spruce/spruce-sasl.h>
#include <spruce/spruce-error.h>
#include <spruce/spruce-tcp-stream.h>
#include <spruce/spruce-tcp-stream-ssl.h>

#include "spruce-pop-store.h"
#include "spruce-pop-engine.h"
#include "spruce-pop-stream.h"
#include "spruce-pop-folder.h"

#define d(x) x

static void spruce_pop_store_class_init (SprucePOPStoreClass *klass);
static void spruce_pop_store_init (SprucePOPStore *store, SprucePOPStoreClass *klass);
static void spruce_pop_store_finalize (GObject *object);

static int pop_connect (SpruceService *service, GError **err);
static int pop_disconnect (SpruceService *service, gboolean clean, GError **err);
static GList *pop_query_auth_types (SpruceService *service, GError **err);

static SpruceFolder *pop_get_default_folder (SpruceStore *store, GError **err);
static SpruceFolder *pop_get_folder (SpruceStore *store, const char *name, GError **err);
static SpruceFolder *pop_get_folder_by_url (SpruceStore *store, SpruceURL *url, GError **err);
static int pop_noop (SpruceStore *store, GError **err);


static SpruceStoreClass *parent_class = NULL;


GType
spruce_pop_store_get_type (void)
{
	static GType type = 0;
	
	if (!type) {
		static const GTypeInfo info = {
			sizeof (SprucePOPStoreClass),
			NULL, /* base_class_init */
			NULL, /* base_class_finalize */
			(GClassInitFunc) spruce_pop_store_class_init,
			NULL, /* class_finalize */
			NULL, /* class_data */
			sizeof (SprucePOPStore),
			0,    /* n_preallocs */
			(GInstanceInitFunc) spruce_pop_store_init,
		};
		
		type = g_type_register_static (SPRUCE_TYPE_STORE, "SprucePOPStore", &info, 0);
	}
	
	return type;
}


static void
spruce_pop_store_class_init (SprucePOPStoreClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	SpruceStoreClass *store_class = SPRUCE_STORE_CLASS (klass);
	SpruceServiceClass *service_class = SPRUCE_SERVICE_CLASS (klass);
	
	parent_class = g_type_class_ref (SPRUCE_TYPE_STORE);
	
	object_class->finalize = spruce_pop_store_finalize;
	
	service_class->connect = pop_connect;
	service_class->disconnect = pop_disconnect;
	service_class->query_auth_types = pop_query_auth_types;
	
	store_class->get_default_folder = pop_get_default_folder;
	store_class->get_folder = pop_get_folder;
	store_class->get_folder_by_url = pop_get_folder_by_url;
	store_class->noop = pop_noop;
}

static void
spruce_pop_store_init (SprucePOPStore *store, SprucePOPStoreClass *klass)
{
	store->engine = NULL;
}

static void
spruce_pop_store_finalize (GObject *object)
{
	SprucePOPStore *store = (SprucePOPStore *) object;
	
	if (store->engine)
		g_object_unref (store->engine);
	
	G_OBJECT_CLASS (parent_class)->finalize (object);
}


struct pop_login_t {
	SpruceService *service;
	SpruceSASL *sasl;
	GError **err;
};

/* APOP or USER/PASS callback - mostly just meant for setting GError */
static int
login_cmd (SprucePOPEngine *engine, SprucePOPCommand *pc, const char *line, void *user_data)
{
	struct pop_login_t *login = user_data;
	
	/* only interested in -ERR */
	if (pc->status == SPRUCE_POP_COMMAND_OK)
		return 0;
	
	/* chop off login credentials (leaves us with just APOP, USER or PASS) */
	pc->cmd[4] = '\0';
	
	g_set_error (login->err, SPRUCE_ERROR, SPRUCE_ERROR_GENERIC,
		     _("Could not authenticate to POP server %s using the %s command: %s"),
		     login->service->url->host, pc->cmd, line);
	return -1;
}

/* SASL equivalent to login_cmd callback */
static int
sasl_cmd (SprucePOPEngine *engine, SprucePOPCommand *pc, const char *line, void *user_data)
{
	struct pop_login_t *login = user_data;
	char *challenge, *linebuf = NULL;
	size_t len;
	
	if (pc->status == SPRUCE_POP_COMMAND_OK) {
		/* SASL didn't need any continuation apparently */
		return 0;
	}
	
	if (pc->status == SPRUCE_POP_COMMAND_ERR)
		goto error;
	
	do {
		while (*line && isspace (*line))
			line++;
		
		if (*line == '\0')
			line = NULL;
		
		if (!(challenge = spruce_sasl_challenge_base64 (login->sasl, line, login->err))) {
			g_free (linebuf);
			return -1;
		}
		
		g_free (linebuf);
		
		if (g_mime_stream_write ((GMimeStream *) engine->stream, challenge, strlen (challenge)) == -1) {
			g_free (challenge);
			goto exception;
		}
		
		g_free (challenge);
		
		if (spruce_pop_engine_get_line (engine, &linebuf, &len) == -1)
			goto exception;
		
		line = linebuf;
		
		if (!strncmp (line, "-ERR", 4)) {
			pc->status = SPRUCE_POP_COMMAND_ERR;
			goto error;
		}
		
		if (!strncmp (line, "+OK", 3)) {
			g_free (linebuf);
			return 0;
		}
	} while (!spruce_sasl_authenticated (login->sasl));
	
	/* if we get here, then we've gone thru all the SASL negotiation steps but the server still expects more */
	
 error:
	
	g_set_error (login->err, SPRUCE_ERROR, SPRUCE_ERROR_GENERIC,
		     _("Could not authenticate to POP server %s using the %s mechanism: %s"),
		     login->service->url->host, login->service->url->auth, line);
	g_free (linebuf);
	
	return -1;
	
 exception:
	
	g_set_error (login->err, SPRUCE_ERROR, errno ? errno : SPRUCE_ERROR_GENERIC,
		     _("Could not authenticate to POP server %s using the %s mechanism: %s"),
		     login->service->url->host, login->service->url->auth,
		     errno ? g_strerror (errno) : _("Unknown"));
	
	return -1;
}

#ifdef HAVE_SSL
struct pop_stls_t {
	SpruceService *service;
	GError **err;
};

static int
stls_cmd (SprucePOPEngine *engine, SprucePOPCommand *pc, const char *line, void *user_data)
{
	SpruceTcpStreamSSL *stream = (SpruceTcpStreamSSL *) engine->stream->stream;
	struct pop_stls_t *stls = user_data;
	GError *lerr = NULL;
	
	if (pc->status == SPRUCE_POP_COMMAND_ERR) {
		
		return -1;
	}
	
	/* Okay, now toggle SSL/TLS mode */
	if (spruce_tcp_stream_ssl_enable_ssl (stream, &lerr) == -1) {
		g_set_error (stls->err, SPRUCE_ERROR, errno ? errno : SPRUCE_ERROR_GENERIC,
			     _("Failed to connect to POP server %s in secure mode: %s"),
			     stls->service->url->host, lerr->message);
		g_error_free (lerr);
		
		return -1;
	}
	
	return 0;
}
#endif /* HAVE_SSL */


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
	SprucePOPStore *pop_store = (SprucePOPStore *) service;
	SprucePOPEngine *pop_engine;
	SprucePOPStream *pop_stream;
	GMimeStream *tcp_stream;
#ifdef HAVE_SSL
	struct pop_stls_t stls;
#endif
	SprucePOPCommand *pc;
	int id, ret;
	
	if (pop_store->engine) {
		g_object_unref (pop_store->engine);
		pop_store->engine = NULL;
	}
	
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
	
	pop_stream = (SprucePOPStream *) spruce_pop_stream_new (tcp_stream);
	g_object_unref (tcp_stream);
	
	pop_engine = spruce_pop_engine_new ();
	if ((ret = spruce_pop_engine_take_stream (pop_engine, pop_stream)) != SPRUCE_POP_COMMAND_OK) {
		switch (ret) {
		case SPRUCE_POP_COMMAND_ERR:
			/* this shouldn't ever happen... */
			g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_GENERIC,
				     _("POP server %s does not seem to be accepting connections"),
				     service->url->host);
			break;
		case SPRUCE_POP_COMMAND_PROTOCOL_ERROR:
			g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_GENERIC,
				     _("Could not read greeting from POP server %s: %s"),
				     service->url->host, _("Unknown error"));
			break;
		default:
			/* errno - the easy one ;-) */
			g_set_error (err, SPRUCE_ERROR, ret,
				     _("Could not read greeting from POP server %s: %s"),
				     service->url->host, g_strerror (ret));
			break;
		}
		
		g_object_unref (pop_engine);
		
		return FALSE;
	}
	
	ret = spruce_pop_engine_capa (pop_engine);
	if (ret != SPRUCE_POP_COMMAND_OK && ret != SPRUCE_POP_COMMAND_ERR) {
		int error = ret > 0 ? ret : SPRUCE_ERROR_GENERIC;
		
		g_set_error (err, SPRUCE_ERROR, error,
			     _("Failed to get a list of capabilities from POP server %s: %s"),
			     service->url->host, ret > 0 ? g_strerror (ret) : _("Unknown error"));
		
		g_object_unref (pop_engine);
		
		return FALSE;
	}
	
	pop_store->engine = pop_engine;
	
#ifdef HAVE_SSL
	if (mode != MODE_TLS) {
		/* we're done */
		return TRUE;
	}
	
	if (!(pop_engine->capa & SPRUCE_POP_CAPA_STLS)) {
		/* server doesn't support STARTTLS, abort */
		g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_SERVICE_UNAVAILABLE,
			     _("Failed to connect to POP server %s in secure mode: "
			       "Server does not support STLS"),
			     service->url->host);
		
		g_object_unref (pop_store->engine);
		pop_store->engine = NULL;
		
		return FALSE;
	}
	
	stls.service = service;
	stls.err = err;
	
	pc = spruce_pop_engine_queue (pop_engine, stls_cmd, &stls, "STLS\r\n");
	while ((id = spruce_pop_engine_iterate (pop_engine)) < pc->id && id != -1)
		;
	
	if (id == -1 || pc->retval == -1) {
		if (id == -1) {
			g_set_error (err, SPRUCE_ERROR, pc->error > 0 ? pc->error : SPRUCE_ERROR_GENERIC,
				     _("Failed to connect to POP server %s in secure mode: %s"),
				     service->url->host,
				     pc->error > 0 ? g_strerror (pc->error) : _("Unknown error"));
		}
		
		spruce_pop_command_free (pop_engine, pc);
		
		g_object_unref (pop_store->engine);
		pop_store->engine = NULL;
		
		return FALSE;
	}
	
	spruce_pop_command_free (pop_engine, pc);
#endif /* HAVE_SSL */
	
	return TRUE;
}

static gboolean
connect_to_server_wrapper (SpruceService *service, GError **err)
{
	const char *starttls, *port, *serv;
	struct addrinfo hints, *ai;
	char servbuf[16];
	int mode, ret;
	
	serv = service->url->protocol;
	if (strcmp (serv, "pops") != 0) {
		mode = MODE_CLEAR;
		port = "110";
		
		if ((starttls = spruce_url_get_param (service->url, "starttls"))) {
			if (!strcmp (starttls, "yes") || !strcmp (starttls, "true"))
				mode = MODE_TLS;
		}
	} else {
		mode = MODE_SSL;
		port = "995";
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
pop_try_authenticate (SpruceService *service, const char *key, gboolean reprompt, const char *errmsg, GError **err)
{
	SprucePOPStore *pop = (SprucePOPStore *) service;
	SpruceSession *session = service->session;
	SprucePOPCommand *pcp, *pcu = NULL;
	SpruceServiceAuthType *mech;
	struct pop_login_t login;
	int id;
	
	login.service = service;
	login.sasl = NULL;
	login.err = err;
	
	if (!service->url->passwd) {
		guint32 flags = SPRUCE_SESSION_PASSWORD_SECRET;
		char *prompt;
		
		if (reprompt)
			flags |= SPRUCE_SESSION_PASSWORD_REPROMPT;
		
		prompt = g_strdup_printf (_("%sPlease enter the POP password for %s on host %s"),
					  errmsg ? errmsg : "",
					  service->url->user,
					  service->url->host);
		
		service->url->passwd = spruce_session_request_passwd (session, prompt, key, flags, err);
		
		g_free (prompt);
		
		if (!service->url->passwd)
			return FALSE;
	}
	
	if (!service->url->auth || !strcmp (service->url->auth, "USER")) {
		pcu = spruce_pop_engine_queue (pop->engine, login_cmd, &login, "USER %s\r\n", service->url->user);
		pcp = spruce_pop_engine_queue (pop->engine, login_cmd, &login, "PASS %s\r\n", service->url->passwd);
	} else if (!strcmp (service->url->auth, "+APOP")) {
		unsigned char md5sum[16], *s;
		char md5asc[33], *d;
		GChecksum *checksum;
		size_t len = 16;
		
		checksum = g_checksum_new (G_CHECKSUM_MD5);
		g_checksum_update (checksum, pop->engine->apop, strlen (pop->engine->apop));
		g_checksum_update (checksum, service->url->passwd, strlen (service->url->passwd));
		g_checksum_get_digest (checksum, md5sum, &len);
		g_checksum_free (checksum);
		
		for (s = md5sum, d = md5asc; d < md5asc + 32; s++, d += 2)
			sprintf (d, "%.2x", *s);
		
		pcp = spruce_pop_engine_queue (pop->engine, login_cmd, &login, "APOP %s %s\r\n", service->url->user, md5asc);
	} else {
		mech = g_hash_table_lookup (pop->engine->authtypes, service->url->auth);
		login.sasl = spruce_sasl_new ("pop3", mech->authproto, service);
		
		pcp = spruce_pop_engine_queue (pop->engine, sasl_cmd, &login, "AUTH %s\r\n", service->url->auth);
	}
	
	while ((id = spruce_pop_engine_iterate (pop->engine)) < pcp->id && id != -1)
		;
	
	if (login.sasl)
		g_object_unref (login.sasl);
	
	if (id == -1) {
		/* unrecoverable error */
		int error;
		
		error = pcu && pcu->error ? pcu->error : pcp->error;
		g_set_error (err, SPRUCE_ERROR, error > 0 ? error : SPRUCE_ERROR_GENERIC,
			     _("Failed to login to POP server %s: %s"), service->url->host,
			     error > 0 ? g_strerror (error) : _("Unknown error"));
		
		if (pcu != NULL)
			spruce_pop_command_free (pop->engine, pcu);
		spruce_pop_command_free (pop->engine, pcp);
		
		return FALSE;
	}
	
	if ((pcu && pcu->retval != 0) || pcp->retval != 0) {
		if (pcu != NULL) {
			g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_SERVICE_CANT_AUTHENTICATE,
				     _("Cannot authenticate to POP server %s: Bad user name"),
				     service->url->host);
			spruce_pop_command_free (pop->engine, pcu);
		} else {
			g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_SERVICE_CANT_AUTHENTICATE,
				     _("Cannot authenticate to POP server %s: Bad password"),
				     service->url->host);
		}
		spruce_pop_command_free (pop->engine, pcp);
		
		/* try again */
		
		return TRUE;
	}
	
	if (pcu != NULL)
		spruce_pop_command_free (pop->engine, pcu);
	spruce_pop_command_free (pop->engine, pcp);
	
	return FALSE;
}

static int
pop_connect (SpruceService *service, GError **err)
{
	SprucePOPStore *store = (SprucePOPStore *) service;
	SpruceServiceAuthType *mech;
	gboolean reprompt = FALSE;
	GError *lerr = NULL;
	char *errmsg = NULL;
	char *key;
	
	if (SPRUCE_SERVICE_CLASS (parent_class)->connect (service, err) == -1)
		return -1;
	
	if (!connect_to_server_wrapper (service, err))
		return -1;
	
	g_assert (store->engine->state == SPRUCE_POP_STATE_AUTH);
	
#define CANT_USE_AUTHMECH (!(mech = g_hash_table_lookup (store->engine->authtypes, service->url->auth)))
	if (service->url->auth && CANT_USE_AUTHMECH) {
		/* Oops. We can't AUTH using the requested mechanism */
		g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_SERVICE_CANT_AUTHENTICATE,
			     _("Cannot authenticate to POP server %s using %s"),
			     service->url->host, service->url->auth);
		
		g_object_unref (store->engine);
		store->engine = NULL;
		
		return -1;
	}
	
	key = spruce_url_to_string (service->url, SPRUCE_URL_HIDE_ALL);
	while (pop_try_authenticate (service, key, reprompt, errmsg, &lerr)) {
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
	
	return 0;
}

static int
pop_disconnect (SpruceService *service, gboolean clean, GError **err)
{
	SprucePOPStore *store = (SprucePOPStore *) service;
	SprucePOPCommand *pc;
	int id;
	
	if (clean && !store->engine->stream->disconnected) {
		pc = spruce_pop_engine_queue (store->engine, NULL, NULL, "QUIT\r\n");
		while ((id = spruce_pop_engine_iterate (store->engine)) < pc->id && id != -1)
			;
		
		spruce_pop_command_free (store->engine, pc);
	}
	
	g_object_unref (store->engine);
	store->engine = NULL;
	
	return 0;
}

extern SpruceServiceAuthType spruce_pop_password_authtype;
extern SpruceServiceAuthType spruce_pop_apop_authtype;

static GList *
pop_query_auth_types (SpruceService *service, GError **err)
{
	SprucePOPStore *pop = (SprucePOPStore *) service;
	GList *l, *n, *authtypes = NULL;
	SpruceServiceAuthType *mech;
	
	if (pop->engine) {
		if (pop->engine->capa & SPRUCE_POP_CAPA_SASL) {
			l = authtypes = spruce_sasl_authtype_list ();
			while (l != NULL) {
				n = l->next;
				
				mech = l->data;
				if (!g_hash_table_lookup (pop->engine->authtypes, mech->authproto))
					authtypes = g_list_delete_link (authtypes, l);
				
				l = n;
			}
		}
		
		if (pop->engine->capa & SPRUCE_POP_CAPA_APOP)
			authtypes = g_list_prepend (authtypes, &spruce_pop_apop_authtype);
		if (pop->engine->capa & SPRUCE_POP_CAPA_USER)
			authtypes = g_list_prepend (authtypes, &spruce_pop_password_authtype);
	} else {
		authtypes = spruce_sasl_authtype_list ();
		authtypes = g_list_prepend (authtypes, &spruce_pop_apop_authtype);
		authtypes = g_list_prepend (authtypes, &spruce_pop_password_authtype);
	}
	
	return authtypes;
}

static SpruceFolder *
pop_get_default_folder (SpruceStore *store, GError **err)
{
	return spruce_pop_folder_new (store, err);
}

static SpruceFolder *
pop_get_folder (SpruceStore *store, const char *name, GError **err)
{
	if (name[0] && g_ascii_strcasecmp (name, "Inbox") != 0) {
		g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_FOLDER_ILLEGAL_NAME,
			     _("Cannot get folder `%s': illegal mailbox name"), name);
		return NULL;
	}
	
	return spruce_pop_folder_new (store, err);
}

static SpruceFolder *
pop_get_folder_by_url (SpruceStore *store, SpruceURL *url, GError **err)
{
	const char *name = url->fragment;
	
	return pop_get_folder (store, name ? name : "", err);
}

static int
pop_noop (SpruceStore *store, GError **err)
{
	SprucePOPStore *pop = (SprucePOPStore *) store;
	SprucePOPCommand *pc;
	int id;
	
	pc = spruce_pop_engine_queue (pop->engine, NULL, NULL, "NOOP\r\n");
	while ((id = spruce_pop_engine_iterate (pop->engine)) < pc->id && id != -1)
		;
	
	if (id == -1 || pc->retval == -1) {
		if (id == -1) {
			/* need to set our own error */
			g_set_error (err, SPRUCE_ERROR, pc->error ? pc->error : SPRUCE_ERROR_GENERIC,
				     _("POP server unexpectedly disconnected: %s"),
				     pc->error ? g_strerror (pc->error) : _("Unknown"));
		}
		
		spruce_pop_command_free (pop->engine, pc);
		
		return -1;
	}
	
	spruce_pop_command_free (pop->engine, pc);
	
	return 0;
}
