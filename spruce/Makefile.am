## Process this file with automake to produce Makefile.in

SUBDIRS = . providers

spruceincludedir = $(includedir)/spruce-$(SPRUCE_API_VERSION)/libspruce

INCLUDES = 							\
	-I$(top_srcdir) 					\
	-DSPRUCE_PROVIDERDIR=\"$(spruce_providerdir)\"		\
	-DG_LOG_DOMAIN=\"libspruce\"				\
	-DG_DISABLE_DEPRECATED					\
	$(LIBSPRUCE_CFLAGS)

lib_LTLIBRARIES = libspruce-1.0.la

libspruce_1_0_la_SOURCES = 		\
	spruce.c			\
	spruce-cache.c			\
	spruce-cache-stream.c		\
	spruce-file-utils.c		\
	spruce-folder.c			\
	spruce-folder-search.c		\
	spruce-folder-summary.c		\
	spruce-list.c			\
	spruce-lock.c			\
	spruce-marshal.c		\
	spruce-offline-folder.c		\
	spruce-offline-journal.c	\
	spruce-offline-store.c		\
	spruce-process.c		\
	spruce-provider.c		\
	spruce-sasl.c			\
	spruce-sasl-anonymous.c		\
	spruce-sasl-cram-md5.c		\
	spruce-sasl-digest-md5.c	\
	spruce-sasl-gssapi.c		\
	spruce-sasl-kerberos4.c		\
	spruce-sasl-login.c		\
	spruce-sasl-plain.c		\
	spruce-service.c		\
	spruce-session.c		\
	spruce-store.c			\
	spruce-string-utils.c		\
	spruce-tcp-stream.c		\
	spruce-tcp-stream-ssl.c		\
	spruce-transport.c		\
	spruce-url.c			\
	search.c

spruceinclude_HEADERS = 		\
	spruce.h			\
	spruce-cache.h			\
	spruce-cache-stream.h		\
	spruce-error.h			\
	spruce-file-utils.h		\
	spruce-folder.h			\
	spruce-folder-search.h		\
	spruce-folder-summary.h		\
	spruce-list.h			\
	spruce-lock.h			\
	spruce-marshal.h		\
	spruce-offline-folder.h		\
	spruce-offline-journal.h	\
	spruce-offline-store.h		\
	spruce-process.h		\
	spruce-provider.h		\
	spruce-sasl.h			\
	spruce-sasl-anonymous.h		\
	spruce-sasl-cram-md5.h		\
	spruce-sasl-digest-md5.h	\
	spruce-sasl-gssapi.h		\
	spruce-sasl-kerberos4.h		\
	spruce-sasl-login.h		\
	spruce-sasl-plain.h		\
	spruce-service.h		\
	spruce-session.h		\
	spruce-store.h			\
	spruce-string-utils.h		\
	spruce-tcp-stream.h		\
	spruce-tcp-stream-ssl.h		\
	spruce-transport.h		\
	spruce-url.h			\
	spruce-version.h

noinst_HEADERS = 			\
	search.h

MARSHAL_GENERATED = spruce-marshal.c spruce-marshal.h
@SPRUCE_MARSHAL_RULE@

libspruce_1_0_la_LIBADD = 			\
	$(LIBSPRUCE_LIBS)

libspruce_1_0_la_LDFLAGS = \
	-version-info $(LT_CURRENT):$(LT_REVISION):$(LT_AGE) \
	-export-dynamic $(no_undefined)

BUILT_SOURCES = $(MARSHAL_GENERATED)
CLEANFILES    = $(BUILT_SOURCES)

EXTRA_DIST = spruce-version.h.in spruce-version.h spruce-marshal.list

dist-hook:
	cd $(distdir); rm -f $(BUILT_SOURCES)
