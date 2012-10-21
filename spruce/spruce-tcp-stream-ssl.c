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

#ifdef HAVE_OPENSSL

#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include <glib/gi18n.h>

#include <openssl/ssl.h>

#include "spruce-error.h"
#include "spruce-file-utils.h"
#include "spruce-tcp-stream-ssl.h"

struct _SpruceTcpStreamSSLPrivate {
	SpruceSession *session;
	char *expected_host;
	SSL *ssl;
	
	guint32 flags;
};

static void spruce_tcp_stream_ssl_class_init (SpruceTcpStreamSSLClass *klass);
static void spruce_tcp_stream_ssl_init (SpruceTcpStreamSSL *stream, SpruceTcpStreamSSLClass *klass);
static void spruce_tcp_stream_ssl_finalize (GObject *object);

static ssize_t stream_read (GMimeStream *stream, char *buf, size_t len);
static ssize_t stream_write (GMimeStream *stream, const char *buf, size_t len);
static int stream_close (GMimeStream *stream);

static int tcp_connect (SpruceTcpStream *stream, struct addrinfo *ai);


static SpruceTcpStreamClass *parent_class = NULL;


GType
spruce_tcp_stream_ssl_get_type (void)
{
	static GType type = 0;
	
	if (!type) {
		static const GTypeInfo info = {
			sizeof (SpruceTcpStreamSSLClass),
			NULL, /* base_class_init */
			NULL, /* base_class_finalize */
			(GClassInitFunc) spruce_tcp_stream_ssl_class_init,
			NULL, /* class_finalize */
			NULL, /* class_data */
			sizeof (SpruceTcpStreamSSL),
			0,    /* n_preallocs */
			(GInstanceInitFunc) spruce_tcp_stream_ssl_init,
		};
		
		type = g_type_register_static (SPRUCE_TYPE_TCP_STREAM, "SpruceTcpStreamSSL", &info, 0);
	}
	
	return type;
}


static void
spruce_tcp_stream_ssl_class_init (SpruceTcpStreamSSLClass *klass)
{
	SpruceTcpStreamClass *tcp_class = SPRUCE_TCP_STREAM_CLASS (klass);
	GMimeStreamClass *stream_class = GMIME_STREAM_CLASS (klass);
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	
	parent_class = g_type_class_ref (SPRUCE_TYPE_TCP_STREAM);
	
	object_class->finalize = spruce_tcp_stream_ssl_finalize;
	
	stream_class->read = stream_read;
	stream_class->write = stream_write;
	stream_class->close = stream_close;
	
	tcp_class->connect = tcp_connect;
	
	SSL_load_error_strings ();
	SSLeay_add_ssl_algorithms ();
}

static void
spruce_tcp_stream_ssl_init (SpruceTcpStreamSSL *stream, SpruceTcpStreamSSLClass *klass)
{
	stream->priv = g_new (struct _SpruceTcpStreamSSLPrivate, 1);
	stream->priv->expected_host = NULL;
	stream->priv->session = NULL;
	stream->priv->ssl = NULL;
	stream->priv->flags = 0;
}

static void
spruce_tcp_stream_ssl_finalize (GObject *object)
{
	SpruceTcpStreamSSL *stream = (SpruceTcpStreamSSL *) object;
	
	if (stream->priv->ssl) {
		SSL_shutdown (stream->priv->ssl);
		SSL_CTX_free (stream->priv->ssl->ctx);
		SSL_free (stream->priv->ssl);
	}
	
	G_OBJECT_CLASS (parent_class)->finalize (object);
}


