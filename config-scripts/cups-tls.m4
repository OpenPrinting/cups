dnl
dnl TLS stuff for CUPS.
dnl
dnl Copyright © 2020-2024 by OpenPrinting.
dnl Copyright © 2007-2019 by Apple Inc.
dnl Copyright © 1997-2007 by Easy Software Products, all rights reserved.
dnl
dnl Licensed under Apache License v2.0.  See the file "LICENSE" for more
dnl information.
dnl

AC_ARG_WITH([tls], AS_HELP_STRING([--with-tls=...], [use gnutls or openssl for TLS support]))
AS_IF([test "x$with_tls" = x], [
    with_tls="yes"
], [test "$with_tls" != gnutls -a "$with_tls" != openssl -a "$with_tls" != yes], [
    AC_MSG_ERROR([Unsupported --with-tls value "$with_tls" specified.])
])

TLSFLAGS=""
TLSLIBS=""
have_tls="0"
CUPS_SERVERKEYCHAIN=""

dnl First look for OpenSSL/LibreSSL...
AS_IF([test $with_tls = yes -o $with_tls = openssl], [
    AS_IF([test "x$PKGCONFIG" != x], [
	# Find openssl using pkg-config...
        AC_MSG_CHECKING([for openssl package])
	AS_IF([$PKGCONFIG --exists openssl], [
	    AC_MSG_RESULT([yes])
	    have_tls="1"
	    with_tls="openssl"
	    TLSLIBS="$($PKGCONFIG --libs openssl)"
	    TLSFLAGS="$($PKGCONFIG --cflags openssl)"
	    PKGCONFIG_REQUIRES="$PKGCONFIG_REQUIRES openssl"
	    AC_DEFINE([HAVE_OPENSSL], [1], [Do we have the OpenSSL library?])
	], [
	    AC_MSG_RESULT([no])
	])
    ], [
	# Find openssl using legacy library/header checks...
	SAVELIBS="$LIBS"
	LIBS="-lcrypto $LIBS"

	AC_CHECK_LIB([ssl], [SSL_new], [
	    AC_CHECK_HEADER([openssl/ssl.h], [
		have_tls="1"
		with_tls="openssl"
		TLSLIBS="-lssl -lcrypto"
		PKGCONFIG_LIBS_STATIC="$PKGCONFIG_LIBS_STATIC $TLSLIBS"
		AC_DEFINE([HAVE_OPENSSL], [1], [Do we have the OpenSSL library?])
	    ])
	])

	LIBS="$SAVELIBS"
    ])

    AS_IF([test $have_tls = 1], [
	CUPS_SERVERKEYCHAIN="ssl"
    ], [test $with_tls = openssl], [
        AC_MSG_ERROR([--with-tls=openssl was specified but neither the OpenSSL nor LibreSSL library were found.])
    ])
])

dnl Then look for GNU TLS...
AS_IF([test $with_tls = yes -o $with_tls = gnutls], [
    AC_PATH_TOOL([LIBGNUTLSCONFIG], [libgnutls-config])
    AS_IF([test "x$PKGCONFIG" != x], [
        AC_MSG_CHECKING([for gnutls package])
	AS_IF([$PKGCONFIG --exists gnutls], [
	    AC_MSG_RESULT([yes])
	    have_tls="1"
	    with_tls="gnutls"
	    TLSLIBS="$($PKGCONFIG --libs gnutls)"
	    TLSFLAGS="$($PKGCONFIG --cflags gnutls)"
	    PKGCONFIG_REQUIRES="$PKGCONFIG_REQUIRES gnutls"
	    AC_DEFINE([HAVE_GNUTLS], [1], [Do we have the GNU TLS library?])
	], [
	    AC_MSG_RESULT([no])
	])
    ])
    AS_IF([test $have_tls = 0 -a "x$LIBGNUTLSCONFIG" != x], [
	have_tls="1"
	with_tls="gnutls"
	TLSLIBS="$($LIBGNUTLSCONFIG --libs)"
	TLSFLAGS="$($LIBGNUTLSCONFIG --cflags)"
	PKGCONFIG_LIBS_STATIC="$PKGCONFIG_LIBS_STATIC $TLSLIBS"
	AC_DEFINE([HAVE_GNUTLS], [1], [Do we have the GNU TLS library?])
    ])

    AS_IF([test $have_tls = 1], [
	CUPS_SERVERKEYCHAIN="ssl"

	SAVELIBS="$LIBS"
	LIBS="$LIBS $TLSLIBS"
	AC_CHECK_FUNC([gnutls_transport_set_pull_timeout_function], [
	    AC_DEFINE([HAVE_GNUTLS_TRANSPORT_SET_PULL_TIMEOUT_FUNCTION], [1], [Do we have the gnutls_transport_set_pull_timeout_function function?])
	])
	AC_CHECK_FUNC([gnutls_priority_set_direct], [
	    AC_DEFINE([HAVE_GNUTLS_PRIORITY_SET_DIRECT], [1], [Do we have the gnutls_priority_set_direct function?])
	])
	LIBS="$SAVELIBS"
    ], [test $with_tls = gnutls], [
        AC_MSG_ERROR([--with-tls=gnutls was specified but the GNU TLS library was not found.])
    ])
])

AS_IF([test $have_tls = 1], [
    AC_MSG_NOTICE([    Using TLSLIBS="$TLSLIBS"])
    AC_MSG_NOTICE([    Using TLSFLAGS="$TLSFLAGS"])
], [
    AC_MSG_ERROR([No compatible TLS libraries could be found.])
])

AC_SUBST([CUPS_SERVERKEYCHAIN])
AC_SUBST([TLSFLAGS])
AC_SUBST([TLSLIBS])

EXPORT_TLSLIBS="$TLSLIBS"
AC_SUBST([EXPORT_TLSLIBS])
