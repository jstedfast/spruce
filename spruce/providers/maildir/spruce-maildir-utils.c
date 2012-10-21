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

#include <glib.h>

#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>

#include "spruce-maildir-utils.h"


static char *maildir_subdirs[] = { "cur", "new", "tmp" };


/**
 * maildir_foreach:
 * @maildir: maildir path
 * @func: foreach func
 * @user_data: user data
 *
 * Calls @func for each message in each of @maildir/cur, @maildir/new,
 * and @maildir/tmp until either @func has been called once for each
 * and every message or until @func returns <= 0. @func should return
 * -1 on an error condition, 0 to simply stop the foreach, or 1 to
 * continue scanning messages.
 *
 * Returns 0 on success or -1 on error (errno will be preserved if set in @func).
 **/
int
maildir_foreach (const char *maildir, MaildirForeachFunc func, void *user_data)
{
	struct dirent *dent;
	char *subdir, *p;
	int save, i;
	int ret = 0;
	DIR *dir;
	
	subdir = g_alloca (strlen (maildir) + 5);
	p = g_stpcpy (subdir, maildir);
	*p++ = '/';
	
	for (i = 0; i < 3; i++) {
		strcpy (p, maildir_subdirs[i]);
		if (!(dir = opendir (subdir)))
			return -1;
		
		while ((dent = readdir (dir))) {
			if (dent->d_name[0] == '.')
				continue;
			
			if ((ret = (*func) (maildir, maildir_subdirs[i], dent->d_name, user_data)) <= 0) {
				save = errno;
				closedir (dir);
				errno = save;
				goto abort;
			}
		}
		
		closedir (dir);
	}
	
 abort:
	
	return ret < 0 ? ret : 0;
}


guint
maildir_hash (const char *key)
{
	const char *p = key;
	guint hash = *p++;
	
	if (!hash)
		return hash;
	
	while (*p && *p != ':')
		hash = (hash << 5) - hash + *p++;
	
	return hash;
}


int
maildir_equal (const char *str1, const char *str2)
{
	while (*str1 && *str1 != ':' && *str2 != ':' && *str1++ == *str2++)
		;
	
	if ((*str1 == '\0' && *str2 == ':') || (*str1 == ':' && *str2 == '\0'))
		return TRUE;
	
	return *str1 == *str2;
}
