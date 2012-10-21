## Process this file with automake to produce Makefile.in

spruce_provider_LTLIBRARIES = libsprucembox.la

spruce_provider_DATA = libsprucembox.urls

INCLUDES = 					\
	-I$(top_srcdir)				\
	-I$(top_srcdir)/spruce			\
	-DG_LOG_DOMAIN=\"mbox-provider\"	\
	-DG_DISABLE_DEPRECATED			\
	$(LIBSPRUCE_CFLAGS)

libsprucembox_la_SOURCES = 			\
	spruce-mbox-folder.c			\
	spruce-mbox-folder.h			\
	spruce-mbox-provider.c			\
	spruce-mbox-store.c			\
	spruce-mbox-store.h			\
	spruce-mbox-summary.c			\
	spruce-mbox-summary.h

libsprucembox_la_LDFLAGS = -avoid-version -module

EXTRA_DIST = libsprucembox.urls
