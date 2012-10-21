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
#include <sys/param.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>

#include <glib.h>
#include <glib/gi18n.h>

#include <gmime/gmime-part.h>
#include <gmime/gmime-message.h>
#include <gmime/gmime-multipart.h>
#include <gmime/gmime-message-part.h>
#include <gmime/gmime-multipart-signed.h>
#include <gmime/gmime-multipart-encrypted.h>
#include <gmime/gmime-stream-buffer.h>
#include <gmime/gmime-stream-filter.h>
#include <gmime/gmime-stream-null.h>
#include <gmime/gmime-filter-best.h>
#include <gmime/gmime-filter-crlf.h>
#include <gmime/gmime-stream-mem.h>
#include <gmime/gmime-encodings.h>
#include <gmime/gmime-parser.h>

#include <spruce/spruce-sasl.h>
#include <spruce/spruce-error.h>
#include <spruce/spruce-tcp-stream.h>
#include <spruce/spruce-tcp-stream-ssl.h>

#include "spruce-smtp-transport.h"


#define d(x) x


#define SPRUCE_SMTP_TRANSPORT_IS_ESMTP               (1 << 0)
#define SPRUCE_SMTP_TRANSPORT_8BITMIME               (1 << 1)
#define SPRUCE_SMTP_TRANSPORT_ENHANCEDSTATUSCODES    (1 << 2)
#define SPRUCE_SMTP_TRANSPORT_STARTTLS               (1 << 3)
#define SPRUCE_SMTP_TRANSPORT_AUTH_EQUAL             (1 << 4)  /* set if we are using authtypes from a broken AUTH= */


struct _SpruceSMTPTransportPrivate {
	GMimeStream *istream, *ostream;
	SpruceTcpAddress *localaddr;
	gboolean connected;
	
	guint32 flags;
	
	GHashTable *authtypes;
	gboolean has_authtypes;
};


static void spruce_smtp_transport_class_init (SpruceSMTPTransportClass *klass);
static void spruce_smtp_transport_init (SpruceSMTPTransport *transport, SpruceSMTPTransportClass *klass);
static void spruce_smtp_transport_finalize (GObject *object);

static int smtp_connect (SpruceService *service, GError **err);
static int smtp_disconnect (SpruceService *service, gboolean clean, GError **err);
static GList *smtp_query_auth_types (SpruceService *service, GError **err);

static int smtp_send (SpruceTransport *transport, GMimeMessage *message,
		      InternetAddressMailbox *from, InternetAddressList *recipients,
		      GError **err);


static gboolean smtp_helo (SpruceSMTPTransport *transport, GError **err);
static gboolean smtp_auth (SpruceSMTPTransport *transport, const char *auth, GError **err);
static gboolean smtp_mail (SpruceSMTPTransport *transport, const char *sender, gboolean has_8bit_parts, GError **err);
static gboolean smtp_rcpt (SpruceSMTPTransport *transport, const char *recipient, GError **err);
static gboolean smtp_data (SpruceSMTPTransport *transport, GMimeMessage *message, GError **err);
static gboolean smtp_rset (SpruceSMTPTransport *transport, GError **err);
static void smtp_quit (SpruceSMTPTransport *transport, GError **err);


static SpruceTransportClass *parent_class = NULL;


GType
spruce_smtp_transport_get_type (void)
{
	static GType type = 0;
	
	if (!type) {
		static const GTypeInfo info = {
			sizeof (SpruceSMTPTransportClass),
			NULL, /* base_class_init */
			NULL, /* base_class_finalize */
			(GClassInitFunc) spruce_smtp_transport_class_init,
			NULL, /* class_finalize */
			NULL, /* class_data */
			sizeof (SpruceSMTPTransport),
			0,    /* n_preallocs */
			(GInstanceInitFunc) spruce_smtp_transport_init,
		};
		
		type = g_type_register_static (SPRUCE_TYPE_TRANSPORT, "SpruceSMTPTransport", &info, 0);
	}
	
	return type;
}


static void
spruce_smtp_transport_class_init (SpruceSMTPTransportClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	SpruceServiceClass *service_class = SPRUCE_SERVICE_CLASS (klass);
	SpruceTransportClass *xport_class = SPRUCE_TRANSPORT_CLASS (klass);
	
	parent_class = g_type_class_ref (SPRUCE_TYPE_TRANSPORT);
	
	object_class->finalize = spruce_smtp_transport_finalize;
	
	service_class->connect = smtp_connect;
	service_class->disconnect = smtp_disconnect;
	service_class->query_auth_types = smtp_query_auth_types;
	
	xport_class->send = smtp_send;
}

static void
spruce_smtp_transport_init (SpruceSMTPTransport *smtp, SpruceSMTPTransportClass *klass)
{
	smtp->priv = g_new0 (struct _SpruceSMTPTransportPrivate, 1);
}

static void
spruce_smtp_transport_finalize (GObject *object)
{
	SpruceSMTPTransport *smtp = (SpruceSMTPTransport *) object;
	struct _SpruceSMTPTransportPrivate *priv = smtp->priv;
	
	if (priv->authtypes) {
		g_hash_table_foreach (priv->authtypes, (GHFunc) g_free, NULL);
		g_hash_table_destroy (priv->authtypes);
		priv->authtypes = NULL;
	}
	
	if (priv->istream) {
		g_object_unref (priv->istream);
		priv->istream = NULL;
	}
	
	if (priv->ostream) {
		g_object_unref (priv->ostream);
		priv->ostream = NULL;
	}
	
	if (priv->localaddr) {
		spruce_tcp_address_free (priv->localaddr);
		priv->localaddr = NULL;
	}
	
	g_free (priv);
	
	G_OBJECT_CLASS (parent_class)->finalize (object);
}


