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

#include <string.h>
#include <time.h>

#include <glib/gi18n.h>

#include "spruce-folder-search.h"


static void spruce_folder_search_class_init (SpruceFolderSearchClass *klass);
static void spruce_folder_search_init (SpruceFolderSearch *search, SpruceFolderSearchClass *klass);
static void spruce_folder_search_finalize (GObject *object);


static SearchResult *match_all (SearchContext *ctx, int argc, SearchTerm **argv,
				SpruceFolderSearch *s);

static SearchResult *body_contains (SearchContext *ctx, int argc, SearchResult **argv,
				    SpruceFolderSearch *s);

static SearchResult *header_contains (SearchContext *ctx, int argc, SearchResult **argv,
				      SpruceFolderSearch *s);

static SearchResult *system_flag (SearchContext *ctx, int argc, SearchResult **argv,
				  SpruceFolderSearch *s);

static SearchResult *sent_date (SearchContext *ctx, int argc, SearchResult **argv,
				SpruceFolderSearch *s);

static SearchResult *received_date (SearchContext *ctx, int argc, SearchResult **argv,
				    SpruceFolderSearch *s);

static SearchResult *current_date (SearchContext *ctx, int argc, SearchResult **argv,
				   SpruceFolderSearch *s);

static SearchResult *size (SearchContext *ctx, int argc, SearchResult **argv,
			   SpruceFolderSearch *s);


static GObjectClass *parent_class = NULL;


GType
spruce_folder_search_get_type (void)
{
	static GType type = 0;
	
	if (!type) {
		static const GTypeInfo info = {
			sizeof (SpruceFolderSearchClass),
			NULL, /* base_class_init */
			NULL, /* base_class_finalize */
			(GClassInitFunc) spruce_folder_search_class_init,
			NULL, /* class_finalize */
			NULL, /* class_data */
			sizeof (SpruceFolderSearch),
			0,    /* n_preallocs */
			(GInstanceInitFunc) spruce_folder_search_init,
		};
		
		type = g_type_register_static (G_TYPE_OBJECT, "SpruceFolderSearch", &info, 0);
	}
	
	return type;
}


static void
spruce_folder_search_class_init (SpruceFolderSearchClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	
	parent_class = g_type_class_ref (G_TYPE_OBJECT);
	
	object_class->finalize = spruce_folder_search_finalize;
	
	klass->match_all = match_all;
	klass->body_contains = body_contains;
	klass->header_contains = header_contains;
	klass->system_flag = system_flag;
	klass->sent_date = sent_date;
	klass->received_date = received_date;
	klass->current_date = current_date;
	klass->size = size;
}

static void
spruce_folder_search_init (SpruceFolderSearch *search, SpruceFolderSearchClass *klass)
{
	search->sexp = NULL;
	search->last_search = NULL;
	search->folder = NULL;
	search->summary = NULL;
	search->summary_hash = NULL;
	search->current = NULL;
	search->match1 = NULL;
	search->message = NULL;
}

static void
spruce_folder_search_finalize (GObject *object)
{
	SpruceFolderSearch *search = (SpruceFolderSearch *) object;
	
	if (search->sexp)
		search_context_unref (search->sexp);
	
	g_free (search->last_search);
	
	if (search->folder)
		g_object_unref (search->folder);
	
	G_OBJECT_CLASS (parent_class)->finalize (object);
}


static SearchResult *
match_all (SearchContext *ctx, int argc, SearchTerm **argv, SpruceFolderSearch *s)
{
	SearchResult *res;
	GPtrArray *uids;
	int i;
	
	if (argc != 1)
		search_context_throw (ctx, _("Incorrect argument count in (match-all )"));
	
	uids = g_ptr_array_new ();
	for (i = 0; i < s->summary->len; i++) {
		s->current = s->summary->pdata[i];
		res = search_term_eval (ctx, argv[0], s);
		if (res->type == SEARCH_RESULT_BOOL && res->value.bool)
			g_ptr_array_add (uids, s->current->uid);
		search_result_free (res);
		if (s->message) {
			g_object_unref (s->message);
			s->message = NULL;
		}
	}
	
	res = search_result_new (SEARCH_RESULT_ARRAY);
	res->value.array = uids;
	
	return res;
}

static SearchResult *
body_contains (SearchContext *ctx, int argc, SearchResult **argv, SpruceFolderSearch *s)
{
	SearchResult *res;
	
	/* FIXME: implement me... */
	res = search_result_new (SEARCH_RESULT_BOOL);
	res->value.bool = FALSE;
	
	return res;
}

