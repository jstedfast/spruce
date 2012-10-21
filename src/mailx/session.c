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
#include <sys/poll.h>
#include <unistd.h>
#include <errno.h>

#include <spruce/spruce-error.h>

#include "session.h"


struct _MailxSessionPrivate {
	GHashTable *passwords;
};


static void mailx_session_class_init (MailxSessionClass *klass);
static void mailx_session_init (MailxSession *session, MailxSessionClass *klass);
static void mailx_session_finalize (GObject *object);

static void mailx_alert_user (SpruceSession *session, const char *warning);
static char *mailx_request_passwd (SpruceSession *session, const char *prompt,
				 const char *key, guint32 flags, GError **err);
static void mailx_forget_passwd (SpruceSession *session, const char *key);


static SpruceSessionClass *parent_class = NULL;


GType
mailx_session_get_type (void)
{
	static GType type = 0;
	
	if (!type) {
		static const GTypeInfo info = {
			sizeof (MailxSessionClass),
			NULL, /* base_class_init */
			NULL, /* base_class_finalize */
			(GClassInitFunc) mailx_session_class_init,
			NULL, /* class_finalize */
			NULL, /* class_data */
			sizeof (MailxSession),
			0,    /* n_preallocs */
			(GInstanceInitFunc) mailx_session_init,
		};
		
		type = g_type_register_static (SPRUCE_TYPE_SESSION, "MailxSession", &info, 0);
	}
	
	return type;
}


static void
mailx_session_class_init (MailxSessionClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	SpruceSessionClass *session_class = SPRUCE_SESSION_CLASS (klass);
	
	parent_class = g_type_class_ref (SPRUCE_TYPE_SESSION);
	
	object_class->finalize = mailx_session_finalize;
	
	session_class->alert_user = mailx_alert_user;
	session_class->request_passwd = mailx_request_passwd;
	session_class->forget_passwd = mailx_forget_passwd;
}

static void
mailx_session_init (MailxSession *session, MailxSessionClass *klass)
{
	session->priv = g_new (struct _MailxSessionPrivate, 1);
	session->priv->passwords = g_hash_table_new (g_str_hash, g_str_equal);
	
	((SpruceSession *) session)->storage_path = g_build_filename (g_get_home_dir (), ".spruce", "mail", NULL);
}

static void
z_free (char *passwd)
{
	memset (passwd, 0, strlen (passwd));
	g_free (passwd);
}

static void
passwd_free (char *key, char *passwd, void *user_data)
{
	g_free (key);
	z_free (passwd);
}

static void
mailx_session_finalize (GObject *object)
{
	MailxSession *session = (MailxSession *) object;
	
	g_hash_table_foreach (session->priv->passwords, (GHFunc) passwd_free, NULL);
	g_hash_table_destroy (session->priv->passwords);
	g_free (session->priv);
	
	G_OBJECT_CLASS (parent_class)->finalize (object);
}


static void
mailx_alert_user (SpruceSession *session, const char *warning)
{
	fprintf (stdout, "\nWARNING: %s\n", warning);
}

static gboolean
prompt_remember (void)
{
	gboolean remember = FALSE;
	char kbdbuf[256];
	
	fputs ("Remember this password? [N/y]: ", stdout);
	fflush (stdout);
	
	if (fgets (kbdbuf, sizeof (kbdbuf), stdin)) {
		remember = kbdbuf[0] == 'Y' || kbdbuf[0] == 'y';
		
		while (kbdbuf[strlen (kbdbuf) - 1] != '\n')
			if (!fgets (kbdbuf, sizeof (kbdbuf), stdin))
				break;
	}
	
	return remember;
}

static char *
mailx_request_passwd (SpruceSession *session, const char *prompt,
		      const char *key, guint32 flags, GError **err)
{
	MailxSession *mailx = (MailxSession *) session;
	char *passwd, *passptr, *buf;
	struct pollfd ufds;
	char kbdbuf[256];
	size_t passleft;
	size_t passlen;
	size_t i;
	
	passwd = g_hash_table_lookup (mailx->priv->passwords, key);
	
	if (passwd && !(flags & SPRUCE_SESSION_PASSWORD_REPROMPT))
		return g_strdup (passwd);
	
	if ((flags & SPRUCE_SESSION_PASSWORD_REPROMPT))
		mailx_forget_passwd (session, key);
	
	fprintf (stdout, "%s\nEnter: ", prompt);
	fflush (stdout);
	
	passlen = 0;
	passleft = sizeof (kbdbuf);
	passptr = passwd = g_malloc (passleft + 1);
	
	ufds.fd = 0;
	ufds.events = POLLIN;
	
	system ("stty -echo");
	
	do {
		ufds.revents = 0;
		if (poll (&ufds, 1, -1) > 0) {
			if (fgets (kbdbuf, sizeof (kbdbuf), stdin)) {
				for (i = 0; kbdbuf[i] && kbdbuf[i] != '\n'; i++)
					fputc ('*', stdout);
				fflush (stdout);
				
				if (i >= passleft) {
					buf = g_malloc (passlen + passleft + sizeof (kbdbuf) + 1);
					memcpy (buf, passwd, passlen + 1);
					passleft += sizeof (kbdbuf);
					passptr = buf + passlen;
					z_free (passwd);
					passwd = buf;
				}
				
				passptr = g_stpcpy (passptr, kbdbuf);
				passleft -= i;
				passlen += i;
				
				memset (kbdbuf, 0, i);
			}
		}
	} while ((passptr == passwd || passptr[-1] != '\n') && !(ufds.revents & (POLLERR | POLLHUP)));
	
	system ("stty echo");
	
	if (passptr > passwd && passptr[-1] == '\n') {
		passptr[-1] = '\0';
		
		fputc ('\n', stdout);
		fflush (stdout);
		
		if (prompt_remember ())
			g_hash_table_insert (mailx->priv->passwords, g_strdup (key), g_strdup (passwd));
		
		return passwd;
	}
	
	z_free (passwd);
	
	g_set_error (err, SPRUCE_ERROR, EINTR, "%s", g_strerror (EINTR));
	
	return NULL;
}

static void
mailx_forget_passwd (SpruceSession *session, const char *key)
{
	MailxSession *mailx = (MailxSession *) session;
	gpointer okey, oval;
	
	if (!g_hash_table_lookup_extended (mailx->priv->passwords, key, &okey, &oval))
		return;
	
	g_hash_table_remove (mailx->priv->passwords, key);
	
	g_free (okey);
	z_free (oval);
}


SpruceSession *
mailx_session_new (void)
{
	return g_object_new (MAILX_TYPE_SESSION, NULL);
}
