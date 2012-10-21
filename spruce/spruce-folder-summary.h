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


#ifndef __SPRUCE_FOLDER_SUMMARY_H__
#define __SPRUCE_FOLDER_SUMMARY_H__

#include <glib.h>
#include <glib-object.h>

#include <stdio.h>
#include <sys/types.h>
#include <time.h>

#include <gmime/gmime-stream.h>
#include <gmime/gmime-message.h>

G_BEGIN_DECLS

#define SPRUCE_TYPE_FOLDER_SUMMARY            (spruce_folder_summary_get_type ())
#define SPRUCE_FOLDER_SUMMARY(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), SPRUCE_TYPE_FOLDER_SUMMARY, SpruceFolderSummary))
#define SPRUCE_FOLDER_SUMMARY_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), SPRUCE_TYPE_FOLDER_SUMMARY, SpruceFolderSummaryClass))
#define SPRUCE_IS_FOLDER_SUMMARY(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SPRUCE_TYPE_FOLDER_SUMMARY))
#define SPRUCE_IS_FOLDER_SUMMARY_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), SPRUCE_TYPE_FOLDER_SUMMARY))
#define SPRUCE_FOLDER_SUMMARY_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), SPRUCE_TYPE_FOLDER_SUMMARY, SpruceFolderSummaryClass))

typedef struct _SpruceMessageInfo SpruceMessageInfo;
typedef struct _SpruceFolderSummary SpruceFolderSummary;
typedef struct _SpruceFolderSummaryClass SpruceFolderSummaryClass;

enum {
	/* universal system flags */
	SPRUCE_MESSAGE_ANSWERED    = (1 << 0),
	SPRUCE_MESSAGE_DELETED     = (1 << 1),
	SPRUCE_MESSAGE_DRAFT       = (1 << 2),
	SPRUCE_MESSAGE_FLAGGED     = (1 << 3),
	SPRUCE_MESSAGE_RECENT      = (1 << 4),
	SPRUCE_MESSAGE_SEEN        = (1 << 5),
	
	/* extensions (not all providers support these natively) */
	SPRUCE_MESSAGE_FORWARDED   = (1 << 6),
	SPRUCE_MESSAGE_MULTIPART   = (1 << 7),
	SPRUCE_MESSAGE_SIGNED      = (1 << 8),
	SPRUCE_MESSAGE_ENCRYPTED   = (1 << 9),
	SPRUCE_MESSAGE_JUNK        = (1 << 10),
	SPRUCE_MESSAGE_NOTJUNK     = (1 << 11),
	
	/* 'dirty bit' indicates that flags need to be sync'd */
	SPRUCE_MESSAGE_DIRTY       = (1 << 31),
};

/* masks */
#define SPRUCE_MESSAGE_SYSTEM_FLAGS  (0x0000006f)   /* Forwarded is not a system flag */
#define SPRUCE_MESSAGE_USER_FLAGS    (0xfffffe10)   /* Forwarded is a user flag */

/* a summary messageid is a 64 bit identifier (partial md5 hash) */
typedef struct _SpruceSummaryMessageID {
	union {
		guint64 id;
		unsigned char hash[8];
		struct {
			guint32 hi;
			guint32 lo;
		} part;
	} id;
} SpruceSummaryMessageID;

typedef struct _SpruceSummaryReferences {
	guint32 count;
	SpruceSummaryMessageID references[1];
} SpruceSummaryReferences;

typedef struct _SpruceSummaryContentInfo {
	struct _SpruceSummaryContentInfo *next;
	struct _SpruceSummaryContentInfo *parent;
	struct _SpruceSummaryContentInfo *children;
	
	GMimeContentType *content_type;
	char *content_id;
	char *description;
	char *encoding;
	size_t octets;
	size_t lines;
} SpruceSummaryContentInfo;

typedef struct _SpruceFlag {
	struct _SpruceFlag *next;
	char name[1];
} SpruceFlag;

typedef struct _SpruceTag {
	struct _SpruceTag *next;
	char *value;
	char name[1];
} SpruceTag;

struct _SpruceMessageInfo {
	guint32 ref_count;
	
	char *sender;
	char *from;
	char *reply_to;
	char *to;
	char *cc;
	char *bcc;
	char *subject;
	time_t date_sent;
	time_t date_received;
	
	char *uid;
	
	SpruceSummaryMessageID message_id;
	SpruceSummaryReferences *references;
	
	guint32 flags;
	
	size_t size;
	guint32 lines;
	
	SpruceFlag *user_flags;
	SpruceTag *user_tags;
	
	/*SpruceSummaryContentInfo *content;*/
};

struct _SpruceFolderSummary {
	GObject parent_object;
	
	struct _SpruceFolderSummaryPrivate *priv;
	
	guint dirty:1;
	guint loaded:1;
	
	guint32 version;           /* version info */
	guint32 flags;             /* supported flags */
	guint32 nextuid;           /* next available uid */
	
	guint32 count;             /* current count of message-info's */
	
	guint32 unread;            /* saved unread count */
	guint32 deleted;           /* saved deleted count */
	
