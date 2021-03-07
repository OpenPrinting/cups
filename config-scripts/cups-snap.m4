dnl
dnl Support for packaging CUPS in a Snap and have it work with client Snaps.
dnl
dnl Copyright © 2021 by OpenPrinting
dnl
dnl Licensed under Apache License v2.0.  See the file "LICENSE" for more
dnl information.
dnl

AC_ARG_ENABLE([snapped_cupsd], AS_HELP_STRING([--enable-snapped-cupsd], [enable support for packaging CUPS in a Snap]))
AC_ARG_ENABLE([snapped_clients], AS_HELP_STRING([--enable-snapped-clients], [enable support for CUPS controlling admin access from snapped clients]))
AC_ARG_WITH([snapctl], AS_HELP_STRING([--with-snapctl], [Set path for snapctl, only needed with --enable-snapped-cupsd, default=/usr/bin/snapctl]), [
    SNAPCTL="$withval"
], [
    SNAPCTL="/usr/bin/snapctl"
])
AC_DEFINE_UNQUOTED([SNAPCTL], ["$SNAPCTL"], [Location of snapctl program.])
AC_ARG_WITH([cups_control_slot], AS_HELP_STRING([--with-cups-control-slot], [Name for cups-control slot as defined in snapcraft.yaml, only needed with --enable-snapped-cupsd, default=admin]), [
    CUPS_CONTROL_SLOT="$withval"
], [
    CUPS_CONTROL_SLOT="admin"
])
AC_DEFINE_UNQUOTED([CUPS_CONTROL_SLOT], ["$CUPS_CONTROL_SLOT"], ["cups-control" slot name for snap.])

APPARMORLIBS=""
SNAPDGLIBLIBS=""
ENABLE_SNAPPED_CUPSD="NO"
ENABLE_SNAPPED_CLIENTS="NO"

dnl Both --enable-snapped-cupsd and --enable-snapped-clients are about
dnl additional access control for clients, allowing clients which are confined
dnl Snaps only to do adminstrative tasks (create queues, delete someone else's
dnl jobs, ...) if they plug the "cups-control" interface, so
dnl --enable-snapped-cupsd implies --enable-snapped-clients.  The difference is
dnl only the method how to determine whether a client Snap is confined and plugs
dnl "cups-control".
AS_IF([test x$enable_snapped_cupsd = xyes], [
    enable_snapped_clients="yes"
])

AS_IF([test "x$PKGCONFIG" != x -a x$enable_snapped_clients = xyes], [
    AC_MSG_CHECKING([for libapparmor])
    AS_IF([$PKGCONFIG --exists libapparmor], [
	AC_MSG_RESULT([yes])

 	CFLAGS="$CFLAGS $($PKGCONFIG --cflags libapparmor)"
 	APPARMORLIBS="$($PKGCONFIG --libs libapparmor)"
 	AC_DEFINE([HAVE_APPARMOR], [1], [Have the apparmor library?])

	AC_MSG_CHECKING([for libsnapd-glib])

	AS_IF([$PKGCONFIG --exists snapd-glib glib-2.0 gio-2.0], [
	    AC_MSG_RESULT([yes])
	    CFLAGS="$CFLAGS $($PKGCONFIG --cflags snapd-glib glib-2.0 gio-2.0)"
	    SNAPDGLIBLIBS="$($PKGCONFIG --libs snapd-glib glib-2.0 gio-2.0)"
	    AC_DEFINE([HAVE_SNAPDGLIB], [1], [Have the snapd-glib library?])
	], [
	    AC_MSG_RESULT([no])
	])

	AS_IF([test x$enable_snapped_cupsd = xyes], [
	    AC_CHECK_LIB([snapd-glib], [snapd_client_run_snapctl2_sync], [
		AC_DEFINE([HAVE_SNAPD_CLIENT_RUN_SNAPCTL2_SYNC], [1], [Have snapd_client_run_snapctl2_sync function?])
		AC_DEFINE([SUPPORT_SNAPPED_CUPSD], [1], [Support snapped cupsd?])
		AC_DEFINE([SUPPORT_SNAPPED_CLIENTS], [1], [Support snapped CUPS clients?])
		ENABLE_SNAPPED_CUPSD="YES"
		ENABLE_SNAPPED_CLIENTS="YES"
	    ], [
		AS_IF([test "x$SNAPDGLIBLIBS" != "x"], [
		    SNAPDGLIBLIBS=""
		])
		AC_PATH_TOOL([SNAPCTL], [snapctl])
		AC_MSG_CHECKING([for snapctl is-connected support])
		AS_IF([test "x$SNAPCTL" != x && $SNAPCTL is-connected --help >/dev/null 2>&1], [
		    AC_MSG_RESULT([yes])
		    AC_DEFINE([HAVE_SNAPCTL_IS_CONNECTED], [1], [Have snapctl is-connected command?])
		    AC_DEFINE([SUPPORT_SNAPPED_CUPSD], [1], [Support snapped cupsd?])
		    AC_DEFINE([SUPPORT_SNAPPED_CLIENTS], [1], [Support snapped CUPS clients?])
		    ENABLE_SNAPPED_CUPSD="YES"
		    ENABLE_SNAPPED_CLIENTS="YES"
		], [
		    AC_MSG_RESULT([no])
		])
	    ])
	], [
	    AS_IF([test "x$SNAPDGLIBLIBS" != "x"], [
		AC_DEFINE([SUPPORT_SNAPPED_CLIENTS], [1], [Support snapped CUPS clients?])
		ENABLE_SNAPPED_CLIENTS="YES"
	    ])
	])
    ], [
	AC_MSG_RESULT([no])
    ])
])

AC_MSG_CHECKING([for Snap support])
AS_IF([test "x$ENABLE_SNAPPED_CLIENTS" != "xNO"], [
    AS_IF([test "x$ENABLE_SNAPPED_CUPSD" != "xNO"], [
	AC_MSG_RESULT([yes: cupsd + clients])
    ], [
	AC_MSG_RESULT([yes: clients only])
    ])
], [
    AC_MSG_RESULT([no])
])

AC_SUBST([APPARMORLIBS])
AC_SUBST([SNAPDGLIBLIBS])
