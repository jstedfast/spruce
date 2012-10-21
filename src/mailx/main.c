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
#include <ctype.h>

#include <spruce/spruce.h>
#include <spruce/spruce-store.h>
#include <spruce/spruce-folder.h>

#include <gmime/gmime-stream-fs.h>

#include "session.h"

static int screenheight = 24;

static int
num_digits (int x)
{
	int n = 1;
	
	while ((x /= 10))
		n++;
	
	return n;
}

static void
help (void)
{
	fprintf (stdout, "    Mailx Commands:\n");
	fprintf (stdout, "d <message list>             delete messages\n");
	fprintf (stdout, "u <message list>             undelete messages\n");
	fprintf (stdout, "q                            quit\n");
	fprintf (stdout, "x                            quit\n");
	fprintf (stdout, "h <first>                    header list starting at <first>\n");
	fprintf (stdout, "n                            header list starting at first unread message\n");
	fprintf (stdout, "\n");
	fprintf (stdout, "A <message list> consists of integers or ranges of integers separated\n");
	fprintf (stdout, "by spaces.\n");
	fflush (stdout);
}

#define EMPTY_SUBJECT  "No Subject"

static int
display_message_list (SpruceFolder *folder, GPtrArray *uids, int first, int last, int cursor)
{
	char padding[40];
	char flag;
	char cur;
	int i;
	
	fprintf (stdout, "%s: %d messages, %d new\n", folder->name,
		 spruce_folder_get_message_count (folder),
		 spruce_folder_get_unread_message_count (folder));
	
	memset (padding, ' ', sizeof (padding) - 1);
	padding[sizeof (padding) - 1] = '\0';
	
	for (i = first; i < last; i++) {
		SpruceMessageInfo *info;
		
		if ((info = spruce_folder_get_message_info (folder, uids->pdata[i]))) {
			char *from, frombuf[21], date[17];
			int subjlen, fromlen, frommax, n;
			InternetAddressList *list;
			InternetAddress *ia;
			
			if ((list = internet_address_list_parse_string (info->from))) {
				ia = internet_address_list_get_address (list, 0);
				if (INTERNET_ADDRESS_IS_MAILBOX (ia))
					from = INTERNET_ADDRESS_MAILBOX (ia)->addr;
				else
					from = ia->name;
			} else
				from = "";
			
			strftime (date, sizeof (date), "%a %b %e %H:%M", localtime (&info->date_sent));
			
			frommax = 20;
			if ((n = num_digits (i + 1)) > 2) {
				/*fprintf (stderr, "%d > 2 digits (%d digits)\n", i + 1, n);*/
				frommax -= n - 2;
			}
			
			fromlen = MIN (frommax, strlen (from));
			memcpy (frombuf, from, fromlen);
			for (n = 0; n < fromlen; n++)
				if (!isprint (frombuf[n]))
					frombuf[n] = 'x';
			
			while (fromlen < frommax)
				frombuf[fromlen++] = ' ';
			frombuf[fromlen] = '\0';
			
			subjlen = 23;
			if ((n = (num_digits (info->lines))) > 2)
				subjlen -= n - 2;
			if ((n = (num_digits (info->size))) > 4)
				subjlen -= n - 4;
			
			if ((info->flags & SPRUCE_MESSAGE_DELETED))
				flag = 'D';
			else if ((info->flags & SPRUCE_MESSAGE_ANSWERED))
				flag = 'A';
			else if ((info->flags & SPRUCE_MESSAGE_SEEN))
				flag = 'O';
			else if ((info->flags & SPRUCE_MESSAGE_RECENT))
				flag = 'N';
			else
				flag = 'U';
			
			if ((cursor >= 0 && cursor == i) || (cursor == -1 && flag == 'N')) {
				cursor = i;
				cur = '>';
			} else
				cur = ' ';
			
			fprintf (stdout, "%c%c %s%d %s  %.16s  %.2u/%.4zu  %.*s\n",
				 cur, flag, (i + 1) < 10 ? " " : "", i + 1, frombuf,
				 date, info->lines, info->size, subjlen,
				 info->subject ? info->subject : EMPTY_SUBJECT);
			
			g_object_unref (list);
			
			spruce_folder_free_message_info (folder, info);
		}
	}
	
	return cursor == -1 ? first : cursor;
}

