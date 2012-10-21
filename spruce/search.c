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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "search.h"

#define _(x) x

struct _SearchStack {
	SearchStack *parent;
	GHashTable *symbols;
	GPtrArray *argv;
	int scope;
};

static SearchResult *search_term_and (SearchContext *ctx, int argc, SearchTerm **argv, void *user_data);
static SearchResult *search_term_or (SearchContext *ctx, int argc, SearchTerm **argv, void *user_data);
static SearchResult *search_term_not (SearchContext *ctx, int argc, SearchResult **argv, void *user_data);
static SearchResult *search_term_lt (SearchContext *ctx, int argc, SearchTerm **argv, void *user_data);
static SearchResult *search_term_gt (SearchContext *ctx, int argc, SearchTerm **argv, void *user_data);
static SearchResult *search_term_eq (SearchContext *ctx, int argc, SearchTerm **argv, void *user_data);
static SearchResult *search_term_add (SearchContext *ctx, int argc, SearchResult **argv, void *user_data);
static SearchResult *search_term_sub (SearchContext *ctx, int argc, SearchResult **argv, void *user_data);
static SearchResult *search_term_mult (SearchContext *ctx, int argc, SearchResult **argv, void *user_data);
static SearchResult *search_term_div (SearchContext *ctx, int argc, SearchResult **argv, void *user_data);
static SearchResult *search_term_if (SearchContext *ctx, int argc, SearchTerm **argv, void *user_data);
static SearchResult *search_term_begin (SearchContext *ctx, int argc, SearchTerm **argv, void *user_data);

static SearchResult *search_term_cast_bool (SearchContext *ctx, int argc, SearchResult **argv, void *user_data);
static SearchResult *search_term_cast_int (SearchContext *ctx, int argc, SearchResult **argv, void *user_data);
static SearchResult *search_term_cast_float (SearchContext *ctx, int argc, SearchResult **argv, void *user_data);
static SearchResult *search_term_cast_string (SearchContext *ctx, int argc, SearchResult **argv, void *user_data);

struct {
	char *name;
	SearchFunc func;
	int type;        /* 0 is SearchFunc; 1 is SearchIFunc */
} symbols[] = {
	{ "and",         (SearchFunc) search_term_and,         1 },
	{ "or",          (SearchFunc) search_term_or,          1 },
	{ "not",         (SearchFunc) search_term_not,         0 },
	{ "<",           (SearchFunc) search_term_lt,          1 },
	{ ">",           (SearchFunc) search_term_gt,          1 },
	{ "=",           (SearchFunc) search_term_eq,          1 },
	{ "+",           (SearchFunc) search_term_add,         0 },
	{ "-",           (SearchFunc) search_term_sub,         0 },
	{ "*",           (SearchFunc) search_term_mult,        0 },
	{ "/",           (SearchFunc) search_term_div,         0 },
	{ "if",          (SearchFunc) search_term_if,          1 },
	{ "begin",       (SearchFunc) search_term_begin,       1 },
	{ "cast-bool",   (SearchFunc) search_term_cast_bool,   0 },
	{ "cast-int",    (SearchFunc) search_term_cast_int,    0 },
	{ "cast-float",  (SearchFunc) search_term_cast_float,  0 },
	{ "cast-string", (SearchFunc) search_term_cast_string, 0 },
};


static void
search_symbol_free (SearchSymbol *sym)
{
	if (sym->type == SEARCH_SYMBOL_VARIABLE)
		search_result_free (sym->value.var);
	
	g_free (sym->name);
	g_free (sym);
}


static SearchStack *
search_stack_new (void)
{
	SearchStack *stack;
	
	stack = g_new (SearchStack, 1);
	stack->symbols = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, (GDestroyNotify) search_symbol_free);
	stack->parent = NULL;
	stack->argv = NULL;
	stack->scope = 0;
	
	return stack;
}

static void
search_stack_free (SearchStack *stack)
{
	g_hash_table_destroy (stack->symbols);
	g_free (stack);
}

static void
search_stack_push (SearchContext *ctx)
{
	SearchStack *stack;
	
	stack = search_stack_new ();
	
	if (ctx->stack) {
		stack->scope = ctx->stack->scope + 1;
		stack->parent = ctx->stack;
	}
	
	ctx->stack = stack;
}

static void
search_stack_pop (SearchContext *ctx)
{
	SearchStack *stack;
	
	stack = ctx->stack;
	if (stack == NULL) {
		fprintf (stderr, "Search stack underflow!\n");
		return;
	}
	
	ctx->stack = stack->parent;
	
	search_stack_free (stack);
}

static SearchSymbol *
search_stack_find_symbol (SearchContext *ctx, const char *name)
{
	SearchStack *stack;
	SearchSymbol *sym;
	
	stack = ctx->stack;
	while (stack) {
		if ((sym = g_hash_table_lookup (stack->symbols, name)))
			return sym;
		
		stack = stack->parent;
	}
	
	return NULL;
}


SearchContext *
search_context_new (void)
{
	SearchContext *ctx;
	int i;
	
	ctx = g_new (SearchContext, 1);
	ctx->exception = NULL;
	ctx->ref_count = 1;
	ctx->stack = NULL;
	ctx->tree = NULL;
	
	for (i = 0; i < G_N_ELEMENTS (symbols); i++) {
		if (symbols[i].type == 1)
			search_context_add_ifunction (ctx, symbols[i].name,
						      (SearchIFunc) symbols[i].func);
		else
			search_context_add_function (ctx, symbols[i].name, symbols[i].func);
	}
	
	return ctx;
}


void
search_context_ref (SearchContext *ctx)
{
	ctx->ref_count++;
}


static SearchTerm *
search_term_new (void)
{
	return g_new (SearchTerm, 1);
}

static void
search_term_free (SearchTerm *term)
{
	int i;
	
	switch (term->type) {
	case SEARCH_TERM_STRING:
		g_free (term->value.string);
		break;
	case SEARCH_TERM_ARRAY:
		g_ptr_array_free (term->value.array, TRUE);
		break;
	case SEARCH_TERM_FUNCTION:
	case SEARCH_TERM_IFUNCTION:
		search_symbol_free (term->value.func.sym);
		for (i = 0; i < term->value.func.argc; i++)
			search_term_free (term->value.func.argv[i]);
		g_free (term->value.func.argv);
		break;
	case SEARCH_TERM_VARIABLE:
		search_symbol_free (term->value.var);
		break;
	default:
		break;
	}
	
	g_free (term);
}

void
search_context_unref (SearchContext *ctx)
{
	ctx->ref_count--;
	
	if (ctx->ref_count == 0) {
		while (ctx->stack)
			search_stack_pop (ctx);
		
		search_term_free (ctx->tree);
		g_free (ctx->exception);
		g_free (ctx);
	}
}

static SearchSymbol *
search_symbol_new (void)
{
	return g_new (SearchSymbol, 1);
}