static const char *
smtp_strerror (int error)
{
	/* SMTP error codes grabbed from rfc821 */
	switch (error) {
	case 0:
		/* looks like a read problem, check errno */
		if (errno)
			return g_strerror (errno);
		else
			return _("Unknown");
	case 500:
		return _("Syntax error, command unrecognized");
	case 501:
		return _("Syntax error in parameters or arguments");
	case 502:
		return _("Command not implemented");
	case 504:
		return _("Command parameter not implemented");
	case 211:
		return _("System status, or system help reply");
	case 214:
		return _("Help message");
	case 220:
		return _("Service ready");
	case 221:
		return _("Service closing transmission channel");
	case 421:
		return _("Service not available, closing transmission channel");
	case 250:
		return _("Requested mail action okay, completed");
	case 251:
		return _("User not local; will forward to <forward-path>");
	case 450:
		return _("Requested mail action not taken: mailbox unavailable");
	case 550:
		return _("Requested action not taken: mailbox unavailable");
	case 451:
		return _("Requested action aborted: error in processing");
	case 551:
		return _("User not local; please try <forward-path>");
	case 452:
		return _("Requested action not taken: insufficient system storage");
	case 552:
		return _("Requested mail action aborted: exceeded storage allocation");
	case 553:
		return _("Requested action not taken: mailbox name not allowed");
	case 354:
		return _("Start mail input; end with <CRLF>.<CRLF>");
	case 554:
		return _("Transaction failed");
		
	/* AUTH error codes: */
	case 432:
		return _("A password transition is needed");
	case 534:
		return _("Authentication mechanism is too weak");
	case 538:
		return _("Encryption required for requested authentication mechanism");
	case 454:
		return _("Temporary authentication failure");
	case 530:
		return _("Authentication required");
		
	default:
		return _("Unknown");
	}
}

static const char *
smtp_next_token (const char *buf)
{
	const unsigned char *token;
	
	token = (const unsigned char *) buf;
	while (*token && !isspace ((int) *token))
		token++;
	
	while (*token && isspace ((int) *token))
		token++;
	
	return (const char *) token;
}

#define HEXVAL(c) (isdigit (c) ? (c) - '0' : (c) - 'A' + 10)

/**
 * example (rfc2034):
 * 5.1.1 Mailbox "nosuchuser" does not exist
 *
 * The human-readable status code is what we want. Since this text
 * could possibly be encoded, we must decode it.
 *
 * "xtext" is formally defined as follows:
 *
 *   xtext = *( xchar / hexchar / linear-white-space / comment )
 *
 *   xchar = any ASCII CHAR between "!" (33) and "~" (126) inclusive,
 *        except for "+", "\" and "(".
 *
 * "hexchar"s are intended to encode octets that cannot be represented
 * as plain text, either because they are reserved, or because they are
 * non-printable.  However, any octet value may be represented by a
 * "hexchar".
 *
 *   hexchar = ASCII "+" immediately followed by two upper case
 *        hexadecimal digits
 **/
static char *
smtp_decode_status_code (const char *in, size_t len)
{
	unsigned char *inptr, *outptr;
	const unsigned char *inend;
	char *outbuf;
	
	outbuf = g_malloc (len + 1);
	
	outptr = (unsigned char *) outbuf;
	inptr = (unsigned char *) in;
	inend = inptr + len;
	
	while (inptr < inend) {
		if (*inptr == '+') {
			if (isxdigit (inptr[1]) && isxdigit (inptr[2])) {
				*outptr++ = HEXVAL (inptr[1]) * 16 + HEXVAL (inptr[2]);
				inptr += 3;
			} else
				*outptr++ = *inptr++;
		} else
			*outptr++ = *inptr++;
	}
	
	*outptr = '\0';
	
	return outbuf;
}

static void
smtp_set_error (GError **err, SpruceSMTPTransport *transport, gboolean disconnect,
		const GByteArray *respbuf, const char *message)
{
	struct _SpruceSMTPTransportPrivate *priv = transport->priv;
	const char *token, *rbuf = respbuf->len ? (char *) respbuf->data : NULL;
	GByteArray *linebuf;
	GString *string;
	guint error;
	char *buf;
	
	if (!respbuf || !(priv->flags & SPRUCE_SMTP_TRANSPORT_ENHANCEDSTATUSCODES)) {
	fake_status_code:
		error = rbuf ? strtoul (rbuf, NULL, 10) : 0;
		g_set_error (err, SPRUCE_ERROR, error ? error + SPRUCE_ERROR_PROVIDER_SPECIFIC : errno,
			     "%s: %s", message, smtp_strerror (error));
	} else {
		string = g_string_new ("");
		linebuf = g_byte_array_new ();
		
		do {
			token = smtp_next_token (rbuf + 4);
			if (*token == '\0') {
				g_byte_array_free (linebuf, TRUE);
				g_string_free (string, TRUE);
				goto fake_status_code;
			}
			
			g_string_append (string, token);
			if (rbuf[3] == '-') {
				g_byte_array_set_size (linebuf, 0);
				g_mime_stream_buffer_readln (priv->istream, linebuf);
				g_string_append_c (string, '\n');
			} else {
				g_byte_array_free (linebuf, TRUE);
				break;
			}
			
			rbuf = (char *) linebuf->data;
		} while (rbuf);
		
		buf = smtp_decode_status_code (string->str, string->len);
		g_string_free (string, TRUE);
		if (!buf)
			goto fake_status_code;
		
		g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_GENERIC, "%s: %s", message, buf);
		
		g_free (buf);
	}
	
	if (!respbuf) {
		/* we got disconnected */
		if (disconnect)
			spruce_service_disconnect ((SpruceService *) transport, FALSE, NULL);
		else
			priv->connected = FALSE;
	}
}


enum {
	MODE_CLEAR,
	MODE_SSL,
	MODE_TLS
};

#define STARTTLS_FLAGS (SPRUCE_TCP_STREAM_SSL_ENABLE_TLS)
#define SSL_PORT_FLAGS (SPRUCE_TCP_STREAM_SSL_ENABLE_SSL3 | SPRUCE_TCP_STREAM_SSL_ENABLE_SSL_CONNECT)

