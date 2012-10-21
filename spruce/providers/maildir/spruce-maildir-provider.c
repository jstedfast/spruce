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

#include "spruce-maildir-store.h"

#define _(x) x


static guint
maildir_hash (SpruceURL *url)
{
	guint hash = 0;
	
#define ADD_HASH(s) if (s) hash ^= g_str_hash (s);
	
	ADD_HASH (url->protocol);
	ADD_HASH (url->path);
	ADD_HASH (url->fragment);
	
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
maildir_equal (SpruceURL *url0, SpruceURL *url1)
{
	return str_equal (url0->protocol, url1->protocol)
		&& str_equal (url0->path, url1->path)
		&& str_equal (url0->fragment, url1->fragment);
}


void
spruce_provider_module_init (void)
{
	SpruceProvider *provider;
	
	provider = g_object_new (SPRUCE_TYPE_PROVIDER, NULL);
	provider->protocol = "maildir";
	provider->name = _("Maildir");
	provider->description = _("Stores mail in the Maildir format.");
	
	provider->object_types[SPRUCE_PROVIDER_TYPE_STORE] =
		spruce_maildir_store_get_type ();
	
	provider->url_hash = (GHashFunc) maildir_hash;
	provider->url_equal = (GCompareFunc) maildir_equal;
	
	spruce_provider_register (provider);
	g_object_unref (provider);
}
