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
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include <glib/gi18n.h>

#include <spruce/spruce-error.h>
#include <spruce/spruce-process.h>

#define d(x)


pid_t
spruce_process_fork (const char *path, char **argv, gboolean redirect, int ignfd, int *infd, int *outfd, int *errfd, GError **err)
{
	int errnosav, fds[6], i;
	pid_t pid;
	
	for (i = 0; i < 6; i++)
		fds[i] = -1;
	
	for (i = 0; i < 6; i += 2) {
		if (pipe (fds + i) == -1) {
			errnosav = errno;
			g_set_error (err, SPRUCE_ERROR, errno,
				     _("Failed to create pipe to '%s': %s"),
				     argv[0], g_strerror (errno));
			
			for (i = 0; i < 6; i++) {
				if (fds[i] == -1)
					break;
				close (fds[i]);
			}
			
			errno = errnosav;
			
			return -1;
		}
	}
	
#if d(!)0
	fprintf (stderr, "exec()'ing %s\n", path);
	for (i = 0; argv[i]; i++)
		fprintf (stderr, "%s ", argv[i]);
	fprintf (stderr, "\n");
#endif
	
	if (!(pid = fork ())) {
		/* child process */
		int maxfd, nullfd = -1;
		
		if (!redirect) {
			if (!infd || !outfd || !errfd)
				nullfd = open ("/dev/null", O_WRONLY);
			
			if (dup2 (infd ? fds[0] : nullfd, STDIN_FILENO) == -1)
				_exit (255);
			
			if (dup2 (outfd ? fds[3] : nullfd, STDOUT_FILENO) == -1)
				_exit (255);
			
			if (dup2 (errfd ? fds[5] : nullfd, STDERR_FILENO) == -1)
				_exit (255);
		}
		
		setsid ();
		
		if ((maxfd = sysconf (_SC_OPEN_MAX)) > 0) {
			int fd;
			
			for (fd = 3; fd < maxfd; fd++) {
				if (fd != ignfd)
					fcntl (fd, F_SETFD, FD_CLOEXEC);
			}
		}
		
		execv (path, argv);
		_exit (255);
	} else if (pid == -1) {
		g_set_error (err, SPRUCE_ERROR, errno,
			     _("Failed to create child process '%s': %s"),
			     argv[0], g_strerror (errno));
		return -1;
	}
	
	/* parent process */
	close (fds[0]);
	close (fds[3]);
	close (fds[5]);
	
	if (infd)
		*infd = fds[1];
	else
		close (fds[1]);
	
	if (outfd)
		*outfd = fds[2];
	else
		close (fds[2]);
	
	if (errfd)
		*errfd = fds[4];
	else
		close (fds[4]);
	
	return pid;
}


int
spruce_process_wait (pid_t pid)
{
	sigset_t mask, omask;
	int status;
	pid_t r;
	
	sigemptyset (&mask);
	sigaddset (&mask, SIGALRM);
	sigprocmask (SIG_BLOCK, &mask, &omask);
	alarm (1);
	
	r = waitpid (pid, &status, 0);
	
	alarm (0);
	sigprocmask (SIG_SETMASK, &omask, NULL);
	
	if (r == (pid_t) -1 && errno == EINTR) {
		kill (pid, SIGTERM);
		sleep (1);
		r = waitpid (pid, &status, WNOHANG);
		if (r == (pid_t) 0) {
			kill (pid, SIGKILL);
			sleep (1);
			r = waitpid (pid, &status, WNOHANG);
		}
	}
	
	return status;
}


int
spruce_process_kill (pid_t pid)
{
	int status;
	pid_t r;
	
	kill (pid, SIGTERM);
	sleep (1);
	r = waitpid (pid, &status, WNOHANG);
	if (r == (pid_t) 0) {
		kill (pid, SIGKILL);
		sleep (1);
		r = waitpid (pid, &status, WNOHANG);
	}
	
	return status;
}