static gboolean
connect_to_server (SpruceService *service, struct addrinfo *ai, int mode, GError **err)
{
	SpruceSMTPTransport *transport = (SpruceSMTPTransport *) service;
	struct _SpruceSMTPTransportPrivate *priv = transport->priv;
	GMimeStream *tcp_stream;
	GByteArray *respbuf;
	GError *lerr = NULL;
	int ret;
	
	/* set some smtp transport defaults */
	priv->flags = 0;
	
	if (priv->authtypes) {
		g_hash_table_foreach (priv->authtypes, (GHFunc) g_free, NULL);
		g_hash_table_destroy (priv->authtypes);
		priv->authtypes = NULL;
	}
	
	if (mode != MODE_CLEAR) {
#ifdef HAVE_SSL
		if (mode == MODE_TLS) {
			tcp_stream = spruce_tcp_stream_ssl_new (service->session, service->url->host, STARTTLS_FLAGS);
		} else {
			tcp_stream = spruce_tcp_stream_ssl_new (service->session, service->url->host, SSL_PORT_FLAGS);
		}
#else
		g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_SERVICE_UNAVAILABLE,
			     _("Could not connect to %s: %s"),
			     service->url->host,
			     _("SSL unavailable"));
		
		return FALSE;
#endif /* HAVE_SSL */
	} else {
		tcp_stream = spruce_tcp_stream_new ();
	}
	
	if ((ret = spruce_tcp_stream_connect ((SpruceTcpStream *) tcp_stream, ai)) == -1) {
		g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_SERVICE_UNAVAILABLE,
			     _("Could not connect to %s: %s"),
			     service->url->host,
			     g_strerror (errno));
		
		g_object_unref (tcp_stream);
		
		return FALSE;
	}
	
	priv->connected = TRUE;
	
	/* get the localaddr - needed later by smtp_helo */
	priv->localaddr = spruce_tcp_stream_getsockaddr ((SpruceTcpStream *) tcp_stream);
	
	priv->ostream = tcp_stream;
	priv->istream = g_mime_stream_buffer_new (tcp_stream, GMIME_STREAM_BUFFER_BLOCK_READ);
	
	respbuf = g_byte_array_new ();
	
	/* Read the greeting, note whether the server is ESMTP or not. */
	do {
		/* Check for "220" */
		g_byte_array_set_size (respbuf, 0);
		g_mime_stream_buffer_readln (priv->istream, respbuf);
		
		d(fprintf (stderr, "received: %s\n", respbuf->len ? (char *) respbuf->data : "(null)"));
		
		if (respbuf->len < 4 || strncmp ((char *) respbuf->data, "220", 3)) {
			smtp_set_error (err, transport, FALSE, respbuf,  _("Welcome response error"));
			g_byte_array_free (respbuf, TRUE);
			goto exception;
		}
	} while (respbuf->data[3] == '-'); /* if we got "220-" then loop again */
	
	g_byte_array_free (respbuf, TRUE);
	
	/* Try sending EHLO */
	priv->flags |= SPRUCE_SMTP_TRANSPORT_IS_ESMTP;
	if (!smtp_helo (transport, err)) {
		if (!priv->connected)
			goto exception;
		
		/* Fall back to HELO */
		g_clear_error (err);
		priv->flags &= ~SPRUCE_SMTP_TRANSPORT_IS_ESMTP;
		if (!smtp_helo (transport, err) && !priv->connected)
			goto exception;
	}
	
	/* clear any EHLO/HELO exception and assume that any SMTP errors encountered were non-fatal */
	g_clear_error (err);
	
	if (mode != MODE_TLS) {
		/* we're done */
		return TRUE;
	}
	
	if (!(priv->flags & SPRUCE_SMTP_TRANSPORT_STARTTLS)) {
		/* server doesn't support STARTTLS, abort */
		g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_SERVICE_UNAVAILABLE,
			     _("Failed to connect to SMTP server %s in secure mode: %s"),
			     service->url->host, _("server does not appear to support SSL"));
		goto exception;
	}
	
	d(fprintf (stderr, "sending : STARTTLS\r\n"));
	if (g_mime_stream_write (tcp_stream, "STARTTLS\r\n", 10) == -1) {
		g_set_error (err, SPRUCE_ERROR, errno,
			     _("STARTTLS command failed: %s"),
			     g_strerror (errno));
		goto exception;
	}
	
	respbuf = g_byte_array_new ();
	
	do {
		/* Check for "220 Ready for TLS" */
		g_byte_array_set_size (respbuf, 0);
		g_mime_stream_buffer_readln (priv->istream, respbuf);
		
		d(fprintf (stderr, "received: %s\n", respbuf->len ? (char *) respbuf->data : "(null)"));
		
		if (respbuf->len < 4 || strncmp ((char *) respbuf->data, "220", 3)) {
			smtp_set_error (err, transport, FALSE, respbuf, _("STARTTLS command failed"));
			g_byte_array_free (respbuf, TRUE);
			goto exception;
		}
	} while (respbuf->data[3] == '-'); /* if we got "220-" then loop again */
	
	g_byte_array_free (respbuf, TRUE);
	
	/* Okay, now toggle SSL/TLS mode */
	if (spruce_tcp_stream_ssl_enable_ssl ((SpruceTcpStreamSSL *) tcp_stream, &lerr) == -1) {
		g_set_error (err, SPRUCE_ERROR, errno,
			     _("Failed to connect to SMTP server %s in secure mode: %s"),
			     service->url->host, lerr->message);
		g_error_free (lerr);
		goto exception;
	}
	
	/* We are supposed to re-EHLO after a successful STARTTLS to
           re-fetch any supported extensions. */
	if (!smtp_helo (transport, err) && !priv->connected)
		goto exception;
	
	return TRUE;
	
 exception:
	
	g_object_unref (priv->istream);
	priv->istream = NULL;
	g_object_unref (priv->ostream);
	priv->ostream = NULL;
	
	return FALSE;
}