void
search_context_add_function (SearchContext *ctx, const char *name, SearchFunc func)
{
	SearchStack *stack = ctx->stack;
	SearchSymbol *sym;
	
	if (!ctx->stack)
		stack = ctx->stack = search_stack_new ();
	
	sym = search_symbol_new ();
	sym->type = SEARCH_SYMBOL_FUNCTION;
	sym->name = g_strdup (name);
	sym->value.func = func;
	
	g_hash_table_insert (stack->symbols, sym->name, sym);
}


void
search_context_add_ifunction (SearchContext *ctx, const char *name, SearchIFunc ifunc)
{
	SearchStack *stack = ctx->stack;
	SearchSymbol *sym;
	
	if (!ctx->stack)
		stack = ctx->stack = search_stack_new ();
	
	sym = g_new (SearchSymbol, 1);
	sym->type = SEARCH_SYMBOL_IFUNCTION;
	sym->name = g_strdup (name);
	sym->value.ifunc = ifunc;
	
	g_hash_table_insert (stack->symbols, sym->name, sym);
}


static SearchResult *
search_result_copy (SearchResult *result)
{
	SearchResult *res, *r;
	int i;
	
	res = search_result_new (result->type);
	switch (result->type) {
	case SEARCH_RESULT_BOOL:
		res->value.bool = result->value.bool;
		break;
	case SEARCH_RESULT_INT:
		res->value.integer = result->value.integer;
		break;
	case SEARCH_RESULT_TIME:
		res->value.time = result->value.time;
		break;
	case SEARCH_RESULT_FLOAT:
		res->value.decimal = result->value.decimal;
		break;
	case SEARCH_RESULT_STRING:
		res->value.string = g_strdup (result->value.string);
		break;
	case SEARCH_RESULT_ARRAY:
		res->value.array = g_ptr_array_new ();
		for (i = 0; i < result->value.array->len; i++)
			g_ptr_array_add (res->value.array, result->value.array->pdata[i]);
		break;
	case SEARCH_RESULT_LIST:
		res->value.array = g_ptr_array_new ();
		for (i = 0; i < result->value.array->len; i++) {
			r = search_result_copy (result->value.array->pdata[i]);
			g_ptr_array_add (res->value.array, r);
		}
		break;
	case SEARCH_RESULT_VOID:
		break;
	}
	
	return res;
}


void
search_context_add_variable (SearchContext *ctx, const char *name, SearchResult *var)
{
	SearchStack *stack = ctx->stack;
	SearchSymbol *sym;
	
	if (!ctx->stack)
		search_stack_push (ctx);
	
	sym = search_symbol_new ();
	sym->type = SEARCH_SYMBOL_VARIABLE;
	sym->name = g_strdup (name);
	sym->value.var = search_result_copy (var);
	
	g_hash_table_insert (stack->symbols, sym->name, sym);
}


void
search_context_remove_symbol (SearchContext *ctx, const char *name)
{
	SearchSymbol *sym = NULL;
	SearchStack *stack;
	
	stack = ctx->stack;
	while (stack) {
		if ((sym = g_hash_table_lookup (stack->symbols, name)))
			break;
		
		stack = stack->parent;
	}
	
	if (stack && sym) {
		g_hash_table_remove (stack->symbols, name);
		search_symbol_free (sym);
	}
}


static void
decode_lwsp (const char **in)
{
	register const unsigned char *inptr;
	
	inptr = (const unsigned char *) *in;
	while (isspace ((int) *inptr))
		inptr++;
	
	*in = (const char *) inptr;
}

static char *
unescape (char *string)
{
	char *d, *s;
	
	s = string;
	while (*s && *s != '\\')
		s++;
	
	d = s;
	s++;
	while (*s) {
		*d = *s++;
		if (*s == '\\')
			s++;
	}
	
	*d = '\0';
	
	return string;
}

static char *
decode_quoted_string (const char **in)
{
	const char *start, *inptr = *in;
	
	if (*inptr == '"') {
		start = inptr;
		
		inptr++;
		while (*inptr && *inptr != '"') {
			if (*inptr == '\\')
				inptr++;
			
			if (*inptr)
				inptr++;
		}
		
		if (*inptr == '"')
			*in = inptr + 1;
		
		start++;
		
		return unescape (g_strndup (start, inptr - start));
	}
	
	return NULL;
}

static char *
decode_token (const char **in, search_term_t *type)
{
	register const char *inptr = *in;
	const char *start;
	
	*type = -1;
	
	if (*inptr == '"') {
		*type = SEARCH_TERM_STRING;
		return decode_quoted_string (in);
	} else {
		start = inptr;
		while (*inptr && *inptr != ')' && !isspace ((unsigned char) *inptr))
			inptr++;
		
		*in = inptr;
		if (inptr > start)
			return g_strndup (start, inptr - start);
		else
			return NULL;
	}
}

static SearchSymbol *
decode_symbol (SearchContext *ctx, const char **in)
{
	SearchSymbol *symbol, *sym;
	char *token, *tend;
	search_term_t type;
	SearchResult *res;
	double decimal;
	int integer;
	
	if (!(token = decode_token (in, &type)))
		return NULL;
	
	if (type == SEARCH_TERM_STRING) {
		sym = search_symbol_new ();
		sym->type = SEARCH_SYMBOL_VARIABLE;
		sym->name = NULL;
		
		res = search_result_new (SEARCH_RESULT_STRING);
		res->value.string = token;
		
		sym->value.var = res;
	} else {
		sym = search_symbol_new ();
		if ((symbol = search_stack_find_symbol (ctx, token))) {
			/* we have a function or variable */
			sym->type = symbol->type;
			sym->name = token;
			if (symbol->type == SEARCH_SYMBOL_FUNCTION)
				sym->value.func = symbol->value.func;
			else if (symbol->type == SEARCH_SYMBOL_IFUNCTION)
				sym->value.ifunc = symbol->value.ifunc;
			else
				sym->value.var = search_result_copy (symbol->value.var);
		} else {
			sym->type = SEARCH_SYMBOL_VARIABLE;
			sym->name = NULL;
			
			integer = strtol (token, &tend, 10);
			if (*tend == '.') {
				/* oops, it looks like a floating point value */
				decimal = g_ascii_strtod (token, &tend);
				res = search_result_new (SEARCH_RESULT_FLOAT);
				res->value.decimal = decimal;
			} else if (*tend == '\0') {
				/* integer value */
				res = search_result_new (SEARCH_RESULT_INT);
				res->value.integer = integer;
			} else if (!strcmp (token, "true") || !strcmp (token, "#t")) {
				res = search_result_new (SEARCH_RESULT_BOOL);
				res->value.bool = TRUE;
			} else if (!strcmp (token, "false") || !strcmp (token, "#f")) {
				res = search_result_new (SEARCH_RESULT_BOOL);
				res->value.bool = FALSE;
			} else {
				g_warning ("Unknown token encountered: %s", token);
				res = search_result_new (SEARCH_RESULT_VOID);
			}
			
			sym->value.var = res;
			g_free (token);
		}
	}
	
	return sym;
}

