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
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <netdb.h>
#include <ctype.h>
#include <time.h>

#include <glib/gi18n.h>

#include "spruce-sasl-digest-md5.h"
#include "spruce-string-utils.h"
#include "spruce-error.h"

#define d(x)

#define PARANOID(x) x


SpruceServiceAuthType spruce_sasl_digest_md5_authtype = {
	N_("DIGEST-MD5"),
	
	N_("This option will connect to the server using a "
	   "secure DIGEST-MD5 password, if the server supports it."),
	
	"DIGEST-MD5",
	TRUE
};

enum {
	STATE_AUTH,
	STATE_FINAL
};

typedef struct {
	char *name;
	guint type;
} DataType;

enum {
	DIGEST_REALM,
	DIGEST_NONCE,
	DIGEST_QOP,
	DIGEST_STALE,
	DIGEST_MAXBUF,
	DIGEST_CHARSET,
	DIGEST_ALGORITHM,
	DIGEST_CIPHER,
	DIGEST_UNKNOWN
};

static DataType digest_args[] = {
	{ "realm",     DIGEST_REALM     },
	{ "nonce",     DIGEST_NONCE     },
	{ "qop",       DIGEST_QOP       },
	{ "stale",     DIGEST_STALE     },
	{ "maxbuf",    DIGEST_MAXBUF    },
	{ "charset",   DIGEST_CHARSET   },
	{ "algorithm", DIGEST_ALGORITHM },
	{ "cipher",    DIGEST_CIPHER    },
	{ NULL,        DIGEST_UNKNOWN   }
};

#define QOP_AUTH           (1 << 0)
#define QOP_AUTH_INT       (1 << 1)
#define QOP_AUTH_CONF      (1 << 2)
#define QOP_INVALID        (1 << 3)

static DataType qop_types[] = {
	{ "auth",      QOP_AUTH      },
	{ "auth-int",  QOP_AUTH_INT  },
	{ "auth-conf", QOP_AUTH_CONF },
	{ NULL,        QOP_INVALID   }
};

#define CIPHER_DES         (1 << 0)
#define CIPHER_3DES        (1 << 1)
#define CIPHER_RC4         (1 << 2)
#define CIPHER_RC4_40      (1 << 3)
#define CIPHER_RC4_56      (1 << 4)
#define CIPHER_INVALID     (1 << 5)

static DataType cipher_types[] = {
	{ "des",    CIPHER_DES     },
	{ "3des",   CIPHER_3DES    },
	{ "rc4",    CIPHER_RC4     },
	{ "rc4-40", CIPHER_RC4_40  },
	{ "rc4-56", CIPHER_RC4_56  },
	{ NULL,     CIPHER_INVALID }
};

struct _param {
	char *name;
	char *value;
};

struct _DigestChallenge {
	GPtrArray *realms;
	char *nonce;
	guint qop;
	gboolean stale;
	gint32 maxbuf;
	char *charset;
	char *algorithm;
	guint cipher;
	GList *params;
};

struct _DigestURI {
	char *type;
	char *host;
	char *name;
};

struct _DigestResponse {
	char *username;
	char *realm;
	char *nonce;
	char *cnonce;
	char nc[9];
	guint qop;
	struct _DigestURI *uri;
	char resp[33];
	guint32 maxbuf;
	char *charset;
	guint cipher;
	char *authzid;
	char *param;
};

struct _SpruceSASLDigestMd5Private {
	struct _DigestChallenge *challenge;
	struct _DigestResponse *response;
	int state;
};


static void spruce_sasl_digest_md5_class_init (SpruceSASLDigestMd5Class *klass);
static void spruce_sasl_digest_md5_init (SpruceSASLDigestMd5 *digest, SpruceSASLDigestMd5Class *klass);
static void spruce_sasl_digest_md5_finalize (GObject *object);

static GByteArray *digest_md5_challenge (SpruceSASL *sasl, GByteArray *token, GError **err);


static SpruceSASLClass *parent_class = NULL;


