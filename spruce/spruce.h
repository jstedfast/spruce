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


#ifndef __SPRUCE_H__
#define __SPRUCE_H__

#include <glib.h>
#include <spruce/spruce-version.h>
#include <spruce/spruce-cache.h>
#include <spruce/spruce-cache-stream.h>
#include <spruce/spruce-error.h>
#include <spruce/spruce-folder.h>
#include <spruce/spruce-folder-search.h>
#include <spruce/spruce-folder-summary.h>
#include <spruce/spruce-provider.h>
#include <spruce/spruce-service.h>
#include <spruce/spruce-session.h>
#include <spruce/spruce-store.h>
#include <spruce/spruce-transport.h>
#include <spruce/spruce-url.h>

G_BEGIN_DECLS

/* Spruce version */

/**
 * spruce_major_version:
 *
 * Spruce's major version.
 **/
extern const guint spruce_major_version;

/**
 * spruce_minor_version:
 *
 * Spruce's minor version.
 **/
extern const guint spruce_minor_version;

/**
 * spruce_micro_version:
 *
 * Spruce's micro version.
 **/
extern const guint spruce_micro_version;

/**
 * spruce_interface_age:
 *
 * Spruce's interface age.
 **/
extern const guint spruce_interface_age;

/**
 * spruce_binary_age:
 *
 * Spruce's binary age.
 **/
extern const guint spruce_binary_age;

gboolean spruce_check_version (guint major, guint minor, guint micro);


int spruce_init (const char *spruce_dir);

int spruce_shutdown (void);

G_END_DECLS

#endif /* __SPRUCE_H__ */
