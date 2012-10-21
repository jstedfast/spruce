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

#include <glib/gi18n.h>

#include <spruce/spruce-error.h>
#include <spruce/spruce-store.h>
#include <spruce/spruce-transport.h>

#include "spruce-session.h"


static void spruce_session_class_init (SpruceSessionClass *klass);
static void spruce_session_init (SpruceSession *session, SpruceSessionClass *klass);
static void spruce_session_finalize (GObject *object);

static void session_alert_user (SpruceSession *session, const char *warning);
static char *session_request_passwd (SpruceSession *session, const char *prompt,
				     const char *key, guint32 flags, GError **err);
static void session_forget_passwd (SpruceSession *session, const char *key);


static GObjectClass *parent_class = NULL;


GType
spruce_session_get_type (void)
{
	static GType type = 0;
	
	if (!type) {
		static const GTypeInfo info = {
			sizeof (SpruceSessionClass),
			NULL, /* base_class_init */
			NULL, /* base_class_finalize */
			(GClassInitFunc) spruce_session_class_init,
			NULL, /* class_finalize */
			NULL, /* class_data */
			sizeof (SpruceSession),
			0,    /* n_preallocs */
			(GInstanceInitFunc) spruce_session_init,
		};
		
		type = g_type_register_static (G_TYPE_OBJECT, "SpruceSession", &info, 0);
	}
	
	return type;
}


static void
spruce_session_class_init (SpruceSessionClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	
	parent_class = g_type_class_ref (G_TYPE_OBJECT);
	
	object_class->finalize = spruce_session_finalize;
	
	klass->alert_user = session_alert_user;
	klass->request_passwd = session_request_passwd;
	klass->forget_passwd = session_forget_passwd;
}

static void
spruce_session_init (SpruceSession *session, SpruceSessionClass *klass)
{
	session->network_state = TRUE;
}

static void
spruce_session_finalize (GObject *object)
{
	SpruceSession *session = (SpruceSession *) object;
	
	g_free (session->storage_path);
	
	G_OBJECT_CLASS (parent_class)->finalize (object);
}


static void
session_alert_user (SpruceSession *session, const char *warning)
{
	g_warning ("%s", warning);
}


void
spruce_session_alert_user (SpruceSession *session, const char *warning)
{
	g_return_if_fail (SPRUCE_IS_SESSION (session));
	
	SPRUCE_SESSION_GET_CLASS (session)->alert_user (session, warning);
}


static char *
session_request_passwd (SpruceSession *session, const char *prompt,
			const char *key, guint32 flags, GError **err)
{
	return NULL;
}


char *
spruce_session_request_passwd (SpruceSession *session, const char *prompt,
			       const char *key, guint32 flags, GError **err)
{
	g_return_val_if_fail (SPRUCE_IS_SESSION (session), NULL);
	
	return SPRUCE_SESSION_GET_CLASS (session)->request_passwd (session, prompt, key, flags, err);
}


static void
session_forget_passwd (SpruceSession *session, const char *key)
{
	;
}


void
spruce_session_forget_passwd (SpruceSession *session, const char *key)
{
	g_return_if_fail (SPRUCE_IS_SESSION (session));
	
	SPRUCE_SESSION_GET_CLASS (session)->forget_passwd (session, key);
}



/**
 * spruce_session_get_service:
 * @session: session
 * @uri: uri for a service
 * @type: expected service type
 * @err: error
 *
 * Gets a SpruceService of type @type (#SPRUCE_PROVIDER_TYPE_STORE or
 * #SPRUCE_PROVIDER_TYPE_TRANSPORT).
 *
 * Returns a SpruceService on success or %NULL on fail.
 **/
void *
spruce_session_get_service (SpruceSession *session, const char *uri, int type, GError **err)
{
	SpruceProvider *provider;
	SpruceService *service;
	SpruceURL *url;
	
	g_return_val_if_fail (SPRUCE_IS_SESSION (session), NULL);
	g_return_val_if_fail (type >= 0 && type < SPRUCE_NUM_PROVIDER_TYPES, NULL);
	g_return_val_if_fail (uri != NULL, NULL);
	
	if (!(url = spruce_url_new_from_string (uri))) {
		g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_SERVICE_UNAVAILABLE,
			     _("Failed to get service for `%s': invalid URI"), uri);
		return NULL;
	}
	
	if (!(provider = spruce_provider_lookup (url->protocol, err))) {
		g_object_unref (url);
		return NULL;
	}
	
	if (!provider->object_types[type]) {
		g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_SERVICE_UNAVAILABLE,
			     _("Failed to get service for `%s': service type not supported by provider"), uri);
		return NULL;
	}
	
	if (!(service = spruce_provider_lookup_service (provider, url, type))) {
		service = g_object_new (provider->object_types[type], NULL);
		spruce_service_construct (service, session, provider, url);
		spruce_provider_insert_service (provider, service, type);
	}
	
	g_object_unref (url);
	
	return service;
}


const char *
spruce_session_get_storage_path (SpruceSession *session)
{
	if (!session->storage_path) {
		session->storage_path = g_build_filename (g_get_home_dir (), ".spruce", "mail", NULL);
		g_warning ("Storage path not set for the Session! Defaulting to %s", session->storage_path);
	}
	
	return session->storage_path;
}


gboolean
spruce_session_get_network_state (SpruceSession *session)
{
	g_return_val_if_fail (SPRUCE_IS_SESSION(session), FALSE);
	
	return session->network_state;
}

void
spruce_session_set_network_state (SpruceSession *session, gboolean network_state)
{
	g_return_if_fail (SPRUCE_IS_SESSION(session));
	
	session->network_state = network_state;
}