GType
spruce_sasl_digest_md5_get_type (void)
{
	static GType type = 0;
	
	if (!type) {
		static const GTypeInfo info = {
			sizeof (SpruceSASLDigestMd5Class),
			NULL, /* base_class_init */
			NULL, /* base_class_finalize */
			(GClassInitFunc) spruce_sasl_digest_md5_class_init,
			NULL, /* class_finalize */
			NULL, /* class_data */
			sizeof (SpruceSASLDigestMd5),
			0,    /* n_preallocs */
			(GInstanceInitFunc) spruce_sasl_digest_md5_init,
		};
		
		type = g_type_register_static (SPRUCE_TYPE_SASL, "SpruceSASLDigestMd5", &info, 0);
	}
	
	return type;
}


static void
spruce_sasl_digest_md5_class_init (SpruceSASLDigestMd5Class *klass)
{
	SpruceSASLClass *sasl_class = SPRUCE_SASL_CLASS (klass);
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	
	parent_class = g_type_class_ref (SPRUCE_TYPE_SASL);
	
	object_class->finalize = spruce_sasl_digest_md5_finalize;
	
	sasl_class->challenge = digest_md5_challenge;
}

static void
spruce_sasl_digest_md5_init (SpruceSASLDigestMd5 *digest, SpruceSASLDigestMd5Class *klass)
{
	digest->priv = g_new0 (struct _SpruceSASLDigestMd5Private, 1);
}

static void
spruce_sasl_digest_md5_finalize (GObject *object)
{
	SpruceSASLDigestMd5 *sasl = (SpruceSASLDigestMd5 *) object;
	struct _DigestChallenge *c = sasl->priv->challenge;
	struct _DigestResponse *r = sasl->priv->response;
	GList *p;
	int i;
	
	for (i = 0; i < c->realms->len; i++)
		g_free (c->realms->pdata[i]);
	g_ptr_array_free (c->realms, TRUE);
	g_free (c->nonce);
	g_free (c->charset);
	g_free (c->algorithm);
	for (p = c->params; p; p = p->next) {
		struct _param *param = p->data;
		
		g_free (param->name);
		g_free (param->value);
		g_free (param);
	}
	g_list_free (c->params);
	g_free (c);
	
	g_free (r->username);
	g_free (r->realm);
	g_free (r->nonce);
	g_free (r->cnonce);
	if (r->uri) {
		g_free (r->uri->type);
		g_free (r->uri->host);
		g_free (r->uri->name);
	}
	g_free (r->charset);
	g_free (r->authzid);
	g_free (r->param);
	g_free (r);
	
	g_free (sasl->priv);
	
	G_OBJECT_CLASS (parent_class)->finalize (object);
}


static void
decode_lwsp (const char **in)
{
	const unsigned char *inptr = (const unsigned char *) *in;
	
	while (isspace (*inptr))
		inptr++;
	
	*in = (const char *) inptr;
}

static char *
decode_quoted_string (const char **in)
{
	const char *inptr = *in;
	char *out = NULL, *outptr;
	int outlen;
	int c;
	
	decode_lwsp (&inptr);
	if (*inptr == '"') {
		const char *intmp;
		int skip = 0;
		
		/* first, calc length */
		inptr++;
		intmp = inptr;
		while ((c = *intmp++) && c != '"') {
			if (c == '\\' && *intmp) {
				intmp++;
				skip++;
			}
		}
		
		outlen = intmp - inptr - skip;
		out = outptr = g_malloc (outlen + 1);
		
		while ((c = *inptr++) && c != '"') {
			if (c == '\\' && *inptr) {
				c = *inptr++;
			}
			*outptr++ = c;
		}
		*outptr = '\0';
	}
	
	*in = inptr;
	
	return out;
}

static char *
decode_token (const char **in)
{
	const char *inptr = *in;
	const char *start;
	
	decode_lwsp (&inptr);
	start = inptr;
	
	while (*inptr && *inptr != '=' && *inptr != ',')
		inptr++;
	
	if (inptr > start) {
		*in = inptr;
		return g_strndup (start, inptr - start);
	} else {
		return NULL;
	}
}