static SearchTerm *
decode_term (SearchContext *ctx, const char **in)
{
	const char *inptr = *in;
	SearchSymbol *sym;
	SearchResult *res;
	SearchTerm *term;
	
	decode_lwsp (&inptr);
	
	if (!(sym = decode_symbol (ctx, &inptr)))
		return NULL;
	
	*in = inptr;
	
	term = search_term_new ();
	if (sym->type == SEARCH_SYMBOL_VARIABLE) {
		if (sym->name) {
			term->type = SEARCH_TERM_VARIABLE;
			term->value.var = sym;
		} else {
			res = sym->value.var;
			
			/* we faked a symbol */
			switch (res->type) {
			case SEARCH_RESULT_BOOL:
				term->type = SEARCH_TERM_BOOL;
				term->value.bool = res->value.bool;
				break;
			case SEARCH_RESULT_INT:
				term->type = SEARCH_TERM_INT;
				term->value.integer = res->value.integer;
				break;
			case SEARCH_RESULT_TIME:
				term->type = SEARCH_TERM_TIME;
				term->value.time = res->value.time;
				break;
			case SEARCH_RESULT_FLOAT:
				term->type = SEARCH_TERM_FLOAT;
				term->value.decimal = res->value.decimal;
				break;
			case SEARCH_RESULT_STRING:
				term->type = SEARCH_TERM_STRING;
				term->value.string = res->value.string;
				res->value.string = NULL;
				break;
			default:
				break;
			}
			
			search_symbol_free (sym);
		}
	} else if (sym->type == SEARCH_SYMBOL_FUNCTION) {
		term->type = SEARCH_TERM_FUNCTION;
		term->value.func.sym = sym;
	} else {
		term->type = SEARCH_TERM_IFUNCTION;
		term->value.func.sym = sym;
	}
	
	return term;
}

/* if a term is a variable, returns the type of the value it holds */
static search_term_t
term_type (SearchTerm *term)
{
	SearchResult *res;
	SearchSymbol *sym;
	
	if (term->type != SEARCH_TERM_VARIABLE)
		return term->type;
	
	sym = term->value.var;
	g_assert (sym->type == SEARCH_SYMBOL_VARIABLE);
	
	res = sym->value.var;
	
	switch (res->type) {
	case SEARCH_RESULT_BOOL:
		return SEARCH_TERM_BOOL;
	case SEARCH_RESULT_INT:
		return SEARCH_TERM_INT;
	case SEARCH_RESULT_TIME:
		return SEARCH_TERM_TIME;
	case SEARCH_RESULT_FLOAT:
		return SEARCH_TERM_FLOAT;
	case SEARCH_RESULT_STRING:
		return SEARCH_TERM_STRING;
	case SEARCH_RESULT_ARRAY:
		return SEARCH_TERM_ARRAY;
	default:
		g_assert_not_reached ();
		return -1;
	}
}

/* finds a common value type for 2 terms */
static search_term_t
term_type_best (search_term_t t1, search_term_t t2)
{
	search_term_t best;
	
	if (t1 == t2)
		return t1;
	
	if (t1 == SEARCH_TERM_TIME || t2 == SEARCH_TERM_TIME)
		return SEARCH_TERM_STRING;
	
	best = MAX (t1, t2);
	
	/* arrays are not compatable with anything but arrays */
	if (best == SEARCH_TERM_ARRAY)
		return -1;
	else if (best == SEARCH_TERM_FUNCTION)
		return -1;
	else if (best == SEARCH_TERM_IFUNCTION)
		return -1;
	else if (best == SEARCH_TERM_VARIABLE)
		return -1;
	
	return best;
}

static SearchTerm *
decode_tree (SearchContext *ctx, const char **in)
{
	SearchTerm *term, *root = NULL;
	GPtrArray *terms, *array;
	const char *inptr = *in;
	search_term_t best;
	int i;
	
	decode_lwsp (&inptr);
	if (*inptr == '(') {
		inptr++;
		root = decode_tree (ctx, &inptr);
		decode_lwsp (&inptr);
		if (*inptr != ')') {
			search_term_free (root);
			root = NULL;
		} else
			inptr++;
	} else
		root = decode_term (ctx, &inptr);
	
	if (root == NULL) {
		g_warning ("failed parsing search expression: %s", inptr);
		*in = inptr;
		return NULL;
	}
	
	decode_lwsp (&inptr);
	
	terms = g_ptr_array_new ();
	while (*inptr && *inptr != ')') {
		if (*inptr == '(') {
			inptr++;
			term = decode_tree (ctx, &inptr);
			decode_lwsp (&inptr);
			if (*inptr != ')') {
				g_warning ("Invalid search term encountered: expected ')'");
				goto exception;
			}
			
			inptr++;
		} else {
			if (!(term = decode_term (ctx, &inptr))) {
				g_warning ("Could not decode term");
				goto exception;
			}
		}
		
		/* we can have empty search terms */
		if (term)
			g_ptr_array_add (terms, term);
		
		decode_lwsp (&inptr);
	}
	
	if (root->type == SEARCH_TERM_FUNCTION || root->type == SEARCH_TERM_IFUNCTION) {
		root->value.func.argc = terms->len;
		root->value.func.argv = (SearchTerm **) terms->pdata;
		g_ptr_array_free (terms, FALSE);
	} else {
		if (terms->len > 0) {
			/* we have an array of terms */
			best = term_type (root);
			for (i = 0; i < terms->len && best != -1; i++) {
				term = terms->pdata[i];
				best = term_type_best (best, term_type (term));
				if (best == -1) {
					g_warning ("Incompatable types in array: %s", *in);
					goto exception;
				}
			}
			
			array = g_ptr_array_new ();
			g_ptr_array_add (array, root);
			for (i = 0; i < terms->len; i++)
				g_ptr_array_add (array, terms->pdata[i]);
			
			root = g_new (SearchTerm, 1);
			root->type = SEARCH_TERM_LIST;
			root->value.array = array;
		}
		
		g_ptr_array_free (terms, TRUE);
	}
	
	*in = inptr;
	
	return root;
	
 exception:
	
	search_term_free (root);
	
	for (i = 0; i < terms->len; i++)
		search_term_free (terms->pdata[i]);
	g_ptr_array_free (terms, TRUE);
	
	return NULL;
}


