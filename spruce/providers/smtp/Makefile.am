## Process this file with automake to produce Makefile.in

spruce_provider_LTLIBRARIES = libsprucesmtp.la

spruce_provider_DATA = libsprucesmtp.urls

INCLUDES = 					\
	-I$(top_srcdir)				\
	-I$(top_srcdir)/spruce			\
	-DG_LOG_DOMAIN=\"smtp-provider\"	\
	-DG_DISABLE_DEPRECATED			\
	$(LIBSPRUCE_CFLAGS)

libsprucesmtp_la_SOURCES = 			\
	spruce-smtp-provider.c			\
	spruce-smtp-transport.c			\
	spruce-smtp-transport.h

libsprucesmtp_la_LDFLAGS = -avoid-version -module

EXTRA_DIST = libsprucesmtp.urls