	time_t timestamp;          /* timestamp of summary file */
	
	size_t message_info_size;  /* sizeof() virtual SpruceMessageInfo struct */
	
	GPtrArray *messages;       /* array of SpruceMessageInfo's */
	GHashTable *messages_hash; /* hash lookup of SpruceMessageInfo's by UID */
};

struct _SpruceFolderSummaryClass {
	GObjectClass parent_class;
	
	/* load/save the global info */
	int (* header_load)  (SpruceFolderSummary *summary, GMimeStream *stream);
	int (* header_save)  (SpruceFolderSummary *summary, GMimeStream *stream);
	
	int (* summary_load) (SpruceFolderSummary *summary);
	int (* summary_save) (SpruceFolderSummary *summary);
	int (* summary_unload) (SpruceFolderSummary *summary);
	
	void (* add) (SpruceFolderSummary *summary, SpruceMessageInfo *info);
	
	/* create/save/load an individual message info */
	SpruceMessageInfo * (* message_info_new) (SpruceFolderSummary *summary);
	SpruceMessageInfo * (* message_info_new_from_message) (SpruceFolderSummary *summary,
							       GMimeMessage *message);
	SpruceMessageInfo * (* message_info_load) (SpruceFolderSummary *summary, GMimeStream *stream);
	int		    (* message_info_save) (SpruceFolderSummary *summary, GMimeStream *stream,
						   SpruceMessageInfo *info);
	void		    (* message_info_free) (SpruceFolderSummary *summary, SpruceMessageInfo *info);
	
	/* get the next uid */
	char * (* next_uid_string) (SpruceFolderSummary *summary);
};


GType spruce_folder_summary_get_type (void);

SpruceFolderSummary *spruce_folder_summary_new (void);

const char *spruce_folder_summary_get_filename (SpruceFolderSummary *summary);
void spruce_folder_summary_set_filename (SpruceFolderSummary *summary, const char *filename);

guint32 spruce_folder_summary_next_uid (SpruceFolderSummary *summary);
char *spruce_folder_summary_uid_string (SpruceFolderSummary *summary);

/* only load the summary header */
int spruce_folder_summary_header_load (SpruceFolderSummary *summary);

/* only save the summary header */
int spruce_folder_summary_header_save (SpruceFolderSummary *summary);

/* load/save the summary */
int spruce_folder_summary_load (SpruceFolderSummary *summary);
int spruce_folder_summary_save (SpruceFolderSummary *summary);
int spruce_folder_summary_unload (SpruceFolderSummary *summary);
int spruce_folder_summary_reload (SpruceFolderSummary *summary);

/* set the dirty bit on the summary */
void spruce_folder_summary_touch (SpruceFolderSummary *summary);

/* add a new raw summary item */
void spruce_folder_summary_add (SpruceFolderSummary *summary, SpruceMessageInfo *info);

/* create a new message-info */
SpruceMessageInfo *spruce_folder_summary_info_new (SpruceFolderSummary *summary);
SpruceMessageInfo *spruce_folder_summary_info_new_from_message (SpruceFolderSummary *summary,
								GMimeMessage *message);

void spruce_folder_summary_info_ref (SpruceFolderSummary *summary, SpruceMessageInfo *info);
void spruce_folder_summary_info_unref (SpruceFolderSummary *summary, SpruceMessageInfo *info);

/* removes a summary item, doesn't fix content offsets */
void spruce_folder_summary_remove (SpruceFolderSummary *summary, SpruceMessageInfo *info);
void spruce_folder_summary_remove_uid (SpruceFolderSummary *summary, const char *uid);
void spruce_folder_summary_remove_index (SpruceFolderSummary *summary, int index);

/* remove all items */
void spruce_folder_summary_clear (SpruceFolderSummary *summary);

/* lookup functions */
int spruce_folder_summary_count (SpruceFolderSummary *summary);

SpruceMessageInfo *spruce_folder_summary_uid (SpruceFolderSummary *summary, const char *uid);
SpruceMessageInfo *spruce_folder_summary_index (SpruceFolderSummary *summary, int index);


/* message flag operations */
gboolean spruce_flag_get (SpruceFlag **list, const char *name);
gboolean spruce_flag_set (SpruceFlag **list, const char *name, gboolean state);
gboolean spruce_flag_list_copy (SpruceFlag **to, SpruceFlag **from);
int spruce_flag_list_size (SpruceFlag **list);
void spruce_flag_list_free (SpruceFlag **list);

guint32 spruce_system_flag (const char *name);
gboolean spruce_system_flag_is_set (guint32 flags, const char *name);

/* message tag operations */
const char *spruce_tag_get (SpruceTag **list, const char *name);
gboolean spruce_tag_set (SpruceTag **list, const char *name, const char *value);
gboolean spruce_tag_list_copy (SpruceTag **to, SpruceTag **from);
int spruce_tag_list_size (SpruceTag **list);
void spruce_tag_list_free (SpruceTag **list);

G_END_DECLS

#endif /* __SPRUCE_FOLDER_SUMMARY_H__ */
