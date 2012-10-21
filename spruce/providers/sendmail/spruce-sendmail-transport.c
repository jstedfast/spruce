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
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>

#include <gmime/gmime-stream-fs.h>

#include <spruce/spruce-error.h>
#include <spruce/spruce-process.h>

#include "spruce-sendmail-transport.h"


#define d(x) x
#define _(x) x


static void spruce_sendmail_transport_class_init (SpruceSendmailTransportClass *klass);
static void spruce_sendmail_transport_init (SpruceSendmailTransport *transport, SpruceSendmailTransportClass *klass);
static void spruce_sendmail_transport_finalize (GObject *object);

static int sendmail_send (SpruceTransport *transport, GMimeMessage *message,
			  InternetAddressMailbox *from, InternetAddressList *recipients,
			  GError **err);


static SpruceTransportClass *parent_class = NULL;


GType
spruce_sendmail_transport_get_type (void)
{
	static GType type = 0;
	
	if (!type) {
		static const GTypeInfo info = {
			sizeof (SpruceSendmailTransportClass),
			NULL, /* base_class_init */
			NULL, /* base_class_finalize */
			(GClassInitFunc) spruce_sendmail_transport_class_init,
			NULL, /* class_finalize */
			NULL, /* class_data */
			sizeof (SpruceSendmailTransport),
			0,    /* n_preallocs */
			(GInstanceInitFunc) spruce_sendmail_transport_init,
		};
		
		type = g_type_register_static (SPRUCE_TYPE_TRANSPORT, "SpruceSendmailTransport", &info, 0);
	}
	
	return type;
}


static void
spruce_sendmail_transport_class_init (SpruceSendmailTransportClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	SpruceTransportClass *xport_class = SPRUCE_TRANSPORT_CLASS (klass);
	
	parent_class = g_type_class_ref (SPRUCE_TYPE_TRANSPORT);
	
	object_class->finalize = spruce_sendmail_transport_finalize;
	
	xport_class->send = sendmail_send;
}

static void
spruce_sendmail_transport_init (SpruceSendmailTransport *sendmail, SpruceSendmailTransportClass *klass)
{
	;
}

static void
spruce_sendmail_transport_finalize (GObject *object)
{
	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static int
rcpt_to_all (GPtrArray *argv, InternetAddressList *recipients, GError **err)
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
			
			g_ptr_array_add (argv, mailbox->addr);
		} else {
			if (rcpt_to_all (argv, INTERNET_ADDRESS_GROUP (ia)->members, err) == -1)
				return -1;
		}
	}
	
	return 0;
}

static int
sendmail_send (SpruceTransport *transport, GMimeMessage *message,
	       InternetAddressMailbox *from, InternetAddressList *recipients,
	       GError **err)
{
	int errnosav, status, fd, ret;
	GMimeStream *stream;
	const char *path;
	GPtrArray *argv;
	pid_t pid;
	
	if (!(path = ((SpruceService *) transport)->url->path))
		path = SENDMAIL_PATH;
	
	argv = g_ptr_array_new ();
	g_ptr_array_add (argv, "sendmail");
	g_ptr_array_add (argv, "-i");
	g_ptr_array_add (argv, "-f");
	g_ptr_array_add (argv, from->addr);
	g_ptr_array_add (argv, "--");
	
	if (rcpt_to_all (argv, recipients, err) == -1) {
		g_ptr_array_free (argv, TRUE);
		return -1;
	}
	
	if (argv->len == 5) {
		g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_TRANSPORT_NO_RECIPIENTS,
			     _("Cannot send message: no recipients specified"));
		g_ptr_array_free (argv, TRUE);
		return -1;
	}
	
	g_ptr_array_add (argv, NULL);
	
	if ((pid = spruce_process_fork (path, (char **) argv->pdata, TRUE, -1, &fd, NULL, NULL, err)) == (pid_t) -1) {
		g_ptr_array_free (argv, TRUE);
		return -1;
	}
	
	stream = g_mime_stream_fs_new (fd);
	if ((ret = g_mime_object_write_to_stream ((GMimeObject *) message, stream)) != -1)
		ret = g_mime_stream_close (stream);
	errnosav = errno;
	g_object_unref (stream);
	
	if (ret == -1) {
		g_set_error (err, SPRUCE_ERROR, errnosav, _("Cannot send message: %s"), g_strerror (errnosav));
		spruce_process_kill (pid);
		return -1;
	}
	
	/* wait for sendmail to exit */
	status = spruce_process_wait (pid);
	
	if (!WIFEXITED (status)) {
		g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_GENERIC,
			     _("Sendmail exited with signal %s: mail not sent."),
			     g_strsignal (WTERMSIG (status)));
		return -1;
	} else if (WEXITSTATUS (status) != 0) {
		if (WEXITSTATUS (status) == 255) {
			g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_SERVICE_UNAVAILABLE,
				     _("Could not execute %s: mail not sent."),
				     SENDMAIL_PATH);
		} else {
			g_set_error (err, SPRUCE_ERROR, SPRUCE_ERROR_GENERIC,
				     _("Sendmail exited with status %d: mail not sent."),
				     WEXITSTATUS (status));
		}
		
		return -1;
	}
	
	return 0;
}
