## Process this file with automake to produce Makefile.in

spruce_provider_LTLIBRARIES = libsprucesendmail.la

spruce_provider_DATA = libsprucesendmail.urls

INCLUDES = 					\
	-I$(top_srcdir)				\
	-I$(top_srcdir)/spruce			\
	-DG_LOG_DOMAIN=\"sendmail-provider\"	\
	-DG_DISABLE_DEPRECATED			\
	$(LIBSPRUCE_CFLAGS)

libsprucesendmail_la_SOURCES = 			\
	spruce-sendmail-provider.c		\
	spruce-sendmail-transport.c		\
	spruce-sendmail-transport.h

libsprucesendmail_la_LDFLAGS = -avoid-version -module

EXTRA_DIST = libsprucesendmail.urls
