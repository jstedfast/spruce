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


#ifndef __SPRUCE_TRANSPORT_H__
#define __SPRUCE_TRANSPORT_H__

#include <glib.h>

#include <gmime/gmime-message.h>

#include <spruce/spruce-service.h>

G_BEGIN_DECLS

#define SPRUCE_TYPE_TRANSPORT            (spruce_transport_get_type ())
#define SPRUCE_TRANSPORT(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), SPRUCE_TYPE_TRANSPORT, SpruceTransport))
#define SPRUCE_TRANSPORT_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), SPRUCE_TYPE_TRANSPORT, SpruceTransportClass))
#define SPRUCE_IS_TRANSPORT(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SPRUCE_TYPE_TRANSPORT))
#define SPRUCE_IS_TRANSPORT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), SPRUCE_TYPE_TRANSPORT))
#define SPRUCE_TRANSPORT_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), SPRUCE_TYPE_TRANSPORT, SpruceTransportClass))

typedef struct _SpruceTransport SpruceTransport;
typedef struct _SpruceTransportClass SpruceTransportClass;

struct _SpruceTransport {
	SpruceService parent_object;
	
};

struct _SpruceTransportClass {
	SpruceServiceClass parent_class;
	
	int (* send) (SpruceTransport *transport, GMimeMessage *message,
		      InternetAddressMailbox *from, InternetAddressList *recipients,
		      GError **err);
};


GType spruce_transport_get_type (void);

int spruce_transport_send (SpruceTransport *transport, GMimeMessage *message,
			   InternetAddressMailbox *from, InternetAddressList *recipients,
			   GError **err);

G_END_DECLS

#endif /* __SPRUCE_TRANSPORT_H__ */
