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
#include <string.h>
#include <ctype.h>

#include "spruce-url.h"
#include "spruce-marshal.h"
#include "spruce-string-utils.h"

/* FIXME: add support for rfc1808? */

/* see rfc1738, section 2.2 */
#define is_unsafe(c) (((unsigned char) c) <= 0x1f || ((unsigned char) c) == 0x7f)

enum {
	CHANGED,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0, };


static void spruce_url_class_init (SpruceURLClass *klass);
static void spruce_url_init (SpruceURL *url, SpruceURLClass *klass);
static void spruce_url_finalize (GObject *object);


static GObjectClass *parent_class = NULL;


GType
spruce_url_get_type (void)
{
	static GType type = 0;
	
	if (!type) {
		static const GTypeInfo info = {
			sizeof (SpruceURLClass),
			NULL, /* base_class_init */
			NULL, /* base_class_finalize */
			(GClassInitFunc) spruce_url_class_init,
			NULL, /* class_finalize */
			NULL, /* class_data */
			sizeof (SpruceURL),
			0,    /* n_preallocs */
			(GInstanceInitFunc) spruce_url_init,
		};
		
		type = g_type_register_static (G_TYPE_OBJECT, "SpruceURL", &info, 0);
	}
	
	return type;
}


static void
spruce_url_class_init (SpruceURLClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	
	parent_class = g_type_class_ref (G_TYPE_OBJECT);
	
	object_class->finalize = spruce_url_finalize;
	
	signals[CHANGED] =
		g_signal_new ("changed",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (SpruceURLClass, changed),
			      NULL, NULL,
			      spruce_marshal_VOID__UINT,
			      G_TYPE_NONE, 1,
			      G_TYPE_UINT);
}

static void
spruce_url_init (SpruceURL *url, SpruceURLClass *klass)
{
	url->protocol = NULL;
	url->user = NULL;
	url->auth = NULL;
	url->passwd = NULL;
	url->host = NULL;
	url->port = 0;
	url->path = NULL;
	url->params = NULL;
	url->query = NULL;
	url->fragment = NULL;
}

static void
spruce_url_finalize (GObject *object)
{
	SpruceURL *url = (SpruceURL *) object;
	
	g_free (url->protocol);
	g_free (url->user);
	g_free (url->auth);
	g_free (url->passwd);
	g_free (url->host);
	g_free (url->path);
	g_datalist_clear (&url->params);
	g_free (url->query);
	g_free (url->fragment);
	
	G_OBJECT_CLASS (parent_class)->finalize (object);
}


SpruceURL *
spruce_url_new (void)
{
	SpruceURL *url;
	
	url = g_object_new (SPRUCE_TYPE_URL, NULL);
	
	return url;
}


SpruceURL *
spruce_url_new_from_string (const char *uri)
{
	SpruceURL *url;
	
	url = g_object_new (SPRUCE_TYPE_URL, NULL);
	if (!spruce_url_parse_string (url, uri)) {
		g_object_unref (url);
		return NULL;
	}
	
	return url;
}


static void
copy_param (GQuark key, gpointer value, gpointer user_data)
{
	GData **data = user_data;
	
	g_datalist_id_set_data_full (data, key, g_strdup (value), g_free);
}

SpruceURL *
spruce_url_copy (SpruceURL *url)
{
	SpruceURL *dup;
	
	dup = g_object_new (SPRUCE_TYPE_URL, NULL);
	dup->protocol = g_strdup (url->protocol);
	dup->user = g_strdup (url->user);
	dup->auth = g_strdup (url->auth);
	dup->passwd = g_strdup (url->passwd);
	dup->host = g_strdup (url->host);
	dup->port = url->port;
	dup->path = g_strdup (url->path);
	dup->query = g_strdup (url->query);
	dup->fragment = g_strdup (url->fragment);
	
	if (url->params)
		g_datalist_foreach (&url->params, copy_param, &dup->params);
	
	return dup;
}


const char *
spruce_url_get_protocol (SpruceURL *url)
{
	g_return_val_if_fail (SPRUCE_IS_URL (url), NULL);
	
	return url->protocol;
}


void
spruce_url_set_protocol (SpruceURL *url, const char *protocol)
{
	char *new;
	
	g_return_if_fail (SPRUCE_IS_URL (url));
	g_return_if_fail (protocol != NULL);
	
	new = g_strdup (protocol);
	spruce_strdown (new);
	
	if (!strcmp (url->protocol, new)) {
		/* new and old values are identical */
		g_free (new);
		return;
	}
	
	g_free (url->protocol);
	url->protocol = new;
	
	g_signal_emit (url, signals[CHANGED], 0, SPRUCE_URL_PROTO_CHANGED);
}


