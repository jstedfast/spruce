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

#include "spruce-sasl.h"

#include "spruce-string-utils.h"

#include "spruce-sasl-cram-md5.h"
#include "spruce-sasl-digest-md5.h"
#include "spruce-sasl-kerberos4.h"
#include "spruce-sasl-login.h"
#include "spruce-sasl-plain.h"

static void spruce_sasl_class_init (SpruceSASLClass *klass);
static void spruce_sasl_init (SpruceSASL *sasl, SpruceSASLClass *klass);
static void spruce_sasl_finalize (GObject *object);

static GByteArray *sasl_challenge (SpruceSASL *sasl, GByteArray *token, GError **err);


static GObjectClass *parent_class = NULL;


GType
spruce_sasl_get_type (void)
{
	static GType type = 0;
	
	if (!type) {
		static const GTypeInfo info = {
			sizeof (SpruceSASLClass),
			NULL, /* base_class_init */
			NULL, /* base_class_finalize */
			(GClassInitFunc) spruce_sasl_class_init,
			NULL, /* class_finalize */
			NULL, /* class_data */
			sizeof (SpruceSASL),
			0,    /* n_preallocs */
			(GInstanceInitFunc) spruce_sasl_init,
		};
		
		type = g_type_register_static (G_TYPE_OBJECT, "SpruceSASL", &info, 0);
	}
	
	return type;
}


static void
spruce_sasl_class_init (SpruceSASLClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	
	parent_class = g_type_class_ref (G_TYPE_OBJECT);
	
	object_class->finalize = spruce_sasl_finalize;
	
	klass->challenge = sasl_challenge;
}

static void
spruce_sasl_init (SpruceSASL *sasl, SpruceSASLClass *klass)
{
	sasl->service_name = NULL;
	sasl->mech = NULL;
	sasl->service = NULL;
	sasl->authenticated = FALSE;
}

static void
spruce_sasl_finalize (GObject *object)
{
	SpruceSASL *sasl = (SpruceSASL *) object;
	
	g_free (sasl->service_name);
	g_free (sasl->mech);
	g_object_unref (sasl->service);
	
	G_OBJECT_CLASS (parent_class)->finalize (object);
}


static GByteArray *
sasl_challenge (SpruceSASL *sasl, GByteArray *token, GError **err)
{
	return NULL;
}


/**
 * spruce_sasl_challenge:
 * @sasl: a SASL object
 * @token: a token, or %NULL
 * @err: exception
 *
 * If @token is %NULL, generate the initial SASL message to send to
 * the server. (This will be %NULL if the client doesn't initiate the
 * exchange.) Otherwise, @token is a challenge from the server, and
 * the return value is the response.
 *
 * Return value: The SASL response or %NULL. If an error occurred, @err
 * will also be set.
 **/
GByteArray *
spruce_sasl_challenge (SpruceSASL *sasl, GByteArray *token, GError **err)
{
	g_return_val_if_fail (SPRUCE_IS_SASL (sasl), NULL);
	
	return SPRUCE_SASL_GET_CLASS (sasl)->challenge (sasl, token, err);
}


/**
 * spruce_sasl_challenge_base64:
 * @sasl: a SASL object
 * @token: a base64-encoded token
 * @err: exception
 *
 * As with spruce_sasl_challenge(), but the challenge @token and the
 * response are both base64-encoded.
 *
 * Returns the next base64 encoded challenge token. If an error
 * occurred, @err will also be set.
 **/
char *
spruce_sasl_challenge_base64 (SpruceSASL *sasl, const char *token, GError **err)
{
	GByteArray *token_binary, *ret_binary;
	size_t len;
	char *ret;
	
	g_return_val_if_fail (SPRUCE_IS_SASL (sasl), NULL);
	
	if (token) {
		token_binary = g_byte_array_new ();
		len = strlen (token);
		g_byte_array_append (token_binary, (unsigned char *) token, len);
		token_binary->len = spruce_base64_decode ((char *) token_binary->data, len);
	} else
		token_binary = NULL;
	
	ret_binary = spruce_sasl_challenge (sasl, token_binary, err);
	if (token_binary)
		g_byte_array_free (token_binary, TRUE);
	if (!ret_binary)
		return NULL;
	
	ret = spruce_base64_encode ((char *) ret_binary->data, ret_binary->len);
	g_byte_array_free (ret_binary, TRUE);
	
	return ret;
}


