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

#include <glib/gi18n.h>

#include "spruce-sasl-cram-md5.h"


SpruceServiceAuthType spruce_sasl_cram_md5_authtype = {
	N_("CRAM-MD5"),
	
	N_("This option will connect to the server using a "
	   "secure CRAM-MD5 password, if the server supports it."),
	
	"CRAM-MD5",
	TRUE
};


static void spruce_sasl_cram_md5_class_init (SpruceSASLCramMd5Class *klass);

static GByteArray *cram_md5_challenge (SpruceSASL *sasl, GByteArray *token, GError **err);


static SpruceSASLClass *parent_class = NULL;


GType
spruce_sasl_cram_md5_get_type (void)
{
	static GType type = 0;
	
	if (!type) {
		static const GTypeInfo info = {
			sizeof (SpruceSASLCramMd5Class),
			NULL, /* base_class_init */
			NULL, /* base_class_finalize */
			(GClassInitFunc) spruce_sasl_cram_md5_class_init,
			NULL, /* class_finalize */
			NULL, /* class_data */
			sizeof (SpruceSASLCramMd5),
			0,    /* n_preallocs */
			NULL,
		};
		
		type = g_type_register_static (SPRUCE_TYPE_SASL, "SpruceSASLCramMd5", &info, 0);
	}
	
	return type;
}


static void
spruce_sasl_cram_md5_class_init (SpruceSASLCramMd5Class *klass)
{
	SpruceSASLClass *sasl_class = SPRUCE_SASL_CLASS (klass);
	
	parent_class = g_type_class_ref (SPRUCE_TYPE_SASL);
	
	sasl_class->challenge = cram_md5_challenge;
}


static GByteArray *
cram_md5_challenge (SpruceSASL *sasl, GByteArray *token, GError **err)
{
	unsigned char digest[16], md5asc[33], ipad[64], opad[64], *s, *p;
	GChecksum *checksum;
	const char *user;
	GByteArray *tok;
	size_t len = 16;
	size_t pw_len;
	char *passwd;
	int i;
	
	/* Need to wait for the server */
	if (!token)
		return NULL;
	
	g_return_val_if_fail (sasl->service->url->passwd != NULL, NULL);
	
	checksum = g_checksum_new (G_CHECKSUM_MD5);
	
	memset (ipad, 0, sizeof (ipad));
	memset (opad, 0, sizeof (opad));
	
	passwd = sasl->service->url->passwd;
	pw_len = strlen (passwd);
	if (pw_len <= 64) {
		memcpy (ipad, passwd, pw_len);
		memcpy (opad, passwd, pw_len);
	} else {
		g_checksum_update (checksum, passwd, pw_len);
		g_checksum_get_digest (checksum, ipad, &len);
		g_checksum_reset (checksum);
		memcpy (opad, ipad, 16);
		len = 16;
	}
	
	for (i = 0; i < 64; i++) {
		ipad[i] ^= 0x36;
		opad[i] ^= 0x5c;
	}
	
	len = 16;
	g_checksum_update (checksum, ipad, 64);
	g_checksum_update (checksum, token->data, token->len);
	g_checksum_get_digest (checksum, digest, &len);
	
	len = 16;
	g_checksum_reset (checksum);
	g_checksum_update (checksum, opad, 64);
	g_checksum_update (checksum, digest, 16);
	g_checksum_get_digest (checksum, digest, &len);
	
	/* lowercase hexify that bad-boy... */
	for (s = digest, p = md5asc; p < md5asc + 32; s++, p += 2)
		sprintf ((char *) p, "%.2x", *s);
	
	tok = g_byte_array_new ();
	user = sasl->service->url->user;
	g_byte_array_append (tok, (unsigned char *) user, strlen (user));
	g_byte_array_append (tok, (unsigned char *) " ", 1);
	g_byte_array_append (tok, md5asc, 32);
	
	g_checksum_free (checksum);
	
	sasl->authenticated = TRUE;
	
	return tok;
}
