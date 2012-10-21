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
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <netdb.h>
#include <fcntl.h>
#include <errno.h>

#include "spruce-file-utils.h"
#include "spruce-tcp-stream.h"

static void spruce_tcp_stream_class_init (SpruceTcpStreamClass *klass);
static void spruce_tcp_stream_init (SpruceTcpStream *stream, SpruceTcpStreamClass *klass);
static void spruce_tcp_stream_finalize (GObject *object);

static ssize_t stream_read (GMimeStream *stream, char *buf, size_t len);
static ssize_t stream_write (GMimeStream *stream, const char *buf, size_t len);
static int stream_flush (GMimeStream *stream);
static int stream_close (GMimeStream *stream);
static gboolean stream_eos (GMimeStream *stream);
static int stream_reset (GMimeStream *stream);
static gint64 stream_seek (GMimeStream *stream, gint64 offset, GMimeSeekWhence whence);
static gint64 stream_tell (GMimeStream *stream);
static gint64 stream_length (GMimeStream *stream);
static GMimeStream *stream_substream (GMimeStream *stream, gint64 start, gint64 end);

static int tcp_connect (SpruceTcpStream *stream, struct addrinfo *ai);
static int tcp_getsockopt (SpruceTcpStream *stream, SpruceSockOptData *data);
static int tcp_setsockopt (SpruceTcpStream *stream, const SpruceSockOptData *data);
static SpruceTcpAddress *tcp_getsockaddr (SpruceTcpStream *stream);
static SpruceTcpAddress *tcp_getpeeraddr (SpruceTcpStream *stream);


static GMimeStreamClass *parent_class = NULL;


GType
spruce_tcp_stream_get_type (void)
{
	static GType type = 0;
	
	if (!type) {
		static const GTypeInfo info = {
			sizeof (SpruceTcpStreamClass),
			NULL, /* base_class_init */
			NULL, /* base_class_finalize */
			(GClassInitFunc) spruce_tcp_stream_class_init,
			NULL, /* class_finalize */
			NULL, /* class_data */
			sizeof (SpruceTcpStream),
			0,    /* n_preallocs */
			(GInstanceInitFunc) spruce_tcp_stream_init,
		};
		
		type = g_type_register_static (GMIME_TYPE_STREAM, "SpruceTcpStream", &info, 0);
	}
	
	return type;
}


static void
spruce_tcp_stream_class_init (SpruceTcpStreamClass *klass)
{
	GMimeStreamClass *stream_class = GMIME_STREAM_CLASS (klass);
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	
	parent_class = g_type_class_ref (GMIME_TYPE_STREAM);
	
	object_class->finalize = spruce_tcp_stream_finalize;
	
	stream_class->read = stream_read;
	stream_class->write = stream_write;
	stream_class->flush = stream_flush;
	stream_class->close = stream_close;
	stream_class->eos = stream_eos;
	stream_class->reset = stream_reset;
	stream_class->seek = stream_seek;
	stream_class->tell = stream_tell;
	stream_class->length = stream_length;
	stream_class->substream = stream_substream;
	
	klass->connect = tcp_connect;
	klass->getsockopt = tcp_getsockopt;
	klass->setsockopt = tcp_setsockopt;
	klass->getsockaddr = tcp_getsockaddr;
	klass->getpeeraddr = tcp_getpeeraddr;
}

static void
spruce_tcp_stream_init (SpruceTcpStream *stream, SpruceTcpStreamClass *klass)
{
	((GMimeStream *) stream)->bound_end = -1;
	
	stream->sockfd = -1;
}

static void
spruce_tcp_stream_finalize (GObject *object)
{
	SpruceTcpStream *stream = (SpruceTcpStream *) object;
	
	if (stream->sockfd != -1)
		close (stream->sockfd);
	
	G_OBJECT_CLASS (parent_class)->finalize (object);
}


static ssize_t
stream_read (GMimeStream *stream, char *buf, size_t n)
{
	SpruceTcpStream *tcp = (SpruceTcpStream *) stream;
	ssize_t nread;
	
	if ((nread = spruce_read (tcp->sockfd, buf, n)) > 0)
		stream->position += nread;
	
	return nread;
}

static ssize_t
stream_write (GMimeStream *stream, const char *buf, size_t n)
{
	SpruceTcpStream *tcp = (SpruceTcpStream *) stream;
	ssize_t nwritten;
	
	if ((nwritten = spruce_write (tcp->sockfd, buf, n)) > 0)
		stream->position += nwritten;
	
	return nwritten;
}

static int
stream_flush (GMimeStream *stream)
{
	return 0;
}

static int
stream_close (GMimeStream *stream)
{
	SpruceTcpStream *tcp = (SpruceTcpStream *) stream;
	int rv;
	
	g_return_val_if_fail (tcp->sockfd != -1, -1);
	
	if ((rv = close (tcp->sockfd)) != -1)
		tcp->sockfd = -1;
	
	return rv;
}

