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


#ifndef __SEARCH_H__
#define __SEARCH_H__

#include <glib.h>
#include <stdarg.h>
#include <setjmp.h>
#include <time.h>

G_BEGIN_DECLS

typedef struct _SearchResult SearchResult;
typedef struct _SearchSymbol SearchSymbol;
typedef struct _SearchTerm SearchTerm;
typedef struct _SearchStack SearchStack;
typedef struct _SearchContext SearchContext;

typedef SearchResult * (*SearchFunc) (SearchContext *ctx, int argc, SearchResult **argv,
				      void *user_data);

typedef SearchResult * (*SearchIFunc) (SearchContext *ctx, int argc, SearchTerm **argv,
				       void *user_data);

typedef enum {
	SEARCH_RESULT_BOOL,
	SEARCH_RESULT_INT,
	SEARCH_RESULT_TIME,
	SEARCH_RESULT_FLOAT,
	SEARCH_RESULT_STRING,
	SEARCH_RESULT_ARRAY,
	SEARCH_RESULT_LIST,  /* list of SearchResults */
	SEARCH_RESULT_VOID,
} search_result_t;

struct _SearchResult {
	search_result_t type;
	union {
		gboolean bool;
		int integer;
		time_t time;
		double decimal;
		char *string;
		GPtrArray *array;
	} value;
};

typedef enum {
	SEARCH_SYMBOL_FUNCTION,
	SEARCH_SYMBOL_IFUNCTION,
	SEARCH_SYMBOL_VARIABLE,
} search_symbol_t;

struct _SearchSymbol {
	search_symbol_t type;
	char *name;
	union {
		SearchFunc func;
		SearchIFunc ifunc;
		SearchResult *var;
	} value;
};

typedef enum {
	SEARCH_TERM_BOOL,
	SEARCH_TERM_INT,
	SEARCH_TERM_TIME,
	SEARCH_TERM_FLOAT,
	SEARCH_TERM_STRING,
	SEARCH_TERM_ARRAY,
	SEARCH_TERM_LIST,      /* list of SearchTerms */
	SEARCH_TERM_FUNCTION,
	SEARCH_TERM_IFUNCTION,
	SEARCH_TERM_VARIABLE,
} search_term_t;

struct _SearchTerm {
	search_term_t type;
	union {
		gboolean bool;
		int integer;
		time_t time;
		double decimal;
		char *string;
		GPtrArray *array;
		struct {
			SearchSymbol *sym;
			SearchTerm **argv;
			int argc;
		} func;
		SearchSymbol *var;
	} value;
};

struct _SearchContext {
	unsigned int ref_count;
	
	SearchStack *stack;
	SearchTerm *tree;
	
	char *exception;
	jmp_buf env;
};


/* Search Context */
SearchContext *search_context_new (void);

void search_context_ref (SearchContext *ctx);
void search_context_unref (SearchContext *ctx);

void search_context_add_function (SearchContext *ctx, const char *name, SearchFunc func);
void search_context_add_ifunction (SearchContext *ctx, const char *name, SearchIFunc ifunc);
void search_context_add_variable (SearchContext *ctx, const char *name, SearchResult *var);

void search_context_remove_symbol (SearchContext *ctx, const char *name);

int search_context_build (SearchContext *ctx, const char *expression);
SearchResult *search_context_run (SearchContext *ctx, void *user_data);

void search_context_throw (SearchContext *ctx, const char *exception, ...);
const char *search_context_exception (SearchContext *ctx);

/* Search Result */
SearchResult *search_result_new (search_result_t type);
void search_result_free (SearchResult *result);

/* Search Term */
SearchResult *search_term_eval (SearchContext *ctx, SearchTerm *term, void *user_data);

G_END_DECLS

#endif /* __SEARCH_H__ */