const char *
spruce_url_get_user (SpruceURL *url)
{
	g_return_val_if_fail (SPRUCE_IS_URL (url), NULL);
	
	return url->user;
}


void
spruce_url_set_user (SpruceURL *url, const char *user)
{
	g_return_if_fail (SPRUCE_IS_URL (url));
	
	if (!url->user && !user)
		return;
	
	if (url->user && user && !strcmp (url->user, user))
		return;
	
	g_free (url->user);
	url->user = g_strdup (user);
	
	g_signal_emit (url, signals[CHANGED], 0, SPRUCE_URL_USER_CHANGED);
}


const char *
spruce_url_get_auth (SpruceURL *url)
{
	g_return_val_if_fail (SPRUCE_IS_URL (url), NULL);
	
	return url->auth;
}


void
spruce_url_set_auth (SpruceURL *url, const char *auth)
{
	g_return_if_fail (SPRUCE_IS_URL (url));
	
	if (!url->auth && !auth)
		return;
	
	if (url->auth && auth && !strcmp (url->auth, auth))
		return;
	
	g_free (url->auth);
	url->auth = g_strdup (auth);
	
	g_signal_emit (url, signals[CHANGED], 0, SPRUCE_URL_AUTH_CHANGED);
}


const char *
spruce_url_get_passwd (SpruceURL *url)
{
	g_return_val_if_fail (SPRUCE_IS_URL (url), NULL);
	
	return url->passwd;
}


void
spruce_url_set_passwd (SpruceURL *url, const char *passwd)
{
	g_return_if_fail (SPRUCE_IS_URL (url));
	
	if (!url->passwd && !passwd)
		return;
	
	if (url->passwd && passwd && !strcmp (url->passwd, passwd))
		return;
	
	g_free (url->passwd);
	url->passwd = g_strdup (passwd);
	
	g_signal_emit (url, signals[CHANGED], 0, SPRUCE_URL_PASS_CHANGED);
}


const char *
spruce_url_get_host (SpruceURL *url)
{
	g_return_val_if_fail (SPRUCE_IS_URL (url), NULL);
	
	return url->host;
}


void
spruce_url_set_host (SpruceURL *url, const char *host)
{
	g_return_if_fail (SPRUCE_IS_URL (url));
	
	if (!url->host && !host)
		return;
	
	if (url->host && host && !strcmp (url->host, host))
		return;
	
	g_free (url->host);
	url->host = g_strdup (host);
	
	g_signal_emit (url, signals[CHANGED], 0, SPRUCE_URL_HOST_CHANGED);
}


int
spruce_url_get_port (SpruceURL *url)
{
	g_return_val_if_fail (SPRUCE_IS_URL (url), -1);
	
	return url->port;
}


void
spruce_url_set_port (SpruceURL *url, int port)
{
	g_return_if_fail (SPRUCE_IS_URL (url));
	
	if (url->port == port)
		return;
	
	url->port = port;
	
	g_signal_emit (url, signals[CHANGED], 0, SPRUCE_URL_PORT_CHANGED);
}


const char *
spruce_url_get_path (SpruceURL *url)
{
	g_return_val_if_fail (SPRUCE_IS_URL (url), NULL);
	
	return url->path;
}


/* canonicalise a path */
static char *
canon_path (char *path, int allow_root)
{
	register char *d, *inptr;
	
	d = inptr = path;
	
	while (*inptr) {
		if (inptr[0] == '/' && (inptr[1] == '/' || inptr[1] == '\0'))
			inptr++;
		else
			*d++ = *inptr++;
	}
	
	if (!allow_root && (d == path + 1) && d[-1] == '/')
		d--;
	else if (allow_root && d == path && path[0] == '/')
		*d++ = '/';
	
	*d = '\0';
	
	return path[0] ? path : NULL;
}

void
spruce_url_set_path (SpruceURL *url, const char *path)
{
	char *buf;
	
	g_return_if_fail (SPRUCE_IS_URL (url));
	
	if (path != NULL) {
		buf = g_alloca (strlen (path) + 1);
		strcpy (buf, path);
		
		path = canon_path (buf, !url->host);
	}
	
	if (!url->path && !path)
		return;
	
	if (url->path && path && !strcmp (url->path, path))
		return;
	
	g_free (url->path);
	url->path = g_strdup (path);
	
	g_signal_emit (url, signals[CHANGED], 0, SPRUCE_URL_PATH_CHANGED);
}


