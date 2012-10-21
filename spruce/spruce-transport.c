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

#include <glib/gi18n.h>

#include <spruce/spruce-error.h>
#include <spruce/spruce-transport.h>


static void spruce_transport_class_init (SpruceTransportClass *klass);
static void spruce_transport_init (SpruceTransport *transport, SpruceTransportClass *klass);
static void spruce_transport_finalize (GObject *object);

static int transport_send (SpruceTransport *transport, GMimeMessage *message,
			   InternetAddressMailbox *from, InternetAddressList *recipients,
			   GError **err);


static SpruceServiceClass *parent_class = NULL;


GType
spruce_transport_get_type (void)
{
	static GType type = 0;
	
	if (!type) {
		static const GTypeInfo info = {
			sizeof (SpruceTransportClass),
			NULL, /* base_class_init */
			NULL, /* base_class_finalize */
			(GClassInitFunc) spruce_transport_class_init,
			NULL, /* class_finalize */
			NULL, /* class_data */
			sizeof (SpruceTransport),
			0,    /* n_preallocs */
			(GInstanceInitFunc) spruce_transport_init,
		};
		
		type = g_type_register_static (G_TYPE_OBJECT, "SpruceTransport", &info, 0);
	}
	
	return type;
}


static void
spruce_transport_class_init (SpruceTransportClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	
	parent_class = g_type_class_ref (SPRUCE_TYPE_SERVICE);
	
	object_class->finalize = spruce_transport_finalize;
	
	klass->send = transport_send;
}

static void
spruce_transport_init (SpruceTransport *transport, SpruceTransportClass *klass)
{
	;
}

static void
spruce_transport_finalize (GObject *object)
{
	G_OBJECT_CLASS (parent_class)->finalize (object);
}


static int
transport_send (SpruceTransport *transport, GMimeMessage *message,
		InternetAddressMailbox *from, InternetAddressList *recipients,
		GError **err)
{
	return -1;
}


int
spruce_transport_send (SpruceTransport *transport, GMimeMessage *message,
		       InternetAddressMailbox *from, InternetAddressList *recipients,
		       GError **err)
{
	g_return_val_if_fail (SPRUCE_IS_TRANSPORT (transport), -1);
	g_return_val_if_fail (IS_INTERNET_ADDRESS_LIST (recipients), -1);
	g_return_val_if_fail (INTERNET_ADDRESS_IS_MAILBOX (from), -1);
	
	if (!((SpruceService *) transport)->connected) {
		g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_SERVICE_NOT_CONNECTED,
			     _("Cannot send message: service not connected"));
		return -1;
	}
	
	if (!from->addr) {
		g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_TRANSPORT_INVALID_SENDER,
			     _("Cannot send message: sender invalid"));
		return -1;
	}
	
	return SPRUCE_TRANSPORT_GET_CLASS (transport)->send (transport, message, from, recipients, err);
}
