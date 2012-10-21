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


#ifndef __SPRUCE_TCP_STREAM_H__
#define __SPRUCE_TCP_STREAM_H__

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include <gmime/gmime-stream.h>

G_BEGIN_DECLS

#define SPRUCE_TYPE_TCP_STREAM            (spruce_tcp_stream_get_type ())
#define SPRUCE_TCP_STREAM(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), SPRUCE_TYPE_TCP_STREAM, SpruceTcpStream))
#define SPRUCE_TCP_STREAM_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), SPRUCE_TYPE_TCP_STREAM, SpruceTcpStreamClass))
#define SPRUCE_IS_TCP_STREAM(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SPRUCE_TYPE_TCP_STREAM))
#define SPRUCE_IS_TCP_STREAM_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), SPRUCE_TYPE_TCP_STREAM))
#define SPRUCE_TCP_STREAM_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), SPRUCE_TYPE_TCP_STREAM, SpruceTcpStreamClass))

typedef struct _SpruceTcpStream SpruceTcpStream;
typedef struct _SpruceTcpStreamClass SpruceTcpStreamClass;

typedef enum {
	SPRUCE_SOCKOPT_NONBLOCKING,     /* nonblocking io */
	SPRUCE_SOCKOPT_LINGER,          /* linger on close if data present */
	SPRUCE_SOCKOPT_REUSEADDR,       /* allow local address reuse */
	SPRUCE_SOCKOPT_KEEPALIVE,       /* keep connections alive */
	SPRUCE_SOCKOPT_RECVBUFFERSIZE,  /* receive buffer size */
	SPRUCE_SOCKOPT_SENDBUFFERSIZE,  /* send buffer size */
	
	SPRUCE_SOCKOPT_IPTIMETOLIVE,    /* time to live */
	SPRUCE_SOCKOPT_IPTYPEOFSERVICE, /* type of service and precedence */
	
	SPRUCE_SOCKOPT_ADDMEMBER,       /* add an IP group membership */
	SPRUCE_SOCKOPT_DROPMEMBER,      /* drop an IP group membership */
	SPRUCE_SOCKOPT_MCASTINTERFACE,  /* multicast interface address */
	SPRUCE_SOCKOPT_MCASTTIMETOLIVE, /* multicast timetolive */
	SPRUCE_SOCKOPT_MCASTLOOPBACK,   /* multicast loopback */
	
	SPRUCE_SOCKOPT_NODELAY,         /* don't delay send to coalesce packets */
	SPRUCE_SOCKOPT_MAXSEGMENT,      /* maximum segment size */
	SPRUCE_SOCKOPT_BROADCAST,       /* enable broadcast */
	SPRUCE_SOCKOPT_LAST
} SpruceSockOpt;

typedef struct _SpruceSockOptData {
	SpruceSockOpt option;
	union {
		guint ip_ttl;               /* IP time to live */
		guint mcast_ttl;            /* IP multicast time to live */
		guint tos;                  /* IP type of service and precedence */
		gboolean non_blocking;      /* Non-blocking (network) I/O */
		gboolean reuse_addr;        /* Allow local address reuse */
		gboolean keep_alive;        /* Keep connections alive */
		gboolean mcast_loopback;    /* IP multicast loopback */
		gboolean no_delay;          /* Don't delay send to coalesce packets */
		gboolean broadcast;         /* Enable broadcast */
		size_t max_segment;         /* Maximum segment size */
		size_t recv_buffer_size;    /* Receive buffer size */
		size_t send_buffer_size;    /* Send buffer size */
		struct linger linger;       /* Time to linger on close if data present */
	} value;
} SpruceSockOptData;

typedef enum {
	SPRUCE_TCP_ADDRESS_IPv4,
	SPRUCE_TCP_ADDRESS_IPv6
} SpruceTcpAddressFamily;

typedef struct {
	SpruceTcpAddressFamily family;
	socklen_t length;
	guint16 port;
	guint8 address[1];
} SpruceTcpAddress;


struct _SpruceTcpStream {
	GMimeStream parent_object;
	
	int sockfd;
};

struct _SpruceTcpStreamClass {
	GMimeStreamClass parent_class;
	
	/* Virtual methods */
	int (* connect)    (SpruceTcpStream *stream, struct addrinfo *ai);
	int (* getsockopt) (SpruceTcpStream *stream, SpruceSockOptData *data);
	int (* setsockopt) (SpruceTcpStream *stream, const SpruceSockOptData *data);
	
	SpruceTcpAddress * (* getsockaddr) (SpruceTcpStream *stream);
	SpruceTcpAddress * (* getpeeraddr) (SpruceTcpStream *stream);
};


GType spruce_tcp_stream_get_type (void);

GMimeStream *spruce_tcp_stream_new (void);

/* public methods */
int spruce_tcp_stream_connect    (SpruceTcpStream *stream, struct addrinfo *ai);
int spruce_tcp_stream_getsockopt (SpruceTcpStream *stream, SpruceSockOptData *data);
int spruce_tcp_stream_setsockopt (SpruceTcpStream *stream, const SpruceSockOptData *data);

SpruceTcpAddress *spruce_tcp_stream_getsockaddr (SpruceTcpStream *stream);
SpruceTcpAddress *spruce_tcp_stream_getpeeraddr (SpruceTcpStream *stream);

SpruceTcpAddress *spruce_tcp_address_new  (SpruceTcpAddressFamily family,
					   guint16 port, socklen_t length,
					   gpointer address);
void spruce_tcp_address_free (SpruceTcpAddress *address);

G_END_DECLS

#endif /* __SPRUCE_TCP_STREAM_H__ */
