dnl
dnl Support for packaging CUPS in different kinds of containers.
dnl
dnl Copyright © 2021 by OpenPrinting
dnl
dnl Licensed under Apache License v2.0.  See the file "LICENSE" for more
dnl information.
dnl

dnl Specify a container mode
CONTAINER="none"

AC_ARG_WITH([container], AS_HELP_STRING([--with-container=...], [configure to use in container (none, snap)]), [
    CONTAINER="$withval"
])

AS_CASE(["$CONTAINER"], [none], [
    # No container in use
], [snap], [
    # Building as a snap
    AC_DEFINE([CUPS_SNAP], [1], [Building as a snap (snapcraft.io)?])
], [*], [
    AC_MSG_ERROR([Unsupported container '$CONTAINER' specified.])
])


dnl Supporting libraries for different containers...
APPARMORLIBS=""
SNAPDGLIBLIBS=""
AC_SUBST([APPARMORLIBS])
AC_SUBST([SNAPDGLIBLIBS])

AS_IF([test "x$PKGCONFIG" != x], [
    AC_MSG_CHECKING([for libapparmor])
    AS_IF([$PKGCONFIG --exists libapparmor], [
	AC_MSG_RESULT([yes])

	CFLAGS="$CFLAGS $($PKGCONFIG --cflags libapparmor)"
	APPARMORLIBS="$($PKGCONFIG --libs libapparmor)"
	AC_DEFINE([HAVE_LIBAPPARMOR], [1], [Have the apparmor library?])

	AC_MSG_CHECKING([for libsnapd-glib-2])
	AS_IF([$PKGCONFIG --exists snapd-glib-2 glib-2.0 gio-2.0], [
	    AC_MSG_RESULT([yes])
	    CFLAGS="$CFLAGS $($PKGCONFIG --cflags snapd-glib-2 glib-2.0 gio-2.0)"
	    SNAPDGLIBLIBS="$($PKGCONFIG --libs snapd-glib-2 glib-2.0 gio-2.0)"
	    AC_DEFINE([HAVE_LIBSNAPDGLIB], [1], [Have the snapd-glib-2 library?])
	    SAVELIBS="$LIBS"
	    LIBS="$SNAPDGLIBLIBS $LIBS"
	    AC_CHECK_FUNC([snapd_client_run_snapctl2_sync], [
		AC_DEFINE([HAVE_SNAPD_CLIENT_RUN_SNAPCTL2_SYNC], [1], [Have the snapd_client_run_snapctl2_sync function?])
	    ])
	    LIBS="$SAVELIBS"
	], [
	    AC_MSG_CHECKING([for libsnapd-glib])
	    AS_IF([$PKGCONFIG --exists snapd-glib glib-2.0 gio-2.0], [
		AC_MSG_RESULT([yes])
		CFLAGS="$CFLAGS $($PKGCONFIG --cflags snapd-glib glib-2.0 gio-2.0)"
		SNAPDGLIBLIBS="$($PKGCONFIG --libs snapd-glib glib-2.0 gio-2.0)"
		AC_DEFINE([HAVE_LIBSNAPDGLIB], [1], [Have the snapd-glib library?])
		SAVELIBS="$LIBS"
		LIBS="$SNAPDGLIBLIBS $LIBS"
		AC_CHECK_FUNC([snapd_client_run_snapctl2_sync], [
		    AC_DEFINE([HAVE_SNAPD_CLIENT_RUN_SNAPCTL2_SYNC], [1], [Have the snapd_client_run_snapctl2_sync function?])
		])
		LIBS="$SAVELIBS"
	    ], [
		AC_MSG_RESULT([no])
	    ])
	])
    ])
])