int
search_context_build (SearchContext *ctx, const char *expression)
{
	const char *inptr = expression;
	
	if (ctx->tree) {
		search_term_free (ctx->tree);
		ctx->tree = NULL;
	}
	
	decode_lwsp (&inptr);
	
	if (*inptr != '(')
		return -1;
	
	inptr++;
	if (!(ctx->tree = decode_tree (ctx, &inptr)))
		goto exception;
	
	decode_lwsp (&inptr);
	if (*inptr != ')') {
		goto exception;
	} else {
		inptr++;
		decode_lwsp (&inptr);
		if (*inptr != '\0')
			goto exception;
	}
	
	return 0;
	
 exception:
	
	g_warning ("parse error: unexpected token at %s", inptr);
	
	return -1;
}


static SearchResult *
search_symbol_eval (SearchContext *ctx, SearchSymbol *sym, void *user_data)
{
	switch (sym->type) {
	case SEARCH_SYMBOL_FUNCTION:
		return sym->value.func (ctx, 0, NULL, user_data);
	case SEARCH_SYMBOL_IFUNCTION:
		return sym->value.ifunc (ctx, 0, NULL, user_data);
	case SEARCH_SYMBOL_VARIABLE:
		return search_result_copy (sym->value.var);
	}
	
	g_assert_not_reached ();
	
	return NULL;
}

SearchResult *
search_term_eval (SearchContext *ctx, SearchTerm *term, void *user_data)
{
	SearchResult *r, *res = NULL;
	SearchSymbol *sym;
	GPtrArray *argv;
	int i;
	
	switch (term->type) {
	case SEARCH_TERM_BOOL:
		res = search_result_new (SEARCH_RESULT_BOOL);
		res->value.bool = term->value.bool;
		return res;
	case SEARCH_TERM_INT:
		res = search_result_new (SEARCH_RESULT_INT);
		res->value.integer = term->value.integer;
		return res;
	case SEARCH_TERM_TIME:
		res = search_result_new (SEARCH_RESULT_TIME);
		res->value.time = term->value.time;
		return res;
	case SEARCH_TERM_FLOAT:
		res = search_result_new (SEARCH_RESULT_FLOAT);
		res->value.decimal = term->value.decimal;
		return res;
	case SEARCH_TERM_STRING:
		res = search_result_new (SEARCH_RESULT_STRING);
		res->value.string = g_strdup (term->value.string);
		return res;
	case SEARCH_TERM_ARRAY:
		res = search_result_new (SEARCH_RESULT_ARRAY);
		res->value.array = g_ptr_array_new ();
		for (i = 0; i < term->value.array->len; i++)
			g_ptr_array_add (res->value.array, term->value.array->pdata[i]);
		return res;
	case SEARCH_TERM_LIST:
		res = search_result_new (SEARCH_RESULT_LIST);
		res->value.array = g_ptr_array_new ();
		for (i = 0; i < term->value.array->len; i++) {
			r = search_term_eval (ctx, term->value.array->pdata[i], NULL);
			g_ptr_array_add (res->value.array, r);
		}
		return res;
	case SEARCH_TERM_FUNCTION:
		sym = term->value.func.sym;
		search_stack_push (ctx);
		
		ctx->stack->argv = argv = g_ptr_array_new ();
		for (i = 0; i < term->value.func.argc; i++)
			g_ptr_array_add (argv, search_term_eval (ctx, term->value.func.argv[i], user_data));
		
		if (sym->value.func)
			res = sym->value.func (ctx, argv->len, (SearchResult **) argv->pdata, user_data);
		
		for (i = 0; i < argv->len; i++)
			search_result_free ((SearchResult *) argv->pdata[i]);
		g_ptr_array_free (argv, TRUE);
		ctx->stack->argv = NULL;
		
		search_stack_pop (ctx);
		
		if (res == NULL)
			return search_result_new (SEARCH_RESULT_VOID);
		
		return res;
	case SEARCH_TERM_IFUNCTION:
		sym = term->value.func.sym;
		return sym->value.ifunc (ctx, term->value.func.argc,
					 term->value.func.argv, user_data);
	case SEARCH_TERM_VARIABLE:
		return search_symbol_eval (ctx, term->value.var, user_data);
	}
	
	g_assert_not_reached ();
	
	return NULL;
}

#if 1
static void
search_term_dump (SearchTerm *term)
{
	int i;
	
	switch (term->type) {
	case SEARCH_TERM_BOOL:
		printf ("%s ", term->value.bool ? "true" : "false");
		break;
	case SEARCH_TERM_INT:
		printf ("%d ", term->value.integer);
		break;
	case SEARCH_TERM_TIME:
		printf ("%ld ", term->value.time);
		break;
	case SEARCH_TERM_FLOAT:
		printf ("%.2f ", term->value.decimal);
		break;
	case SEARCH_TERM_STRING:
		printf ("\"%s\" ", term->value.string);
		break;
	case SEARCH_TERM_ARRAY:
		printf ("<array> ");
		break;
	case SEARCH_TERM_LIST:
		printf ("(");
		for (i = 0; i < term->value.array->len; i++)
			search_term_dump (term->value.array->pdata[i]);
		printf (")");
		break;
	case SEARCH_TERM_FUNCTION:
	case SEARCH_TERM_IFUNCTION:
		printf ("(%s ", term->value.func.sym->name);
		for (i = 0; i < term->value.func.argc; i++)
			search_term_dump (term->value.func.argv[i]);
		printf (") ");
		break;
	case SEARCH_TERM_VARIABLE:
		printf ("%s ", term->value.var->name);
		break;
	}
}
#endif


SearchResult *
search_context_run (SearchContext *ctx, void *user_data)
{
	SearchResult *res = NULL;
	
	if (ctx->tree == NULL) {
		ctx->exception = g_strdup_printf (_("No expression to evaluate"));
		return NULL;
	}
	
	g_free (ctx->exception);
	ctx->exception = NULL;
	
	if (setjmp (ctx->env) == 0)
		res = search_term_eval (ctx, ctx->tree, user_data);
	
	return res;
}


void
search_context_throw (SearchContext *ctx, const char *exception, ...)
{
	SearchStack *stack;
	va_list ap;
	int i;
	
	va_start (ap, exception);
	ctx->exception = g_strdup_vprintf (exception, ap);
	va_end (ap);
	
	/* clean up our stack */
	stack = ctx->stack;
	while (stack) {
		if (stack->argv) {
			for (i = 0; i < stack->argv->len; i++)
				search_result_free (stack->argv->pdata[i]);
			
			g_ptr_array_free (stack->argv, TRUE);
			stack->argv = NULL;
		}
		
		stack = stack->parent;
	}
	
	longjmp (ctx->env, 1);
}


const char *
search_context_exception (SearchContext *ctx)
{
	return ctx->exception;
}


