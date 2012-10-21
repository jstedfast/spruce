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


#ifndef __SPRUCE_FOLDER_SEARCH_H__
#define __SPRUCE_FOLDER_SEARCH_H__

#include <glib.h>

#include <spruce/search.h>
#include <spruce/spruce-folder.h>

G_BEGIN_DECLS

#define SPRUCE_TYPE_FOLDER_SEARCH            (spruce_folder_search_get_type ())
#define SPRUCE_FOLDER_SEARCH(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), SPRUCE_TYPE_FOLDER_SEARCH, SpruceFolderSearch))
#define SPRUCE_FOLDER_SEARCH_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), SPRUCE_TYPE_FOLDER_SEARCH, SpruceFolderSearchClass))
#define SPRUCE_IS_FOLDER_SEARCH(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SPRUCE_TYPE_FOLDER_SEARCH))
#define SPRUCE_IS_FOLDER_SEARCH_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), SPRUCE_TYPE_FOLDER_SEARCH))
#define SPRUCE_FOLDER_SEARCH_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), SPRUCE_TYPE_FOLDER_SEARCH, SpruceFolderSearchClass))

typedef struct _SpruceFolderSearch SpruceFolderSearch;
typedef struct _SpruceFolderSearchClass SpruceFolderSearchClass;

struct _SpruceFolderSearch {
	GObject parent_object;
	
	SearchContext *sexp;
	char *last_search;
	
	SpruceFolder *folder;
	GPtrArray *summary;
	GHashTable *summary_hash;
	SpruceMessageInfo *current;
	SpruceMessageInfo *match1;
	GMimeMessage *message;
};

struct _SpruceFolderSearchClass {
	GObjectClass parent_class;
	
	/* search options */
	SearchResult * (*match_all) (SearchContext *ctx, int argc, SearchTerm **argv,
				     SpruceFolderSearch *s);
	
	SearchResult * (*body_contains) (SearchContext *ctx, int argc, SearchResult **argv,
					 SpruceFolderSearch *s);
	
	SearchResult * (*header_contains) (SearchContext *ctx, int argc, SearchResult **argv,
					   SpruceFolderSearch *s);
	
	SearchResult * (*system_flag) (SearchContext *ctx, int argc, SearchResult **argv,
				       SpruceFolderSearch *s);
	
	SearchResult * (*sent_date) (SearchContext *ctx, int argc, SearchResult **argv,
				     SpruceFolderSearch *s);
	
	SearchResult * (*received_date) (SearchContext *ctx, int argc, SearchResult **argv,
					 SpruceFolderSearch *s);
	
	SearchResult * (*current_date) (SearchContext *ctx, int argc, SearchResult **argv,
					SpruceFolderSearch *s);
	
	SearchResult * (*size) (SearchContext *ctx, int argc, SearchResult **argv,
				SpruceFolderSearch *s);
};


GType spruce_folder_search_get_type (void);

SpruceFolderSearch *spruce_folder_search_new (void);
void spruce_folder_search_construct (SpruceFolderSearch *search);

void spruce_folder_search_set_folder (SpruceFolderSearch *search, SpruceFolder *folder);
void spruce_folder_search_set_summary (SpruceFolderSearch *search, GPtrArray *summary);

GPtrArray *spruce_folder_search_match_all (SpruceFolderSearch *search, const char *expr);

gboolean spruce_folder_search_match1 (SpruceFolderSearch *search, const char *expr,
				      const SpruceMessageInfo *info);

void spruce_folder_search_free_result (SpruceFolderSearch *search, GPtrArray *uids);

G_END_DECLS

#endif /* __SPRUCE_FOLDER_SEARCH_H__ */