static char *
decode_value (const char **in)
{
	const char *inptr = *in;
	
	decode_lwsp (&inptr);
	if (*inptr == '"') {
		d(printf ("decoding quoted string token\n"));
		return decode_quoted_string (in);
	} else {
		d(printf ("decoding string token\n"));
		return decode_token (in);
	}
}

static GList *
parse_param_list (const char *tokens)
{
	GList *params = NULL;
	struct _param *param;
	const char *ptr;
	
	for (ptr = tokens; ptr && *ptr; ) {
		param = g_new0 (struct _param, 1);
		param->name = decode_token (&ptr);
		if (*ptr == '=') {
			ptr++;
			param->value = decode_value (&ptr);
		}
		
		params = g_list_prepend (params, param);
		
		if (*ptr == ',')
			ptr++;
	}
	
	return params;
}

static guint
decode_data_type (DataType *dtype, const char *name)
{
	int i;
	
	for (i = 0; dtype[i].name; i++) {
		if (!g_ascii_strcasecmp (dtype[i].name, name))
			break;
	}
	
	return dtype[i].type;
}

#define get_digest_arg(name) decode_data_type (digest_args, name)
#define decode_qop(name)     decode_data_type (qop_types, name)
#define decode_cipher(name)  decode_data_type (cipher_types, name)

static const char *
type_to_string (DataType *dtype, guint type)
{
	int i;
	
	for (i = 0; dtype[i].name; i++) {
		if (dtype[i].type == type)
			break;
	}
	
	return dtype[i].name;
}

#define qop_to_string(type)    type_to_string (qop_types, type)
#define cipher_to_string(type) type_to_string (cipher_types, type)

static void
digest_abort (gboolean *have_type, gboolean *abort)
{
	if (*have_type)
		*abort = TRUE;
	*have_type = TRUE;
}

static struct _DigestChallenge *
parse_server_challenge (const char *tokens, gboolean *abort)
{
	struct _DigestChallenge *challenge = NULL;
	GList *params, *p;
	const char *ptr;
#ifdef PARANOID
	gboolean got_algorithm = FALSE;
	gboolean got_stale = FALSE;
	gboolean got_maxbuf = FALSE;
	gboolean got_charset = FALSE;
#endif /* PARANOID */
	
	params = parse_param_list (tokens);
	if (!params) {
		*abort = TRUE;
		return NULL;
	}
	
	*abort = FALSE;
	
	challenge = g_new0 (struct _DigestChallenge, 1);
	challenge->realms = g_ptr_array_new ();
	challenge->maxbuf = 65536;
	
	for (p = params; p; p = p->next) {
		struct _param *param = p->data;
		int type;
		
		type = get_digest_arg (param->name);
		switch (type) {
		case DIGEST_REALM:
			for (ptr = param->value; ptr && *ptr; ) {
				char *token;
				
				token = decode_token (&ptr);
				if (token)
					g_ptr_array_add (challenge->realms, token);
				
				if (*ptr == ',')
					ptr++;
			}
			g_free (param->value);
			g_free (param->name);
			g_free (param);
			break;
		case DIGEST_NONCE:
			g_free (challenge->nonce);
			challenge->nonce = param->value;
			g_free (param->name);
			g_free (param);
			break;
		case DIGEST_QOP:
			for (ptr = param->value; ptr && *ptr; ) {
				char *token;
				
				token = decode_token (&ptr);
				if (token)
					challenge->qop |= decode_qop (token);
				
				if (*ptr == ',')
					ptr++;
			}
			
			if (challenge->qop & QOP_INVALID)
				challenge->qop = QOP_INVALID;
			g_free (param->value);
			g_free (param->name);
			g_free (param);
			break;
		case DIGEST_STALE:
			PARANOID (digest_abort (&got_stale, abort));
			if (!g_ascii_strcasecmp (param->value, "true"))
				challenge->stale = TRUE;
			else
				challenge->stale = FALSE;
			g_free (param->value);
			g_free (param->name);
			g_free (param);
			break;
		case DIGEST_MAXBUF:
			PARANOID (digest_abort (&got_maxbuf, abort));
			challenge->maxbuf = atoi (param->value);
			g_free (param->value);
			g_free (param->name);
			g_free (param);
			break;
		case DIGEST_CHARSET:
			PARANOID (digest_abort (&got_charset, abort));
			g_free (challenge->charset);
			if (param->value && *param->value)
				challenge->charset = param->value;
			else
				challenge->charset = NULL;
			g_free (param->name);
			g_free (param);
			break;
		case DIGEST_ALGORITHM:
			PARANOID (digest_abort (&got_algorithm, abort));
			g_free (challenge->algorithm);
			challenge->algorithm = param->value;
			g_free (param->name);
			g_free (param);
			break;
		case DIGEST_CIPHER:
			for (ptr = param->value; ptr && *ptr; ) {
				char *token;
				
				token = decode_token (&ptr);
				if (token)
					challenge->cipher |= decode_cipher (token);
				
				if (*ptr == ',')
					ptr++;
			}
			if (challenge->cipher & CIPHER_INVALID)
				challenge->cipher = CIPHER_INVALID;
			g_free (param->value);
			g_free (param->name);
			g_free (param);
			break;
		default:
			challenge->params = g_list_prepend (challenge->params, param);
			break;
		}
	}
	
	g_list_free (params);
	
	return challenge;
}

