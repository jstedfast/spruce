# Process this file with autoconf to produce a configure script.
# require autoconf 2.54
AC_PREREQ(2.54)

# Making releases:
#   spruce_micro_version += 1;
#   spruce_interface_age += 1;
#   spruce_binary_age += 1;
# if any functions have been added, set spruce_interface_age to 0.
# if backwards compatibility has been broken,
# set spruce_binary_age _and_ spruce_interface_age to 0.
#
m4_define([spruce_major_version], [0])
m4_define([spruce_minor_version], [8])
m4_define([spruce_micro_version], [0])
m4_define([spruce_interface_age], [0])
m4_define([spruce_binary_age],
          [m4_eval(100 * spruce_minor_version + spruce_micro_version)])
m4_define([spruce_version],
          [spruce_major_version.spruce_minor_version.spruce_micro_version])

# the API version is the major.minor since the last API breakage
m4_define([spruce_api_major], [1])
m4_define([spruce_api_minor], [0])
m4_define([spruce_api_version], [spruce_api_major.spruce_api_minor])

# libtool version related macros
m4_define([spruce_lt_release], [spruce_major_version.spruce_minor_version])
m4_define([spruce_lt_current],
          [m4_eval(100 * spruce_minor_version + spruce_micro_version - spruce_interface_age)])
m4_define([spruce_lt_revision], [spruce_interface_age])
m4_define([spruce_lt_age], [m4_eval(spruce_binary_age - spruce_interface_age)])
m4_define([spruce_lt_current_minus_age],
          [m4_eval(spruce_lt_current - spruce_lt_age)])

# if the minor version number is odd, then we want debugging.  Otherwise
# we only want minimal debugging support.
m4_define([spruce_debug_default],
          [m4_if(m4_eval(spruce_minor_version % 2), [1], [yes], [minimum])])


AC_INIT(spruce, [spruce_version],
        [http://bugzilla.gnome.org/enter_bug.cgi?product=spruce])

AC_CONFIG_SRCDIR([README])
AC_CONFIG_MACRO_DIR([m4])
AC_CANONICAL_SYSTEM
AC_CANONICAL_HOST

# Save this value here, since automake will set cflags later
cflags_set=${CFLAGS+set}

AM_INIT_AUTOMAKE([1.9 dist-bzip2 tar-ustar no-dist-gzip foreign])
AC_SUBST([ACLOCAL_AMFLAGS], ["-I m4 \${ACLOCAL_FLAGS}"])
dnl m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])
AM_CONFIG_HEADER([config.h])
AM_MAINTAINER_MODE


SPRUCE_MAJOR_VERSION=spruce_major_version
SPRUCE_MINOR_VERSION=spruce_minor_version
SPRUCE_MICRO_VERSION=spruce_micro_version
SPRUCE_INTERFACE_AGE=spruce_interface_age
SPRUCE_BINARY_AGE=spruce_binary_age
SPRUCE_API_VERSION=spruce_api_version
SPRUCE_VERSION=spruce_version

AC_SUBST(SPRUCE_MAJOR_VERSION)
AC_SUBST(SPRUCE_MINOR_VERSION)
AC_SUBST(SPRUCE_MICRO_VERSION)
AC_SUBST(SPRUCE_API_VERSION)
AC_SUBST(SPRUCE_VERSION)
AC_SUBST(SPRUCE_INTERFACE_AGE)
AC_SUBST(SPRUCE_BINARY_AGE)

AC_DEFINE(SPRUCE_VERSION, ["spruce_version"],
	  [Define to the Spruce version])

# libtool versioning
LT_RELEASE=spruce_lt_release
LT_CURRENT=spruce_lt_current
LT_REVISION=spruce_lt_revision
LT_AGE=spruce_lt_age
LT_CURRENT_MINUS_AGE=spruce_lt_current_minus_age
AC_SUBST(LT_RELEASE)
AC_SUBST(LT_CURRENT)
AC_SUBST(LT_REVISION)
AC_SUBST(LT_AGE)
AC_SUBST(LT_CURRENT_MINUS_AGE)


dnl Initialize maintainer mode
AM_MAINTAINER_MODE

AC_CANONICAL_HOST