static gboolean
connect_to_server_wrapper (SpruceService *service, GError **err)
{
	const char *starttls, *port, *serv;
	struct addrinfo hints, *ai;
	char servbuf[16];
	int mode, ret;
	
	if (!SPRUCE_SERVICE_CLASS (parent_class)->connect (service, err))
		return FALSE;
	
	serv = service->url->protocol;
	if (strcmp (serv, "smtps") != 0) {
		mode = MODE_CLEAR;
		port = "25";
		
		if ((starttls = spruce_url_get_param (service->url, "starttls"))) {
			if (!strcmp (starttls, "yes") || !strcmp (starttls, "true"))
				mode = MODE_TLS;
		}
	} else {
		mode = MODE_SSL;
		port = "465";
	}
	
	if (service->url->port != 0) {
		sprintf (servbuf, "%d", service->url->port);
		serv = servbuf;
		port = NULL;
	}
	
	memset (&hints, 0, sizeof (hints));
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_family = PF_UNSPEC;
	
	if (!(ai = spruce_getaddrinfo (service->url->host, serv, port, &hints, err)))
		return FALSE;
	
	ret = connect_to_server (service, ai, mode, err);
	
	spruce_freeaddrinfo (ai);
	
	return ret;
}

static int
smtp_connect (SpruceService *service, GError **err)
{
	SpruceSMTPTransport *transport = (SpruceSMTPTransport *) service;
	struct _SpruceSMTPTransportPrivate *priv = transport->priv;
	
	if (SPRUCE_SERVICE_CLASS (parent_class)->connect (service, err) == -1)
		return -1;
	
	if (!connect_to_server_wrapper (service, err))
		return -1;
	
	if (service->url->auth && priv->has_authtypes) {
		SpruceSession *session = service->session;
		SpruceServiceAuthType *authtype;
		gboolean authenticated = FALSE;
		GError *lerr = NULL;
		char *errbuf = NULL;
		guint32 flags = 0;
		char *key;
		
		if (!g_hash_table_lookup (priv->authtypes, service->url->auth)) {
			g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_SERVICE_CANT_AUTHENTICATE,
				     _("Cannot authenticate with %s: server does not support the "
				       "requested authentication mechanism: %s"),
				     service->url->host, service->url->auth);
			spruce_service_disconnect (service, TRUE, NULL);
			return -1;
		}
		
		if (!(authtype = spruce_sasl_authtype (service->url->auth))) {
			g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_SERVICE_CANT_AUTHENTICATE,
				     _("No support for the %s authentication mechanism"),
				     service->url->auth);
			spruce_service_disconnect (service, TRUE, NULL);
			return -1;
		}
		
		if (!authtype->need_password) {
			/* authentication mechanism doesn't need a password,
			   so if it fails there's nothing we can do */
			if (!(authenticated = smtp_auth (transport, authtype->authproto, err))) {
				spruce_service_disconnect (service, TRUE, NULL);
				return -1;
			}
		}
		
		key = spruce_url_to_string (service->url, SPRUCE_URL_HIDE_ALL);
		
		while (authenticated) {
			if (errbuf) {
				/* We need to un-cache the password before prompting again */
				spruce_session_forget_passwd (session, key);
				g_free (service->url->passwd);
				service->url->passwd = NULL;
				flags |= SPRUCE_SESSION_PASSWORD_REPROMPT;
			}
			
			if (!service->url->passwd) {
				char *prompt;
				
				prompt = g_strdup_printf (_("%sPlease enter the SMTP password for %s on host %s"),
							  errbuf ? errbuf : "", service->url->user,
							  service->url->host);
				
				service->url->passwd = spruce_session_request_passwd (session, prompt, key, flags, err);
				
				g_free (prompt);
				g_free (errbuf);
				errbuf = NULL;
				
				if (!service->url->passwd) {
					spruce_service_disconnect (service, TRUE, NULL);
					return -1;
				}
			}
			
			if (!(authenticated = smtp_auth (transport, authtype->authproto, &lerr))) {
				errbuf = g_strdup_printf (_("Unable to authenticate to SMTP server: %s"),
							  lerr->message);
				g_error_free (lerr);
				lerr = NULL;
			}
		}
		
		g_free (key);
		
		/* The spec says we have to re-EHLO, but some servers
		 * we won't bother to name don't want you to... so ignore
		 * errors.
		 */
		if (!smtp_helo (transport, err) && !priv->connected)
			return -1;
		
		g_clear_error (err);
	}
	
	return 0;
}

static int
smtp_disconnect (SpruceService *service, gboolean clean, GError **err)
{
	SpruceSMTPTransport *transport = (SpruceSMTPTransport *) service;
	struct _SpruceSMTPTransportPrivate *priv = transport->priv;
	
	if (clean)
		smtp_quit (transport, err);
	
	if (!SPRUCE_SERVICE_CLASS (parent_class)->disconnect (service, clean, err))
		return -1;
	
	if (priv->authtypes) {
		g_hash_table_foreach (priv->authtypes, (GHFunc) g_free, NULL);
		g_hash_table_destroy (priv->authtypes);
		priv->authtypes = NULL;
	}
	
	if (priv->istream) {
		g_object_unref (priv->istream);
		priv->istream = NULL;
	}
	
	if (priv->ostream) {
		g_object_unref (priv->ostream);
		priv->ostream = NULL;
	}
	
	spruce_tcp_address_free (priv->localaddr);
	priv->localaddr = NULL;
	
	priv->connected = FALSE;
	
	return 0;
}

static GHashTable *
esmtp_get_authtypes (const char *buffer)
{
	const unsigned char *start, *end;
	GHashTable *table = NULL;
	
	/* advance to the first token */
	start = (unsigned char *) buffer;
	while (isspace ((int) *start) || *start == '=')
		start++;
	
	if (!*start)
		return NULL;
	
	table = g_hash_table_new (g_str_hash, g_str_equal);
	
	for ( ; *start; ) {
		char *type;
		
		/* advance to the end of the token */
		end = start;
		while (*end && !isspace ((int) *end))
			end++;
		
		type = g_strndup ((char *) start, end - start);
		g_hash_table_insert (table, type, type);
		
		/* advance to the next token */
		start = end;
		while (isspace ((int) *start))
			start++;
	}
	
	return table;
}

