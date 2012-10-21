## Process this file with automake to produce Makefile.in

SUBDIRS = .

noinst_LTLIBRARIES = libutil.la

INCLUDES = -I$(top_srcdir) 		\
	$(VERSION_FLAGS) 		\
	-DG_LOG_DOMAIN=\"util\"		\
	-DG_DISABLE_DEPRECATED		\
	$(GMIME_CFLAGS)			\
	$(GLIB_CFLAGS)

libutil_la_SOURCES =			\
	list.c				\
	list.h