dnl Check for win32 platforms
AC_MSG_CHECKING([if building for Win32])
LIB_EXE_MACHINE_FLAG=X86
case "$host" in
  *-*-mingw*)
    platform_win32="yes"
    native_win32="yes"
    case "$host" in
    x86_64-*-*)
      LIB_EXE_MACHINE_FLAG=X64
      ;;
    esac
    ;;
  *)
    platform_win32="no"
    native_win32="no"
    ;;
esac

AC_MSG_RESULT([$platform_win32])

AC_SUBST(LIB_EXE_MACHINE_FLAG)

AM_CONDITIONAL(PLATFORM_WIN32, [test "x$platform_win32" = "xyes"])
AM_CONDITIONAL(OS_WIN32, test ["$native_win32" = "xyes"])
AM_CONDITIONAL(OS_WIN32_X64, [test "$LIB_EXE_MACHINE_FLAG" = "X64"])

if test "$glib_native_win32" = "yes"; then
  AC_CHECK_TOOL(WINDRES, windres, no)
  if test "$WINDRES" = no; then
    AC_MSG_ERROR([*** Could not find an implementation of windres in your PATH.])
  fi
  AC_CHECK_TOOL(NM, nm, no)
  if test "$NM" = no; then
    AC_MSG_ERROR([*** Could not find an implementation of nm in your PATH.])
  fi
  AC_CHECK_TOOL(RANLIB, ranlib, :)
  AC_CHECK_PROG(ms_librarian, [lib.exe], [yes], [no])
fi
AM_CONDITIONAL(MS_LIB_AVAILABLE, [test x$ms_librarian = xyes])

dnl Checks for programs.
AC_ISC_POSIX
AC_PROG_CC
AM_PROG_CC_STDC
AC_C_INLINE
dnl AC_HEADER_STDC
AC_STDC_HEADERS
dnl AC_ARG_PROGRAM
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_MAKE_SET
AC_PATH_PROG(RM, rm, /bin/rm)
AC_PATH_PROG(MV, mv, /bin/mv)
AC_PATH_PROG(TAR, tar, /bin/tar)

AC_EXEEXT
AC_PROG_LIBTOOL
dnl when using libtool 2.x create libtool early, because it's used in configure
m4_ifdef([LT_OUTPUT], [LT_OUTPUT])
DOLT

SPRUCE_MARSHAL_RULE=$srcdir/marshal.mk
AC_SUBST_FILE(SPRUCE_MARSHAL_RULE)

LDFLAGS=${_ldflags}

AC_PATH_PROG(SENDMAIL, sendmail, /usr/sbin/sendmail, /usr/sbin:/usr/lib)
AC_DEFINE_UNQUOTED(SENDMAIL_PATH, "$SENDMAIL", [Path to a sendmail binary, or equivalent])

dnl Checks for header files.
AC_CHECK_HEADERS(string.h)
AC_CHECK_HEADERS(stdlib.h)
AC_CHECK_HEADERS(unistd.h)
AC_CHECK_HEADERS(netdb.h)
AC_CHECK_HEADERS(time.h)
AC_CHECK_HEADERS(poll.h)

AC_TYPE_OFF_T
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T

