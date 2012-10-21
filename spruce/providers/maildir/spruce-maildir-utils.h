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


#ifndef __SPRUCE_MAILDIR_UTILS_H__
#define __SPRUCE_MAILDIR_UTILS_H__

#include <glib.h>

G_BEGIN_DECLS

/* return 1 to keep going, 0 to stop, or -1 on error (will abort the foreach) */
typedef int (*MaildirForeachFunc) (const char *maildir, const char *subdir,
				   const char *d_name, void *user_data);

int maildir_foreach (const char *maildir, MaildirForeachFunc func, void *user_data);

guint maildir_hash (const char *key);
int maildir_equal (const char *str1, const char *str2);

G_END_DECLS

#endif /* __SPRUCE_MAILDIR_UTILS_H__ */