static void
digest_hex (unsigned char *digest, unsigned char hex[33])
{
	unsigned char *s, *p;
	
	/* lowercase hexify that bad-boy... */
	for (s = digest, p = hex; p < hex + 32; s++, p += 2)
		sprintf ((char *) p, "%.2x", *s);
}

static char *
digest_uri_to_string (struct _DigestURI *uri)
{
	if (uri->name)
		return g_strdup_printf ("%s/%s/%s", uri->type, uri->host, uri->name);
	else
		return g_strdup_printf ("%s/%s", uri->type, uri->host);
}

static void
compute_response (struct _DigestResponse *resp, const char *passwd, gboolean client, unsigned char out[33])
{
	unsigned char hex_a1[33], hex_a2[33];
	unsigned char digest[16];
	GChecksum *checksum;
	size_t len = 16;
	char *buf;
	
	checksum = g_checksum_new (G_CHECKSUM_MD5);
	
	/* compute A1 */
	g_checksum_update (checksum, (unsigned char *) resp->username, strlen (resp->username));
	g_checksum_update (checksum, (unsigned char *) ":", 1);
	g_checksum_update (checksum, (unsigned char *) resp->realm, strlen (resp->realm));
	g_checksum_update (checksum, (unsigned char *) ":", 1);
	g_checksum_update (checksum, (unsigned char *) passwd, strlen (passwd));
	g_checksum_get_digest (checksum, digest, &len);
	
	g_checksum_reset (checksum);
	len = 16;
	
	g_checksum_update (checksum, digest, 16);
	g_checksum_update (checksum, (unsigned char *) ":", 1);
	g_checksum_update (checksum, (unsigned char *) resp->nonce, strlen (resp->nonce));
	g_checksum_update (checksum, (unsigned char *) ":", 1);
	g_checksum_update (checksum, (unsigned char *) resp->cnonce, strlen (resp->cnonce));
	if (resp->authzid) {
		g_checksum_update (checksum, (unsigned char *) ":", 1);
		g_checksum_update (checksum, (unsigned char *) resp->authzid, strlen (resp->authzid));
	}
	
	/* hexify A1 */
	g_checksum_get_digest (checksum, digest, &len);
	digest_hex (digest, hex_a1);
	
	/* compute A2 */
	g_checksum_reset (checksum);
	len = 16;
	
	if (client) {
		/* we are calculating the client response */
		g_checksum_update (checksum, (unsigned char *) "AUTHENTICATE:", strlen ("AUTHENTICATE:"));
	} else {
		/* we are calculating the server rspauth */
		g_checksum_update (checksum, (unsigned char *) ":", 1);
	}
	
	buf = digest_uri_to_string (resp->uri);
	g_checksum_update (checksum, (unsigned char *) buf, strlen (buf));
	g_free (buf);
	
	if (resp->qop == QOP_AUTH_INT || resp->qop == QOP_AUTH_CONF)
		g_checksum_update (checksum, (unsigned char *) ":00000000000000000000000000000000", 33);
	
	/* now hexify A2 */
	g_checksum_get_digest (checksum, digest, &len);
	digest_hex (digest, hex_a2);
	
	/* compute KD */
	g_checksum_reset (checksum);
	len = 16;
	
	g_checksum_update (checksum, hex_a1, 32);
	g_checksum_update (checksum, (unsigned char *) ":", 1);
	g_checksum_update (checksum, (unsigned char *) resp->nonce, strlen (resp->nonce));
	g_checksum_update (checksum, (unsigned char *) ":", 1);
	g_checksum_update (checksum, (unsigned char *) resp->nc, 8);
	g_checksum_update (checksum, (unsigned char *) ":", 1);
	g_checksum_update (checksum, (unsigned char *) resp->cnonce, strlen (resp->cnonce));
	g_checksum_update (checksum, (unsigned char *) ":", 1);
	g_checksum_update (checksum, (unsigned char *) qop_to_string (resp->qop), strlen (qop_to_string (resp->qop)));
	g_checksum_update (checksum, (unsigned char *) ":", 1);
	g_checksum_update (checksum, hex_a2, 32);
	g_checksum_get_digest (checksum, digest, &len);
	
	g_checksum_free (checksum);
	
	digest_hex (digest, out);
}