static GList *
smtp_query_auth_types (SpruceService *service, GError **err)
{
	SpruceSMTPTransport *transport = (SpruceSMTPTransport *) service;
	struct _SpruceSMTPTransportPrivate *priv = transport->priv;
	SpruceServiceAuthType *authtype;
	GList *types, *t, *next;
	
	if (!connect_to_server_wrapper (service, err))
		return NULL;
	
	types = g_list_copy (service->provider->authtypes);
	for (t = types; t; t = next) {
		authtype = t->data;
		next = t->next;
		
		if (!g_hash_table_lookup (priv->authtypes, authtype->authproto)) {
			types = g_list_remove_link (types, t);
			g_list_free_1 (t);
		}
	}
	
	smtp_disconnect (service, TRUE, NULL);
	
	return types;
}

static int
rcpt_to_all (SpruceSMTPTransport *smtp, InternetAddressList *recipients, GError **err)
{
	InternetAddressMailbox *mailbox;
	InternetAddress *ia;
	int count, i;
	
	count = internet_address_list_length (recipients);
	for (i = 0; i < count; i++) {
		ia = internet_address_list_get_address (recipients, i);
		
		if (INTERNET_ADDRESS_IS_MAILBOX (ia)) {
			mailbox = (InternetAddressMailbox *) ia;
			
			if (!mailbox->addr) {
				g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_TRANSPORT_INVALID_RECIPIENT,
					     _("Cannot send message: invalid recipient: %s <>"),
					     ia->name ? ia->name : "");
				return -1;
			}
			
			if (!smtp_rcpt (smtp, mailbox->addr, err))
				return -1;
		} else {
			if (rcpt_to_all (smtp, INTERNET_ADDRESS_GROUP (ia)->members, err) == -1)
				return -1;
		}
	}
	
	return 0;
}

static int
smtp_send (SpruceTransport *transport, GMimeMessage *message,
	   InternetAddressMailbox *from, InternetAddressList *recipients,
	   GError **err)
{
	SpruceSMTPTransport *smtp = (SpruceSMTPTransport *) transport;
	gboolean has_8bit_parts = TRUE;
	
	/*has_8bit_parts = g_mime_message_has_8bit_parts (message);*/
	
	/* rfc1652 (8BITMIME) requires that you notify the ESMTP daemon that
	   you'll be sending an 8bit mime message at "MAIL FROM:" time. */
	if (!smtp_mail (smtp, from->addr, has_8bit_parts, err)) {
		return FALSE;
	}
	
	if ((rcpt_to_all (smtp, recipients, err)) == -1)
		return -1;
	
	if (!smtp_data (smtp, message, err))
		return -1;
	
	/* reset the service for our next transfer session */
	if (!smtp_rset (smtp, NULL))
		spruce_service_disconnect ((SpruceService *) transport, FALSE, NULL);
	
	return 0;
}


static gboolean
smtp_helo (SpruceSMTPTransport *transport, GError **err)
{
	/* say hello to the server */
	struct _SpruceSMTPTransportPrivate *priv = transport->priv;
	const char *token, *numeric = NULL;
	char *name = NULL, *cmdbuf = NULL;
	GByteArray *respbuf;
	
	/* clear our EHLO extension flags */
	priv->flags &= SPRUCE_SMTP_TRANSPORT_IS_ESMTP;
	
	if (priv->authtypes) {
		g_hash_table_foreach (priv->authtypes, (GHFunc) g_free, NULL);
		g_hash_table_destroy (priv->authtypes);
		priv->authtypes = NULL;
	}
	
	/* force name resolution first, fallback to numerical, we need to know when it falls back */
	if (spruce_getnameinfo ((const struct sockaddr *) priv->localaddr->address, priv->localaddr->length, &name, NULL, NI_NAMEREQD, NULL) != 0) {
		if (spruce_getnameinfo ((const struct sockaddr *) priv->localaddr->address, priv->localaddr->length, &name, NULL, NI_NUMERICHOST, NULL) != 0) {
			name = g_strdup ("localhost.localdomain");
		} else {
			if (priv->localaddr->family == SPRUCE_TCP_ADDRESS_IPv6)
				numeric = "IPv6:";
			else
				numeric = "";
		}
	}
	
	/* hiya server! how are you today? */
	token = (priv->flags & SPRUCE_SMTP_TRANSPORT_IS_ESMTP) ? "EHLO" : "HELO";
	if (numeric)
		cmdbuf = g_strdup_printf ("%s [%s%s]\r\n", token, numeric, name);
	else
		cmdbuf = g_strdup_printf ("%s [%s]\r\n", token, name);
	g_free (name);
	
	d(fprintf (stderr, "sending : %s", cmdbuf));
	if (g_mime_stream_write (priv->ostream, cmdbuf, strlen (cmdbuf)) == -1) {
		g_free (cmdbuf);
		g_set_error (err, SPRUCE_ERROR, errno, 
			     _("HELO command failed: %s"),
			     g_strerror (errno));
		
		spruce_service_disconnect ((SpruceService *) transport, FALSE, NULL);
		
		return FALSE;
	}
	g_free (cmdbuf);
	
	respbuf = g_byte_array_new ();
	
	do {
		/* Check for "250" */
		g_byte_array_set_size (respbuf, 0);
		g_mime_stream_buffer_readln (priv->istream, respbuf);
		
		d(fprintf (stderr, "received: %s\n", respbuf->len ? (char *) respbuf->data : "(null)"));
		
		if (respbuf->len < 4 || strncmp ((char *) respbuf->data, "250", 3)) {
			smtp_set_error (err, transport, FALSE, respbuf, _("HELO command failed"));
			g_byte_array_free (respbuf, TRUE);
			
			return FALSE;
		}
		
		token = (char *) respbuf->data + 4;
		
		if (priv->flags & SPRUCE_SMTP_TRANSPORT_IS_ESMTP) {
			if (!strncmp (token, "8BITMIME", 8)) {
				priv->flags |= SPRUCE_SMTP_TRANSPORT_8BITMIME;
			} else if (!strncmp (token, "ENHANCEDSTATUSCODES", 19)) {
				priv->flags |= SPRUCE_SMTP_TRANSPORT_ENHANCEDSTATUSCODES;
			} else if (!strncmp (token, "STARTTLS", 8)) {
				priv->flags |= SPRUCE_SMTP_TRANSPORT_STARTTLS;
			} else if (!strncmp (token, "AUTH", 4)) {
				if (!priv->authtypes || priv->flags & SPRUCE_SMTP_TRANSPORT_AUTH_EQUAL) {
					/* Don't bother parsing any authtypes if we already have a list.
					 * Some servers will list AUTH twice, once the standard way and
					 * once the way Microsoft Outlook requires them to be:
					 *
					 * 250-AUTH LOGIN PLAIN DIGEST-MD5 CRAM-MD5
					 * 250-AUTH=LOGIN PLAIN DIGEST-MD5 CRAM-MD5
					 *
					 * Since they can come in any order, parse each list that we get
					 * until we parse an authtype list that does not use the AUTH=
					 * format. We want to let the standard way have priority over the
					 * broken way.
					 **/
					
					if (token[4] == '=')
						priv->flags |= SPRUCE_SMTP_TRANSPORT_AUTH_EQUAL;
					else
						priv->flags &= ~SPRUCE_SMTP_TRANSPORT_AUTH_EQUAL;
					
					/* parse for supported AUTH types */
					token += 5;
					
					if (priv->authtypes) {
						g_hash_table_foreach (priv->authtypes, (GHFunc) g_free, NULL);
						g_hash_table_destroy (priv->authtypes);
					}
					
					priv->authtypes = esmtp_get_authtypes (token);
				}
			}
		}
	} while (respbuf->data[3] == '-'); /* if we got "250-" then loop again */
	
	g_byte_array_free (respbuf, TRUE);
	
	return TRUE;
}