#define FLAGS_NEW(flags) (!(flags & (SPRUCE_MESSAGE_ANSWERED | SPRUCE_MESSAGE_DELETED | SPRUCE_MESSAGE_SEEN)))

static int
list_msgs (SpruceFolder *folder, GPtrArray *uids, const char *cmd, int cursor)
{
	SpruceMessageInfo *info;
	int first, last;
	guint32 flags;
	
	if (cmd && cmd[0] >= '1' && cmd[0] <= '9') {
		if ((first = strtol (cmd, NULL, 10) - 1) >= uids->len)
			first = 0;
	} else if (cmd && !strcmp (cmd, "new")) {
		for (first = 0; first < uids->len; first++) {
			if ((info = spruce_folder_get_message_info (folder, uids->pdata[first]))) {
				flags = info->flags;
				spruce_folder_free_message_info (folder, info);
				if (FLAGS_NEW (flags)) {
					cursor = first;
					break;
				}
			}
		}
		
		if (first == uids->len)
			first = 0;
	} else {
		first = 0;
	}
	
	last = first + screenheight - (cmd == NULL ? 3 : 2);
	last = MIN (last, uids->len);
	
	return display_message_list (folder, uids, first, last, cursor);
}

static void
mark_msgs (SpruceFolder *folder, GPtrArray *uids, const char *cmd, int cursor, guint32 flags, guint32 set)
{
	int id, nextid;
	char *p;
	
	if (cmd == NULL) {
		spruce_folder_set_message_flags (folder, uids->pdata[cursor], flags, set);
		return;
	}
	
	do {
		if (!(cmd[0] >= '1' && cmd[0] <= '9'))
			return;
		
		id = strtol (cmd, &p, 10);
		cmd = p;
		
		switch (cmd[0]) {
		case '-':
			cmd++;
			if (!(cmd[0] >= '1' && cmd[0] <= '9'))
				return;
			
			nextid = strtol (cmd, &p, 10);
			cmd = p;
			
			if (!(cmd[0] == ' ' || cmd[0] == '\t' || cmd[0] == '\n'))
				return;
			
			while (id <= nextid && id <= uids->len) {
				spruce_folder_set_message_flags (folder, uids->pdata[id-1], flags, set);
				id++;
			}
			break;
		case ' ':
		case '\t':
		case '\n':
			if (id <= uids->len)
				spruce_folder_set_message_flags (folder, uids->pdata[id-1], flags, set);
			break;
		default:
			return;
		}
		
		while (cmd[0] == ' ' || cmd[0] == '\t')
			cmd++;
	} while (cmd[0] != '\n');
}

static void
delete_msgs (SpruceFolder *folder, GPtrArray *uids, const char *cmd, int cursor)
{
	mark_msgs (folder, uids, cmd, cursor, SPRUCE_MESSAGE_DELETED, SPRUCE_MESSAGE_DELETED);
}

static void
undelete_msgs (SpruceFolder *folder, GPtrArray *uids, const char *cmd, int cursor)
{
	mark_msgs (folder, uids, cmd, cursor, SPRUCE_MESSAGE_DELETED, 0);
}

static void
read_msg (SpruceFolder *folder, GPtrArray *uids, int cursor)
{
	GMimeMessage *message;
	GMimeStream *stream;
	GError *err = NULL;
	
	if (!(message = spruce_folder_get_message (folder, uids->pdata[cursor], &err))) {
		fprintf (stderr, "can't get message: %s\n", err ? err->message : "(null)");
		g_error_free (err);
		return;
	}
	
	stream = g_mime_stream_fs_new (dup (fileno (stdout)));
	g_mime_object_write_to_stream ((GMimeObject *) message, stream);
	g_mime_stream_write (stream, "\n", 1);
	g_mime_stream_flush (stream);
	g_object_unref (message);
	g_object_unref (stream);
	
	mark_msgs (folder, uids, NULL, cursor, SPRUCE_MESSAGE_SEEN, SPRUCE_MESSAGE_SEEN);
}