static struct _DigestResponse *
generate_response (struct _DigestChallenge *challenge, struct addrinfo *ai,
		   const char *protocol, const char *user, const char *passwd)
{
	struct _DigestResponse *resp;
	struct _DigestURI *uri;
	unsigned char digest[16];
	GChecksum *checksum;
	size_t len = 16;
	char *bgen;
	
	resp = g_new0 (struct _DigestResponse, 1);
	resp->username = g_strdup (user);
	/* FIXME: we should use the preferred realm */
	if (challenge->realms && challenge->realms->len > 0)
		resp->realm = g_strdup (challenge->realms->pdata[0]);
	else
		resp->realm = g_strdup ("");
	
	resp->nonce = g_strdup (challenge->nonce);
	
	/* generate the cnonce */
	bgen = g_strdup_printf ("%p:%lu:%lu", resp,
				(unsigned long) getpid (),
				(unsigned long) time (0));
	
	checksum = g_checksum_new (G_CHECKSUM_MD5);
	g_checksum_update (checksum, bgen, strlen (bgen));
	g_checksum_get_digest (checksum, digest, &len);
	g_checksum_free (checksum);
	g_free (bgen);
	
	/* take our recommended 64 bits of entropy */
	resp->cnonce = spruce_base64_encode ((char *) digest, 8);
	
	/* we don't support re-auth so the nonce count is always 1 */
	strcpy (resp->nc, "00000001");
	
	/* choose the QOP */
	/* FIXME: choose - probably choose "auth" ??? */
	resp->qop = QOP_AUTH;
	
	/* create the URI */
	uri = g_new0 (struct _DigestURI, 1);
	uri->type = g_strdup (protocol);
	uri->host = g_strdup (ai->ai_canonname);
	uri->name = NULL;
	resp->uri = uri;
	
	/* charsets... yay */
	if (challenge->charset) {
		/* I believe that this is only ever allowed to be
		 * UTF-8. We strdup the charset specified by the
		 * challenge anyway, just in case it's not UTF-8.
		 */
		resp->charset = g_strdup (challenge->charset);
	}
	
	resp->cipher = CIPHER_INVALID;
	if (resp->qop == QOP_AUTH_CONF) {
		/* FIXME: choose a cipher? */
		resp->cipher = CIPHER_INVALID;
	}
	
	/* we don't really care about this... */
	resp->authzid = NULL;
	
	compute_response (resp, passwd, TRUE, (unsigned char *) resp->resp);
	
	return resp;
}