static SearchResult *
header_contains (SearchContext *ctx, int argc, SearchResult **argv, SpruceFolderSearch *s)
{
	SearchResult *res;
	const char *header;
	const char *match;
	
	if (argc != 2)
		search_context_throw (ctx, _("Incorrect argument count in (header-contains )"));
	
	if (argv[0]->type != SEARCH_RESULT_STRING || argv[1]->type != SEARCH_RESULT_STRING)
		search_context_throw (ctx, _("Incompatable argument types in (header-contains )"));
	
	header = argv[0]->value.string;
	match = argv[1]->value.string;
	
	res = search_result_new (SEARCH_RESULT_BOOL);
	res->value.bool = FALSE;
	
	if (!g_ascii_strcasecmp (header, "From")) {
		if (s->current->from)
			res->value.bool = strstr (s->current->from, match) != NULL;
	} else if (!g_ascii_strcasecmp (header, "To")) {
		if (s->current->to)
			res->value.bool = strstr (s->current->to, match) != NULL;
	} else if (!g_ascii_strcasecmp (header, "Cc")) {
		if (s->current->cc)
			res->value.bool = strstr (s->current->cc, match) != NULL;
	} else if (!g_ascii_strcasecmp (header, "Subject")) {
		if (s->current->subject)
			res->value.bool = strstr (s->current->subject, match) != NULL;
	} else {
		if (!s->message)
			s->message = spruce_folder_get_message (s->folder, s->current->uid, NULL);
		
		if (s->message) {
			if ((header = g_mime_object_get_header ((GMimeObject *) s->message, header)))
				res->value.bool = strstr (header, match) != NULL;
		}
	}
	
	return res;
}

static SearchResult *
system_flag (SearchContext *ctx, int argc, SearchResult **argv, SpruceFolderSearch *s)
{
	SearchResult *res;
	
	if (argc != 1)
		search_context_throw (ctx, _("Incorrect argument count in (system-flag )"));
	
	if (argv[0]->type != SEARCH_RESULT_STRING)
		search_context_throw (ctx, _("Incorrect argument type in (system-flag )"));
	
	res = search_result_new (SEARCH_RESULT_INT);
	res->value.integer = spruce_system_flag (argv[0]->value.string);
	
	return res;
}

static SearchResult *
sent_date (SearchContext *ctx, int argc, SearchResult **argv, SpruceFolderSearch *s)
{
	SearchResult *res;
	
	if (argc != 0)
		search_context_throw (ctx, _("Incorrect argument count in (sent-date )"));
	
	res = search_result_new (SEARCH_RESULT_TIME);
	res->value.time = s->current->date_sent;
	
	return res;
}

static SearchResult *
received_date (SearchContext *ctx, int argc, SearchResult **argv, SpruceFolderSearch *s)
{
	SearchResult *res;
	
	if (argc != 0)
		search_context_throw (ctx, _("Incorrect argument count in (received-date )"));
	
	res = search_result_new (SEARCH_RESULT_TIME);
	res->value.time = s->current->date_received;
	
	return res;
}

static SearchResult *
current_date (SearchContext *ctx, int argc, SearchResult **argv, SpruceFolderSearch *s)
{
	SearchResult *res;
	
	if (argc != 0)
		search_context_throw (ctx, _("Incorrect argument count in (current-date )"));
	
	res = search_result_new (SEARCH_RESULT_TIME);
	res->value.time = time (NULL);
	
	return res;
}

static SearchResult *
size (SearchContext *ctx, int argc, SearchResult **argv, SpruceFolderSearch *s)
{
	SearchResult *res;
	
	if (argc != 0)
		search_context_throw (ctx, _("Incorrect argument count in (size )"));
	
	res = search_result_new (SEARCH_RESULT_INT);
	res->value.integer = s->current->size;
	
	return res;
}


SpruceFolderSearch *
spruce_folder_search_new (void)
{
	SpruceFolderSearch *search;
	
	search = g_object_new (SPRUCE_TYPE_FOLDER_SEARCH, NULL, NULL);
	spruce_folder_search_construct (search);
	
	return search;
}


#ifdef offsetof
#define SPRUCE_STRUCT_OFFSET(type, field)  ((int) offsetof (type, field))
#else
#define SPRUCE_STRUCT_OFFSET(type, field)  ((int) ((char *) &((type *) 0)->field))
#endif

struct {
	char *name;
	int offset;
	int flags;
} builtins[] = {
	{ "match-all",       SPRUCE_STRUCT_OFFSET (SpruceFolderSearchClass, match_all),       1 },
	{ "body-contains",   SPRUCE_STRUCT_OFFSET (SpruceFolderSearchClass, body_contains),   0 },
	{ "header-contains", SPRUCE_STRUCT_OFFSET (SpruceFolderSearchClass, header_contains), 0 },
	{ "system-flag",     SPRUCE_STRUCT_OFFSET (SpruceFolderSearchClass, system_flag),     0 },
	{ "sent-date",       SPRUCE_STRUCT_OFFSET (SpruceFolderSearchClass, sent_date),       0 },
	{ "received-date",   SPRUCE_STRUCT_OFFSET (SpruceFolderSearchClass, received_date),   0 },
	{ "current-date",    SPRUCE_STRUCT_OFFSET (SpruceFolderSearchClass, current_date),    0 },
	{ "size",            SPRUCE_STRUCT_OFFSET (SpruceFolderSearchClass, size),            0 },
};