static void
read_folder (SpruceFolder *folder, const char *uri)
{
	int cursor, quit = 0;
	GPtrArray *uids;
	
	if (!(uids = spruce_folder_get_uids (folder))) {
		fprintf (stdout, "No mail for %s\n", folder->name);
		return;
	}
	
	fprintf (stdout, "Mailx version 0.1 0/0/0. Type ? for help.\n");
	
	cursor = list_msgs (folder, uids, NULL, -1);
	
	do {
		char kbdbuf[256], *cmd;
		
		if (cursor >= uids->len)
			cursor = uids->len - 1;
		
		fputs ("& ", stdout);
		fflush (stdout);
		
		if (!(cmd = fgets (kbdbuf, sizeof (kbdbuf), stdin)))
			break;
		
		while (*cmd == ' ' || *cmd == '\t')
			cmd++;
		
		switch (cmd[0]) {
		case 'q':
		case 'x':
			quit = 1;
			break;
		case '?':
			help ();
			break;
		case 'd':
			cmd++;
			while (*cmd == ' ' || *cmd == '\t')
				cmd++;
			
			delete_msgs (folder, uids, cmd, cursor);
			break;
		case 'u':
			cmd++;
			while (*cmd == ' ' || *cmd == '\t')
				cmd++;
			
			undelete_msgs (folder, uids, cmd, cursor);
			break;
		case 'h':
			cmd++;
			while (*cmd == ' ' || *cmd == '\t')
				cmd++;
			
			cursor = list_msgs (folder, uids, cmd, cursor);
			break;
		case 'n':
			cursor = list_msgs (folder, uids, "new", cursor);
			break;
		default:
			while (*cmd == ' ' || *cmd == '\t')
				cmd++;
			
			if (*cmd >= '1' && *cmd <= '9')
				cursor = strtol (cmd, NULL, 10) - 1;
			
			if (cursor < 0 || cursor >= uids->len)
				cursor = 0;
			
			read_msg (folder, uids, cursor++);
		}
	} while (!quit);
	
	spruce_folder_free_uids (folder, uids);
}

int main (int argc, char **argv)
{
	SpruceFolder *folder = NULL;
	char *sprucedir, *uri, *p;
	SpruceSession *session;
	SpruceStore *store;
	GError *err = NULL;
	SpruceURL *url;
	
	sprucedir = g_build_filename (g_get_home_dir (), ".spruce", NULL);
	spruce_init (sprucedir);
	g_free (sprucedir);
	
	if (argc == 2) {
		if (!(url = spruce_url_new_from_string (argv[1])))
			goto default_mbox;
		
		uri = spruce_url_to_string (url, 0);
	} else {
	default_mbox:
		uri = g_strdup_printf ("mbox://%s", argc > 1 ? argv[1] : getenv ("MAIL"));
		if ((p = strrchr (uri, '/')) == uri + 6) {
			g_free (uri);
			return 0;
		}
		
		/* change mbox:///var/spool/mail/user into mbox:///var/spool/mail#user */
		*p = '#';
		
		url = spruce_url_new_from_string (uri);
	}
	
	session = mailx_session_new ();
	
	if (!(store = spruce_session_get_store (session, uri, &err)))
		goto error;
	
	if (spruce_service_connect ((SpruceService *) store, &err) == -1)
		goto error;
	
	if (!(folder = spruce_store_get_folder_by_url (store, url, &err)))
		goto error;
	
	if (!spruce_folder_exists (folder))
		goto exit;
	
	if (spruce_folder_open (folder, &err) == -1)
		goto error;
	
	read_folder (folder, uri);
	
	if (spruce_folder_close (folder, TRUE, &err) == -1)
		goto error;
	
	if (err != NULL) {
	error:
		fprintf (stderr, "ERROR: %s\n", err ? err->message : "Unknown");
		g_error_free (err);
	}
	
 exit:
	
	g_object_unref (url);
	
	if (folder)
		g_object_unref (folder);
	
	if (store) {
		spruce_service_disconnect ((SpruceService *) store, TRUE, NULL);
		g_object_unref (store);
	}
	
	g_object_unref (session);
	g_free (uri);
	
	spruce_shutdown ();
	
	return 0;
}
