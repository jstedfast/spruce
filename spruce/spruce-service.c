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
#include <errno.h>

#include <glib/gi18n.h>

#include <spruce/spruce-error.h>
#include <spruce/spruce-service.h>


static void spruce_service_class_init (SpruceServiceClass *klass);
static void spruce_service_init (SpruceService *service, SpruceServiceClass *klass);
static void spruce_service_finalize (GObject *object);

static int service_connect (SpruceService *service, GError **err);
static int service_disconnect (SpruceService *service, gboolean clean, GError **err);
static GList *service_query_auth_types (SpruceService *service, GError **err);


static GObjectClass *parent_class = NULL;


GType
spruce_service_get_type (void)
{
	static GType type = 0;
	
	if (!type) {
		static const GTypeInfo info = {
			sizeof (SpruceServiceClass),
			NULL, /* base_class_init */
			NULL, /* base_class_finalize */
			(GClassInitFunc) spruce_service_class_init,
			NULL, /* class_finalize */
			NULL, /* class_data */
			sizeof (SpruceService),
			0,    /* n_preallocs */
			(GInstanceInitFunc) spruce_service_init,
		};
		
		type = g_type_register_static (G_TYPE_OBJECT, "SpruceService", &info, 0);
	}
	
	return type;
}


static void
spruce_service_class_init (SpruceServiceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	
	parent_class = g_type_class_ref (G_TYPE_OBJECT);
	
	object_class->finalize = spruce_service_finalize;
	
	klass->connect = service_connect;
	klass->disconnect = service_disconnect;
	klass->query_auth_types = service_query_auth_types;
}

static void
spruce_service_init (SpruceService *service, SpruceServiceClass *klass)
{
	service->session = NULL;
	service->provider = NULL;
	service->url = NULL;
	service->connected = FALSE;
}

static void
spruce_service_finalize (GObject *object)
{
	SpruceService *service = (SpruceService *) object;
	
	if (service->session)
		g_object_unref (service->session);
	
	if (service->provider)
		g_object_unref (service->provider);
	
	if (service->url)
		g_object_unref (service->url);
	
	G_OBJECT_CLASS (parent_class)->finalize (object);
}


void
spruce_service_construct (SpruceService *service, SpruceSession *session, SpruceProvider *provider, SpruceURL *url)
{
	g_return_if_fail (SPRUCE_IS_SERVICE (service));
	g_return_if_fail (SPRUCE_IS_SESSION (session));
	g_return_if_fail (SPRUCE_IS_PROVIDER (provider));
	g_return_if_fail (SPRUCE_IS_URL (url));
	
	g_object_ref (session);
	g_object_ref (provider);
	g_object_ref (url);
	
	service->session = session;
	service->provider = provider;
	service->url = url;
}


static int
service_connect (SpruceService *service, GError **err)
{
	service->connected = TRUE;
	return 0;
}


int
spruce_service_connect (SpruceService *service, GError **err)
{
	g_return_val_if_fail (SPRUCE_IS_SERVICE (service), -1);
	
	if (service->connected)
		return 0;
	
	return SPRUCE_SERVICE_GET_CLASS (service)->connect (service, err);
}


static int
service_disconnect (SpruceService *service, gboolean clean, GError **err)
{
	service->connected = FALSE;
	return 0;
}


int
spruce_service_disconnect (SpruceService *service, gboolean clean, GError **err)
{
	g_return_val_if_fail (SPRUCE_IS_SERVICE (service), -1);
	
	if (!service->connected)
		return 0;
	
	return SPRUCE_SERVICE_GET_CLASS (service)->disconnect (service, clean, err);
}


static GList *
service_query_auth_types (SpruceService *service, GError **err)
{
	return NULL;
}


GList *
spruce_service_query_auth_types (SpruceService *service, GError **err)
{
	g_return_val_if_fail (SPRUCE_IS_SERVICE (service), NULL);
	
	return SPRUCE_SERVICE_GET_CLASS (service)->query_auth_types (service, err);
}


SpruceURL *
spruce_service_get_url (SpruceService *service)
{
	g_return_val_if_fail (SPRUCE_IS_SERVICE (service), NULL);
	
	g_object_ref (service->url);
	
	return service->url;
}


gboolean
spruce_service_is_connected (SpruceService *service)
{
	g_return_val_if_fail (SPRUCE_IS_SERVICE (service), FALSE);
	
	return service->connected;
}


#if 0
struct hostent *
spruce_service_hostent (SpruceService *service, GError **err)
{
	const char *hostname;
	
	g_return_val_if_fail (SPRUCE_IS_SERVICE (service), NULL);
	
	hostname = service->url->host ? service->url->host : "localhost";
	return spruce_gethostbyname (hostname, err);
}


