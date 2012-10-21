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
#include <unistd.h>
#include <limits.h>
#include <utime.h>
#include <fcntl.h>
#include <ctype.h>

#include <gmime/gmime.h>
#include <spruce/spruce-file-utils.h>

#include "spruce-mbox-summary.h"


#define MBOX_SUMMARY_VERSION  1

static void spruce_mbox_summary_class_init (SpruceMboxSummaryClass *klass);
static void spruce_mbox_summary_init (SpruceMboxSummary *summary, SpruceMboxSummaryClass *klass);
static void spruce_mbox_summary_finalize (GObject *object);

static int mbox_header_load (SpruceFolderSummary *summary, GMimeStream *stream);
static int mbox_summary_load (SpruceFolderSummary *summary);
static int mbox_summary_save (SpruceFolderSummary *summary);
static SpruceMessageInfo *mbox_message_info_new (SpruceFolderSummary *summary);
static SpruceMessageInfo *mbox_message_info_load (SpruceFolderSummary *summary, GMimeStream *stream);
static int mbox_message_info_save (SpruceFolderSummary *summary, GMimeStream *stream, SpruceMessageInfo *info);


static SpruceFolderSummaryClass *parent_class = NULL;


GType
spruce_mbox_summary_get_type (void)
{
	static GType type = 0;
	
	if (!type) {
		static const GTypeInfo info = {
			sizeof (SpruceMboxSummaryClass),
			NULL, /* base_class_init */
			NULL, /* base_class_finalize */
			(GClassInitFunc) spruce_mbox_summary_class_init,
			NULL, /* class_finalize */
			NULL, /* class_data */
			sizeof (SpruceMboxSummary),
			0,    /* n_preallocs */
			(GInstanceInitFunc) spruce_mbox_summary_init,
		};
		
		type = g_type_register_static (SPRUCE_TYPE_FOLDER_SUMMARY, "SpruceMboxSummary", &info, 0);
	}
	
	return type;
}


static void
spruce_mbox_summary_class_init (SpruceMboxSummaryClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	SpruceFolderSummaryClass *summary_class = SPRUCE_FOLDER_SUMMARY_CLASS (klass);
	
	parent_class = g_type_class_ref (SPRUCE_TYPE_FOLDER_SUMMARY);
	
	object_class->finalize = spruce_mbox_summary_finalize;
	
	summary_class->header_load = mbox_header_load;
	summary_class->summary_load = mbox_summary_load;
	summary_class->summary_save = mbox_summary_save;
	summary_class->message_info_new = mbox_message_info_new;
	summary_class->message_info_load = mbox_message_info_load;
	summary_class->message_info_save = mbox_message_info_save;
}

static void
spruce_mbox_summary_init (SpruceMboxSummary *summary, SpruceMboxSummaryClass *klass)
{
	SpruceFolderSummary *folder_summary = (SpruceFolderSummary *) summary;
	
	folder_summary->version += MBOX_SUMMARY_VERSION;
	folder_summary->flags = SPRUCE_MESSAGE_ANSWERED | SPRUCE_MESSAGE_DELETED |
		SPRUCE_MESSAGE_DRAFT | SPRUCE_MESSAGE_FLAGGED | SPRUCE_MESSAGE_SEEN;
	
	folder_summary->message_info_size = sizeof (SpruceMboxMessageInfo);
	
	summary->mbox = NULL;
	summary->fd = -1;
}

static void
spruce_mbox_summary_finalize (GObject *object)
{
	SpruceMboxSummary *summary = (SpruceMboxSummary *) object;
	
	g_free (summary->mbox);
	if (summary->fd != -1)
		close (summary->fd);
	
	G_OBJECT_CLASS (parent_class)->finalize (object);
}


SpruceFolderSummary *
spruce_mbox_summary_new (const char *mbox)
{
	SpruceFolderSummary *summary;
	
	summary = g_object_new (SPRUCE_TYPE_MBOX_SUMMARY, NULL, NULL);
	((SpruceMboxSummary *) summary)->mbox = g_strdup (mbox);
	
	return summary;
}


void
spruce_mbox_summary_set_mbox (SpruceMboxSummary *summary, const char *mbox)
{
	g_free (summary->mbox);
	summary->mbox = g_strdup (mbox);
}