static SearchResult *
search_result_convert (SearchContext *ctx, SearchResult *r, search_result_t type)
{
	SearchResult *res;
	
	if (r->type == SEARCH_RESULT_ARRAY)
		return NULL;
	
	switch (type) {
	case SEARCH_RESULT_BOOL:
		res = search_term_cast_bool (ctx, 1, &r, NULL);
		break;
	case SEARCH_RESULT_INT:
		res = search_term_cast_int (ctx, 1, &r, NULL);
		break;
	case SEARCH_RESULT_FLOAT:
		res = search_term_cast_float (ctx, 1, &r, NULL);
		break;
	case SEARCH_RESULT_STRING:
		res = search_term_cast_string (ctx, 1, &r, NULL);
		break;
	default:
		return NULL;
	}
	
	return res;
}


SearchResult *
search_result_new (search_result_t type)
{
	SearchResult *res;
	
	res = g_new (SearchResult, 1);
	res->type = type;
	res->value.integer = 0;
	
	return res;
}


void
search_result_free (SearchResult *result)
{
	int i;
	
	switch (result->type) {
	case SEARCH_RESULT_STRING:
		g_free (result->value.string);
		break;
	case SEARCH_RESULT_ARRAY:
		g_ptr_array_free (result->value.array, TRUE);
		break;
	case SEARCH_RESULT_LIST:
		for (i = 0; i < result->value.array->len; i++)
			search_result_free (result->value.array->pdata[i]);
		g_ptr_array_free (result->value.array, TRUE);
	default:
		break;
	}
	
	g_free (result);
}



/* Builtin functions */

struct _intersect {
	GPtrArray *array;
	int num;
};

static void
and_array_intersect (gpointer key, gpointer val, gpointer user_data)
{
	struct _intersect *isect = user_data;
	
	if (GPOINTER_TO_INT (val) >= isect->num)
		g_ptr_array_add (isect->array, key);
}

static SearchResult *
search_term_and (SearchContext *ctx, int argc, SearchTerm **argv, void *user_data)
{
	search_result_t type = SEARCH_RESULT_BOOL;
	struct _intersect isect;
	GHashTable *hash = NULL;
	SearchResult *res, *r;
	gboolean bool = TRUE;
	gpointer okey, oval;
	GPtrArray *array;
	int val, num = 1;
	int i, j;
	
	if (argc == 0)
		search_context_throw (ctx, _("No arguments in AND expression"));
	
	/* evaluate the first term so we can figure out what type of AND this is... */
	res = search_term_eval (ctx, argv[0], user_data);
	
	/* if the type is an array, then we intersect the arrays, every other type is treated as boolean */
	if (res->type == SEARCH_RESULT_ARRAY) {
		type = SEARCH_RESULT_ARRAY;
		hash = g_hash_table_new (g_direct_hash, g_direct_equal);
		for (j = 0; j < res->value.array->len; j++) {
			if (g_hash_table_lookup_extended (hash, res->value.array->pdata[j], &okey, &oval)) {
				val = GPOINTER_TO_INT (oval);
				oval = GINT_TO_POINTER (val + 1);
			} else {
				g_hash_table_insert (hash, res->value.array->pdata[j], GINT_TO_POINTER (1));
			}
		}
	} else {
		type = SEARCH_RESULT_BOOL;
		r = search_result_convert (ctx, res, type);
		bool = r->value.bool;
		search_result_free (r);
	}
	
	search_result_free (res);
	
	for (i = 1; i < argc && bool; i++) {
		res = search_term_eval (ctx, argv[i], user_data);
		if (type == SEARCH_RESULT_ARRAY) {
			if (res->type != SEARCH_RESULT_ARRAY)
				goto exception;
			
			num++;
			for (j = 0; j < res->value.array->len; j++) {
				if (g_hash_table_lookup_extended (hash, res->value.array->pdata[j],
								  &okey, &oval)) {
					val = GPOINTER_TO_INT (oval);
					oval = GINT_TO_POINTER (val + 1);
				} else {
					g_hash_table_insert (hash, res->value.array->pdata[j],
							     GINT_TO_POINTER (1));
				}
			}
		} else {
			if (res->type == SEARCH_RESULT_ARRAY)
				goto exception;
			
			r = search_result_convert (ctx, res, SEARCH_RESULT_BOOL);
			bool = bool && r->value.bool;
			search_result_free (r);
		}
		
		search_result_free (res);
	}
	
	res = search_result_new (type);
	if (type == SEARCH_RESULT_ARRAY) {
		array = g_ptr_array_new ();
		isect.array = array;
		isect.num = num;
		g_hash_table_foreach (hash, and_array_intersect, &isect);
		g_hash_table_destroy (hash);
		res->value.array = array;
	} else
		res->value.bool = bool;
	
	return res;
	
 exception:
	
	if (type == SEARCH_RESULT_ARRAY)
		g_hash_table_destroy (hash);
	
	search_context_throw (ctx, _("Invalid types in AND"));
	
	return NULL;
}

static SearchResult *
search_term_or (SearchContext *ctx, int argc, SearchTerm **argv, void *user_data)
{
	search_result_t type = SEARCH_RESULT_BOOL;
	GHashTable *hash = NULL;
	GPtrArray *array = NULL;
	gboolean bool = FALSE;
	SearchResult *res, *r;
	int i, j;
	
	if (argc == 0)
		search_context_throw (ctx, _("No arguments in OR expression"));
	
	/* evaluate the first term so we can figure out what type of AND this is... */
	res = search_term_eval (ctx, argv[0], user_data);
	
	/* if the type is an array, then we union the arrays, every other type is treated as boolean */
	if (res->type == SEARCH_RESULT_ARRAY) {
		type = SEARCH_RESULT_ARRAY;
		hash = g_hash_table_new (g_direct_hash, g_direct_equal);
		array = g_ptr_array_new ();
		for (j = 0; j < res->value.array->len; j++) {
			if (!g_hash_table_lookup (hash, res->value.array->pdata[j])) {
				g_ptr_array_add (array, res->value.array->pdata[j]);
				g_hash_table_insert (hash, res->value.array->pdata[j],
						     GINT_TO_POINTER (1));
			}
		}
	} else {
		type = SEARCH_RESULT_BOOL;
		r = search_result_convert (ctx, res, type);
		bool = r->value.bool;
		search_result_free (r);
	}
	
	search_result_free (res);
	
	for (i = 1; i < argc && bool; i++) {
		res = search_term_eval (ctx, argv[i], user_data);
		if (type == SEARCH_RESULT_ARRAY) {
			if (res->type != SEARCH_RESULT_ARRAY)
				goto exception;
			
			for (j = 0; j < res->value.array->len; j++) {
				if (!g_hash_table_lookup (hash, res->value.array->pdata[j])) {
					g_ptr_array_add (array, res->value.array->pdata[j]);
					g_hash_table_insert (hash, res->value.array->pdata[j],
							     GINT_TO_POINTER (1));
				}
			}
		} else {
			if (res->type == SEARCH_RESULT_ARRAY)
				goto exception;
			
			r = search_result_convert (ctx, res, type);
			bool = bool || r->value.bool;
			search_result_free (r);
		}
		
		search_result_free (res);
	}
	
	res = search_result_new (type);
	if (type == SEARCH_RESULT_ARRAY) {
		res->value.array = array;
		g_hash_table_destroy (hash);
	} else
		res->value.bool = bool;
	
	return res;
	
 exception:
	
	if (type == SEARCH_RESULT_ARRAY)
		g_hash_table_destroy (hash);
	
	search_context_throw (ctx, _("Invalid types in OR"));
	
	return NULL;
}

