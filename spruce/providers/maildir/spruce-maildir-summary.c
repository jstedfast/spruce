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

#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <limits.h>
#include <unistd.h>
#include <utime.h>
#include <fcntl.h>
#include <ctype.h>

#include <gmime/gmime.h>
#include <spruce/spruce-file-utils.h>

#include "spruce-maildir-summary.h"
#include "spruce-maildir-utils.h"

#define MAILDIR_SUMMARY_VERSION  1

static void spruce_maildir_summary_class_init (SpruceMaildirSummaryClass *klass);
static void spruce_maildir_summary_init (SpruceMaildirSummary *summary, SpruceMaildirSummaryClass *klass);
static void spruce_maildir_summary_finalize (GObject *object);

static int maildir_header_load (SpruceFolderSummary *summary, GMimeStream *stream);
static int maildir_summary_load (SpruceFolderSummary *summary);
static int maildir_summary_save (SpruceFolderSummary *summary);


static SpruceFolderSummaryClass *parent_class = NULL;


GType
spruce_maildir_summary_get_type (void)
{
	static GType type = 0;
	
	if (!type) {
		static const GTypeInfo info = {
			sizeof (SpruceMaildirSummaryClass),
			NULL, /* base_class_init */
			NULL, /* base_class_finalize */
			(GClassInitFunc) spruce_maildir_summary_class_init,
			NULL, /* class_finalize */
			NULL, /* class_data */
			sizeof (SpruceMaildirSummary),
			0,    /* n_preallocs */
			(GInstanceInitFunc) spruce_maildir_summary_init,
		};
		
		type = g_type_register_static (G_TYPE_OBJECT, "SpruceMaildirSummary", &info, 0);
	}
	
	return type;
}


static void
spruce_maildir_summary_class_init (SpruceMaildirSummaryClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	SpruceFolderSummaryClass *summary_class = SPRUCE_FOLDER_SUMMARY_CLASS (klass);
	
	parent_class = g_type_class_ref (SPRUCE_TYPE_FOLDER_SUMMARY);
	
	object_class->finalize = spruce_maildir_summary_finalize;
	
	summary_class->header_load = maildir_header_load;
	summary_class->summary_load = maildir_summary_load;
	summary_class->summary_save = maildir_summary_save;
}

static void
spruce_maildir_summary_init (SpruceMaildirSummary *summary, SpruceMaildirSummaryClass *klass)
{
	SpruceFolderSummary *folder_summary = (SpruceFolderSummary *) summary;
	
	folder_summary->version += MAILDIR_SUMMARY_VERSION;
	folder_summary->flags = SPRUCE_MESSAGE_ANSWERED | SPRUCE_MESSAGE_DELETED |
		SPRUCE_MESSAGE_DRAFT | SPRUCE_MESSAGE_FLAGGED |
		SPRUCE_MESSAGE_SEEN | SPRUCE_MESSAGE_RECENT;
	
	folder_summary->message_info_size = sizeof (SpruceMessageInfo);
	
	summary->maildir = NULL;
	summary->fd = -1;
}

static void
spruce_maildir_summary_finalize (GObject *object)
{
	SpruceMaildirSummary *summary = (SpruceMaildirSummary *) object;
	
	g_free (summary->maildir);
	if (summary->fd != -1)
		close (summary->fd);
	
	G_OBJECT_CLASS (parent_class)->finalize (object);
}


SpruceFolderSummary *
spruce_maildir_summary_new (const char *maildir)
{
	SpruceFolderSummary *summary;
	
	summary = g_object_new (SPRUCE_TYPE_MAILDIR_SUMMARY, NULL, NULL);
	((SpruceMaildirSummary *) summary)->maildir = g_strdup (maildir);
	
	return summary;
}


void
spruce_maildir_summary_set_maildir (SpruceMaildirSummary *summary, const char *maildir)
{
	g_free (summary->maildir);
	summary->maildir = g_strdup (maildir);
}


static int
maildir_header_load (SpruceFolderSummary *summary, GMimeStream *stream)
{
	SpruceMaildirSummary *msummary = (SpruceMaildirSummary *) summary;
	struct stat st;
	
	if (stat (msummary->maildir, &st) == -1 || !S_ISDIR (st.st_mode))
		return -1;
	
	if (SPRUCE_FOLDER_SUMMARY_CLASS (parent_class)->header_load (summary, stream) == -1)
		return -1;
	
	if (st.st_mtime > summary->timestamp)
		return -1;
	
	return 0;
}


