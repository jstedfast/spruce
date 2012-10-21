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

#ifdef HAVE_KRB4

#include <string.h>

#include <krb.h>
/* MIT krb4 des.h #defines _ */
#undef _

#include <glib/gi18n.h>

#include "spruce-error.h"
#include "spruce-sasl-kerberos4.h"


SpruceServiceAuthType spruce_sasl_kerberos4_authtype = {
	N_("Kerberos 4"),
	
	N_("This option will connect to the server using "
	   "Kerberos 4 authentication."),
	
	"KERBEROS_V4",
	FALSE
};

#define KERBEROS_V4_PROTECTION_NONE      1
#define KERBEROS_V4_PROTECTION_INTEGRITY 2
#define KERBEROS_V4_PROTECTION_PRIVACY   4

struct _SpruceSASLKerberos4Private {
	int state;
	
	guint32 nonce_n;
	guint32 nonce_h;
	
	des_cblock session;
	des_key_schedule schedule;
};


static void spruce_sasl_kerberos4_class_init (SpruceSASLKerberos4Class *klass);
static void spruce_sasl_kerberos4_init (SpruceSASLKerberos4 *kerberos4, SpruceSASLKerberos4Class *klass);
static void spruce_sasl_kerberos4_finalize (GObject *object);

static GByteArray *kerberos4_challenge (SpruceSASL *sasl, GByteArray *token, GError **err);


static SpruceSASLClass *parent_class = NULL;


GType
spruce_sasl_kerberos4_get_type (void)
{
	static GType type = 0;
	
	if (!type) {
		static const GTypeInfo info = {
			sizeof (SpruceSASLKerberos4Class),
			NULL, /* base_class_init */
			NULL, /* base_class_finalize */
			(GClassInitFunc) spruce_sasl_kerberos4_class_init,
			NULL, /* class_finalize */
			NULL, /* class_data */
			sizeof (SpruceSASLKerberos4),
			0,    /* n_preallocs */
			(GInstanceInitFunc) spruce_sasl_kerberos4_init,
		};
		
		type = g_type_register_static (SPRUCE_TYPE_SASL, "SpruceSASLKerberos4", &info, 0);
	}
	
	return type;
}


static void
spruce_sasl_kerberos4_class_init (SpruceSASLKerberos4Class *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	SpruceSASLClass *sasl_class = SPRUCE_SASL_CLASS (klass);
	
	parent_class = g_type_class_ref (SPRUCE_TYPE_SASL);
	
	object_class->finalize = spruce_sasl_kerberos4_finalize;
	
	sasl_class->challenge = kerberos4_challenge;
}

static void
spruce_sasl_kerberos4_init (SpruceSASLKerberos4 *kerberos4, SpruceSASLKerberos4Class *klass)
{
	kerberos4->priv = g_new0 (struct _SpruceSASLKerberos4Private, 1);
}

static void
spruce_sasl_kerberos4_finalize (GObject *object)
{
	SpruceSASLKerberos4 *sasl = (SpruceSASLKerberos4 *) object;
	
	if (sasl->priv) {
		memset (sasl->priv, 0, sizeof (sasl->priv));
		g_free (sasl->priv);
	}
}


static GByteArray *
kerberos4_challenge (SpruceSASL *sasl, GByteArray *token, GError **err)
{
	struct _SpruceSASLKerberos4Private *priv = ((SpruceSASLKerberos4 *) sasl)->priv;
	GByteArray *ret = NULL;
	char *inst, *realm, *username;
	struct addrinfo hints, *ai;
	KTEXT_ST authenticator;
	CREDENTIALS credentials;
	int status, len;
	guint32 plus1;
	
	/* Need to wait for the server */
	if (!token)
		return NULL;
	
	switch (priv->state) {
	case 0:
		if (token->len != 4) {
			g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_SERVICE_CANT_AUTHENTICATE,
				     "%s", _("Bad authentication response from server."));
			g_warning ("Bad authentication response from server.");
			goto lose;
		}
		
		memcpy (&priv->nonce_n, token->data, 4);
		priv->nonce_h = ntohl (priv->nonce_n);
		
		/* Our response is an authenticator including that number. */
		memset (&hints, 0, sizeof (hints));
		hints.ai_flags = AI_CANONNAME;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_family = PF_UNSPEC;
		
		if (!(ai = spruce_getaddrinfo (sasl->service->url->host, sasl->service->url->protocol, NULL, &hints, err)))
			goto lose;
		
		inst = g_ascii_strdown (ai->ai_canonname, strcspn (ai->ai_canonname, "."));
		realm = g_strdup (krb_realmofhost (ai->ai_canonname));
		
		status = krb_mk_req (&authenticator, sasl->service_name, inst, realm, priv->nonce_h);
		if (status == KSUCCESS) {
			status = krb_get_cred (sasl->service_name, inst, realm, &credentials);
			memcpy (priv->session, credentials.session, sizeof (priv->session));
			memset (&credentials, 0, sizeof (credentials));
		}
		g_free (inst);
		g_free (realm);
		
		if (status != KSUCCESS) {
			g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_SERVICE_CANT_AUTHENTICATE,
				     _("Could not get Kerberos ticket: %s"), krb_err_txt[status]);
			g_warning ("Could not get Kerberos ticket: %s", krb_err_txt[status]);
			goto lose;
		}
		
		des_key_sched (&priv->session, priv->schedule);
		
		ret = g_byte_array_new ();
		
		g_byte_array_append (ret, (unsigned char *) authenticator.dat, authenticator.length);
		
		break;
	case 1:
		if (token->len != 8) {
			g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_SERVICE_CANT_AUTHENTICATE,
				     "%s", _("Bad authentication response from server."));
			g_warning ("Bad authentication response from server.");
			goto lose;
		}
		
		/* This one is encrypted. */
		des_ecb_encrypt ((des_cblock *) token->data, (des_cblock *) token->data, priv->schedule, 0);
		
		/* Check that the returned value is the original nonce plus one. */
		memcpy (&plus1, token->data, 4);
		if (ntohl (plus1) != priv->nonce_h + 1) {
			g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_SERVICE_CANT_AUTHENTICATE,
				     "%s", _("Bad authentication response from server."));
			g_warning ("Bad authentication response from server.");
			goto lose;
		}
		
		/* "the fifth octet contain[s] a bit-mask specifying the
		 * protection mechanisms supported by the server"
		 */
		if (!(token->data[4] & KERBEROS_V4_PROTECTION_NONE)) {
			g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_SERVICE_CANT_AUTHENTICATE,
				     "%s", _("Server does not support `no protection'"));
			g_warning ("Server does not support `no protection'");
			goto lose;
		}
		
		username = sasl->service->url->user;
		len = strlen (username) + 9;
		len += 8 - len % 8;
		ret = g_byte_array_new ();
		g_byte_array_set_size (ret, len);
		memset (ret->data, 0, len);
		memcpy (ret->data, &priv->nonce_n, 4);
		ret->data[4] = KERBEROS_V4_PROTECTION_NONE;
		ret->data[5] = ret->data[6] = ret->data[7] = 0;
		strcpy (ret->data + 8, username);
		
		des_pcbc_encrypt ((void *) ret->data, (void *) ret->data, len,
				  priv->schedule, &priv->session, 1);
		memset (&priv->session, 0, sizeof (priv->session));
		
		sasl->authenticated = TRUE;
		break;
	}
	
	priv->state++;
	return ret;
	
 lose:
	memset (&priv->session, 0, sizeof (priv->session));
	
	return NULL;
}

#endif /* HAVE_KRB4 */