static gboolean
stream_eos (GMimeStream *stream)
{
	return FALSE;
}

static int
stream_reset (GMimeStream *stream)
{
	return 0;
}

static gint64
stream_seek (GMimeStream *stream, gint64 offset, GMimeSeekWhence whence)
{
	return -1;
}

static gint64
stream_tell (GMimeStream *stream)
{
	return -1;
}

static gint64
stream_length (GMimeStream *stream)
{
	return -1;
}

static GMimeStream *
stream_substream (GMimeStream *stream, gint64 start, gint64 end)
{
	return NULL;
}


static int
socket_connect (struct addrinfo *ai)
{
	int errnosav;
	int sockfd;
	
	if (ai->ai_socktype != SOCK_STREAM) {
		errno = EINVAL;
		return -1;
	}
	
	if ((sockfd = socket (ai->ai_family, SOCK_STREAM, 0)) == -1)
		return -1;
	
	if (connect (sockfd, ai->ai_addr, ai->ai_addrlen) == -1) {
		errnosav = errno;
		close (sockfd);
		errno = errnosav;
		return -1;
	}
	
	return sockfd;
}

static int
tcp_connect (SpruceTcpStream *stream, struct addrinfo *ai)
{
	int sockfd;
	
	while (ai != NULL) {
		if ((sockfd = socket_connect (ai)) != -1) {
			stream->sockfd = sockfd;
			return 0;
		}
		
		ai = ai->ai_next;
	}
	
	return -1;
}


/**
 * spruce_tcp_stream_connect:
 * @stream: tcp stream
 * @ai: addr info
 *
 * Connects the tcp stream to the host in @ai
 *
 * Returns 0 on success. On error, -1 is returned and errno is set
 * appropriately.
 **/
int
spruce_tcp_stream_connect (SpruceTcpStream *stream, struct addrinfo *ai)
{
	g_return_val_if_fail (SPRUCE_IS_TCP_STREAM (stream), -1);
	
	return SPRUCE_TCP_STREAM_GET_CLASS (stream)->connect (stream, ai);
}


static int
sockopt_level (const SpruceSockOptData *data)
{
	switch (data->option) {
	case SPRUCE_SOCKOPT_MAXSEGMENT:
	case SPRUCE_SOCKOPT_NODELAY:
		return IPPROTO_TCP;
	default:
		return SOL_SOCKET;
	}
}

static int
sockopt_optname (const SpruceSockOptData *data)
{
	switch (data->option) {
	case SPRUCE_SOCKOPT_MAXSEGMENT:
		return TCP_MAXSEG;
	case SPRUCE_SOCKOPT_NODELAY:
		return TCP_NODELAY;
	case SPRUCE_SOCKOPT_BROADCAST:
		return SO_BROADCAST;
	case SPRUCE_SOCKOPT_KEEPALIVE:
		return SO_KEEPALIVE;
	case SPRUCE_SOCKOPT_LINGER:
		return SO_LINGER;
	case SPRUCE_SOCKOPT_RECVBUFFERSIZE:
		return SO_RCVBUF;
	case SPRUCE_SOCKOPT_SENDBUFFERSIZE:
		return SO_SNDBUF;
	case SPRUCE_SOCKOPT_REUSEADDR:
		return SO_REUSEADDR;
	case SPRUCE_SOCKOPT_IPTYPEOFSERVICE:
		return SO_TYPE;
	default:
		return -1;
	}
}

static int
tcp_getsockopt (SpruceTcpStream *stream, SpruceSockOptData *data)
{
	int optname, level;
	socklen_t optlen;
	
	if (data->option == SPRUCE_SOCKOPT_NONBLOCKING) {
		int flags;
		
		if ((flags = fcntl (stream->sockfd, F_GETFL)) == -1)
			return -1;
		
		data->value.non_blocking = flags & O_NONBLOCK ? TRUE : FALSE;
		
		return 0;
	}
	
	if ((optname = sockopt_optname (data)) == -1)
		return -1;
	
	level = sockopt_level (data);
	
	return getsockopt (stream->sockfd, level, optname, (void *) &data->value, &optlen);
}


/**
 * spruce_tcp_stream_getsockopt:
 * @stream: tcp stream
 * @data: requested socket option
 *
 * Gets the value associated with the requested socket option @data
 * and fills in the appropriate field within @data.
 *
 * Returns 0 on success. On error, -1 is returned and errno is set
 * appropriately.
 **/
int
spruce_tcp_stream_getsockopt (SpruceTcpStream *stream, SpruceSockOptData *data)
{
	g_return_val_if_fail (SPRUCE_IS_TCP_STREAM (stream), -1);
	
	return SPRUCE_TCP_STREAM_GET_CLASS (stream)->getsockopt (stream, data);
}

