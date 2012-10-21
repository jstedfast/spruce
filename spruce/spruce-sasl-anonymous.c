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

#include "spruce-error.h"
#include "spruce-sasl-anonymous.h"


SpruceServiceAuthType spruce_sasl_anonymous_authtype = {
	N_("Anonymous"),
	
	N_("This option will connect to the server using an anonymous login."),
	
	"ANONYMOUS",
	FALSE
};


static void spruce_sasl_anonymous_class_init (SpruceSASLAnonymousClass *klass);
static void spruce_sasl_anonymous_init (SpruceSASLAnonymous *anon, SpruceSASLAnonymousClass *klass);
static void spruce_sasl_anonymous_finalize (GObject *object);

static GByteArray *anon_challenge (SpruceSASL *sasl, GByteArray *token, GError **err);


static SpruceSASLClass *parent_class = NULL;


GType
spruce_sasl_anonymous_get_type (void)
{
	static GType type = 0;
	
	if (!type) {
		static const GTypeInfo info = {
			sizeof (SpruceSASLAnonymousClass),
			NULL, /* base_class_init */
			NULL, /* base_class_finalize */
			(GClassInitFunc) spruce_sasl_anonymous_class_init,
			NULL, /* class_finalize */
			NULL, /* class_data */
			sizeof (SpruceSASLAnonymous),
			0,    /* n_preallocs */
			(GInstanceInitFunc) spruce_sasl_anonymous_init,
		};
		
		type = g_type_register_static (SPRUCE_TYPE_SASL, "SpruceSASLAnonymous", &info, 0);
	}
	
	return type;
}


static void
spruce_sasl_anonymous_class_init (SpruceSASLAnonymousClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	SpruceSASLClass *sasl_class = SPRUCE_SASL_CLASS (klass);
	
	parent_class = g_type_class_ref (SPRUCE_TYPE_SASL);
	
	object_class->finalize = spruce_sasl_anonymous_finalize;
	
	sasl_class->challenge = anon_challenge;
}

static void
spruce_sasl_anonymous_init (SpruceSASLAnonymous *anon, SpruceSASLAnonymousClass *klass)
{
	anon->trace_info = NULL;
}

static void
spruce_sasl_anonymous_finalize (GObject *object)
{
	SpruceSASLAnonymous *anon = (SpruceSASLAnonymous *) object;
	
	g_free (anon->trace_info);
	
	G_OBJECT_CLASS (parent_class)->finalize (object);
}


static GByteArray *
anon_challenge (SpruceSASL *sasl, GByteArray *token, GError **err)
{
	SpruceSASLAnonymous *anon = (SpruceSASLAnonymous *) sasl;
	GByteArray *ret = NULL;
	
	if (token) {
		g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_SERVICE_CANT_AUTHENTICATE,
			     "%s", _("Authentication failed"));
		return NULL;
	}
	
	switch (anon->type) {
	case SPRUCE_SASL_ANON_TRACE_EMAIL:
		if (!strchr (anon->trace_info, '@')) {
			g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_SERVICE_CANT_AUTHENTICATE,
				     _("Invalid email trace information: %s"), anon->trace_info);
			return NULL;
		}
		
		ret = g_byte_array_new ();
		g_byte_array_append (ret, (unsigned char *) anon->trace_info, strlen (anon->trace_info));
		break;
	case SPRUCE_SASL_ANON_TRACE_OPAQUE:
		if (strchr (anon->trace_info, '@')) {
			g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_SERVICE_CANT_AUTHENTICATE,
				     _("Invalid opaque trace information: %s"), anon->trace_info);
			return NULL;
		}
		
		ret = g_byte_array_new ();
		g_byte_array_append (ret, (unsigned char *) anon->trace_info, strlen (anon->trace_info));
		break;
	case SPRUCE_SASL_ANON_TRACE_EMPTY:
		ret = g_byte_array_new ();
		break;
	default:
		g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_SERVICE_CANT_AUTHENTICATE,
			     _("Invalid trace information: %s"), anon->trace_info);
		return NULL;
	}
	
	sasl->authenticated = TRUE;
	
	return ret;
}


SpruceSASL *
spruce_sasl_anonymous_new (SpruceSASLAnonTraceType type, const char *trace_info)
{
	SpruceSASLAnonymous *anon;
	
	if (!trace_info && type != SPRUCE_SASL_ANON_TRACE_EMPTY)
		return NULL;
	
	anon = g_object_new (SPRUCE_TYPE_SASL_ANONYMOUS, NULL);
	anon->trace_info = g_strdup (trace_info);
	anon->type = type;
	
	return (SpruceSASL *) anon;
}