char *
spruce_mbox_summary_flags_encode (SpruceMboxMessageInfo *minfo)
{
	SpruceMessageInfo *info = (SpruceMessageInfo *) minfo;
	const unsigned char *uidstr, *p;
	guint32 uid = 0;
	
	p = uidstr = (unsigned char *) info->uid;
	while (*p && isdigit ((int) *p) && uid < (UINT_MAX / 10)) {
		uid = (uid * 10) + (*p - '0');
		p++;
	}
	
	if (*p == '\0') 
		return g_strdup_printf ("%08x-%04x", uid, info->flags & 0xffff);
	else
		return g_strdup_printf ("%s-%04x", uidstr, info->flags & 0xffff);
}


int
spruce_mbox_summary_flags_decode (const char *in, char **uidstr, guint32 *flags)
{
	const char *inptr;
	guint32 uid = 0;
	
	uid = strtoul (in, (char **) &inptr, 16);
	
	if (*inptr == '-') {
		*uidstr = g_strdup_printf ("%u", uid);
	} else {
		if (!(inptr = strchr (inptr, '-')))
			return -1;
		
		*uidstr = g_strndup (in, inptr - in);
		g_strstrip (*uidstr);
	}
	
	inptr++;
	
	*flags = strtoul (inptr, NULL, 16);
	
	return 0;
}


static int
mbox_header_load (SpruceFolderSummary *summary, GMimeStream *stream)
{
	SpruceMboxSummary *mbox = (SpruceMboxSummary *) summary;
	struct stat st;
	
	if (stat (mbox->mbox, &st) == -1 || !S_ISREG (st.st_mode))
		return -1;
	
	if (SPRUCE_FOLDER_SUMMARY_CLASS (parent_class)->header_load (summary, stream) == -1)
		return -1;
	
	if (st.st_mtime > summary->timestamp)
		return -1;
	
	return 0;
}


struct status {
	gint64 offset;
	guint32 flags;
	char *uid;
};

static struct {
	char tag;
	guint32 flag;
} status_flags[] = {
	{ 'F', SPRUCE_MESSAGE_FLAGGED  },
	{ 'A', SPRUCE_MESSAGE_ANSWERED },
	{ 'D', SPRUCE_MESSAGE_DELETED  },
	{ 'R', SPRUCE_MESSAGE_SEEN     },
};

static int num_status_flags = sizeof (status_flags) / sizeof (status_flags[0]);

static guint32
decode_status (const char *status)
{
	guint32 flags = 0;
	const char *p;
	int i;
	
	p = status;
	while (*p) {
		for (i = 0; i < num_status_flags; i++) {
			if (*p == status_flags[i].tag)
				flags |= status_flags[i].flag;
		}
		
		p++;
	}
	
	return flags;
}

static void
parser_got_status (GMimeParser *parser, const char *header, const char *value,
		   gint64 offset, gpointer user_data)
{
	struct status *status = user_data;
	guint32 flags;
	char *uid;
	
	if (!g_ascii_strcasecmp (header, "X-Spruce")) {
		status->offset = offset;
		
		if (spruce_mbox_summary_flags_decode (value, &uid, &flags) != -1) {
			status->flags = flags;
			status->uid = uid;
		}
	} else {
		if (!status->flags)
			status->flags = decode_status (value);
	}
}

static int
mbox_summary_load (SpruceFolderSummary *summary)
{
	SpruceMboxSummary *mbox_summary = (SpruceMboxSummary *) summary;
	SpruceMessageInfo *info;
	GMimeMessage *message;
	struct status status;
	GMimeParser *parser;
	GMimeStream *stream;
	gint64 offset;
	size_t n = 0;
	int fd = -1;
	
	if (!mbox_summary->mbox) {
		errno = ENOENT;
		return -1;
	}
	
	if ((fd = open (mbox_summary->mbox, O_LARGEFILE | O_RDONLY)) == -1)
		return -1;
	
	stream = g_mime_stream_fs_new (fd);
	parser = g_mime_parser_new ();
	g_mime_parser_init_with_stream (parser, stream);
	g_object_unref (stream);
	
	g_mime_parser_set_scan_from (parser, TRUE);
	g_mime_parser_set_header_regex (parser, "^X-Spruce$|^Status$|^X-Status$",
					parser_got_status, &status);
	
	while (!g_mime_parser_eos (parser)) {
		status.offset = -1;
		status.flags = 0;
		status.uid = NULL;
		
		if (!(message = g_mime_parser_construct_message (parser)))
			goto fail;
		
		info = spruce_folder_summary_info_new_from_message (summary, message);
		g_object_unref (message);
		if (info == NULL)
			goto fail;
		
		info->uid = status.uid;
		info->flags |= status.flags;
		
		if ((offset = g_mime_parser_get_from_offset (parser)) == -1)
			goto fail;
		
		if (n == 0 && offset != 0)
			goto fail;
		
		((SpruceMboxMessageInfo *) info)->frompos = offset;
		((SpruceMboxMessageInfo *) info)->flagspos = status.offset;
		
		spruce_folder_summary_add (summary, info);
		n++;
	}
	
	g_object_unref (parser);
	
	return 0;
	
 fail:
	
	g_object_unref (parser);
	
	spruce_folder_summary_clear (summary);
	
	return -1;
}

