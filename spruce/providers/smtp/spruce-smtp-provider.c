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

#include <spruce/spruce-provider.h>

#include "spruce-smtp-transport.h"

#define _(x) x


static int
smtp_port (const char *proto)
{
	if (proto) {
		if (!strcmp (proto, "smtp"))
			return 25;
		
		if (!strcmp (proto, "smtps"))
			return 465;
	}
	
	return 0;
}

static guint
smtp_hash (SpruceURL *url)
{
	int port = url->port ? url->port : smtp_port (url->protocol);
	guint hash = 0;
	
#define ADD_HASH(s) if (s) hash ^= g_str_hash (s);
	
	ADD_HASH (url->protocol);
	ADD_HASH (url->user);
	ADD_HASH (url->auth);
	hash ^= port;
	
	return hash;
}

static int
str_equal (const char *str0, const char *str1)
{
	if (str0 == NULL) {
		if (str1 == NULL)
			return TRUE;
		else
			return FALSE;
	}
	
	if (str1 == NULL)
		return FALSE;
	
	return strcmp (str0, str1) == 0;
}

static int
smtp_equal (SpruceURL *url0, SpruceURL *url1)
{
	int port0 = url0->port ? url0->port : smtp_port (url0->protocol);
	int port1 = url1->port ? url1->port : smtp_port (url1->protocol);
	
	return str_equal (url0->protocol, url1->protocol)
		&& str_equal (url0->user, url1->user)
		&& str_equal (url0->auth, url1->auth)
		&& str_equal (url0->host, url1->host)
		&& port0 == port1;
}


static void
register_provider (const char *proto, const char *name, const char *desc)
{
	SpruceProvider *provider;
	
	provider = g_object_new (SPRUCE_TYPE_PROVIDER, NULL);
	provider->protocol = proto;
	provider->name = name;
	provider->description = desc;
	
	provider->object_types[SPRUCE_PROVIDER_TYPE_TRANSPORT] =
		spruce_smtp_transport_get_type ();
	
	provider->url_hash = (GHashFunc) smtp_hash;
	provider->url_equal = (GCompareFunc) smtp_equal;
	
	spruce_provider_register (provider);
	g_object_unref (provider);
}

void
spruce_provider_module_init (void)
{
	register_provider ("smtp", _("SMTP"), _("Sends mail using the SMTP protocol."));
#ifdef HAVE_SSL
	register_provider ("smtps", _("SMTP/S"), _("Sends mail using the SMTP protocol over an SSL connection."));
#endif
}