const char *
spruce_url_get_param (SpruceURL *url, const char *name)
{
	g_return_val_if_fail (SPRUCE_IS_URL (url), NULL);
	
	return g_datalist_get_data (&url->params, name);
}


void
spruce_url_set_param (SpruceURL *url, const char *name, const char *value)
{
	const char *current;
	
	g_return_if_fail (SPRUCE_IS_URL (url));
	
	current = g_datalist_get_data (&url->params, name);
	
	if (current && value && !strcmp (current, value))
		return;
	
	g_datalist_set_data_full (&url->params, name, value ? g_strdup (value) : NULL, g_free);
	
	g_signal_emit (url, signals[CHANGED], 0, SPRUCE_URL_PARAMS_CHANGED);
}


const char *
spruce_url_get_query (SpruceURL *url)
{
	g_return_val_if_fail (SPRUCE_IS_URL (url), NULL);
	
	return url->query;
}


void
spruce_url_set_query (SpruceURL *url, const char *query)
{
	g_return_if_fail (SPRUCE_IS_URL (url));
	
	if (!url->query && !query)
		return;
	
	if (url->query && query && !strcmp (url->query, query))
		return;
	
	g_free (url->query);
	url->query = g_strdup (query);
	
	g_signal_emit (url, signals[CHANGED], 0, SPRUCE_URL_QUERY_CHANGED);
}


const char *
spruce_url_get_fragment (SpruceURL *url)
{
	g_return_val_if_fail (SPRUCE_IS_URL (url), NULL);
	
	return url->fragment;
}


void
spruce_url_set_fragment (SpruceURL *url, const char *fragment)
{
	g_return_if_fail (SPRUCE_IS_URL (url));
	
	if (!url->fragment && !fragment)
		return;
	
	if (url->fragment && fragment && !strcmp (url->fragment, fragment))
		return;
	
	g_free (url->fragment);
	url->fragment = g_strdup (fragment);
	
	g_signal_emit (url, signals[CHANGED], 0, SPRUCE_URL_FRAGMENT_CHANGED);
}


static void
append_url_encoded (GString *string, const char *in, const char *extra)
{
	register const char *inptr = in;
	const char *start;
	
	while (*inptr) {
		start = inptr;
		while (*inptr && !is_unsafe (*inptr) && !strchr (extra, *inptr))
			inptr++;
		
		g_string_append_len (string, start, inptr - start);
		
		while (*inptr && (is_unsafe (*inptr) || strchr (extra, *inptr)))
			g_string_append_printf (string, "%%%.02hx", *inptr++);
	}
}

static void
append_param (GQuark key_id, gpointer value, gpointer user_data)
{
	GString *string = user_data;
	
	g_string_append_c (string, ';');
	append_url_encoded (string, g_quark_to_string (key_id), "?=#");
	if (*(char *) value) {
		g_string_append_c (string, '=');
		append_url_encoded (string, value, "?;#");
	}
}

char *
spruce_url_to_string (SpruceURL *url, guint32 hide)
{
	GString *string;
	char *uri;
	
	g_return_val_if_fail (SPRUCE_IS_URL (url), NULL);
	
	string = g_string_new (url->protocol);
	g_string_append (string, "://");
	
	if (url->host) {
		if (url->user) {
			append_url_encoded (string, url->user, ":;@/");
			
			if (url->auth) {
				g_string_append (string, ";auth=");
				append_url_encoded (string, url->auth, ":@/");
			}
			
			if (!(hide & SPRUCE_URL_HIDE_PASSWD) && url->passwd) {
				g_string_append_c (string, ':');
				append_url_encoded (string, url->passwd, "@/");
			}
			
			g_string_append_c (string, '@');
		}
		
		g_string_append (string, url->host);
		
		if (url->port > 0)
			g_string_append_printf (string, ":%d", url->port);
	}
	
	if (url->path) {
		if (url->host && *url->path != '/')
			g_string_append_c (string, '/');
		
		append_url_encoded (string, url->path, ";?#");
	} else if (url->host && (url->params || url->query || url->fragment)) {
		g_string_append_c (string, '/');
	}
	
	if (url->params && !(hide & SPRUCE_URL_HIDE_PARAMS))
		g_datalist_foreach (&url->params, append_param, string);
	
	if (url->query && !(hide & SPRUCE_URL_HIDE_QUERY)) {
		g_string_append_c (string, '?');
		append_url_encoded (string, url->query, "#");
	}
	
	if (url->fragment && !(hide & SPRUCE_URL_HIDE_FRAGMENT)) {
		g_string_append_c (string, '#');
		append_url_encoded (string, url->fragment, "");
	}
	
	uri = string->str;
	g_string_free (string, FALSE);
	
	return uri;
}


