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


#ifndef __SPRUCE_SASL_ANONYMOUS_H__
#define __SPRUCE_SASL_ANONYMOUS_H__

#include <spruce/spruce-sasl.h>

G_BEGIN_DECLS

#define SPRUCE_TYPE_SASL_ANONYMOUS            (spruce_sasl_anonymous_get_type ())
#define SPRUCE_SASL_ANONYMOUS(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), SPRUCE_TYPE_SASL_ANONYMOUS, SpruceSASLAnonymous))
#define SPRUCE_SASL_ANONYMOUS_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), SPRUCE_TYPE_SASL_ANONYMOUS, SpruceSASLAnonymousClass))
#define SPRUCE_IS_SASL_ANONYMOUS(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SPRUCE_TYPE_SASL_ANONYMOUS))
#define SPRUCE_IS_SASL_ANONYMOUS_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), SPRUCE_TYPE_SASL_ANONYMOUS))
#define SPRUCE_SASL_ANONYMOUS_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), SPRUCE_TYPE_SASL_ANONYMOUS, SpruceSASLAnonymousClass))

typedef struct _SpruceSASLAnonymous SpruceSASLAnonymous;
typedef struct _SpruceSASLAnonymousClass SpruceSASLAnonymousClass;

typedef enum {
	SPRUCE_SASL_ANON_TRACE_EMAIL,
	SPRUCE_SASL_ANON_TRACE_OPAQUE,
	SPRUCE_SASL_ANON_TRACE_EMPTY
} SpruceSASLAnonTraceType;

struct _SpruceSASLAnonymous {
	SpruceSASL parent_object;
	
	SpruceSASLAnonTraceType type;
	char *trace_info;
};

struct _SpruceSASLAnonymousClass {
	SpruceSASLClass parent_class;
	
};


GType spruce_sasl_anonymous_get_type (void);

SpruceSASL *spruce_sasl_anonymous_new (SpruceSASLAnonTraceType type, const char *trace_info);

extern SpruceServiceAuthType spruce_sasl_anonymous_authtype;

G_END_DECLS

#endif /* __SPRUCE_SASL_ANONYMOUS_H__ */
