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


#ifndef __SPRUCE_SERVICE_H__
#define __SPRUCE_SERVICE_H__

#include <glib.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include <spruce/spruce-url.h>
#include <spruce/spruce-session.h>
#include <spruce/spruce-provider.h>

G_BEGIN_DECLS

#define SPRUCE_TYPE_SERVICE            (spruce_service_get_type ())
#define SPRUCE_SERVICE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), SPRUCE_TYPE_SERVICE, SpruceService))
#define SPRUCE_SERVICE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), SPRUCE_TYPE_SERVICE, SpruceServiceClass))
#define SPRUCE_IS_SERVICE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SPRUCE_TYPE_SERVICE))
#define SPRUCE_IS_SERVICE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), SPRUCE_TYPE_SERVICE))
#define SPRUCE_SERVICE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), SPRUCE_TYPE_SERVICE, SpruceServiceClass))

typedef struct _SpruceService SpruceService;
typedef struct _SpruceServiceClass SpruceServiceClass;

/* query_auth_types returns a GList of these */
typedef struct {
	char *name;               /* user-friendly name */
	char *description;
	char *authproto;
	
	gboolean need_password;   /* needs a password to authenticate */
} SpruceServiceAuthType;

struct _SpruceService {
	GObject parent_object;
	
	SpruceSession *session;
	SpruceProvider *provider;
	SpruceURL *url;
	
	gboolean connected;
};

struct _SpruceServiceClass {
	GObjectClass parent_class;
	
	int (* connect) (SpruceService *service, GError **err);
	int (* disconnect) (SpruceService *service, gboolean clean, GError **err);
	
	GList * (* query_auth_types) (SpruceService *service, GError **err);
};


GType spruce_service_get_type (void);

void spruce_service_construct (SpruceService *service, SpruceSession *session, SpruceProvider *provider, SpruceURL *url);

int spruce_service_connect (SpruceService *service, GError **err);
int spruce_service_disconnect (SpruceService *service, gboolean clean, GError **err);

GList *spruce_service_query_auth_types (SpruceService *service, GError **err);

SpruceURL *spruce_service_get_url (SpruceService *service);
gboolean spruce_service_is_connected (SpruceService *service);


/* host lookup utility functions */
int spruce_getnameinfo (const struct sockaddr *sa, socklen_t salen, char **name, char **serv, int flags, GError **err);

struct addrinfo *spruce_service_addrinfo (SpruceService *service, GError **err);
struct addrinfo *spruce_getaddrinfo (const char *name, const char *serv, const char *port, struct addrinfo *hints, GError **err);
void spruce_freeaddrinfo (struct addrinfo *ai);

G_END_DECLS

#endif /* __SPRUCE_SERVICE_H__ */
