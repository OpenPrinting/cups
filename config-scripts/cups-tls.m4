dnl
dnl TLS stuff for CUPS.
dnl
dnl Copyright © 2021 by OpenPrinting.
dnl Copyright © 2007-2019 by Apple Inc.
dnl Copyright © 1997-2007 by Easy Software Products, all rights reserved.
dnl
dnl Licensed under Apache License v2.0.  See the file "LICENSE" for more
dnl information.
dnl

AC_ARG_WITH([tls], AS_HELP_STRING([--with-tls=...], [use cdsa (macOS) or gnutls for TLS support]))
AS_IF([test "x$with_tls" = x], [
    with_tls="yes"
], [test "$with_tls" != cdsa -a "$with_tls" != gnutls -a "$with_tls" != no -a "$with_tls" != yes], [
    AC_MSG_ERROR([Unsupported --with-tls value "$with_tls" specified.])
])

TLSFLAGS=""
TLSLIBS=""
have_tls="0"
CUPS_SERVERKEYCHAIN=""

dnl First try using CSDA SSL (macOS)...
AS_IF([test $with_tls = yes -o $with_tls = cdsa], [
    dnl Look for CDSA...
    AS_IF([test $host_os_name = darwin], [
	AC_CHECK_HEADER([Security/SecureTransport.h], [
	    have_tls="1"
	    with_tls="cdsa"
	    AC_DEFINE([HAVE_TLS], [1], [Do we support TLS?])
	    AC_DEFINE([HAVE_CDSASSL], [1], [Do we have the macOS SecureTransport API?])
	    CUPS_SERVERKEYCHAIN="/Library/Keychains/System.keychain"

	    dnl Check for the various security headers...
	    AC_CHECK_HEADER([Security/SecCertificate.h], [
		AC_DEFINE([HAVE_SECCERTIFICATE_H], [1], [Have the <Security/SecCertificate.h> header?])
	    ])
	    AC_CHECK_HEADER([Security/SecItem.h], [
		AC_DEFINE([HAVE_SECITEM_H], [1], [Have the <Security/SecItem.h> header?])
	    ])
	    AC_CHECK_HEADER([Security/SecPolicy.h], [
		AC_DEFINE([HAVE_SECPOLICY_H], [1], [Have the <Security/SecPolicy.h header?])
	    ])
	])
    ], [test $with_tls = cdsa], [
        AC_MSG_ERROR([--with-tls=cdsa is not compatible with your host operating system.])
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
	    AC_DEFINE([HAVE_TLS], [1], [Do we support TLS?])
	    AC_DEFINE([HAVE_GNUTLS], [1], [Do we have the GNU TLS library?])
	], [
	    AC_MSG_RESULT([no])
	    echo "pkg-config --list-all"
	    $PKGCONFIG --list-all
	    echo "pkg-config --print-requires gnutls"
	    $PKGCONFIG --print-requires gnutls
	])
    ])
    AS_IF([test $have_tls = 0 -a "x$LIBGNUTLSCONFIG" != x], [
	have_tls="1"
	with_tls="gnutls"
	TLSLIBS="$($LIBGNUTLSCONFIG --libs)"
	TLSFLAGS="$($LIBGNUTLSCONFIG --cflags)"
	AC_DEFINE([HAVE_TLS], [1], [Do we support TLS?])
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

IPPALIASES="http"
AS_IF([test $have_tls = 1], [
    AC_MSG_NOTICE([    Using TLSLIBS="$TLSLIBS"])
    AC_MSG_NOTICE([    Using TLSFLAGS="$TLSFLAGS"])
    IPPALIASES="http https ipps"
], [test $with_tls = yes], [
    AC_MSG_ERROR([--with-tls=yes was specified but no compatible TLS libraries could be found.])
])

AC_SUBST([CUPS_SERVERKEYCHAIN])
AC_SUBST([IPPALIASES])
AC_SUBST([TLSFLAGS])
AC_SUBST([TLSLIBS])

EXPORT_TLSLIBS="$TLSLIBS"
AC_SUBST([EXPORT_TLSLIBS])