static ssize_t
stream_read (GMimeStream *stream, char *buf, size_t len)
{
	struct _SpruceTcpStreamSSLPrivate *priv = ((SpruceTcpStreamSSL *) stream)->priv;
	SpruceTcpStream *tcp_stream = (SpruceTcpStream *) stream;
	ssize_t nread;
	
	if (priv->ssl) {
		do {
			if ((nread = SSL_read (priv->ssl, buf, len)) >= 0)
				break;
			
			switch (SSL_get_error (priv->ssl, nread)) {
			case SSL_ERROR_ZERO_RETURN:
				nread = 0;
				break;
			case SSL_ERROR_WANT_READ:
				errno = EAGAIN;
				break;
			case SSL_ERROR_SYSCALL:
				/* errno will be set appropriately */
				break;
			default:
				break;
			}
		} while (nread < 0 && (errno == EINTR || errno == EAGAIN));
		
		if (nread < 0)
			nread = -1;
	} else {
		nread = spruce_read (tcp_stream->sockfd, buf, len);
	}
	
	if (nread > 0)
		stream->position += nread;
	
	return nread;
}

static ssize_t
stream_write (GMimeStream *stream, const char *buf, size_t len)
{
	struct _SpruceTcpStreamSSLPrivate *priv = ((SpruceTcpStreamSSL *) stream)->priv;
	SpruceTcpStream *tcp_stream = (SpruceTcpStream *) stream;
	ssize_t n, nwritten = 0;
	
	if (priv->ssl) {
		do {
			do {
				if ((n = SSL_write (priv->ssl, buf + nwritten, len - nwritten)) >= 0)
					break;
				
				switch (SSL_get_error (priv->ssl, n)) {
				case SSL_ERROR_ZERO_RETURN:
					n = 0;
					break;
				case SSL_ERROR_WANT_READ:
					errno = EAGAIN;
					break;
				case SSL_ERROR_SYSCALL:
					/* errno will be set appropriately */
					break;
				default:
					break;
				}
			} while (n < 0 && (errno == EINTR || errno == EAGAIN));
			
			if (n > 0)
				nwritten += n;
		} while (n > 0 && nwritten < len);
		
		if (n < 0)
			n = -1;
	} else {
		n = nwritten = spruce_write (tcp_stream->sockfd, buf, len);
	}
	
	if (nwritten > 0)
		stream->position += nwritten;
	else if (n < 0)
		return -1;
	
	return nwritten;
}

static int
stream_close (GMimeStream *stream)
{
	struct _SpruceTcpStreamSSLPrivate *priv = ((SpruceTcpStreamSSL *) stream)->priv;
	
	g_return_val_if_fail (((SpruceTcpStream *) stream)->sockfd != -1, -1);
	
	if (priv->ssl) {
		SSL_shutdown (priv->ssl);
		SSL_CTX_free (priv->ssl->ctx);
		SSL_free (priv->ssl);
		priv->ssl = NULL;
	}
	
	return GMIME_STREAM_CLASS (parent_class)->close (stream);
}


static int
verify (int ok, X509_STORE_CTX *ctx) 
{
	return 1;
}


#define ENABLE_TLS SPRUCE_TCP_STREAM_SSL_ENABLE_TLS
#define ENABLE_SSLv3 SPRUCE_TCP_STREAM_SSL_ENABLE_SSL3
#define ENABLE_SSLv2 SPRUCE_TCP_STREAM_SSL_ENABLE_SSL2
#define ENABLE_SSLv23 (ENABLE_SSLv2 | ENABLE_SSLv3)