static SearchResult *
search_term_not (SearchContext *ctx, int argc, SearchResult **argv, void *user_data)
{
	SearchResult *res;
	
	if (argc != 1)
		search_context_throw (ctx, _("Incorrect number of arguments in NOT expression"));
	
	if (argv[0]->type == SEARCH_RESULT_ARRAY) {
		/* FIXME: invert the array? */
		res = search_result_new (SEARCH_RESULT_VOID);
	} else {
		res = search_term_cast_bool (ctx, 1, argv, user_data);
		res->value.bool = !res->value.bool;
	}
	
	return res;
}

static SearchResult *
search_term_lt (SearchContext *ctx, int argc, SearchTerm **argv, void *user_data)
{
	SearchResult *res, *r1 = NULL, *r2 = NULL;
	search_result_t type;
	
	if (argc != 2)
		search_context_throw (ctx, _("Incorrect number of arguments in < expression"));
	
	r1 = search_term_eval (ctx, argv[0], user_data);
	r2 = search_term_eval (ctx, argv[1], user_data);
	
	if (r1->type == r2->type) {
	compare:
		res = search_result_new (SEARCH_RESULT_BOOL);
		switch (r1->type) {
		case SEARCH_RESULT_BOOL:
			res->value.bool = (r1->value.bool ? 1 : 0) < (r2->value.bool ? 1 : 0);
			break;
		case SEARCH_RESULT_INT:
			res->value.bool = r1->value.integer < r2->value.integer;
			break;
		case SEARCH_RESULT_TIME:
			res->value.bool = r1->value.time < r2->value.time;
			break;
		case SEARCH_RESULT_FLOAT:
			res->value.bool = r1->value.decimal < r2->value.decimal;
			break;
		case SEARCH_RESULT_STRING:
			res->value.bool = strcmp (r1->value.string, r2->value.string) < 0;
			break;
		default:
			goto exception;
			break;
		}
	} else {
		type = MAX (r1->type, r2->type);
		if (r1->type == type) {
			res = search_result_convert (ctx, r2, type);
			search_result_free (r2);
			r2 = res;
			
			if (r2 == NULL)
				goto exception;
		} else {
			res = search_result_convert (ctx, r1, type);
			search_result_free (r1);
			r1 = res;
			
			if (r1 == NULL)
				goto exception;
		}
		
		goto compare;
	}
	
	search_result_free (r1);
	search_result_free (r2);
	
	return res;
	
 exception:
	
	if (r1)
		search_result_free (r1);
	
	if (r2)
		search_result_free (r2);
	
	search_context_throw (ctx, _("Incompatable types in comparison (<)"));
	
	return NULL;
}

static SearchResult *
search_term_gt (SearchContext *ctx, int argc, SearchTerm **argv, void *user_data)
{
	SearchResult *res, *r1 = NULL, *r2 = NULL;
	search_result_t type;
	
	if (argc != 2)
		search_context_throw (ctx, _("Incorrect number of arguments in > expression"));
	
	r1 = search_term_eval (ctx, argv[0], user_data);
	r2 = search_term_eval (ctx, argv[1], user_data);
	
	if (r1->type == r2->type) {
	compare:
		res = search_result_new (SEARCH_RESULT_BOOL);
		switch (r1->type) {
		case SEARCH_RESULT_BOOL:
			res->value.bool = (r1->value.bool ? 1 : 0) > (r2->value.bool ? 1 : 0);
			break;
		case SEARCH_RESULT_INT:
			res->value.bool = r1->value.integer > r2->value.integer;
			break;
		case SEARCH_RESULT_TIME:
			res->value.bool = r1->value.time > r2->value.time;
			break;
		case SEARCH_RESULT_FLOAT:
			res->value.bool = r1->value.decimal > r2->value.decimal;
			break;
		case SEARCH_RESULT_STRING:
			res->value.bool = strcmp (r1->value.string, r2->value.string) > 0;
			break;
		default:
			goto exception;
			break;
		}
	} else {
		type = MAX (r1->type, r2->type);
		if (r1->type == type) {
			res = search_result_convert (ctx, r2, type);
			search_result_free (r2);
			r2 = res;
			
			if (r2 == NULL)
				goto exception;
		} else {
			res = search_result_convert (ctx, r1, type);
			search_result_free (r1);
			r1 = res;
			
			if (r1 == NULL)
				goto exception;
		}
		
		goto compare;
	}
	
	search_result_free (r1);
	search_result_free (r2);
	
	return res;
	
 exception:
	
	if (r1)
		search_result_free (r1);
	
	if (r2)
		search_result_free (r2);
	
	search_context_throw (ctx, _("Incompatable types in comparison (>)"));
	
	return NULL;
}

static SearchResult *
search_term_eq (SearchContext *ctx, int argc, SearchTerm **argv, void *user_data)
{
	SearchResult *res, *r1 = NULL, *r2 = NULL;
	search_result_t type;
	
	if (argc != 2)
		search_context_throw (ctx, _("Incorrect number of arguments to = expression"));
	
	r1 = search_term_eval (ctx, argv[0], user_data);
	r2 = search_term_eval (ctx, argv[1], user_data);
	
	if (r1->type == r2->type) {
	compare:
		res = search_result_new (SEARCH_RESULT_BOOL);
		switch (r1->type) {
		case SEARCH_RESULT_BOOL:
			res->value.bool = (r1->value.bool ? 1 : 0) == (r2->value.bool ? 1 : 0);
			break;
		case SEARCH_RESULT_INT:
			res->value.bool = r1->value.integer == r2->value.integer;
			break;
		case SEARCH_RESULT_TIME:
			res->value.bool = r1->value.time == r2->value.time;
			break;
		case SEARCH_RESULT_FLOAT:
			res->value.bool = r1->value.decimal == r2->value.decimal;
			break;
		case SEARCH_RESULT_STRING:
			res->value.bool = strcmp (r1->value.string, r2->value.string) == 0;
			break;
		default:
			goto exception;
			break;
		}
	} else {
		type = MAX (r1->type, r2->type);
		if (r1->type == type) {
			res = search_result_convert (ctx, r2, type);
			search_result_free (r2);
			r2 = res;
			
			if (r2 == NULL)
				goto exception;
		} else {
			res = search_result_convert (ctx, r1, type);
			search_result_free (r1);
			r1 = res;
			
			if (r1 == NULL)
				goto exception;
		}
		
		goto compare;
	}
	
	search_result_free (r1);
	search_result_free (r2);
	
	return res;
	
 exception:
	
	if (r1)
		search_result_free (r1);
	
	if (r2)
		search_result_free (r2);
	
	search_context_throw (ctx, _("Incompatable types in comparison (=)"));
	
	return NULL;
}

