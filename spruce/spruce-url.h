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


#ifndef __SPRUCE_URL_H__
#define __SPRUCE_URL_H__

#include <glib.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define SPRUCE_TYPE_URL            (spruce_url_get_type ())
#define SPRUCE_URL(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), SPRUCE_TYPE_URL, SpruceURL))
#define SPRUCE_URL_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), SPRUCE_TYPE_URL, SpruceURLClass))
#define SPRUCE_IS_URL(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SPRUCE_TYPE_URL))
#define SPRUCE_IS_URL_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), SPRUCE_TYPE_URL))
#define SPRUCE_URL_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), SPRUCE_TYPE_URL, SpruceURLClass))

typedef struct _SpruceURL SpruceURL;
typedef struct _SpruceURLClass SpruceURLClass;


enum {
	SPRUCE_URL_PROTO_CHANGED    = (1 << 0),
	SPRUCE_URL_USER_CHANGED     = (1 << 1),
	SPRUCE_URL_AUTH_CHANGED     = (1 << 2),
	SPRUCE_URL_PASS_CHANGED     = (1 << 3),
	SPRUCE_URL_HOST_CHANGED     = (1 << 4),
	SPRUCE_URL_PORT_CHANGED     = (1 << 5),
	SPRUCE_URL_PATH_CHANGED     = (1 << 6),
	SPRUCE_URL_QUERY_CHANGED    = (1 << 7),
	SPRUCE_URL_PARAMS_CHANGED   = (1 << 8),
	SPRUCE_URL_FRAGMENT_CHANGED = (1 << 9),
};

enum {
	SPRUCE_URL_HIDE_PASSWD      = (1 << 0),
	SPRUCE_URL_HIDE_PARAMS      = (1 << 1),
	SPRUCE_URL_HIDE_QUERY       = (1 << 2),
	SPRUCE_URL_HIDE_FRAGMENT    = (1 << 3),
};

#define SPRUCE_URL_HIDE_ALL ((guint32) ~0)

struct _SpruceURL {
	GObject parent_object;
	
	char *protocol;
	char *user;
	char *auth;
	char *passwd;
	char *host;
	int port;
	char *path;
	GData *params;
	char *query;
	char *fragment;
};

struct _SpruceURLClass {
	GObjectClass parent_class;
	
	/* signals */
	void (* changed) (SpruceURL *url, guint32 flags);
};


GType spruce_url_get_type (void);


SpruceURL *spruce_url_new (void);
SpruceURL *spruce_url_new_from_string (const char *uri);

SpruceURL *spruce_url_copy (SpruceURL *url);

const char *spruce_url_get_protocol (SpruceURL *url);
void        spruce_url_set_protocol (SpruceURL *url, const char *protocol);
const char *spruce_url_get_user     (SpruceURL *url);
void        spruce_url_set_user     (SpruceURL *url, const char *user);
const char *spruce_url_get_auth     (SpruceURL *url);
void        spruce_url_set_auth     (SpruceURL *url, const char *auth);
const char *spruce_url_get_passwd   (SpruceURL *url);
void        spruce_url_set_passwd   (SpruceURL *url, const char *passwd);
const char *spruce_url_get_host     (SpruceURL *url);
void        spruce_url_set_host     (SpruceURL *url, const char *host);
int         spruce_url_get_port     (SpruceURL *url);
void        spruce_url_set_port     (SpruceURL *url, int port);
const char *spruce_url_get_path     (SpruceURL *url);
void        spruce_url_set_path     (SpruceURL *url, const char *path);
const char *spruce_url_get_param    (SpruceURL *url, const char *name);
void        spruce_url_set_param    (SpruceURL *url, const char *name, const char *value);
const char *spruce_url_get_query    (SpruceURL *url);
void        spruce_url_set_query    (SpruceURL *url, const char *query);
const char *spruce_url_get_fragment (SpruceURL *url);
void        spruce_url_set_fragment (SpruceURL *url, const char *fragment);

char       *spruce_url_to_string    (SpruceURL *url, guint32 hide);
gboolean    spruce_url_parse_string (SpruceURL *url, const char *uri);

G_END_DECLS

#endif /* __SPRUCE_URL_H__ */