static int
enable_ssl (SpruceTcpStreamSSL *stream, GError **err)
{
	SpruceTcpStream *tcp_stream = (SpruceTcpStream *) stream;
	struct _SpruceTcpStreamSSLPrivate *priv = stream->priv;
	SSL_CTX *ctx = NULL;
	SSL *ssl;
	int rv;
	
	if ((priv->flags & ENABLE_TLS) == ENABLE_TLS) {
		if (!(ctx = SSL_CTX_new (TLSv1_client_method ()))) {
			g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_GENERIC,
				     _("Failed to create TLS context"));
			return -1;
		}
	} else if ((priv->flags & ENABLE_SSLv23) == ENABLE_SSLv23) {
		if (!(ctx = SSL_CTX_new (SSLv23_client_method ()))) {
			g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_GENERIC,
				     _("Failed to create SSLv2/3 context"));
			return -1;
		}
	} else if ((priv->flags & ENABLE_SSLv3) == ENABLE_SSLv3) {
		if (!(ctx = SSL_CTX_new (SSLv3_client_method ()))) {
			g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_GENERIC,
				     _("Failed to create SSLv3 context"));
			return -1;
		}
	} else if ((priv->flags & ENABLE_SSLv2) == ENABLE_SSLv2) {
		if (!(ctx = SSL_CTX_new (SSLv2_client_method ()))) {
			g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_GENERIC,
				     _("Failed to create SSLv2 context"));
			return -1;
		}
	} else {
		return 0;
	}
	
	SSL_CTX_set_verify (ctx, SSL_VERIFY_PEER, &verify);
	ssl = SSL_new (ctx);
	
	SSL_set_fd (ssl, tcp_stream->sockfd);
	
	if ((rv = SSL_connect (ssl)) <= 0) {
		/* FIXME: use rv to determine he exact error using SSL_get_error()? */
		if (priv->flags & SPRUCE_TCP_STREAM_SSL_ENABLE_TLS) {
			g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_GENERIC,
				     _("Failed to negotiate TLS encryption"));
		} else {
			g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_GENERIC,
				     _("Failed to negotiate SSL encryption"));
		}
		
		SSL_CTX_free (ctx);
		SSL_free (ssl);
		
		return -1;
	}
	
	priv->ssl = ssl;
	
	return 0;
}

static int
tcp_connect (SpruceTcpStream *stream, struct addrinfo *ai)
{
	struct _SpruceTcpStreamSSLPrivate *priv = ((SpruceTcpStreamSSL *) stream)->priv;
	
	if (SPRUCE_TCP_STREAM_CLASS (parent_class)->connect (stream, ai) == -1)
		return -1;
	
	if (priv->flags & SPRUCE_TCP_STREAM_SSL_ENABLE_SSL_CONNECT) {
		if (enable_ssl ((SpruceTcpStreamSSL *) stream, NULL) == -1) {
			close (stream->sockfd);
			stream->sockfd = -1;
			errno = EIO;
			return -1;
		}
	}
	
	return 0;
}


/**
 * spruce_tcp_stream_ssl_new:
 * @session: active session
 * @expected_host: host that the stream is expected to connect with.
 * @flags: #SPRUCE_TCP_STREAM_SSL_ENABLE_SSL2, ENABLE_SSL3 and/or ENABLE_TLS and ENABLE_SSL_CONNECT
 *
 * Since the SSL certificate authenticator may need to prompt the
 * user, a #SpruceSession is needed. @expected_host is needed as a
 * protection against an MITM attack. The @flags are used to limit
 * which SSL/TLS versions are available as well as to specify if the
 * SSL stream should connect in SSL mode (as opposed to waiting for
 * spruce_tcp_stream_ssl_enable_ssl() to be called).
 *
 * Return value: a ssl stream (optionally in ssl mode)
 **/
GMimeStream *
spruce_tcp_stream_ssl_new (SpruceSession *session, const char *expected_host, guint32 flags)
{
	SpruceTcpStreamSSL *stream;
	
	g_assert (SPRUCE_IS_SESSION (session));
	
	g_object_ref (session);
	
	stream = g_object_new (SPRUCE_TYPE_TCP_STREAM_SSL, NULL);
	
	stream->priv->session = session;
	stream->priv->expected_host = g_strdup (expected_host);
	stream->priv->flags = flags;
	
	return (GMimeStream *) stream;
}


/**
 * spruce_tcp_stream_ssl_enable_ssl:
 * @stream: ssl stream
 * @err: a #GError
 *
 * Toggles an ssl-capable stream into ssl mode (if it isn't already).
 *
 * Returns 0 on success or -1 on fail.
 **/
int
spruce_tcp_stream_ssl_enable_ssl (SpruceTcpStreamSSL *stream, GError **err)
{
	SpruceTcpStream *tcp_stream = (SpruceTcpStream *) stream;
	
	g_return_val_if_fail (SPRUCE_IS_TCP_STREAM_SSL (stream), -1);
	
	if (tcp_stream->sockfd && !stream->priv->ssl) {
		if (enable_ssl (stream, err) == -1)
			return -1;
	}
	
	return 0;
}

#endif /* HAVE_OPENSSL */