typedef struct {
	char tag;
	guint32 flag;
} maildir_flags_t;

/* I made this one up, I can't actually find the definition of these flags anywhere */
static maildir_flags_t maildir1_flags[] = {
	{ 'R', SPRUCE_MESSAGE_ANSWERED  },
	{ 'T', SPRUCE_MESSAGE_DELETED   },
	{ 'D', SPRUCE_MESSAGE_DRAFT     },
	{ 'F', SPRUCE_MESSAGE_FLAGGED   },
	{ 'P', SPRUCE_MESSAGE_FORWARDED },
	{ 'S', SPRUCE_MESSAGE_SEEN      },
};

static int num_maildir1_flags = sizeof (maildir1_flags) / sizeof (maildir1_flags[0]);

/* this table can be found at http://cr.yp.to/proto/maildir.html */
static maildir_flags_t maildir2_flags[] = {
	{ 'R', SPRUCE_MESSAGE_ANSWERED  },
	{ 'T', SPRUCE_MESSAGE_DELETED   },
	{ 'D', SPRUCE_MESSAGE_DRAFT     },
	{ 'F', SPRUCE_MESSAGE_FLAGGED   },
	{ 'P', SPRUCE_MESSAGE_FORWARDED },
	{ 'S', SPRUCE_MESSAGE_SEEN      },
};

static int num_maildir2_flags = sizeof (maildir2_flags) / sizeof (maildir2_flags[0]);

char *
spruce_maildir_summary_flags_encode (SpruceMessageInfo *info)
{
	char *flags, *p;
	int i;
	
	/* Note: we only ever encode using the "2," format */
	
	p = flags = g_malloc (num_maildir2_flags + 1);
	for (i = 0; i < num_maildir2_flags; i++) {
		if (info->flags & maildir2_flags[i].flag)
			*p++ = maildir2_flags[i].tag;
	}
	
	*p = '\0';
	
	return flags;
}


int
spruce_maildir_summary_flags_decode (const char *filename, char **uid, guint32 *flags)
{
	const maildir_flags_t *mflags;
	register const char *inptr;
	const char *colon;
	int max, i;
	
	*flags = 0;
	
	if ((colon = strrchr (filename, ':'))) {
		if (uid)
			*uid = g_strndup (filename, colon - filename);
		
		inptr = colon + 1;
	} else {
		if (uid) {
			/* the filename does not contain any flags? */
			*uid = g_strdup (filename);
			return 0;
		}
		
		/* apparently our caller is not expecting a UID and so
		   presume he just wants us to parse the flags */
		inptr = filename;
	}
	
	if (*inptr >= '0' && *inptr <= '9') {
		if (*inptr == '1') {
			/* we know this is a valid value and will eventually support it */
			mflags = maildir1_flags;
			max = num_maildir1_flags;
		} else if (*inptr == '2') {
			/* this one we got covered... */
			mflags = maildir2_flags;
			max = num_maildir2_flags;
		} else {
			/* some new maildir flags format? */
			g_warning ("Unknown Maildir flags format: %s", inptr);
			return 0;
		}
		
		while (*inptr && *inptr != ',')
			inptr++;
		
		inptr++;
	} else if (*inptr != '\0') {
		/* some broken format? */
		g_warning ("Unknown Maildir info format: %s", inptr);
		if (uid)
			g_free (*uid);
		
		return -1;
	}
	
	while (*inptr) {
		for (i = 0; i < max; i++) {
			if (*inptr == mflags[i].tag) {
				*flags |= mflags[i].flag;
				break;
			}
		}
		
		inptr++;
	}
	
	return 0;
}