static GByteArray *
digest_response (struct _DigestResponse *resp)
{
	GByteArray *buffer;
	const char *str;
	char *buf;
	
	buffer = g_byte_array_new ();
	g_byte_array_append (buffer, (unsigned char *) "username=\"", 10);
	if (resp->charset) {
		/* Encode the username using the requested charset */
		size_t nread, nwritten;
		char *username;
		
		if (!(username = g_convert (resp->username, -1, resp->charset, "UTF-8", &nread, &nwritten, NULL))) {
			/* We can't convert to charset - pretend we never got a charset param? */
			g_free (resp->charset);
			resp->charset = NULL;
			
			/* Set the username to the non-UTF-8 version */
			username = g_strdup (resp->username);
			nwritten = strlen (username);
		}
		
		g_byte_array_append (buffer, (unsigned char *) username, nwritten);
		g_free (username);
	} else {
		g_byte_array_append (buffer, (unsigned char *) resp->username, strlen (resp->username));
	}
	
	g_byte_array_append (buffer, (unsigned char *) "\",realm=\"", 9);
	g_byte_array_append (buffer, (unsigned char *) resp->realm, strlen (resp->realm));
	
	g_byte_array_append (buffer, (unsigned char *) "\",nonce=\"", 9);
	g_byte_array_append (buffer, (unsigned char *) resp->nonce, strlen (resp->nonce));
	
	g_byte_array_append (buffer, (unsigned char *) "\",cnonce=\"", 10);
	g_byte_array_append (buffer, (unsigned char *) resp->cnonce, strlen (resp->cnonce));
	
	g_byte_array_append (buffer, (unsigned char *) "\",nc=", 5);
	g_byte_array_append (buffer, (unsigned char *) resp->nc, 8);
	
	g_byte_array_append (buffer, (unsigned char *) ",qop=\"", 6);
	str = qop_to_string (resp->qop);
	g_byte_array_append (buffer, (unsigned char *) str, strlen (str));
	
	g_byte_array_append (buffer, (unsigned char *) "\",digest-uri=\"", 14);
	buf = digest_uri_to_string (resp->uri);
	g_byte_array_append (buffer, (unsigned char *) buf, strlen (buf));
	g_free (buf);
	
	g_byte_array_append (buffer, (unsigned char *) "\",response=\"", 12);
	g_byte_array_append (buffer, (unsigned char *) resp->resp, 32);
	g_byte_array_append (buffer, (unsigned char *) "\"", 1);
	
	if (resp->maxbuf > 0) {
		g_byte_array_append (buffer, (unsigned char *) ",maxbuf=", 8);
		buf = g_strdup_printf ("%d", resp->maxbuf);
		g_byte_array_append (buffer, (unsigned char *) buf, strlen (buf));
		g_free (buf);
	}
	
	if (resp->charset) {
		g_byte_array_append (buffer, (unsigned char *) ",charset=\"", 10);
		g_byte_array_append (buffer, (unsigned char *) resp->charset, strlen (resp->charset));
		g_byte_array_append (buffer, (unsigned char *) "\"", 1);
	}
	
	if (resp->cipher != CIPHER_INVALID) {
		str = cipher_to_string (resp->cipher);
		if (str) {
			g_byte_array_append (buffer, (unsigned char *) ",cipher=\"", 9);
			g_byte_array_append (buffer, (unsigned char *) str, strlen (str));
			g_byte_array_append (buffer, (unsigned char *) "\"", 1);
		}
	}
	
	if (resp->authzid) {
		g_byte_array_append (buffer, (unsigned char *) ",authzid=\"", 10);
		g_byte_array_append (buffer, (unsigned char *) resp->authzid, strlen (resp->authzid));
		g_byte_array_append (buffer, (unsigned char *) "\"", 1);
	}
	
	return buffer;
}