#define HEXVAL(c) (isdigit (c) ? (c) - '0' : tolower (c) - 'a' + 10)

static void
url_decode (char *in, const char *url)
{
	register const unsigned char *inptr;
	register unsigned char *outptr;
	
	inptr = outptr = (unsigned char *) in;
	while (*inptr) {
		if (*inptr == '%') {
			if (isxdigit ((int) inptr[1]) && isxdigit ((int) inptr[2])) {
				*outptr++ = HEXVAL (inptr[1]) * 16 + HEXVAL (inptr[2]);
				inptr += 3;
			} else {
				g_warning ("Invalid encoding in url: %s at %s", url, inptr);
				*outptr++ = *inptr++;
			}
		} else
			*outptr++ = *inptr++;
	}
	
	*outptr = '\0';
}


static int
str_equal (const char *str0, const char *str1)
{
	if (str0 == NULL) {
		if (str1 == NULL)
			return TRUE;
		else
			return FALSE;
	}
	
	if (str1 == NULL)
		return FALSE;
	
	return strcmp (str0, str1) == 0;
}

static int
params_equal (GData *p0, GData *p1)
{
	if (p0 == NULL) {
		if (p1 == NULL)
			return TRUE;
		else
			return FALSE;
	}
	
	if (p1 == NULL)
		return FALSE;
	
	/* FIXME: compare name/value pairs */
	
	return FALSE;
}

