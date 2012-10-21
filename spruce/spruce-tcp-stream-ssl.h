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


#ifndef __SPRUCE_TCP_STREAM_SSL_H__
#define __SPRUCE_TCP_STREAM_SSL_H__

#include <spruce/spruce-session.h>
#include <spruce/spruce-tcp-stream.h>

G_BEGIN_DECLS

#define SPRUCE_TYPE_TCP_STREAM_SSL            (spruce_tcp_stream_ssl_get_type ())
#define SPRUCE_TCP_STREAM_SSL(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), SPRUCE_TYPE_TCP_STREAM_SSL, SpruceTcpStreamSSL))
#define SPRUCE_TCP_STREAM_SSL_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), SPRUCE_TYPE_TCP_STREAM_SSL, SpruceTcpStreamSSLClass))
#define SPRUCE_IS_TCP_STREAM_SSL(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SPRUCE_TYPE_TCP_STREAM_SSL))
#define SPRUCE_IS_TCP_STREAM_SSL_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), SPRUCE_TYPE_TCP_STREAM_SSL))
#define SPRUCE_TCP_STREAM_SSL_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), SPRUCE_TYPE_TCP_STREAM_SSL, SpruceTcpStreamSSLClass))

typedef struct _SpruceTcpStreamSSL SpruceTcpStreamSSL;
typedef struct _SpruceTcpStreamSSLClass SpruceTcpStreamSSLClass;

enum {
	SPRUCE_TCP_STREAM_SSL_ENABLE_SSL2        = (1 << 0),
	SPRUCE_TCP_STREAM_SSL_ENABLE_SSL3        = (1 << 1),
	SPRUCE_TCP_STREAM_SSL_ENABLE_TLS         = (1 << 2),
	SPRUCE_TCP_STREAM_SSL_ENABLE_SSL_CONNECT = (1 << 3),
};

struct _SpruceTcpStreamSSL {
	SpruceTcpStream parent_object;
	
	struct _SpruceTcpStreamSSLPrivate *priv;
};

struct _SpruceTcpStreamSSLClass {
	SpruceTcpStreamClass parent_class;
	
};


GType spruce_tcp_stream_ssl_get_type (void);

GMimeStream *spruce_tcp_stream_ssl_new (SpruceSession *session, const char *expected_host, guint32 flags);

int spruce_tcp_stream_ssl_enable_ssl (SpruceTcpStreamSSL *ssl, GError **err);

G_END_DECLS

#endif /* __SPRUCE_TCP_STREAM_SSL_H__ */
