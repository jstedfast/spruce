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


#ifndef __SPRUCE_MAILDIR_SUMMARY_H__
#define __SPRUCE_MAILDIR_SUMMARY_H__

#include <sys/types.h>

#include <spruce/spruce-folder-summary.h>

G_BEGIN_DECLS

#define SPRUCE_TYPE_MAILDIR_SUMMARY            (spruce_maildir_summary_get_type ())
#define SPRUCE_MAILDIR_SUMMARY(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), SPRUCE_TYPE_MAILDIR_SUMMARY, SpruceMaildirSummary))
#define SPRUCE_MAILDIR_SUMMARY_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), SPRUCE_TYPE_MAILDIR_SUMMARY, SpruceMaildirSummaryClass))
#define SPRUCE_IS_MAILDIR_SUMMARY(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SPRUCE_TYPE_MAILDIR_SUMMARY))
#define SPRUCE_IS_MAILDIR_SUMMARY_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), SPRUCE_TYPE_MAILDIR_SUMMARY))
#define SPRUCE_MAILDIR_SUMMARY_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), SPRUCE_TYPE_FOLDER_SUMMARY, SpruceMaildirSummaryClass))

typedef struct _SpruceMaildirSummary SpruceMaildirSummary;
typedef struct _SpruceMaildirSummaryClass SpruceMaildirSummaryClass;


struct _SpruceMaildirSummary {
	SpruceFolderSummary parent_object;
	
	char *maildir;
	int fd;
};

struct _SpruceMaildirSummaryClass {
	SpruceFolderSummaryClass parent_class;
	
};


GType spruce_maildir_summary_get_type (void);

SpruceFolderSummary *spruce_maildir_summary_new (const char *maildir);

void spruce_maildir_summary_set_maildir (SpruceMaildirSummary *summary, const char *maildir);

char *spruce_maildir_summary_flags_encode (SpruceMessageInfo *info);
int spruce_maildir_summary_flags_decode (const char *filename, char **uid, guint32 *flags);

G_END_DECLS

#endif /* __SPRUCE_MAILDIR_SUMMARY_H__ */