static int
maildir_summary_load_cb (const char *maildir, const char *subdir,
			 const char *d_name, void *user_data)
{
	SpruceFolderSummary *summary = user_data;
	SpruceMessageInfo *info;
	GMimeMessage *message;
	GMimeParser *parser;
	GMimeStream *stream;
	char *filename;
	guint32 flags;
	char *uid;
	int fd;
	
	if (spruce_maildir_summary_flags_decode (d_name, &uid, &flags) == -1)
		return -1;
	
	filename = g_strdup_printf ("%s/%s/%s", maildir, subdir, d_name);
	if ((fd = open (filename, O_RDONLY)) == -1) {
		/* ignore and continue?? */
		g_warning ("Failed loading Maildir summary info for %s: %s",
			   filename, g_strerror (errno));
		g_free (filename);
		g_free (uid);
		return 1;
	}
	
	stream = g_mime_stream_fs_new (fd);
	parser = g_mime_parser_new ();
	g_mime_parser_init_with_stream (parser, stream);
	g_object_unref (stream);
	
	if (!(message = g_mime_parser_construct_message (parser))) {
		/* ignore and continue?? */
		g_warning ("Failed loading Maildir summary info for %s: %s",
			   filename, "parse error");
		g_object_unref (parser);
		g_free (filename);
		g_free (uid);
		return 1;
	}
	
	g_object_unref (parser);
	g_free (filename);
	
	info = spruce_folder_summary_info_new_from_message (summary, message);
	g_object_unref (message);
	if (info == NULL) {
		g_free (uid);
		return -1;
	}
	
	info->uid = uid;
	info->flags |= flags;
	
	if (!strcmp (subdir, "new")) {
		/* all messages in the new/ subdir are \Recent */
		info->flags |= SPRUCE_MESSAGE_RECENT;
	}
	
	spruce_folder_summary_add (summary, info);
	
	return 1;
}

static int
maildir_summary_load (SpruceFolderSummary *summary)
{
	SpruceMaildirSummary *maildir_summary = (SpruceMaildirSummary *) summary;
	
	if (maildir_foreach (maildir_summary->maildir, maildir_summary_load_cb, summary) == -1) {
		spruce_folder_summary_clear (summary);
		
		return -1;
	}
	
	return 0;
}


static int
maildir_summary_save_cb (const char *maildir, const char *subdir, const char *d_name, void *user_data)
{
	SpruceFolderSummary *summary = user_data;
	SpruceMessageInfo *info;
	guint32 flags;
	char *uid;
	int ret = 1;
	
	/* if we get to tmp/, we've gone too far */
	if (!strcmp (subdir, "tmp"))
		return 0;
	
	if (spruce_maildir_summary_flags_decode (d_name, &uid, &flags) == -1) {
		/* *shrug* just ignore it and continue? */
		return 1;
	}
	
	if ((info = spruce_folder_summary_uid (summary, uid))) {
		char *oldname, *newname, *str;
		gboolean sync_flags = FALSE;
		
		/* if the flags are not identical... */
		if (flags != (info->flags & ~SPRUCE_MESSAGE_DIRTY)) {
			/* and our flags are dirty... */
			if (info->flags & SPRUCE_MESSAGE_DIRTY) {
				/* sync our flags to disk */
				sync_flags = TRUE;
			} else {
				/* update our flags */
				info->flags = flags;
			}
		}
		
		if (sync_flags) {
			str = spruce_maildir_summary_flags_encode (info);
			oldname = g_strdup_printf ("%s/%s/%s", maildir, subdir, d_name);
			newname = g_strdup_printf ("%s/%s/%s:2,%s", maildir, subdir, info->uid, str);
			g_free (str);
			
			/* rename the message file to reflect the new flags */
			if (rename (oldname, newname) == 0)
				ret = 1;
			
			g_free (oldname);
			g_free (newname);
		}
		
		/* clear the dirty bit */
		if (ret != -1)
			info->flags &= ~SPRUCE_MESSAGE_DIRTY;
	} else {
		/* our summary seems to not know about this message,
		   may have been delivered by another client */
		ret = maildir_summary_load_cb (maildir, subdir, d_name, user_data);
	}
	
	g_free (uid);
	
	return ret;
}

static int
maildir_summary_save (SpruceFolderSummary *summary)
{
	SpruceMaildirSummary *msummary = (SpruceMaildirSummary *) summary;
	struct utimbuf mtime;
	int ret;
	
	/* sync flags the Maildir way (tm) and load any newly
	   delivered messages that we don't know about */
	
	if (maildir_foreach (msummary->maildir, maildir_summary_save_cb, summary) == -1)
		return -1;
	
	ret = SPRUCE_FOLDER_SUMMARY_CLASS (parent_class)->summary_save (summary);
	
	mtime.actime = summary->timestamp;
	mtime.modtime = summary->timestamp;
	utime (msummary->maildir, &mtime);
	
	return ret;
}
