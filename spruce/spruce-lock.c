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

#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include <glib/gi18n.h>

#include <spruce/spruce-error.h>
#include <spruce/spruce-lock.h>


#define SPRUCE_DOT_LOCK_RETRY    5
#define SPRUCE_DOT_LOCK_DELAY    2
#define SPRUCE_DOT_LOCK_STALE    60

#ifdef USE_DOT_LOCK
static int
spruce_lock_dot (const char *path, GError **err)
{
	char *lock, *tmp;
	struct stat st;
	int retry = 0;
	size_t len;
	int fd;
	
	len = strlen (path);
	lock = g_alloca (len + 6);
	sprintf (lock, "%s.lock", path);
	
	tmp = g_alloca (len + 7);
	
	do {
		sprintf (tmp, "%sXXXXXX", path);
		if (mktemp (tmp) && (fd = open (tmp, O_WRONLY | O_CREAT | O_TRUNC | O_EXCL, 0600)) != -1) {
			close (fd);
			
			/* apparently the return val of link(2) is unreliable on NFS so ignore it */
			link (tmp, lock);
			
			if (stat (tmp, &st) == 0) {
				unlink (tmp);
				
				if (st.st_nlink == 2)
					return 0;
			}
			
			unlink (lock);
			unlink (tmp);
		}
		
		/* remove stale locks */
		if (stat (lock, &st) == 0) {
			time_t now = time (NULL);
			
			if (st.st_ctime < now - SPRUCE_DOT_LOCK_STALE)
				unlink (lock);
		}
		
		retry++;
		if (retry < SPRUCE_DOT_LOCK_RETRY)
			sleep (SPRUCE_DOT_LOCK_DELAY);
	} while (retry < SPRUCE_DOT_LOCK_RETRY);
	
	g_set_error (err, SPRUCE_ERROR, errno, _("Cannot get lock file for `%s': %s"), path, g_strerror (errno));
	
	return -1;
}
#endif /* USE_DOT_LOCK */

#ifdef USE_FCNTL
static int
spruce_lock_fcntl (const char *path, int fd, GError **err)
{
	struct flock lock;
	
	lock.l_type = F_WRLCK;
	lock.l_whence = SEEK_SET;
	lock.l_start = 0;
	lock.l_len = 0;
	lock.l_pid = getpid ();
	
	if (fcntl (fd, F_SETLK, &lock) == 0)
		return 0;
	
	g_set_error (err, SPRUCE_ERROR, errno, _("Cannot get lock file for `%s': %s"), path, g_strerror (errno));
	
	return -1;
}
#endif /* USE_FCNTL */

#ifdef USE_FLOCK
static int
spruce_lock_flock (const char *path, int fd, GError **err)
{
	if (flock (fd, LOCK_EX | LOCK_NB) == 0)
		return 0;
	
	g_set_error (err, SPRUCE_ERROR, errno, _("Cannot get lock file for `%s': %s"), path, g_strerror (errno));
	
	return -1;
}
#endif /* USE_FLOCK */

#ifdef USE_LOCKF
static int
spruce_lock_lockf (const char *path, int fd, GError **err)
{
	if (lockf (fd, F_TLOCK, 0) == 0)
		return 0;
	
	g_set_error (err, SPRUCE_ERROR, errno, _("Cannot get lock file for `%s': %s"), path, g_strerror (errno));
	
	return -1;
}
#endif /* USE_LOCKF */


int
spruce_lock (const char *path, int fd, GError **err)
{
#ifdef USE_DOT_LOCK
	if (spruce_lock_dot (path, err) == -1)
		return -1;
#endif
	
#ifdef USE_FCNTL
	if (spruce_lock_fcntl (path, fd, err) == -1)
		return -1;
#endif
	
#ifdef USE_FLOCK
	if (spruce_lock_flock (path, fd, err) == -1)
		return -1;
#endif

#ifdef USE_LOCKF
	if (spruce_lock_lockf (path, fd, err) == -1)
		return -1;
#endif
	
	return 0;
}


#ifdef USE_DOT_LOCK
static int
spruce_unlock_dot (const char *path, GError **err)
{
	char *lock;
	
	lock = g_alloca (strlen (path) + 6);
	sprintf (lock, "%s.lock", path);
	
	unlink (lock);
	
	return 0;
}
#endif /* USE_DOT_LOCK */

#ifdef USE_FCNTL
static int
spruce_unlock_fcntl (const char *path, int fd, GError **err)
{
	struct flock lock;
	
	lock.l_type = F_UNLCK;
	lock.l_whence = SEEK_SET;
	lock.l_start = 0;
	lock.l_len = 0;
	lock.l_pid = getpid ();
	
	if (fcntl (fd, F_SETLK, &lock) == 0)
		return 0;
	
	g_set_error (err, SPRUCE_ERROR, errno, _("Cannot get lock file for `%s': %s"), path, g_strerror (errno));
	
	return -1;
}
#endif /* USE_FCNTL */

#ifdef USE_FLOCK
static int
spruce_unlock_flock (const char *path, int fd, GError **err)
{
	if (flock (fd, LOCK_UN) == 0)
		return 0;
	
	g_set_error (err, SPRUCE_ERROR, errno, _("Cannot unlock `%s': %s"), path, g_strerror (errno));
	
	return -1;
}
#endif /* USE_FLOCK */

#ifdef USE_LOCKF
static int
spruce_unlock_lockf (const char *path, int fd, GError **err)
{
	if (lockf (fd, F_ULOCK, 0) == 0)
		return 0;
	
	g_set_error (err, SPRUCE_ERROR, errno, _("Cannot unlock `%s': %s"), path, g_strerror (errno));
	
	return -1;
}
#endif /* USE_LOCKF */


int
spruce_unlock (const char *path, int fd, GError **err)
{
#ifdef USE_DOT_LOCK
	if (spruce_unlock_dot (path, err) == -1)
		return -1;
#endif
	
#ifdef USE_FCNTL
	if (spruce_unlock_fcntl (path, fd, err) == -1)
		return -1;
#endif
	
#ifdef USE_FLOCK
	if (spruce_unlock_flock (path, fd, err) == -1)
		return -1;
#endif

#ifdef USE_LOCKF
	if (spruce_unlock_lockf (path, fd, err) == -1)
		return -1;
#endif
	
	return 0;
}
