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

#include <glib/gi18n.h>

#include "spruce-sasl-plain.h"


SpruceServiceAuthType spruce_sasl_plain_authtype = {
	N_("Plain"),
	
	N_("This option will connect to the server using a "
	   "simple password."),
	
	"PLAIN",
	TRUE
};


static void spruce_sasl_plain_class_init (SpruceSASLPlainClass *klass);

static GByteArray *plain_challenge (SpruceSASL *sasl, GByteArray *token, GError **err);


static SpruceSASLClass *parent_class = NULL;


GType
spruce_sasl_plain_get_type (void)
{
	static GType type = 0;
	
	if (!type) {
		static const GTypeInfo info = {
			sizeof (SpruceSASLPlainClass),
			NULL, /* base_class_init */
			NULL, /* base_class_finalize */
			(GClassInitFunc) spruce_sasl_plain_class_init,
			NULL, /* class_finalize */
			NULL, /* class_data */
			sizeof (SpruceSASLPlain),
			0,    /* n_preallocs */
			NULL,
		};
		
		type = g_type_register_static (SPRUCE_TYPE_SASL, "SpruceSASLPlain", &info, 0);
	}
	
	return type;
}


static void
spruce_sasl_plain_class_init (SpruceSASLPlainClass *klass)
{
	SpruceSASLClass *sasl_class = SPRUCE_SASL_CLASS (klass);
	
	parent_class = g_type_class_ref (SPRUCE_TYPE_SASL);
	
	sasl_class->challenge = plain_challenge;
}


static GByteArray *
plain_challenge (SpruceSASL *sasl, GByteArray *token, GError **err)
{
	SpruceURL *url = sasl->service->url;
	GByteArray *buf = NULL;
	
	g_return_val_if_fail (url->passwd != NULL, NULL);
	
	buf = g_byte_array_new ();
	g_byte_array_append (buf, (unsigned char *) "", 1);
	g_byte_array_append (buf, (unsigned char *) url->user, strlen (url->user));
	g_byte_array_append (buf, (unsigned char *) "", 1);
	g_byte_array_append (buf, (unsigned char *) url->passwd, strlen (url->passwd));
	
	sasl->authenticated = TRUE;
	
	return buf;
}