static gboolean
smtp_auth (SpruceSMTPTransport *transport, const char *mech, GError **err)
{
	struct _SpruceSMTPTransportPrivate *priv = transport->priv;
	gboolean auth_challenge = FALSE;
	char *cmdbuf, *challenge;
	SpruceSASL *sasl = NULL;
	GByteArray *respbuf;
	
	if (!(sasl = spruce_sasl_new ("smtp", mech, (SpruceService *) transport))) {
		g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_GENERIC,
			     _("Error creating SASL authentication object."));
		return FALSE;
	}
	
	if ((challenge = spruce_sasl_challenge_base64 (sasl, NULL, err))) {
		auth_challenge = TRUE;
		cmdbuf = g_strdup_printf ("AUTH %s %s\r\n", mech, challenge);
		g_free (challenge);
	} else {
		cmdbuf = g_strdup_printf ("AUTH %s\r\n", mech);
	}
	
	d(fprintf (stderr, "sending : %s", cmdbuf));
	if (g_mime_stream_write (priv->ostream, cmdbuf, strlen (cmdbuf)) == -1) {
		g_free (cmdbuf);
		g_set_error (err, SPRUCE_ERROR, errno,
			     _("AUTH request timed out: %s"),
			     g_strerror (errno));
		goto lose;
	}
	g_free (cmdbuf);
	
	respbuf = g_byte_array_new ();
	
	g_mime_stream_buffer_readln (priv->istream, respbuf);
	d(fprintf (stderr, "received: %s\n", respbuf->len ? (char *) respbuf->data : "(null)"));
	
	while (!spruce_sasl_authenticated (sasl)) {
		if (respbuf->len < 4) {
			g_byte_array_free (respbuf, TRUE);
			g_set_error (err, SPRUCE_ERROR, errno,
				     _("AUTH request timed out: %s"),
				     g_strerror (errno));
			goto lose;
		}
		
		/* the server challenge/response should follow a 334 code */
		if (strncmp ((char *) respbuf->data, "334", 3)) {
			g_byte_array_free (respbuf, TRUE);
			g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_SERVICE_CANT_AUTHENTICATE,
				     _("AUTH request failed."));
			goto lose;
		}
		
		if (FALSE) {
		broken_smtp_server:
			d(fprintf (stderr, "Your SMTP server's implementation of the %s SASL\n"
				   "authentication mechanism is broken. Please report this to the\n"
				   "appropriate vendor and suggest that they re-read rfc2554 again\n"
				   "for the first time (specifically Section 4).\n",
				   mech));
		}
		
		/* eat whtspc */
		challenge = (char *) respbuf->data + 4;
		while (isspace ((int) *((unsigned char *) challenge)))
		       challenge++;
		
		if (!(challenge = spruce_sasl_challenge_base64 (sasl, challenge, err)))
			goto break_and_lose;
		
		/* send our challenge */
		cmdbuf = g_strdup_printf ("%s\r\n", challenge);
		g_free (challenge);
		d(fprintf (stderr, "sending : %s", cmdbuf));
		if (g_mime_stream_write (priv->ostream, cmdbuf, strlen (cmdbuf)) == -1) {
			g_free (cmdbuf);
			goto lose;
		}
		g_free (cmdbuf);
		
		/* get the server's response */
		g_byte_array_set_size (respbuf, 0);
		g_mime_stream_buffer_readln (priv->istream, respbuf);
		d(fprintf (stderr, "received: %s\n", respbuf->len ? (char *) respbuf->data : "(null)"));
	}
	
	/* check that the server says we are authenticated */
	if (respbuf->len < 4 || strncmp ((char *) respbuf->data, "235", 3)) {
		if (respbuf->len >= 4 && auth_challenge && !strncmp ((char *) respbuf->data, "334", 3)) {
			/* broken server, but lets try and work around it anyway... */
			goto broken_smtp_server;
		}
		g_byte_array_free (respbuf, TRUE);
		goto lose;
	}
	
	g_byte_array_free (respbuf, TRUE);
	g_object_unref (sasl);
	
	return TRUE;
	
 break_and_lose:
	/* Get the server out of "waiting for continuation data" mode. */
	d(fprintf (stderr, "sending : *\r\n"));
	g_mime_stream_write (priv->ostream, "*\r\n", 3);
	
	g_byte_array_set_size (respbuf, 0);
	g_mime_stream_buffer_readln (priv->istream, respbuf);
	d(fprintf (stderr, "received: %s\n", respbuf->len ? (char *) respbuf->data : "(null)"));
	g_byte_array_free (respbuf, TRUE);
	
 lose:
	if (err && *err == NULL) {
		g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_SERVICE_CANT_AUTHENTICATE,
			     _("Bad authentication response from server.\n"));
	}
	
	g_object_unref (sasl);
	
	return FALSE;
}

