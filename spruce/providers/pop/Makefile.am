## Process this file with automake to produce Makefile.in

spruce_provider_LTLIBRARIES = libsprucepop.la

spruce_provider_DATA = libsprucepop.urls

INCLUDES = 					\
	-I$(top_srcdir)				\
	-I$(top_srcdir)/spruce			\
	-DG_LOG_DOMAIN=\"pop-provider\"		\
	-DG_DISABLE_DEPRECATED			\
	$(LIBSPRUCE_CFLAGS)

libsprucepop_la_SOURCES = 			\
	spruce-pop-provider.c			\
	spruce-pop-engine.c			\
	spruce-pop-engine.h			\
	spruce-pop-folder.c			\
	spruce-pop-folder.h			\
	spruce-pop-store.c			\
	spruce-pop-store.h			\
	spruce-pop-stream.c			\
	spruce-pop-stream.h

libsprucepop_la_LDFLAGS = -avoid-version -module

EXTRA_DIST = libsprucepop.urls
