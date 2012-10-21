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


#ifndef __SPRUCE_IMAP_FOLDER_H__
#define __SPRUCE_IMAP_FOLDER_H__

#include <spruce/spruce-cache.h>
#include <spruce/spruce-store.h>
#include <spruce/spruce-folder.h>

G_BEGIN_DECLS

#define SPRUCE_TYPE_IMAP_FOLDER            (spruce_imap_folder_get_type ())
#define SPRUCE_IMAP_FOLDER(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), SPRUCE_TYPE_IMAP_FOLDER, SpruceIMAPFolder))
#define SPRUCE_IMAP_FOLDER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), SPRUCE_TYPE_IMAP_FOLDER, SpruceIMAPFolderClass))
#define SPRUCE_IS_IMAP_FOLDER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SPRUCE_TYPE_IMAP_FOLDER))
#define SPRUCE_IS_IMAP_FOLDER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), SPRUCE_TYPE_IMAP_FOLDER))
#define SPRUCE_IMAP_FOLDER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), SPRUCE_TYPE_IMAP_FOLDER, SpruceIMAPFolderClass))

struct _spruce_imap_list_t;

typedef struct _SpruceIMAPFolder SpruceIMAPFolder;
typedef struct _SpruceIMAPFolderClass SpruceIMAPFolderClass;

struct _SpruceIMAPFolder {
	SpruceFolder parent_object;
	
	SpruceCache *cache;
	
	char *cachedir;
	char *utf7_name;
};

struct _SpruceIMAPFolderClass {
	SpruceFolderClass parent_class;
	
};


GType spruce_imap_folder_get_type (void);

SpruceFolder *spruce_imap_folder_new (SpruceStore *store, const char *full_name, gboolean query, GError **err);
SpruceFolder *spruce_imap_folder_new_list (SpruceFolder *parent, struct _spruce_imap_list_t *list);

const char *spruce_imap_folder_utf7_name (SpruceIMAPFolder *folder);

G_END_DECLS

#endif /* __SPRUCE_IMAP_FOLDER_H__ */