static gboolean
smtp_mail (SpruceSMTPTransport *transport, const char *sender, gboolean has_8bit_parts, GError **err)
{
	struct _SpruceSMTPTransportPrivate *priv = transport->priv;
	GByteArray *respbuf;
	char *cmdbuf;
	
	if (priv->flags & SPRUCE_SMTP_TRANSPORT_8BITMIME && has_8bit_parts)
		cmdbuf = g_strdup_printf ("MAIL FROM:<%s> BODY=8BITMIME\r\n", sender);
	else
		cmdbuf = g_strdup_printf ("MAIL FROM:<%s>\r\n", sender);
	
	d(fprintf (stderr, "sending : %s", cmdbuf));
	
	if (g_mime_stream_write (priv->ostream, cmdbuf, strlen (cmdbuf)) == -1) {
		g_free (cmdbuf);
		g_set_error (err, SPRUCE_ERROR, errno,
			     _("MAIL FROM command failed: %s: mail not sent"),
			     g_strerror (errno));
		
		spruce_service_disconnect ((SpruceService *) transport, FALSE, NULL);
		
		return FALSE;
	}
	g_free (cmdbuf);
	
	respbuf = g_byte_array_new ();
	
	do {
		/* Check for "250 Sender OK..." */
		g_byte_array_set_size (respbuf, 0);
		g_mime_stream_buffer_readln (priv->istream, respbuf);
		
		d(fprintf (stderr, "received: %s\n", respbuf->len ? (char *) respbuf->data : "(null)"));
		
		if (respbuf->len < 4 || strncmp ((char *) respbuf->data, "250", 3)) {
			smtp_set_error (err, transport, TRUE, respbuf, _("MAIL FROM command failed"));
			g_byte_array_free (respbuf, TRUE);
			return FALSE;
		}
	} while (respbuf->data[3] == '-'); /* if we got "250-" then loop again */
	
	g_byte_array_free (respbuf, TRUE);
	
	return TRUE;
}

static gboolean
smtp_rcpt (SpruceSMTPTransport *transport, const char *recipient, GError **err)
{
	struct _SpruceSMTPTransportPrivate *priv = transport->priv;
	GByteArray *respbuf;
	char *cmdbuf;
	
	cmdbuf = g_strdup_printf ("RCPT TO:<%s>\r\n", recipient);
	
	d(fprintf (stderr, "sending : %s", cmdbuf));
	
	if (g_mime_stream_write (priv->ostream, cmdbuf, strlen (cmdbuf)) == -1) {
		g_free (cmdbuf);
		g_set_error (err, SPRUCE_ERROR, errno,
			     _("RCPT TO command failed: %s: mail not sent"),
			     g_strerror (errno));
		
		spruce_service_disconnect ((SpruceService *) transport, FALSE, NULL);
		
		return FALSE;
	}
	g_free (cmdbuf);
	
	respbuf = g_byte_array_new ();
	
	do {
		/* Check for "250 Recipient OK..." */
		g_byte_array_set_size (respbuf, 0);
		g_mime_stream_buffer_readln (priv->istream, respbuf);
		
		d(fprintf (stderr, "received: %s\n", respbuf->len ? (char *) respbuf->data : "(null)"));
		
		if (respbuf->len < 4 || strncmp ((char *) respbuf->data, "250", 3)) {
			char *message;
			
			message = g_strdup_printf (_("RCPT TO <%s> failed"), recipient);
			smtp_set_error (err, transport, TRUE, respbuf, message);
			g_byte_array_free (respbuf, TRUE);
			g_free (message);
			
			return FALSE;
		}
	} while (respbuf->data[3] == '-'); /* if we got "250-" then loop again */
	
	g_byte_array_free (respbuf, TRUE);
	
	return TRUE;
}

/* Strip Bcc/Resent-Bcc headers as well as constrict encodings according to server capabilities */
static GMimeMessage *
prepare_message (GMimeMessage *message, GMimeEncodingConstraint constraint)
{
	GMimeParser *parser;
	GMimeStream *stream;
	GMimeMessage *msg;
	
	stream = g_mime_stream_mem_new ();
	g_mime_object_write_to_stream ((GMimeObject *) message, stream);
	g_mime_stream_reset (stream);
	
	parser = g_mime_parser_new_with_stream (stream);
	msg = g_mime_parser_construct_message (parser);
	g_object_unref (stream);
	g_object_unref (parser);
	
	/* Remove some headers we don't want... */
	g_mime_header_list_remove (((GMimeObject *) msg)->headers, "Bcc");
	g_mime_header_list_remove (((GMimeObject *) msg)->headers, "Resent-Bcc");
	g_mime_header_list_remove (((GMimeObject *) msg)->headers, "Content-Length");
	
	g_mime_object_encode ((GMimeObject *) msg, constraint);
	
	return msg;
}