/**
 * spruce_sasl_authenticated:
 * @sasl: a SASL object
 *
 * Returns whether or not @sasl has successfully authenticated the
 * user. This will be %TRUE after it returns the last needed response.
 * The caller must still pass that information on to the server and
 * verify that it has accepted it.
 **/
gboolean
spruce_sasl_authenticated (SpruceSASL *sasl)
{
	g_return_val_if_fail (SPRUCE_IS_SASL (sasl), FALSE);
	
	return sasl->authenticated;
}


/**
 * spruce_sasl_new:
 * @service_name: the SASL service name
 * @mechanism: the SASL mechanism
 * @service: the SpruceService that will be using this SASL
 *
 * Returns a new SpruceSASL for the given @service_name, @mechanism,
 * and @service, or %NULL if the mechanism is not supported.
 **/
SpruceSASL *
spruce_sasl_new (const char *service_name, const char *mechanism, SpruceService *service)
{
	SpruceSASL *sasl;
	
	g_return_val_if_fail (SPRUCE_IS_SERVICE (service), NULL);
	g_return_val_if_fail (service_name != NULL, NULL);
	g_return_val_if_fail (mechanism != NULL, NULL);
	
	/* We don't do ANONYMOUS here, because it's a little bit weird. */
	
	if (!strcmp (mechanism, "CRAM-MD5"))
		sasl = (SpruceSASL *) g_object_new (SPRUCE_TYPE_SASL_CRAM_MD5, NULL, NULL);
	else if (!strcmp (mechanism, "DIGEST-MD5"))
		sasl = (SpruceSASL *) g_object_new (SPRUCE_TYPE_SASL_DIGEST_MD5, NULL, NULL);
#ifdef HAVE_KRB4
	else if (!strcmp (mechanism, "KERBEROS_V4"))
		sasl = (SpruceSASL *) g_object_new (SPRUCE_TYPE_SASL_KERBEROS4, NULL, NULL);
#endif
	else if (!strcmp (mechanism, "PLAIN"))
		sasl = (SpruceSASL *) g_object_new (SPRUCE_TYPE_SASL_PLAIN, NULL, NULL);
	else if (!strcmp (mechanism, "LOGIN"))
		sasl = (SpruceSASL *) g_object_new (SPRUCE_TYPE_SASL_LOGIN, NULL, NULL);
	/*else if (!strcmp (mechanism, "NTLM"))
	  sasl = (SpruceSASL *) g_object_new (SPRUCE_TYPE_SASL_NTLM, NULL, NULL);*/
	else
		return NULL;
	
	sasl->mech = g_strdup (mechanism);
	sasl->service_name = g_strdup (service_name);
	sasl->service = service;
	g_object_ref (service);
	
	return sasl;
}


/**
 * spruce_sasl_authtype_list:
 *
 * Returns a GList of SASL-supported authtypes. The caller must free
 * the list, but not the contents.
 **/
GList *
spruce_sasl_authtype_list (void)
{
	GList *types = NULL;
	
	types = g_list_prepend (types, &spruce_sasl_cram_md5_authtype);
	types = g_list_prepend (types, &spruce_sasl_digest_md5_authtype);
#ifdef HAVE_KRB4
	types = g_list_prepend (types, &spruce_sasl_kerberos4_authtype);
#endif
	/*types = g_list_prepend (types, &spruce_sasl_ntlm_authtype);*/
	types = g_list_prepend (types, &spruce_sasl_plain_authtype);
	
	return types;
}


/**
 * spruce_sasl_authtype:
 * @mechanism: the SASL mechanism to get an authtype for
 *
 * Returns a SpruceServiceAuthType for the given mechanism, if it is
 * supported.
 **/
SpruceServiceAuthType *
spruce_sasl_authtype (const char *mechanism)
{
	if (!strcmp (mechanism, "CRAM-MD5"))
		return &spruce_sasl_cram_md5_authtype;
	else if (!strcmp (mechanism, "DIGEST-MD5"))
		return &spruce_sasl_digest_md5_authtype;
#ifdef HAVE_KRB4
	else if (!strcmp (mechanism, "KERBEROS_V4"))
		return &spruce_sasl_kerberos4_authtype;
#endif
	else if (!strcmp (mechanism, "PLAIN"))
		return &spruce_sasl_plain_authtype;
	else if (!strcmp (mechanism, "LOGIN"))
		return &spruce_sasl_login_authtype;
	/*else if (!strcmp (mechanism, "NTLM"))
	  return &spruce_sasl_ntlm_authtype;*/
	else
		return NULL;
}
