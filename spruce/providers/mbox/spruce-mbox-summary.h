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


#ifndef __SPRUCE_MBOX_SUMMARY_H__
#define __SPRUCE_MBOX_SUMMARY_H__

#include <sys/types.h>

#include <spruce/spruce-folder-summary.h>

G_BEGIN_DECLS

#define SPRUCE_TYPE_MBOX_SUMMARY            (spruce_mbox_summary_get_type ())
#define SPRUCE_MBOX_SUMMARY(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), SPRUCE_TYPE_MBOX_SUMMARY, SpruceMboxSummary))
#define SPRUCE_MBOX_SUMMARY_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), SPRUCE_TYPE_MBOX_SUMMARY, SpruceMboxSummaryClass))
#define SPRUCE_IS_MBOX_SUMMARY(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SPRUCE_TYPE_MBOX_SUMMARY))
#define SPRUCE_IS_MBOX_SUMMARY_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), SPRUCE_TYPE_MBOX_SUMMARY))
#define SPRUCE_MBOX_SUMMARY_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), SPRUCE_TYPE_FOLDER_SUMMARY, SpruceMboxSummaryClass))

typedef struct _SpruceMboxMessageInfo SpruceMboxMessageInfo;
typedef struct _SpruceMboxSummary SpruceMboxSummary;
typedef struct _SpruceMboxSummaryClass SpruceMboxSummaryClass;

struct _SpruceMboxMessageInfo {
	SpruceMessageInfo parent_info;
	
	gint64 frompos;    /* offset of From-line */
	gint64 flagspos;   /* offset of X-Spruce header */
};

struct _SpruceMboxSummary {
	SpruceFolderSummary parent_object;
	
	char *mbox;
	int fd;
};

struct _SpruceMboxSummaryClass {
	SpruceFolderSummaryClass parent_class;
	
};


GType spruce_mbox_summary_get_type (void);

SpruceFolderSummary *spruce_mbox_summary_new (const char *mbox);

void spruce_mbox_summary_set_mbox (SpruceMboxSummary *summary, const char *mbox);

char *spruce_mbox_summary_flags_encode (SpruceMboxMessageInfo *info);
int spruce_mbox_summary_flags_decode (const char *in, char **uid, guint32 *flags);

G_END_DECLS

#endif /* __SPRUCE_MBOX_SUMMARY_H__ */