static SearchResult *
search_term_add (SearchContext *ctx, int argc, SearchResult **argv, void *user_data)
{
	search_result_t type = SEARCH_RESULT_INT;
	SearchResult *res;
	double fsum = 0.0;
	int i, isum = 0;
	
	for (i = 0; i < argc; i++) {
		if (argv[i]->type == SEARCH_RESULT_FLOAT && type == SEARCH_RESULT_INT) {
			type = SEARCH_RESULT_FLOAT;
			fsum = (double) isum;
		}
		
		switch (argv[i]->type) {
		case SEARCH_RESULT_INT:
			if (type == SEARCH_RESULT_INT)
				isum += argv[i]->value.integer;
			else
				fsum += (double) argv[i]->value.integer;
			break;
		case SEARCH_RESULT_FLOAT:
			fsum += argv[i]->value.decimal;
			break;
		default:
			goto exception;
		}
	}
	
	res = search_result_new (type);
	if (type == SEARCH_RESULT_INT)
		res->value.integer = isum;
	else
		res->value.decimal = fsum;
	
	return res;
	
 exception:
	
	search_context_throw (ctx, _("Invalid types in (+ <%s>...)"),
			      type == SEARCH_RESULT_INT ? "int" : "float");
	
	return NULL;
}

static SearchResult *
search_term_sub (SearchContext *ctx, int argc, SearchResult **argv, void *user_data)
{
	search_result_t type = SEARCH_RESULT_INT;
	SearchResult *res;
	double fsum = 0.0;
	int i, isum = 0;
	
	if (argc > 0) {
		type = argv[0]->type;
		switch (type) {
		case SEARCH_RESULT_INT:
			isum = argv[0]->value.integer;
			break;
		case SEARCH_RESULT_FLOAT:
			fsum = argv[0]->value.decimal;
			break;
		default:
			goto exception;
		}
	}
	
	for (i = 1; i < argc; i++) {
		if (argv[i]->type == SEARCH_RESULT_FLOAT && type == SEARCH_RESULT_INT) {
			type = SEARCH_RESULT_FLOAT;
			fsum = (double) isum;
		}
		
		switch (argv[i]->type) {
		case SEARCH_RESULT_INT:
			if (type == SEARCH_RESULT_INT)
				isum -= argv[i]->value.integer;
			else
				fsum -= (double) argv[i]->value.integer;
			break;
		case SEARCH_RESULT_FLOAT:
			fsum -= argv[i]->value.decimal;
			break;
		default:
			goto exception;
		}
	}
	
	res = search_result_new (type);
	if (type == SEARCH_RESULT_INT)
		res->value.integer = isum;
	else
		res->value.decimal = fsum;
	
	return res;
	
 exception:
	
	search_context_throw (ctx, _("Invalid types in (- <%s>...)"),
			      type == SEARCH_RESULT_INT ? "int" : "float");
	
	return NULL;
}

static SearchResult *
search_term_mult (SearchContext *ctx, int argc, SearchResult **argv, void *user_data)
{
	search_result_t type = SEARCH_RESULT_INT;
	SearchResult *res;
	double fmult = 1.0;
	int i, imult = 1;
	
	for (i = 0; i < argc; i++) {
		if (argv[i]->type == SEARCH_RESULT_FLOAT && type == SEARCH_RESULT_INT) {
			type = SEARCH_RESULT_FLOAT;
			fmult = (double) imult;
		}
		
		switch (argv[i]->type) {
		case SEARCH_RESULT_INT:
			if (type == SEARCH_RESULT_INT)
				imult *= argv[i]->value.integer;
			else
				fmult *= (double) argv[i]->value.integer;
			break;
		case SEARCH_RESULT_FLOAT:
			fmult *= argv[i]->value.decimal;
			break;
		default:
			goto exception;
		}
	}
	
	res = search_result_new (type);
	if (type == SEARCH_RESULT_INT)
		res->value.integer = imult;
	else
		res->value.decimal = fmult;
	
	return res;
	
 exception:
	
	search_context_throw (ctx, _("Invalid types in (* <%s>...)"),
			      type == SEARCH_RESULT_INT ? "int" : "float");
	
	return NULL;
}

static SearchResult *
search_term_div (SearchContext *ctx, int argc, SearchResult **argv, void *user_data)
{
	search_result_t type = SEARCH_RESULT_INT;
	SearchResult *res;
	double fdiv = 0.0;
	int i, idiv = 1;
	
	if (argc > 0) {
		type = argv[0]->type;
		switch (type) {
		case SEARCH_RESULT_INT:
			idiv = argv[0]->value.integer;
			break;
		case SEARCH_RESULT_FLOAT:
			fdiv = argv[0]->value.decimal;
			break;
		default:
			goto exception;
		}
	}
	
	for (i = 0; i < argc; i++) {
		if (argv[i]->type == SEARCH_RESULT_FLOAT && type == SEARCH_RESULT_INT) {
			type = SEARCH_RESULT_FLOAT;
			fdiv = (double) idiv;
		}
		
		switch (argv[i]->type) {
		case SEARCH_RESULT_INT:
			if (argv[i]->value.integer == 0)
				goto exception;
			
			if (type == SEARCH_RESULT_INT)
				idiv /= argv[i]->value.integer;
			else
				fdiv /= (double) argv[i]->value.integer;
			break;
		case SEARCH_RESULT_FLOAT:
			if (argv[i]->value.decimal == 0)
				goto exception;
			
			fdiv /= argv[i]->value.decimal;
			break;
		default:
			goto exception;
		}
	}
	
	res = search_result_new (type);
	if (type == SEARCH_RESULT_INT)
		res->value.integer = idiv;
	else
		res->value.decimal = fdiv;
	
	return res;
	
 exception:
	
	search_context_throw (ctx, _("Invalid types in (/ <%s>...)"),
			      type == SEARCH_RESULT_INT ? "int" : "float");
	
	return NULL;
}

static SearchResult *
search_term_if (SearchContext *ctx, int argc, SearchTerm **argv, void *user_data)
{
	SearchResult *res, *expr;
	
	if (argc < 2 || argc > 3)
		search_context_throw (ctx, _("Invalid number of arguments to (if expr func )"));
	
	if (argc >= 2 && argc <= 3) {
		res = search_term_eval (ctx, argv[0], user_data);
		if (res->type != SEARCH_RESULT_BOOL) {
			expr = search_term_cast_bool (ctx, 1, &res, user_data);
			search_result_free (res);
		} else
			expr = res;
		
		if (expr->value.bool) {
			search_result_free (expr);
			return search_term_eval (ctx, argv[1], user_data);
		} else if (argc == 3) {
			search_result_free (expr);
			return search_term_eval (ctx, argv[2], user_data);
		}
	} else
		expr = search_result_new (SEARCH_RESULT_VOID);
	
	return expr;
}