static int
mbox_summary_save (SpruceFolderSummary *summary)
{
	SpruceMboxSummary *mbox_summary = (SpruceMboxSummary *) summary;
	struct utimbuf mtime;
	int ret;
	
	if (mbox_summary->mbox) {
		mbox_summary->fd = open (mbox_summary->mbox, O_LARGEFILE | O_WRONLY, 0666);
		/* FIXME: lock the mbox file */
	}
	
	ret = SPRUCE_FOLDER_SUMMARY_CLASS (parent_class)->summary_save (summary);
	
	if (mbox_summary->fd != -1) {
		fsync (mbox_summary->fd);
		
		mtime.actime = summary->timestamp;
		mtime.modtime = summary->timestamp;
		
		utime (mbox_summary->mbox, &mtime);
		
		/* FIXME: unlock mbox file */
		
		close (mbox_summary->fd);
		mbox_summary->fd = -1;
	}
	
	return ret;
}

static SpruceMessageInfo *
mbox_message_info_new (SpruceFolderSummary *summary)
{
	SpruceMessageInfo *info;
	
	info = SPRUCE_FOLDER_SUMMARY_CLASS (parent_class)->message_info_new (summary);
	
	((SpruceMboxMessageInfo *) info)->frompos = -1;
	((SpruceMboxMessageInfo *) info)->flagspos = -1;
	
	return info;
}

static SpruceMessageInfo *
mbox_message_info_load (SpruceFolderSummary *summary, GMimeStream *stream)
{
	SpruceMboxMessageInfo *minfo;
	SpruceMessageInfo *info;
	
	if (!(info = SPRUCE_FOLDER_SUMMARY_CLASS (parent_class)->message_info_load (summary, stream)))
		return NULL;
	
	minfo = (SpruceMboxMessageInfo *) info;
	
	if (spruce_file_util_decode_int64 (stream, &minfo->frompos) == -1)
		goto exception;
	
	if (spruce_file_util_decode_int64 (stream, &minfo->flagspos) == -1)
		goto exception;
	
	return info;
	
 exception:
	
	spruce_folder_summary_info_unref (summary, info);
	
	return NULL;
}

static int
mbox_message_info_save (SpruceFolderSummary *summary, GMimeStream *stream, SpruceMessageInfo *info)
{
	SpruceMboxSummary *mbox_summary = (SpruceMboxSummary *) summary;
	SpruceMboxMessageInfo *minfo = (SpruceMboxMessageInfo *) info;
	gint64 offset = minfo->flagspos;
	char *flags;
	int fd;
	
	if (SPRUCE_FOLDER_SUMMARY_CLASS (parent_class)->message_info_save (summary, stream, info) == -1)
		return -1;
	
	if (spruce_file_util_encode_int64 (stream, minfo->frompos) == -1)
		return -1;
	
	if (spruce_file_util_encode_int64 (stream, minfo->flagspos) == -1)
		return -1;
	
	if (offset != -1 && mbox_summary->fd != -1) {
		/* attempt to sync the flags to the mbox file too */
		
		offset += strlen ("X-Spruce: ");
		fd = mbox_summary->fd;
		
		if (lseek (fd, (off_t) offset, SEEK_SET) != -1) {
			flags = spruce_mbox_summary_flags_encode (minfo);
			spruce_write (fd, flags, strlen (flags));
			g_free (flags);
		}
	}
	
	return 0;
}
