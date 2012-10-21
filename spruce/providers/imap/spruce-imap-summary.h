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


#ifndef __SPRUCE_IMAP_SUMMARY_H__
#define __SPRUCE_IMAP_SUMMARY_H__

#include <sys/types.h>

#include <spruce/spruce-folder.h>
#include <spruce/spruce-folder-summary.h>

G_BEGIN_DECLS

#define SPRUCE_TYPE_IMAP_SUMMARY            (spruce_imap_summary_get_type ())
#define SPRUCE_IMAP_SUMMARY(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), SPRUCE_TYPE_IMAP_SUMMARY, SpruceIMAPSummary))
#define SPRUCE_IMAP_SUMMARY_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), SPRUCE_TYPE_IMAP_SUMMARY, SpruceIMAPSummaryClass))
#define SPRUCE_IS_IMAP_SUMMARY(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SPRUCE_TYPE_IMAP_SUMMARY))
#define SPRUCE_IS_IMAP_SUMMARY_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), SPRUCE_TYPE_IMAP_SUMMARY))
#define SPRUCE_IMAP_SUMMARY_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), SPRUCE_TYPE_FOLDER_SUMMARY, SpruceIMAPSummaryClass))

typedef struct _SpruceIMAPMessageInfo SpruceIMAPMessageInfo;
typedef struct _SpruceIMAPSummary SpruceIMAPSummary;
typedef struct _SpruceIMAPSummaryClass SpruceIMAPSummaryClass;

struct _SpruceIMAPMessageInfo {
	SpruceMessageInfo parent_info;
	
	guint32 server_flags;
};

struct _SpruceIMAPSummary {
	SpruceFolderSummary parent_object;
	
	SpruceFolder *folder;
	
	guint32 exists;
	guint32 recent;
	guint32 unseen;
	
	guint32 uidvalidity;
	
	guint uidvalidity_changed:1;
	guint update_flags:1;
};

struct _SpruceIMAPSummaryClass {
	SpruceFolderSummaryClass parent_class;
	
};


GType spruce_imap_summary_get_type (void);

SpruceFolderSummary *spruce_imap_summary_new (SpruceFolder *folder);

void spruce_imap_summary_set_exists (SpruceFolderSummary *summary, guint32 exists);
void spruce_imap_summary_set_recent (SpruceFolderSummary *summary, guint32 recent);
void spruce_imap_summary_set_unseen (SpruceFolderSummary *summary, guint32 unseen);
void spruce_imap_summary_set_uidnext (SpruceFolderSummary *summary, guint32 uidnext);

void spruce_imap_summary_set_uidvalidity (SpruceFolderSummary *summary, guint32 uidvalidity);

void spruce_imap_summary_expunge (SpruceFolderSummary *summary, int seqid);

int spruce_imap_summary_flush_updates (SpruceFolderSummary *summary, GError **err);

G_END_DECLS

#endif /* __SPRUCE_IMAP_SUMMARY_H__ */