static struct hostent *
hostent_copy (struct hostent *hostent)
{
	struct hostent *host;
	int n;
	
	host = g_new (struct hostent, 1);
	host->h_name = g_strdup (hostent->h_name);
	
	if (hostent->h_aliases) {
		for (n = 0; hostent->h_aliases[n]; n++)
			;
		
		host->h_aliases = g_new (char *, n + 1);
		for (n = 0; hostent->h_aliases[n]; n++)
			host->h_aliases[n] = g_strdup (hostent->h_aliases[n]);
		
		host->h_aliases[n] = NULL;
	} else {
		host->h_aliases = NULL;
	}
	
	host->h_addrtype = hostent->h_addrtype;
	host->h_length = hostent->h_length;
	
	if (hostent->h_addr_list) {
		for (n = 0; hostent->h_addr_list[n]; n++)
			;
		
		host->h_addr_list = g_new (char *, n + 1);
		for (n = 0; hostent->h_addr_list[n]; n++) {
			host->h_addr_list[n] = g_malloc (host->h_length);
			memcpy (host->h_addr_list[n], hostent->h_addr_list[n], host->h_length);
		}
		
		host->h_addr_list[n] = NULL;
		host->h_addr = host->h_addr_list[0];
	} else {
		host->h_addr_list = NULL;
	}
	
	return host;
}


struct hostent *
spruce_gethostbyname (const char *hostname, GError **err)
{
	struct hostent hostbuf, *host;
	size_t hostbuflen;
	char *hostbufmem;
	int retval;
	
	g_return_val_if_fail (hostname != NULL, NULL);
	
	hostbuflen = 1024;
	hostbufmem = g_malloc (hostbuflen);
	
	while ((retval = g_gethostbyname_r (hostname, &hostbuf, hostbufmem, hostbuflen, err)) == ERANGE)
		hostbufmem = g_realloc (hostbufmem, (hostbuflen += 1024));
	
	if (retval == -1) {
		g_free (hostbufmem);
		return NULL;
	}
	
	host = hostent_copy (&hostbuf);
	
	g_free (hostbufmem);
	
	return host;
}


struct hostent *
spruce_gethostbyaddr (const char *addr, int length, int af, GError **err)
{
	struct hostent hostbuf, *host;
	size_t hostbuflen;
	char *hostbufmem;
	int retval;
	
	hostbuflen = 1024;
	hostbufmem = g_malloc (hostbuflen);
	
	while ((retval = g_gethostbyaddr_r (addr, length, af, &hostbuf, hostbufmem, hostbuflen, err)) == ERANGE)
		hostbufmem = g_realloc (hostbufmem, (hostbuflen += 1024));
	
	if (retval == -1) {
		g_free (hostbufmem);
		return NULL;
	}
	
	host = hostent_copy (&hostbuf);
	
	g_free (hostbufmem);
	
	return host;
}


void
spruce_hostent_free (struct hostent *hostent)
{
	int i;
	
	if (hostent == NULL)
		return;
	
	g_free (hostent->h_name);
	
	if (hostent->h_aliases) {
		for (i = 0; hostent->h_aliases[i]; i++)
			g_free (hostent->h_aliases[i]);
		g_free (hostent->h_aliases);
	}
	
	if (hostent->h_addr_list) {
		for (i = 0; hostent->h_addr_list[i]; i++)
			g_free (hostent->h_addr_list[i]);
		g_free (hostent->h_addr_list);
	}
	
	g_free (hostent);
}
#endif


int
spruce_getnameinfo (const struct sockaddr *sa, socklen_t salen, char **name, char **serv, int flags, GError **err)
{
	char *namebuf = NULL, *servbuf = NULL;
	size_t namelen = 0, servlen = 0;
	int rc;
	
	if (name) {
		namebuf = g_malloc (NI_MAXHOST + 1);
		namelen = NI_MAXHOST;
		namebuf[0] = '\0';
	}
	
	if (serv) {
		servbuf = g_malloc (NI_MAXSERV + 1);
		servlen = NI_MAXSERV;
		servbuf[0] = '\0';
	}
	
	if ((rc = getnameinfo (sa, salen, namebuf, namelen, servbuf, servlen, flags)) == 0) {
		if (name)
			*name = g_strdup (namebuf);
		
		if (serv)
			*serv = g_strdup (servbuf);
	} else {
		g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_SERVICE_UNAVAILABLE,
			     _("Name lookup failed: %s"),
			     rc == EAI_SYSTEM ? g_strerror (errno) : gai_strerror (rc));
	}
	
	g_free (namebuf);
	g_free (servbuf);
	
	return rc;
}

struct addrinfo *
spruce_service_addrinfo (SpruceService *service, GError **err)
{
	struct addrinfo hints;
	char *serv;
	
	serv = service->url->protocol;
	if (service->url->port != 0) {
		serv = g_alloca (16);
		sprintf (serv, "%d", service->url->port);
	}
	
	memset (&hints, 0, sizeof (hints));
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_family = PF_UNSPEC;
	
	return spruce_getaddrinfo (service->url->host, serv, NULL, &hints, err);
}


struct addrinfo *
spruce_getaddrinfo (const char *name, const char *serv, const char *port, struct addrinfo *hints, GError **err)
{
	struct addrinfo *res;
	int ret;
	
	if ((ret = getaddrinfo (name, serv, hints, &res)) == EAI_SERVICE && port != NULL)
		ret = getaddrinfo (name, port, hints, &res);
	
	if (ret != 0) {
		g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_SERVICE_UNAVAILABLE,
			     _("Cannot resolve host `%s': %s"), name,
			     ret == EAI_SYSTEM ? g_strerror (errno) : gai_strerror (ret));
		
		return NULL;
	}
	
	return res;
}


void
spruce_freeaddrinfo (struct addrinfo *ai)
{
	freeaddrinfo (ai);
}