static gboolean
smtp_data (SpruceSMTPTransport *transport, GMimeMessage *message, GError **err)
{
	struct _SpruceSMTPTransportPrivate *priv = transport->priv;
	GMimeEncodingConstraint constraint;
	GMimeStreamFilter *filtered_stream;
	GMimeFilter *crlffilter;
	GByteArray *respbuf;
	int ret;
	
	/* If the server supports 8BITMIME, use 8bit as our encoding
	 * constraint, otherwise enforce 7bit */
	if (priv->flags & SPRUCE_SMTP_TRANSPORT_8BITMIME)
		constraint = GMIME_ENCODING_CONSTRAINT_8BIT;
	else
		constraint = GMIME_ENCODING_CONSTRAINT_7BIT;
	
	d(fprintf (stderr, "sending : DATA\r\n"));
	
	if (g_mime_stream_write (priv->ostream, "DATA\r\n", 6) == -1) {
		g_set_error (err, SPRUCE_ERROR, errno,
			     _("DATA command failed: %s: mail not sent"),
			     g_strerror (errno));
		
		spruce_service_disconnect ((SpruceService *) transport, FALSE, NULL);
		
		return FALSE;
	}
	
	respbuf = g_byte_array_new ();
	g_mime_stream_buffer_readln (priv->istream, respbuf);
	
	d(fprintf (stderr, "received: %s\n", respbuf->len ? (char *) respbuf->data : "(null)"));
	
	if (respbuf->len < 4 || strncmp ((char *) respbuf->data, "354", 3)) {
		/* we should have gotten instructions on how to use the DATA command:
		 * 354 Enter mail, end with "." on a line by itself
		 */
		smtp_set_error (err, transport, TRUE, respbuf, _("DATA command failed"));
		g_byte_array_free (respbuf, TRUE);
		
		return FALSE;
	}
	
	g_byte_array_free (respbuf, TRUE);
	
	/* Changes the encoding of all mime parts to fit within our required
	   encoding type and also force any text parts with long lines (longer
	   than 998 octets) to wrap by QP or base64 encoding them. */
	message = prepare_message (message, constraint);
	
	/* setup stream filtering */
	crlffilter = g_mime_filter_crlf_new (TRUE, TRUE);
	filtered_stream = (GMimeStreamFilter *) g_mime_stream_filter_new (priv->ostream);
	g_mime_stream_filter_add (filtered_stream, crlffilter);
	g_object_unref (crlffilter);
	
	/* write the message */
	if ((ret = g_mime_object_write_to_stream ((GMimeObject *) message, (GMimeStream *) filtered_stream)) != -1)
		ret = g_mime_stream_flush ((GMimeStream *) filtered_stream);
	
	g_object_unref (filtered_stream);
	
	/* unref our modified message */
	g_object_unref (message);
	
	if (ret == -1) {
		g_set_error (err, SPRUCE_ERROR, errno,
			     _("DATA command failed: %s: mail not sent"),
			     g_strerror (errno));
		
		spruce_service_disconnect ((SpruceService *) transport, FALSE, NULL);
		
		return FALSE;
	}
	
	/* terminate the message body */
	
	d(fprintf (stderr, "sending : \\r\\n.\\r\\n\n"));
	
	if (g_mime_stream_write (priv->ostream, "\r\n.\r\n", 5) == -1) {
		g_set_error (err, SPRUCE_ERROR, errno,
			     _("DATA command failed: %s: mail not sent"),
			     g_strerror (errno));
		
		spruce_service_disconnect ((SpruceService *) transport, FALSE, NULL);
		
		return FALSE;
	}
	
	respbuf = g_byte_array_new ();
	
	do {
		/* Check for "250 Sender OK..." */
		g_byte_array_set_size (respbuf, 0);
		g_mime_stream_buffer_readln (priv->istream, respbuf);
		
		d(fprintf (stderr, "received: %s\n", respbuf->len ? (char *) respbuf->data : "(null)"));
		
		if (respbuf->len < 4 || strncmp ((char *) respbuf->data, "250", 3)) {
			smtp_set_error (err, transport, TRUE, respbuf, _("DATA command failed"));
			g_byte_array_free (respbuf, TRUE);
			return FALSE;
		}
	} while (respbuf->data[3] == '-'); /* if we got "250-" then loop again */
	
	g_byte_array_free (respbuf, TRUE);
	
	return TRUE;
}

static gboolean
smtp_rset (SpruceSMTPTransport *transport, GError **err)
{
	struct _SpruceSMTPTransportPrivate *priv = transport->priv;
	GByteArray *respbuf;
	
	d(fprintf (stderr, "sending : RSET\r\n"));
	
	if (g_mime_stream_write (priv->ostream, "RSET\r\n", 6) == -1) {
		g_set_error (err, SPRUCE_ERROR, errno,
			     _("RSET command failed: %s"),
			     g_strerror (errno));
		
		spruce_service_disconnect ((SpruceService *) transport, FALSE, NULL);
		
		return FALSE;
	}
	
	respbuf = g_byte_array_new ();
	
	do {
		/* Check for "250" */
		g_byte_array_set_size (respbuf, 0);
		g_mime_stream_buffer_readln (priv->istream, respbuf);
		
		d(fprintf (stderr, "received: %s\n", respbuf->len ? (char *) respbuf->data : "(null)"));
		
		if (respbuf->len < 4 || strncmp ((char *) respbuf->data, "250", 3)) {
			smtp_set_error (err, transport, TRUE, respbuf, _("RSET command failed"));
			g_byte_array_free (respbuf, TRUE);
			return FALSE;
		}
	} while (respbuf->data[3] == '-'); /* if we got "250-" then loop again */
	
	g_byte_array_free (respbuf, TRUE);
	
	return TRUE;
}

static void
smtp_quit (SpruceSMTPTransport *transport, GError **err)
{
	struct _SpruceSMTPTransportPrivate *priv = transport->priv;
	GByteArray *respbuf;
	
	d(fprintf (stderr, "sending : QUIT\r\n"));
	
	if (g_mime_stream_write (priv->ostream, "QUIT\r\n", 6) == -1) {
		g_set_error (err, SPRUCE_ERROR, errno,
			     _("QUIT command failed: %s"),
			     g_strerror (errno));
		
		return;
	}
	
	respbuf = g_byte_array_new ();
	
	do {
		/* Check for "221" */
		g_byte_array_set_size (respbuf, 0);
		g_mime_stream_buffer_readln (priv->istream, respbuf);
		
		d(fprintf (stderr, "received: %s\n", respbuf->len ? (char *) respbuf->data : "(null)"));
		
		if (respbuf->len < 4 || strncmp ((char *) respbuf->data, "221", 3)) {
			smtp_set_error (err, transport, FALSE, respbuf, _("QUIT command failed"));
			g_byte_array_free (respbuf, TRUE);
			return;
		}
	} while (respbuf->data[3] == '-'); /* if we got "221-" then loop again */
	
	g_byte_array_free (respbuf, TRUE);
}