static int
tcp_setsockopt (SpruceTcpStream *stream, const SpruceSockOptData *data)
{
	int optname, level;
	
	if (data->option == SPRUCE_SOCKOPT_NONBLOCKING) {
		int flags, set;
		
		if ((flags = fcntl (stream->sockfd, F_GETFL)) == -1)
			return -1;
		
		set = data->value.non_blocking ? O_NONBLOCK : 0;
		flags = (flags & ~O_NONBLOCK) | set;
		
		if (fcntl (stream->sockfd, F_SETFL, flags) == -1)
			return -1;
		
		return 0;
	}
	
	if ((optname = sockopt_optname (data)) == -1)
		return -1;
	
	level = sockopt_level (data);
	
	return setsockopt (stream->sockfd, level, optname, (void *) &data->value, sizeof (data->value));
}


/**
 * spruce_tcp_stream_setsockopt:
 * @stream: tcp stream
 * @data: socket option
 *
 * Sets the socket option @data.
 *
 * Returns 0 on success. On error, -1 is returned and errno is set
 * appropriately.
 **/
int
spruce_tcp_stream_setsockopt (SpruceTcpStream *stream, const SpruceSockOptData *data)
{
	g_return_val_if_fail (SPRUCE_IS_TCP_STREAM (stream), -1);
	
	return SPRUCE_TCP_STREAM_GET_CLASS (stream)->setsockopt (stream, data);
}


#ifdef ENABLE_IPv6
#define MIN_SOCKADDR_BUFLEN  (sizeof (struct sockaddr_in6))
#else
#define MIN_SOCKADDR_BUFLEN  (sizeof (struct sockaddr_in))
#endif

static SpruceTcpAddress *
tcp_getsockaddr (SpruceTcpStream *stream)
{
	unsigned char buf[MIN_SOCKADDR_BUFLEN];
#ifdef ENABLE_IPv6
	struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *) buf;
#endif
	struct sockaddr_in *sin = (struct sockaddr_in *) buf;
	struct sockaddr *saddr = (struct sockaddr *) buf;
	gpointer address;
	socklen_t len;
	int family;
	
	len = MIN_SOCKADDR_BUFLEN;
	
	if (getsockname (stream->sockfd, saddr, &len) == -1)
		return NULL;
	
	if (saddr->sa_family == AF_INET) {
		family = SPRUCE_TCP_ADDRESS_IPv4;
		address = &sin->sin_addr;
#ifdef ENABLE_IPv6
	} else if (saddr->sa_family == AF_INET6) {
		family = SPRUCE_TCP_ADDRESS_IPv6;
		address = &sin6->sin6_addr;
#endif
	} else
		return NULL;
	
	return spruce_tcp_address_new (family, sin->sin_port, len, address);
}


SpruceTcpAddress *
spruce_tcp_stream_getsockaddr (SpruceTcpStream *stream)
{
	return SPRUCE_TCP_STREAM_GET_CLASS (stream)->getsockaddr (stream);
}

static SpruceTcpAddress *
tcp_getpeeraddr (SpruceTcpStream *stream)
{
	unsigned char buf[MIN_SOCKADDR_BUFLEN];
#ifdef ENABLE_IPv6
	struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *) buf;
#endif
	struct sockaddr_in *sin = (struct sockaddr_in *) buf;
	struct sockaddr *saddr = (struct sockaddr *) buf;
	gpointer address;
	socklen_t len;
	int family;
	
	len = MIN_SOCKADDR_BUFLEN;
	
	if (getpeername (stream->sockfd, saddr, &len) == -1)
		return NULL;
	
	if (saddr->sa_family == AF_INET) {
		family = SPRUCE_TCP_ADDRESS_IPv4;
		address = &sin->sin_addr;
#ifdef ENABLE_IPv6
	} else if (saddr->sa_family == AF_INET6) {
		family = SPRUCE_TCP_ADDRESS_IPv6;
		address = &sin6->sin6_addr;
#endif
	} else
		return NULL;
	
	return spruce_tcp_address_new (family, sin->sin_port, len, address);
}


SpruceTcpAddress *
spruce_tcp_stream_getpeeraddr (SpruceTcpStream *stream)
{
	return SPRUCE_TCP_STREAM_GET_CLASS (stream)->getpeeraddr (stream);
}


/**
 * spruce_tcp_stream_new:
 *
 * Creates a new TCP stream.
 *
 * Returns a new TCP stream.
 **/
GMimeStream *
spruce_tcp_stream_new (void)
{
	return g_object_new (SPRUCE_TYPE_TCP_STREAM, NULL);
}


SpruceTcpAddress *
spruce_tcp_address_new (SpruceTcpAddressFamily family,
			guint16 port, socklen_t length,
			gpointer address)
{
	SpruceTcpAddress *addr;
	
	addr = g_malloc (sizeof (SpruceTcpAddress) + length - 1);
	addr->family = family;
	addr->port = port;
	addr->length = length;
	memcpy (&addr->address, address, length);
	
	return addr;
}


void
spruce_tcp_address_free (SpruceTcpAddress *address)
{
	g_free (address);
}
