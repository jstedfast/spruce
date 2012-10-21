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


#ifndef __SPRUCE_SASL_H__
#define __SPRUCE_SASL_H__

#include <glib.h>

#include <spruce/spruce-service.h>

G_BEGIN_DECLS

#define SPRUCE_TYPE_SASL            (spruce_sasl_get_type ())
#define SPRUCE_SASL(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), SPRUCE_TYPE_SASL, SpruceSASL))
#define SPRUCE_SASL_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), SPRUCE_TYPE_SASL, SpruceSASLClass))
#define SPRUCE_IS_SASL(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SPRUCE_TYPE_SASL))
#define SPRUCE_IS_SASL_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), SPRUCE_TYPE_SASL))
#define SPRUCE_SASL_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), SPRUCE_TYPE_SASL, SpruceSASLClass))

typedef struct _SpruceSASL SpruceSASL;
typedef struct _SpruceSASLClass SpruceSASLClass;

struct _SpruceSASL {
	GObject parent_object;
	
	char *service_name;
	char *mech;		/* mechanism */
	SpruceService *service;
	gboolean authenticated;
};

struct _SpruceSASLClass {
	GObjectClass parent_class;
	
	GByteArray * (* challenge) (SpruceSASL *sasl, GByteArray *token, GError **err);
};

GType spruce_sasl_get_type (void);

GByteArray *spruce_sasl_challenge (SpruceSASL *sasl, GByteArray *token, GError **err);
char *spruce_sasl_challenge_base64 (SpruceSASL *sasl, const char *token, GError **err);

gboolean spruce_sasl_authenticated (SpruceSASL *sasl);

/* utility functions */
SpruceSASL *spruce_sasl_new (const char *service_name, const char *mechanism, SpruceService *service);

GList *spruce_sasl_authtype_list (void);
SpruceServiceAuthType *spruce_sasl_authtype (const char *mechanism);

G_END_DECLS

#endif /* __SPRUCE_SASL_H__ */