static SearchResult *
search_term_begin (SearchContext *ctx, int argc, SearchTerm **argv, void *user_data)
{
	SearchResult *res;
	int i;
	
	res = search_result_new (SEARCH_RESULT_VOID);
	for (i = 0; i < argc; i++) {
		search_result_free (res);
		res = search_term_eval (ctx, argv[i], user_data);
	}
	
	return res;
}

static SearchResult *
search_term_cast_bool (SearchContext *ctx, int argc, SearchResult **argv, void *user_data)
{
	SearchResult *res;
	
	if (argc != 1)
		search_context_throw (ctx, _("Incorrect number of arguments to (cast-bool )"));
	
	res = search_result_new (SEARCH_RESULT_BOOL);
	switch (argv[0]->type) {
	case SEARCH_RESULT_BOOL:
		res->value.bool = argv[0]->value.bool;
		break;
	case SEARCH_RESULT_INT:
		res->value.bool = argv[0]->value.integer ? TRUE : FALSE;
		break;
	case SEARCH_RESULT_FLOAT:
		res->value.bool = ((int) argv[0]->value.decimal) ? TRUE : FALSE;
		break;
	case SEARCH_RESULT_STRING:
		if (!strcmp (argv[0]->value.string, "true") || !strcmp (argv[0]->value.string, "#t"))
			res->value.bool = TRUE;
		else
			res->value.bool = FALSE;
		break;
	default:
		res->value.bool = FALSE;
		break;
	}
	
	return res;
}

static SearchResult *
search_term_cast_int (SearchContext *ctx, int argc, SearchResult **argv, void *user_data)
{
	SearchResult *res;
	
	if (argc != 1)
		search_context_throw (ctx, _("Incorrect number of arguments to (cast-int )"));
	
	res = search_result_new (SEARCH_RESULT_INT);
	switch (argv[0]->type) {
	case SEARCH_RESULT_BOOL:
		res->value.integer = argv[0]->value.bool ? 1 : 0;
		break;
	case SEARCH_RESULT_INT:
		res->value.integer = argv[0]->value.integer;
		break;
	case SEARCH_RESULT_FLOAT:
		res->value.integer = (int) argv[0]->value.decimal;
		break;
	case SEARCH_RESULT_STRING:
		res->value.integer = strtol (argv[0]->value.string, NULL, 10);
		break;
	default:
		res->value.integer = 0;
		break;
	}
	
	return res;
}

static SearchResult *
search_term_cast_float (SearchContext *ctx, int argc, SearchResult **argv, void *user_data)
{
	SearchResult *res;
	
	if (argc != 1)
		search_context_throw (ctx, _("Incorrect number of arguments to (cast-float )"));
	
	res = search_result_new (SEARCH_RESULT_FLOAT);
	switch (argv[0]->type) {
	case SEARCH_RESULT_BOOL:
		res->value.decimal = argv[0]->value.bool ? 1.0 : 0.0;
		break;
	case SEARCH_RESULT_INT:
		res->value.decimal = argv[0]->value.integer * 1.0;
		break;
	case SEARCH_RESULT_FLOAT:
		res->value.decimal = argv[0]->value.decimal;
		break;
	case SEARCH_RESULT_STRING:
		res->value.decimal = strtod (argv[0]->value.string, NULL);
		break;
	default:
		res->value.decimal = 0.0;
		break;
	}
	
	return res;
}

static SearchResult *
search_term_cast_string (SearchContext *ctx, int argc, SearchResult **argv, void *user_data)
{
	SearchResult *res;
	
	if (argc != 1)
		search_context_throw (ctx, _("Incorrect number of arguments to (cast-string )"));
	
	res = search_result_new (SEARCH_RESULT_STRING);
	switch (argv[0]->type) {
	case SEARCH_RESULT_BOOL:
		res->value.string = argv[0]->value.bool ? g_strdup ("true") : g_strdup ("false");
		break;
	case SEARCH_RESULT_INT:
		res->value.string = g_strdup_printf ("%d", argv[0]->value.integer);
		break;
	case SEARCH_RESULT_FLOAT:
		res->value.string = g_strdup_printf ("%.2f", argv[0]->value.decimal);
		break;
	case SEARCH_RESULT_STRING:
		res->value.string = g_strdup (argv[0]->value.string);
		break;
	default:
		res->value.string = g_strdup ("");
		break;
	}
	
	return res;
}


static void
dump_value (SearchResult *res)
{
	int i;
	
	switch (res->type) {
	case SEARCH_RESULT_BOOL:
		printf ("%s ", res->value.bool ? "true" : "false");
		break;
	case SEARCH_RESULT_INT:
		printf ("%d ", res->value.integer);
		break;
	case SEARCH_RESULT_TIME:
		printf ("%ld ", res->value.time);
		break;
	case SEARCH_RESULT_FLOAT:
		printf ("%.2f ", res->value.decimal);
		break;
	case SEARCH_RESULT_STRING:
		printf ("\"%s\" ", res->value.string);
		break;
	case SEARCH_RESULT_LIST:
		printf ("(");
		for (i = 0; i < res->value.array->len; i++)
			dump_value (res->value.array->pdata[i]);
		printf (")");
		break;
	default:
		printf ("not a printable type: %d\n", res->type);
	}
}

int main (int argc, char **argv)
{
	SearchContext *ctx;
	SearchResult *res;
	
	ctx = search_context_new ();
	if (search_context_build (ctx, argv[1]) == -1) {
		printf ("failed to build?\n");
		return 0;
	}
	
	search_term_dump (ctx->tree);
	
	if (!(res = search_context_run (ctx, NULL))) {
		printf ("failed to evaluate: %s\n", search_context_exception (ctx));
		return 0;
	}
	
	switch (res->type) {
	case SEARCH_RESULT_BOOL:
		printf ("bool val = %s\n", res->value.bool ? "true" : "false");
		break;
	case SEARCH_RESULT_INT:
		printf ("int val = %d\n", res->value.integer);
		break;
	case SEARCH_RESULT_TIME:
		printf ("time_t val = %ld\n", res->value.time);
		break;
	case SEARCH_RESULT_FLOAT:
		printf ("float val = %.2f\n", res->value.decimal);
		break;
	case SEARCH_RESULT_STRING:
		printf ("string val = %s\n", res->value.string);
		break;
	case SEARCH_RESULT_LIST:
		printf ("list val = ");
		dump_value (res);
		printf ("\n");
		break;
	default:
		printf ("not a printable type: %d\n", res->type);
	}
	
	search_result_free (res);
	search_context_unref (ctx);
	
	return 0;
}
