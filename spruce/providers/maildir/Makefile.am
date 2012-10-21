## Process this file with automake to produce Makefile.in

spruce_provider_LTLIBRARIES = libsprucemaildir.la

spruce_provider_DATA = libsprucemaildir.urls

INCLUDES = 					\
	-I$(top_srcdir)				\
	-I$(top_srcdir)/spruce			\
	-DG_LOG_DOMAIN=\"maildir-provider\"	\
	-DG_DISABLE_DEPRECATED			\
	$(LIBSPRUCE_CFLAGS)

libsprucemaildir_la_SOURCES = 			\
	spruce-maildir-folder.c			\
	spruce-maildir-folder.h			\
	spruce-maildir-provider.c		\
	spruce-maildir-store.c			\
	spruce-maildir-store.h			\
	spruce-maildir-summary.c		\
	spruce-maildir-summary.h		\
	spruce-maildir-utils.c			\
	spruce-maildir-utils.h

libsprucemaildir_la_LDFLAGS = -avoid-version -module

EXTRA_DIST = libsprucemaildir.urls
