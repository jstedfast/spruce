## Process this file with automake to produce Makefile.in

SUBDIRS = .

INCLUDES = 				\
	-I$(top_srcdir)			\
	-I$(top_srcdir)/spruce		\
	-DG_LOG_DOMAIN=\"spruce-mailx\"	\
	-DG_DISABLE_DEPRECATED		\
	$(LIBSPRUCE_CFLAGS)

bin_PROGRAMS = spruce-mailx

DEPS = 						\
	$(top_builddir)/spruce/libspruce-1.0.la

LDADDS = 					\
	$(top_builddir)/spruce/libspruce-1.0.la	\
	$(LIBSPRUCE_LIBS)

spruce_mailx_SOURCES = 				\
	session.c				\
	session.h				\
	main.c

spruce_mailx_LDFLAGS = 
spruce_mailx_DEPENDENCIES = $(DEPS)
spruce_mailx_LDADD = $(LDADDS)
