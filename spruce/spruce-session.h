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


#ifndef __SPRUCE_SESSION_H__
#define __SPRUCE_SESSION_H__

#include <glib.h>
#include <glib-object.h>

#include <spruce/spruce-provider.h>

G_BEGIN_DECLS

#define SPRUCE_TYPE_SESSION            (spruce_session_get_type ())
#define SPRUCE_SESSION(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), SPRUCE_TYPE_SESSION, SpruceSession))
#define SPRUCE_SESSION_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), SPRUCE_TYPE_SESSION, SpruceSessionClass))
#define SPRUCE_IS_SESSION(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SPRUCE_TYPE_SESSION))
#define SPRUCE_IS_SESSION_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), SPRUCE_TYPE_SESSION))
#define SPRUCE_SESSION_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), SPRUCE_TYPE_SESSION, SpruceSessionClass))

typedef struct _SpruceSession SpruceSession;
typedef struct _SpruceSessionClass SpruceSessionClass;

enum {
	SPRUCE_SESSION_PASSWORD_SECRET    = 0,         /* we *always* want it to be secret */
	SPRUCE_SESSION_PASSWORD_REPROMPT  = (1 << 1),
	SPRUCE_SESSION_PASSWORD_STATIC    = (1 << 2),
};

struct _SpruceSession {
	GObject parent_object;
	
	char *storage_path;
	
	unsigned int network_state:1;
};

struct _SpruceSessionClass {
	GObjectClass parent_class;
	
	void   (* alert_user)     (SpruceSession *session, const char *warning);
	
	char * (* request_passwd) (SpruceSession *session, const char *prompt,
				   const char *key, guint32 flags, GError **err);
	
	void   (* forget_passwd)  (SpruceSession *session, const char *key);
};


GType spruce_session_get_type (void);

const char *spruce_session_get_storage_path (SpruceSession *session);

void spruce_session_alert_user (SpruceSession *session, const char *warning);

char *spruce_session_request_passwd (SpruceSession *session, const char *prompt,
				     const char *key, guint32 flags, GError **err);

void spruce_session_forget_passwd (SpruceSession *session, const char *key);


void *spruce_session_get_service (SpruceSession *session, const char *uri, int type, GError **err);
#define spruce_session_get_store(session, uri, err) spruce_session_get_service (session, uri, SPRUCE_PROVIDER_TYPE_STORE, err)
#define spruce_session_get_transport(session, uri, err) spruce_session_get_service (session, uri, SPRUCE_PROVIDER_TYPE_TRANSPORT, err)

gboolean spruce_session_get_network_state (SpruceSession *session);
void spruce_session_set_network_state (SpruceSession *session, gboolean network_state);

G_END_DECLS

#endif /* __SPRUCE_SESSION_H__ */
