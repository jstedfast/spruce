## Process this file with automake to produce Makefile.in

spruce_provider_LTLIBRARIES = libspruceimap.la

spruce_provider_DATA = libspruceimap.urls

INCLUDES = 					\
	-I$(top_srcdir)				\
	-I$(top_srcdir)/spruce			\
	-DG_LOG_DOMAIN=\"imap-provider\"	\
	-DG_DISABLE_DEPRECATED			\
	$(LIBSPRUCE_CFLAGS)

libspruceimap_la_SOURCES = 			\
	spruce-imap-provider.c			\
	spruce-imap-command.c			\
	spruce-imap-command.h			\
	spruce-imap-engine.c			\
	spruce-imap-engine.h			\
	spruce-imap-folder.c			\
	spruce-imap-folder.h			\
	spruce-imap-specials.c			\
	spruce-imap-specials.h			\
	spruce-imap-store.c			\
	spruce-imap-store.h			\
	spruce-imap-stream.c			\
	spruce-imap-stream.h			\
	spruce-imap-summary.c			\
	spruce-imap-summary.h			\
	spruce-imap-utils.c			\
	spruce-imap-utils.h

libspruceimap_la_LDFLAGS = -avoid-version -module

EXTRA_DIST = libspruceimap.urls