gboolean
spruce_url_parse_string (SpruceURL *url, const char *uri)
{
	char *protocol, *user = NULL, *auth = NULL, *passwd = NULL, *host = NULL, *path = NULL, *query = NULL, *fragment = NULL;
	register const char *start, *inptr;
	GData *params = NULL;
	guint32 changed = 0;
	int port = 0;
	size_t n;
	
	g_return_if_fail (SPRUCE_IS_URL (url));
	
	start = uri;
	if (!(inptr = strchr (start, ':'))) {
		g_warning ("No protocol detected in url: %s", uri);
		return FALSE;
	}
	
	protocol = g_ascii_strdown (start, inptr - start);
	
	inptr++;
	if (!*inptr)
		goto done;
	
	if (!strncmp (inptr, "//", 2))
		inptr += 2;
	
	start = inptr;
	while (*inptr && *inptr != ';' && *inptr != ':' && *inptr != '@' && *inptr != '/')
		inptr++;
	
	switch (*inptr) {
	case ';': /* <user>;auth= */
	case ':': /* <user>:passwd or <host>:port */
	case '@': /* <user>@host */
		if (inptr - start) {
			user = g_strndup (start, inptr - start);
			url_decode (user, uri);
		}
		
		switch (*inptr) {
		case ';': /* ;auth= */
			if (!g_ascii_strncasecmp (inptr, ";auth=", 6)) {
				inptr += 6;
				start = inptr;
				while (*inptr && *inptr != ':' && *inptr != '@')
					inptr++;
				
				if (inptr - start) {
					auth = g_strndup (start, inptr - start);
					url_decode (auth, uri);
				}
				
				if (*inptr == ':') {
					inptr++;
					start = inptr;
					goto decode_passwd;
				} else if (*inptr == '@') {
					inptr++;
					start = inptr;
					goto decode_host;
				}
			}
			break;
		case ':': /* <user>:passwd@ or <host>:port */
			inptr++;
			start = inptr;
		decode_passwd:
			while (*inptr && *inptr != '@' && *inptr != '/')
				inptr++;
			
			if (*inptr == '@') {
				/* <user>:passwd@ */
				if (inptr - start) {
					passwd = g_strndup (start, inptr - start);
					url_decode (passwd, uri);
				}
				
				inptr++;
				start = inptr;
				goto decode_host;
			} else {
				/* <host>:port */
				host = user;
				user = NULL;
				inptr = start;
				goto decode_port;
			}
			
			break;
		case '@': /* <user>@host */
			inptr++;
			start = inptr;
		decode_host:
			while (*inptr && *inptr != ':' && *inptr != '/')
				inptr++;
			
			if (inptr > start) {
				n = inptr - start;
				while (n > 0 && start[n - 1] == '.')
					n--;
				
				if (n > 0)
					host = g_strndup (start, n);
			}
			
			if (*inptr == ':') {
				inptr++;
			decode_port:
				port = 0;
				
				while (*inptr >= '0' && *inptr <= '9' && port < 6554)
					port = (port * 10) + ((*inptr++) - '0');
				
				if (port > 65535) {
					/* chop off the last digit */
					port /= 10;
				}
				
				while (*inptr && *inptr != '/')
					inptr++;
			}
		}
		break;
	case '/': /* <host>/path or simply <host> */
	case '\0':
		if (inptr > start) {
			n = inptr - start;
			while (n > 0 && start[n - 1] == '.')
				n--;
			
			if (n > 0)
				host = g_strndup (start, n);
		}
		break;
	default:
		break;
	}
	
	if (*inptr == '/') {
		char *name, *value;
		
		/* look for params, query, or fragment */
		start = inptr;
		while (*inptr && *inptr != ';' && *inptr != '?' && *inptr != '#')
			inptr++;
		
		/* canonicalise and save the path component */
		if ((n = (inptr - start))) {
			value = g_strndup (start, n);
			url_decode (value, uri);
			
			if (!(path = canon_path (value, !host)))
				g_free (value);
		}
		
		switch (*inptr) {
		case ';':
			while (*inptr == ';') {
				while (*inptr == ';')
					inptr++;
				
				start = inptr;
				while (*inptr && *inptr != '=' && *inptr != ';' && *inptr != '?' && *inptr != '#')
					inptr++;
				
				name = g_strndup (start, inptr - start);
				url_decode (name, uri);
				
				if (*inptr == '=') {
					inptr++;
					start = inptr;
					while (*inptr && *inptr != ';' && *inptr != '?' && *inptr != '#')
						inptr++;
					
					value = g_strndup (start, inptr - start);
					url_decode (value, uri);
				} else {
					value = g_strdup ("");
				}
				
				g_datalist_set_data_full (&params, name, value, g_free);
				g_free (name);
			}
			
			if (*inptr == '#')
				goto decode_fragment;
			else if (*inptr != '?')
				break;
			
			/* fall thru */
		case '?':
			inptr++;
			start = inptr;
			while (*inptr && *inptr != '#')
				inptr++;
			
			query = g_strndup (start, inptr - start);
			url_decode (query, uri);
			
			if (*inptr != '#')
				break;
			
			/* fall thru */
		case '#':
		decode_fragment:
			fragment = g_strdup (inptr + 1);
			url_decode (fragment, uri);
			break;
		}
	}
	
 done:
	
	if (!str_equal (url->protocol, protocol)) {
		changed |= SPRUCE_URL_PROTO_CHANGED;
		g_free (url->protocol);
		url->protocol = protocol;
	} else {
		g_free (protocol);
	}
	
	if (!str_equal (url->user, user)) {
		changed |= SPRUCE_URL_USER_CHANGED;
		g_free (url->user);
		url->user = user;
	} else {
		g_free (user);
	}
	
	if (!str_equal (url->auth, auth)) {
		changed |= SPRUCE_URL_AUTH_CHANGED;
		g_free (url->auth);
		url->auth = auth;
	} else {
		g_free (auth);
	}
	
	if (!str_equal (url->passwd, passwd)) {
		changed |= SPRUCE_URL_PASS_CHANGED;
		g_free (url->passwd);
		url->passwd = passwd;
	} else {
		g_free (passwd);
	}
	
	if (!str_equal (url->host, host)) {
		changed |= SPRUCE_URL_HOST_CHANGED;
		g_free (url->host);
		url->host = host;
	} else {
		g_free (host);
	}
	
	if (url->port != port) {
		changed |= SPRUCE_URL_PORT_CHANGED;
		url->port = port;
	}
	
	if (!str_equal (url->path, path)) {
		changed |= SPRUCE_URL_PATH_CHANGED;
		g_free (url->path);
		url->path = path;
	} else {
		g_free (path);
	}
	
	if (!params_equal (url->params, params)) {
		changed |= SPRUCE_URL_PARAMS_CHANGED;
		g_datalist_clear (&url->params);
		url->params = params;
	} else {
		g_datalist_clear (&params);
	}
	
	if (!str_equal (url->query, query)) {
		changed |= SPRUCE_URL_QUERY_CHANGED;
		g_free (url->query);
		url->query = query;
	} else {
		g_free (query);
	}
	
	if (!str_equal (url->fragment, fragment)) {
		changed |= SPRUCE_URL_FRAGMENT_CHANGED;
		g_free (url->fragment);
		url->fragment = fragment;
	} else {
		g_free (fragment);
	}
	
	g_signal_emit (url, signals[CHANGED], 0, changed);
	
	return TRUE;
}