void
spruce_folder_search_construct (SpruceFolderSearch *search)
{
	SpruceFolderSearchClass *klass;
	void *func;
	int i;
	
	g_return_if_fail (SPRUCE_IS_FOLDER_SEARCH (search));
	
	klass = SPRUCE_FOLDER_SEARCH_GET_CLASS (search);
	
	for (i = 0; i < sizeof (builtins) / sizeof (builtins[0]); i++) {
		func = *((void **)(((char *) klass) + builtins[i].offset));
		if (func != NULL) {
			if (builtins[i].flags == 1) {
				search_context_add_ifunction (search->sexp, builtins[i].name,
							      (SearchIFunc) func);
			} else {
				search_context_add_function (search->sexp, builtins[i].name,
							     (SearchFunc) func);
			}
		} else
			g_warning ("Search class doesn't implement '%s' method", builtins[i].name);
	}
}


void
spruce_folder_search_set_folder (SpruceFolderSearch *search, SpruceFolder *folder)
{
	g_return_if_fail (SPRUCE_IS_FOLDER_SEARCH (search));
	g_return_if_fail (SPRUCE_IS_FOLDER (folder));
	
	g_object_ref (folder);
	if (search->folder) {
		g_object_unref (search->folder);
		search->summary = folder->summary ? folder->summary->messages : NULL;
	}
	
	search->folder = folder;
}


void
spruce_folder_search_set_summary (SpruceFolderSearch *search, GPtrArray *summary)
{
	SpruceMessageInfo *info;
	int i;
	
	g_return_if_fail (SPRUCE_IS_FOLDER_SEARCH (search));
	g_return_if_fail (summary != NULL);
	
	search->summary = summary;
	if (search->summary_hash)
		g_hash_table_destroy (search->summary_hash);
	
	search->summary_hash = g_hash_table_new (g_str_hash, g_str_equal);
	for (i = 0; i < summary->len; i++) {
		info = summary->pdata[i];
		g_hash_table_insert (search->summary_hash, info->uid, info);
	}
}


GPtrArray *
spruce_folder_search_match_all (SpruceFolderSearch *search, const char *expr)
{
	GPtrArray *matches;
	SearchResult *res;
	const char *uid;
	int i;
	
	g_return_val_if_fail (SPRUCE_IS_FOLDER_SEARCH (search), NULL);
	g_return_val_if_fail (expr != NULL, NULL);
	
	search->current = search->match1 = NULL;
	
	if (!search->last_search || strcmp (search->last_search, expr)) {
		g_free (search->last_search);
		search->last_search = NULL;
		
		if (search_context_build (search->sexp, expr) == -1)
			return NULL;
		
		search->last_search = g_strdup (expr);
	}
	
	if (!(res = search_context_run (search->sexp, search))) {
		search->current = search->match1 = NULL;
		g_free (search->last_search);
		search->last_search = NULL;
		return NULL;
	}
	
	matches = g_ptr_array_new ();
	
	if (res->type == SEARCH_RESULT_ARRAY) {
		for (i = 0; i < res->value.array->len; i++) {
			uid = res->value.array->pdata[i];
			g_ptr_array_add (matches, g_strdup (uid));
		}
	}
	
	search_result_free (res);
	
	search->current = NULL;
	
	return matches;
}


gboolean
spruce_folder_search_match1 (SpruceFolderSearch *search, const char *expr,
			     const SpruceMessageInfo *info)
{
	SearchResult *res;
	gboolean matches;
	
	g_return_val_if_fail (SPRUCE_IS_FOLDER_SEARCH (search), FALSE);
	g_return_val_if_fail (expr != NULL, FALSE);
	
	if (!search->last_search || strcmp (search->last_search, expr)) {
		g_free (search->last_search);
		search->last_search = NULL;
		
		if (search_context_build (search->sexp, expr) == -1)
			return FALSE;
		
		search->last_search = g_strdup (expr);
	}
	
	search->current = search->match1 = (SpruceMessageInfo *) info;
	
	if (!(res = search_context_run (search->sexp, search))) {
		search->current = search->match1 = NULL;
		g_free (search->last_search);
		search->last_search = NULL;
		return FALSE;
	}
	
	if (res->type == SEARCH_RESULT_BOOL) {
		matches = res->value.bool;
	} else if (res->type == SEARCH_RESULT_ARRAY) {
		matches = res->value.array->len == 1;
	} else
		matches = FALSE;
	
	search_result_free (res);
	
	if (search->message) {
		g_object_unref (search->message);
		search->message = NULL;
	}
	
	search->current = search->match1 = NULL;
	
	return matches;
}


void
spruce_folder_search_free_result (SpruceFolderSearch *search, GPtrArray *uids)
{
	g_ptr_array_free (uids, TRUE);
}