static GByteArray *
digest_md5_challenge (SpruceSASL *sasl, GByteArray *token, GError **err)
{
	SpruceSASLDigestMd5 *sasl_digest = (SpruceSASLDigestMd5 *) sasl;
	struct _SpruceSASLDigestMd5Private *priv = sasl_digest->priv;
	struct addrinfo hints, *ai;
	struct _param *rspauth;
	GByteArray *ret = NULL;
	gboolean abort = FALSE;
	unsigned char out[33];
	const char *ptr;
	char *tokens;
	
	/* Need to wait for the server */
	if (!token)
		return NULL;
	
	g_return_val_if_fail (sasl->service->url->passwd != NULL, NULL);
	
	switch (priv->state) {
	case STATE_AUTH:
		if (token->len > 2048) {
			g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_SERVICE_CANT_AUTHENTICATE,
				     "%s", _("Server challenge too long (>2048 octets)"));
			g_warning ("Server challenge too long (>2048 octets)");
			return NULL;
		}
		
		tokens = g_strndup ((char *) token->data, token->len);
		priv->challenge = parse_server_challenge (tokens, &abort);
		g_free (tokens);
		if (!priv->challenge || abort) {
			g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_SERVICE_CANT_AUTHENTICATE,
				     "%s", _("Server challenge invalid"));
			g_warning ("Server challenge invalid");
			return NULL;
		}
		
		if (priv->challenge->qop == QOP_INVALID) {
			g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_SERVICE_CANT_AUTHENTICATE,
				     "%s", _("Server challenge contained invalid \"Quality of Protection\" token"));
			g_warning ("Server challenge contained invalid \"Quality of Protection\" token");
			return NULL;
		}
		
		memset (&hints, 0, sizeof (hints));
		hints.ai_flags = AI_CANONNAME;
		if (!(ai = spruce_getaddrinfo (sasl->service->url->host, NULL, NULL, &hints, err)))
			return NULL;
		
		priv->response = generate_response (priv->challenge, ai, sasl->service_name,
						    sasl->service->url->user,
						    sasl->service->url->passwd);
		
		spruce_freeaddrinfo (ai);
		
		ret = digest_response (priv->response);
		
		break;
	case STATE_FINAL:
		if (token->len)
			tokens = g_strndup ((char *) token->data, token->len);
		else
			tokens = NULL;
		
		if (!tokens || !*tokens) {
			g_free (tokens);
			
			g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_SERVICE_CANT_AUTHENTICATE,
				     "%s", _("Server response did not contain authorization data"));
			g_warning ("Server response did not contain authorization data");
			
			return NULL;
		}
		
		rspauth = g_new0 (struct _param, 1);
		
		ptr = tokens;
		rspauth->name = decode_token (&ptr);
		if (*ptr == '=') {
			ptr++;
			rspauth->value = decode_value (&ptr);
		}
		g_free (tokens);
		
		if (!rspauth->value) {
			g_free (rspauth->name);
			g_free (rspauth);
			
			g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_SERVICE_CANT_AUTHENTICATE,
				     "%s", _("Server response contained incomplete authorization data"));
			g_warning ("Server response contained incomplete authorization data");
			
			return NULL;
		}
		
		compute_response (priv->response, sasl->service->url->passwd, FALSE, out);
		if (memcmp (out, rspauth->value, 32) != 0) {
			g_free (rspauth->name);
			g_free (rspauth->value);
			g_free (rspauth);
			
			sasl->authenticated = TRUE;
			
			g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_SERVICE_CANT_AUTHENTICATE,
				     "%s", _("Server response does not match"));
			g_warning ("Server response does not match");
			
			return NULL;
		}
		
		g_free (rspauth->name);
		g_free (rspauth->value);
		g_free (rspauth);
		
		ret = g_byte_array_new ();
		
		sasl->authenticated = TRUE;
	default:
		break;
	}
	
	priv->state++;
	
	return ret;
}