dnl Check for nfds_t - used by poll()
AC_MSG_CHECKING(for nfds_t)
AC_TRY_COMPILE([
	#include <sys/types.h>
	#include <poll.h>
	], [
	nfds_t nfds = 1;
	return nfds;
	]
,
	AC_MSG_RESULT(yes)
,
	AC_DEFINE(nfds_t, unsigned long int, [Define to `unsigned long int' if <poll.h> does not define.])
	AC_MSG_RESULT(no)
)

dnl Check for some time functions
AC_CHECK_FUNCS(localtime localtime_r)

dnl Check for select() and poll()
AC_CHECK_FUNCS(select poll)

dnl ************************************
dnl Checks for gtk-doc and docbook-tools
dnl ************************************

GTK_DOC_CHECK([1.8])

AC_CHECK_PROG(DB2HTML, db2html, true, false)
AM_CONDITIONAL(HAVE_DOCBOOK, $DB2HTML)

dnl NOTE: We need to use a separate automake conditional for this
dnl       to make this work with the tarballs.
AM_CONDITIONAL(ENABLE_GTK_DOC, test "x$enable_gtk_doc" = "xyes")

dnl We need at *least* glib 2.18.0 for g_checksum_reset
AM_PATH_GLIB_2_0(2.18.0, ,
		 AC_MSG_ERROR(Cannot find GLIB: Is pkg-config in your path?),
		 gobject gmodule gthread)

PKG_CHECK_MODULES(LIBSPRUCE, gmime-2.6 >= 2.6.0 glib-2.0 >= 2.18.0 gobject-2.0 gmodule-2.0 gthread-2.0 sqlite3)

dnl *****************************
dnl *** Checks for zlib       ***
dnl *****************************
AC_CHECK_HEADERS(zlib.h)
AC_CHECK_LIB(z, inflate, ZLIB="-lz")

dnl Check for libiconv
AM_ICONV()

dnl Check for fsync (native win32 doesn't have this)
AC_MSG_CHECKING(for fsync)
AC_TRY_LINK([
	#include <unistd.h>
	], [
	fsync(0);
	]
,
	AC_MSG_RESULT(yes)
	AC_DEFINE(HAVE_FSYNC, 1, [Define to 1 if you have the `fsync' function.])
,
	AC_MSG_RESULT(no)
)

dnl Timezone checks
AC_CACHE_CHECK(for tm_gmtoff in struct tm, ac_cv_struct_tm_gmtoff,
	AC_TRY_COMPILE([
		#include <time.h>
		], [
		struct tm tm;
		tm.tm_gmtoff = 1;
		return tm.tm_gmtoff;
		], ac_cv_struct_tm_gmtoff="yes", ac_cv_struct_tm_gmtoff="no"))
if test "$ac_cv_struct_tm_gmtoff" = "yes"; then
	AC_DEFINE(HAVE_TM_GMTOFF, 1, [Define if struct tm has a tm_gmtoff member])
else
	AC_CACHE_CHECK(for timezone variable, ac_cv_var_timezone,
		AC_TRY_COMPILE([
			#include <time.h>
		], [
			timezone = 1;
		], ac_cv_var_timezone="yes", ac_cv_var_timezone="no"))
	if test "$ac_cv_var_timezone" = "yes"; then
		AC_DEFINE(HAVE_TIMEZONE, 1, [Define if libc defines a timezone variable])
		AC_CACHE_CHECK(for altzone variable, ac_cv_var_altzone,
			AC_TRY_COMPILE([
				#include <time.h>
			], [
				altzone = 1;
			], ac_cv_var_altzone="yes", ac_cv_var_altzone="no"))
		if test "$ac_cv_var_altzone" = "yes"; then
			AC_DEFINE(HAVE_ALTZONE, 1, [Define if libc defines an altzone variable])
		fi
	else
		AC_CACHE_CHECK(for _timezone variable, ac_cv_var__timezone,
			AC_TRY_COMPILE([
				#include <time.h>
			], [
				_timezone = 1;
			], ac_cv_var__timezone="yes", ac_cv_var__timezone="no"))
		if test "$ac_cv_var__timezone" = "yes"; then
			AC_DEFINE(HAVE__TIMEZONE, 1, [Define if libc defines a _timezone variable])
		else
			AC_ERROR(unable to find a way to determine timezone)
		fi
	fi
fi

dnl *************************************
dnl *** Checks for large file support ***
dnl *************************************
AC_ARG_ENABLE(largefile,
	      AC_HELP_STRING([--enable-largefile],
	      [enable support for large files [[default=yes]]]),,
	      enable_largefile="yes")
if test "x$enable_largefile" != "xno"; then
    AC_SYS_LARGEFILE
    AC_CACHE_CHECK([for _LARGEFILE64_SOURCE value needed for large files], ac_cv_largefile64_source,
    [
	AC_TRY_COMPILE([
		#include <sys/types.h>
		#include <sys/stat.h>
		#include <fcntl.h>
	],[
		return open ("__o_largefile", O_CREAT | O_RDWR | O_LARGEFILE, 0644);
	],[
		ac_cv_largefile64_source="no"
	],[
		AC_TRY_COMPILE([
			#define _LARGEFILE64_SOURCE
			#include <sys/types.h>
			#include <sys/stat.h>
			#include <fcntl.h>
		],[
			return open ("__o_largefile", O_CREAT | O_RDWR | O_LARGEFILE, 0644);
		],[
			ac_cv_largefile64_source="yes"
		],[
			ac_cv_largefile64_source="unknown"
		])
	])
    ])
    
    enable_largefile="no"
    
    if test "x$ac_cv_largefile64_source" = "xyes"; then
        LFS_CFLAGS="-D_LARGEFILE64_SOURCE"
        enable_largefile="yes"
    elif test "x$ac_cv_largefile64_source" = "xunknown"; then
        AC_DEFINE(O_LARGEFILE, 0, [Define to 0 if your system does not have the O_LARGEFILE flag])
    fi
    
    if test -n "$ac_cv_sys_large_files" -a "x$ac_cv_sys_large_files" != "xno"; then
        LFS_CFLAGS="$LFS_CFLAGS -D_LARGE_FILES"
        enable_largefile="yes"
    fi
    
    if test "x$ac_cv_sys_file_offset_bits" != "xno"; then
        LFS_CFLAGS="$LFS_CFLAGS -D_FILE_OFFSET_BITS=$ac_cv_sys_file_offset_bits"
        enable_largefile="yes"
    fi
else
    LFS_CFLAGS=""
fi
AM_CONDITIONAL(ENABLE_LARGEFILE, test "x$enable_largefile" = "xyes")

dnl Check type sizes
CFLAGS_save="$CFLAGS"
CFLAGS="$CFLAGS $LFS_CFLAGS"
AC_CHECK_SIZEOF(ssize_t)
AC_CHECK_SIZEOF(size_t)
AC_CHECK_SIZEOF(off_t)
CFLAGS="$CFLAGS_save"

dnl Check for some network functions
AC_CHECK_FUNCS(getaddrinfo)

dnl ****************************
dnl *** Checks for libsocket ***
dnl ****************************
LIBSOCKET=""
AC_CHECK_LIB(socket, getaddrinfo, LIBSOCKET="-lsocket")

dnl *************************
dnl *** Checks for libnsl ***
dnl *************************
LIBNSL=""
AC_CHECK_LIB(nsl, getaddrinfo, LIBNSL="-lnsl")

dnl ******************
dnl *** Kerberos 4 ***
dnl ******************
AC_ARG_WITH(krb4, AC_HELP_STRING([--with-krb4=PREFIX],
            [Location of Kerberos 4 libs/includes]),
            with_krb4="$withval", with_krb4="no")
enable_krb4="no"
if test "x$with_krb4" != "xno"; then
  LDFLAGS_save="$LDFLAGS"
  AC_CACHE_CHECK(for Kerberos 4, ac_cv_lib_kerberos4,
  [
    ac_cv_lib_kerberos4="no"
    
    mitcompatlibs="-lkrb4 -ldes425 -lkrb5 -lk5crypto -lcom_err"
    # Look for MIT krb5 compat krb4
    LDFLAGS="$LDFLAGS -L$with_krb4/lib $mitcompatlibs"
    AC_TRY_LINK_FUNC(krb_mk_req, ac_cv_lib_kerberos4="$mitcompatlibs")
    
    if test "$ac_cv_lib_kerberos4" = "no"; then
      # Look for KTH krb4
      LDFLAGS="$LDFLAGS_save -L$with_krb4/lib -lkrb -lcrypto -lcom_err -lroken"
      AC_TRY_LINK_FUNC(krb_mk_req, ac_cv_lib_kerberos4="-lkrb -lcrypto -lcom_err -lroken")
    fi
    if test "$ac_cv_lib_kerberos4" = "no"; then
      # Look for old MIT krb4
      LDFLAGS="$LDFLAGS_save -L$with_krb4/lib -lkrb"
      AC_TRY_LINK_FUNC(krb_mk_req, ac_cv_lib_kerberos4="-lkrb",
      [
        LDFLAGS="$LDFLAGS -ldes"
        AC_TRY_LINK_FUNC(krb_mk_req, ac_cv_lib_kerberos4="-lkrb -ldes")
      ])
    fi
  ])
  LDFLAGS="$LDFLAGS_save"
  if test "$ac_cv_lib_kerberos4" != "no"; then
    AC_DEFINE(HAVE_KRB4, 1, [Define if you have Krb4])
    enable_krb4="yes"
    if test -f "$with_krb4/include/krb.h" -o -f "$with_krb4/include/port-sockets.h"; then
      KRB4_CFLAGS="-I$with_krb4/include"
    fi
    if test -d "$with_krb4/include/kerberosIV"; then
      KRB4_CFLAGS="$KRB4_CFLAGS -I$with_krb4/include/kerberosIV"
    fi
    KRB4_LDFLAGS="-L$with_krb4/lib $ac_cv_lib_kerberos4"
    
    CFLAGS_save="$CFLAGS"
    CFLAGS="$CFLAGS $KRB4_CFLAGS"
    AC_TRY_COMPILE([#include "krb.h"
    int krb_sendauth;
    ],[return 0],[AC_DEFINE(NEED_KRB_SENDAUTH_PROTO, 1, [Need krb_sendauth proto])],)
    CFLAGS="$CFLAGS_save"
  fi
else
  AC_MSG_CHECKING(for Kerberos 4)
  AC_MSG_RESULT($with_krb4)
fi
AC_SUBST(KRB4_CFLAGS)
AC_SUBST(KRB4_LDFLAGS)


dnl ******************
dnl *** Kerberos 5 ***
dnl ******************
AC_ARG_WITH(krb5, AC_HELP_STRING([--with-krb5=PREFIX],
            [Location of Kerberos 5 libs/includes]),
            with_krb5="$withval", with_krb5="auto")
enable_krb5="no"
if test "x$with_krb5" != "xno"; then
  LDFLAGS_save="$LDFLAGS"
  
  if test "x$with_krb5" = "xauto"; then
    with_krb5="/usr/kerberos"
  fi
  
  mitlibs="-lkrb5 -lk5crypto -lcom_err -lgssapi_krb5"
  heimlibs="-lkrb5 -lcrypto -lasn1 -lcom_err -lroken -lgssapi"
  AC_CACHE_CHECK([for Kerberos 5], ac_cv_lib_kerberos5,
  [
    LDFLAGS="$LDFLAGS -L$with_krb5/lib $mitlibs"
    AC_TRY_LINK_FUNC(krb5_init_context, ac_cv_lib_kerberos5="$mitlibs",
    [
      LDFLAGS="$LDFLAGS_save -L$with_krb5/lib $heimlibs"
      AC_TRY_LINK_FUNC(krb5_init_context, ac_cv_lib_kerberos5="$heimlibs", ac_cv_lib_kerberos5="no")
    ])
    LDFLAGS="$LDFLAGS_save"
  ])
  if test "$ac_cv_lib_kerberos5" != "no"; then
    AC_DEFINE(HAVE_KRB5, 1, [Define if you have Krb5])
    if test "$ac_cv_lib_kerberos5" = "$mitlibs"; then
      AC_DEFINE(HAVE_MIT_KRB5, 1, [Define if you have MIT Krb5])
      KRB5_CFLAGS="-I$with_krb5/include"
      enable_krb5="yes (MIT)"
    else
      AC_DEFINE(HAVE_HEIMDAL_KRB5, 1, [Define if you have Heimdal])
      KRB5_CFLAGS="-I$with_krb5/include/heimdal"
      enable_krb5="yes (Heimdal)"
    fi
    KRB5_LDFLAGS="-L$with_krb5/lib $ac_cv_lib_kerberos5"
  fi
else
  AC_MSG_CHECKING(for Kerberos 5)
  AC_MSG_RESULT($with_krb5)
fi
AC_SUBST(KRB5_CFLAGS)
AC_SUBST(KRB5_LDFLAGS)


dnl ***************
dnl *** OpenSSL ***
dnl ***************
AC_ARG_ENABLE(openssl, AC_HELP_STRING([--enable-openssl],
              [Enable OpenSSL support [ default=auto ]]),
              enable_openssl="$enableval", enable_openssl="auto")
AC_ARG_WITH(openssl-libs, AC_HELP_STRING([--with-openssl-libs=PREFIX],
            [Specify the location of the OpenSSL libs]),
            with_openssl_libs="$withval")
AC_ARG_WITH(openssl-includes, AC_HELP_STRING([--with-openssl-includes=PREFIX],
            [Specify the location of the OpenSSL includes]),
            with_openssl_includes="$withval")
if test "x$enable_openssl" != "xno"; then
  have_openssl_includes="no"
  CPPFLAGS_save="$CPPFLAGS"
  AC_MSG_CHECKING(for OpenSSL includes)
  AC_MSG_RESULT()
  if test "x$with_openssl_includes" != "x"; then
    CPPFLAGS="$CPPFLAGS $KRB5_CFLAGS -I$with_openssl_includes"
  else
    CPPFLAGS="$CPPFLAGS $KRB5_CFLAGS"
  fi
  AC_CHECK_HEADERS(openssl/ssl.h openssl/ssl2.h openssl/ssl3.h openssl/ssl23.h openssl/tls1.h, [ openssl_includes="yes" ])
  CPPFLAGS="$CPPFLAGS_save"
  if test "x$openssl_includes" != "xno" -a "x$openssl_includes" != "x" ; then
    have_openssl_includes="yes"
    if test "x$with_openssl_includes" != "x"; then
      OPENSSL_CFLAGS="-I$with_openssl_includes"
    fi
  fi
  AC_MSG_RESULT($have_openssl_includes)
  if test "x$have_openssl_includes" = "xno" -a "x$enable_openssl" = "xyes"; then
    AC_MSG_ERROR(Failed to find needed OpenSSL includes)
  fi
  
  if test "x$have_openssl_includes" = "xyes"; then
    have_openssl_libs="no"
    CFLAGS_save="$CFLAGS"
    LDFLAGS_save="$LDFLAGS"
    LIBS_save="$LIBS"
    AC_MSG_CHECKING(for OpenSSL libs)
    if test "x$with_openssl_libs" != "x"; then
      OPENSSL_LDFLAGS="-L$with_openssl_libs"
    else
      OPENSSL_LDFLAGS=""
    fi
    CFLAGS="$CFLAGS $OPENSSL_CFLAGS"
    LDFLAGS="$LDFLAGS $OPENSSL_LDFLAGS -lssl -lcrypto"
    LIBS="$LIBS -lssl -lcrypto"
    AC_TRY_LINK_FUNC(SSL_read, openssl_libs="yes", openssl_libs="no")
    CFLAGS="$CFLAGS_save"
    LDFLAGS="$LDFLAGS_save"
    LIBS="$LIBS_save"
    if test "x$openssl_libs" = "xyes"; then
      have_openssl_libs="yes"
      OPENSSL_LIBS="-lssl -lcrypto"
    fi
    AC_MSG_RESULT($have_openssl_libs)
    if test "x$have_openssl_libs" = "xno" -a "x$enable_openssl" = "xyes"; then
      AC_MSG_ERROR(Failed to find needed OpenSSL libs)
    fi
  fi
  
  if test "x$have_openssl_includes" = "xyes" -a "x$have_openssl_libs" = "xyes"; then
    SSL_CFLAGS="$OPENSSL_CFLAGS"
    SSL_LDFLAGS="$OPENSSL_LDFLAGS $OPENSSL_LIBS"
    AC_DEFINE(HAVE_OPENSSL, 1, [Define if you have OpenSSL])
    enable_openssl="yes"
  else
    enable_openssl="no"
  fi
fi
dnl AC_MSG_CHECKING(whether to build with OpenSSL support)
dnl AC_MSG_RESULT($enable_openssl)

if test "x$enable_openssl" = "xyes"; then
	AC_DEFINE(HAVE_SSL, 1, [Define if OpenSSL is enabled])
fi
AC_SUBST(SSL_CFLAGS)
AC_SUBST(SSL_LDFLAGS)


dnl ********************
dnl *** File Locking ***
dnl ********************
AC_ARG_ENABLE(dot-locking, AC_HELP_STRING([--enable-dot-locking],
              [Enable the use of .lock files for locking mbox files]),
              enable_dot_locking="$enable_val", enable_dot_locking="yes")
if test "x$enable_dot_locking" = "xyes"; then
  AC_DEFINE(USE_DOT_LOCKING, 1, [Define to use dot locking for mbox files])
  enable_dotlock="yes"
else
  enable_dotlock="no"
fi

AC_ARG_ENABLE(file-locking, 
              AC_HELP_STRING([--enable-file-locking=[fcntl/flock/lockf/no]],
              [Enable support for locking mbox files]),
              enable_file_locking="$enable_val", enable_file_locking="fcntl")
if test "x$enable_file_locking" = "xfcntl"; then
  AC_DEFINE(USE_FCNTL, 1, [Define to use fcntl locking for mbox files])
  enable_filelock="fcntl"
else
  if test "x$enable_file_locking" = "xflock"; then
    AC_DEFINE(USE_FLOCK, 1, [Define to use flock locking for mbox files])
    enable_filelock="flock"
  else
    if test "x$enable_file_locking" = "xlockf"; then
      AC_DEFINE(USE_LOCKF, 1, [Define to use lockf locking for mbox files])
      enable_filelock="lockf"
    else
      enable_filelock="no"
    fi	
  fi
fi


spruce_providerdir="${libdir}/spruce/providers"
AC_SUBST(spruce_providerdir)

LIBSPRUCE_CFLAGS="$LIBSPRUCE_CFLAGS $KRB5_CFLAGS $KRB4_CFLAGS $SSL_CFLAGS"
LIBSPRUCE_LIBS="$LIBSPRUCE_LIBS $KRB5_LDFLAGS $KRB4_LDFLAGS $SSL_LDFLAGS"
AC_SUBST(LIBSPRUCE_CFLAGS)
AC_SUBST(LIBSPRUCE_LIBS)

dnl Extra cflags
EXTRA_CFLAGS=""
if test "x$KRB5_CFLAGS" != "x"; then
  EXTRA_CFLAGS="$EXTRA_CFLAGS $KRB5_CFLAGS"
fi
if test "x$KRB4_CFLAGS" != "x"; then
  EXTRA_CFLAGS="$EXTRA_CFLAGS $KRB4_CFLAGS"
fi
if test "x$SSL_CFLAGS" != "x"; then
  EXTRA_CFLAGS="$EXTRA_CFLAGS $SSL_CFLAGS"
fi

dnl Extra libs
EXTRA_LIBS="$ZLIB"
if test "x$LIBSOCKET" != "x"; then
  EXTRA_LIBS="$EXTRA_LIBS $LIBSOCKET"
fi
if test "x$LIBNSL" != "x"; then
  EXTRA_LIBS="$EXTRA_LIBS $LIBNSL"
fi
if test "x$LIBICONV" != "x"; then
  EXTRA_LIBS="$EXTRA_LIBS $LIBICONV"
fi
if test "x$KRB5_LDFLAGS" != "x"; then
  EXTRA_LIBS="$EXTRA_LIBS $KRB5_LDFLAGS"
fi
if test "x$KRB4_LDFLAGS" != "x"; then
  EXTRA_LIBS="$EXTRA_LIBS $KRB4_LDFLAGS"
fi
if test "x$SSL_LDFLAGS" != "x"; then
  EXTRA_LIBS="$EXTRA_LIBS $SSL_LDFLAGS"
fi

SPRUCE_LIBDIR="-L${libdir}"
SPRUCE_INCLUDEDIR="-I${includedir}/spruce-1.0"
SPRUCE_LIBS="-lspruce-1.0 $EXTRA_LIBS"
SPRUCE_CFLAGS="$EXTRA_CFLAGS"

AC_SUBST(SPRUCE_LIBS)
AC_SUBST(SPRUCE_LIBDIR)
AC_SUBST(SPRUCE_INCLUDEDIR)
AC_SUBST(SPRUCE_CFLAGS)

CFLAGS="$CFLAGS $EXTRA_CFLAGS"
LIBS="$LIBS $EXTRA_LIBS"
AC_SUBST(CFLAGS)
AC_SUBST(LIBS)

AC_OUTPUT(
Makefile
util/Makefile
spruce/Makefile
spruce/spruce-version.h
spruce/providers/Makefile
spruce/providers/mbox/Makefile
spruce/providers/maildir/Makefile
spruce/providers/pop/Makefile
spruce/providers/imap/Makefile
spruce/providers/sendmail/Makefile
spruce/providers/smtp/Makefile
src/Makefile
src/mailx/Makefile
spruce-1.0.pc
)

echo "

Configuration:

  Source code location: ${srcdir}
  Install prefix:       ${prefix}
  Compiler:             ${CC}

  Large file support:   ${enable_largefile}
  IPv6 support:         yes
  Kerberos4 support:    ${enable_krb4}
  Kerberos5 support:    ${enable_krb5}
  OpenSSL support:      ${enable_openssl}

  .lock support:        ${enable_dotlock}
  file locking support: ${enable_filelock}
"
