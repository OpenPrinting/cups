dnl
dnl Support for packaging CUPS in a Snap and have it work with client Snaps.
dnl
dnl Copyright © 2021 by Till Kamppeter
dnl Copyright © 2021 by OpenPrinting
dnl
dnl Licensed under Apache License v2.0.  See the file "LICENSE" for more
dnl information.
dnl

# Snap packaging and Snap interaction support

AC_ARG_ENABLE(snapped_cupsd, [  --enable-snapped-cupsd  enable support for packaging CUPS in a Snap])
AC_ARG_ENABLE(snapped_clients, [  --enable-snapped-clients enable support for CUPS controlling admin access from snapped clients])
AC_ARG_WITH(snapctl, [  --with-snapctl          Set path for snapctl, only needed with --enable-snapped-cupsd, default=/usr/bin/snapctl],
        SNAPCTL="$withval", SNAPCTL="/usr/bin/snapctl")
AC_DEFINE_UNQUOTED(SNAPCTL, "$SNAPCTL")
AC_ARG_WITH(cups_control_slot, [  --with-cups-control-slot Name for cups-control slot as defined in snapcraft.yaml, only needed with --enable-snapped-cupsd, default=admin],
        CUPS_CONTROL_SLOT="$withval", CUPS_CONTROL_SLOT="admin")
AC_DEFINE_UNQUOTED(CUPS_CONTROL_SLOT, "$CUPS_CONTROL_SLOT")

APPARMORLIBS=""
SNAPDGLIBLIBS=""
ENABLE_SNAPPED_CUPSD="NO"
ENABLE_SNAPPED_CLIENTS="NO"

# Both --enable-snapped-cupsd and --enable-snapped-clients are about additional
# access control for clients, allowing clients which are confined Snaps only
# to do adminstrative tasks (create queues, delete someone else's jobs, ...)
# if they plug the "cups-control" interface, so  --enable-snapped-cupsd implies
# --enable-snapped-clients. The difference is only the method how to determine
# whether a client Snap is confined and plugs "cups-control".
if test x$enable_snapped_cupsd == xyes; then
	enable_snapped_clients=yes;
fi

if test "x$PKGCONFIG" != x -a x$enable_snapped_clients == xyes; then
	AC_MSG_CHECKING(for libapparmor)
	if $PKGCONFIG --exists libapparmor; then
		AC_MSG_RESULT(yes)
		CFLAGS="$CFLAGS `$PKGCONFIG --cflags libapparmor`"
		APPARMORLIBS="`$PKGCONFIG --libs libapparmor`"
		AC_DEFINE(HAVE_APPARMOR)
		AC_MSG_CHECKING(for libsnapd-glib)
		if $PKGCONFIG --exists snapd-glib glib-2.0 gio-2.0; then
			AC_MSG_RESULT(yes)
			CFLAGS="$CFLAGS `$PKGCONFIG --cflags snapd-glib glib-2.0 gio-2.0`"
			SNAPDGLIBLIBS="`$PKGCONFIG --libs snapd-glib glib-2.0 gio-2.0`"
			AC_DEFINE(HAVE_SNAPDGLIB)
		else
			AC_MSG_RESULT(no)
		fi
		if test x$enable_snapped_cupsd == xyes; then
			AC_CHECK_LIB(snapd-glib, snapd_client_run_snapctl2_sync, [
				AC_DEFINE(HAVE_SNAPD_CLIENT_RUN_SNAPCTL2_SYNC)
				AC_DEFINE(SUPPORT_SNAPPED_CUPSD)
				AC_DEFINE(SUPPORT_SNAPPED_CLIENTS)
				ENABLE_SNAPPED_CUPSD="YES"
				ENABLE_SNAPPED_CLIENTS="YES"
			], [
				if test "x$SNAPDGLIBLIBS" != "x"; then
					SNAPDGLIBLIBS=""
				fi
				AC_PATH_TOOL(SNAPCTL, snapctl)
				AC_MSG_CHECKING(for "snapctl is-connected" support)
				if test "x$SNAPCTL" != x && $SNAPCTL is-connected --help >/dev/null 2>&1; then
					AC_MSG_RESULT(yes)
					AC_DEFINE(HAVE_SNAPCTL_IS_CONNECTED)
					AC_DEFINE(SUPPORT_SNAPPED_CUPSD)
					AC_DEFINE(SUPPORT_SNAPPED_CLIENTS)
					ENABLE_SNAPPED_CUPSD="YES"
					ENABLE_SNAPPED_CLIENTS="YES"
				else
					AC_MSG_RESULT(no)
				fi
			])
		else
			if test "x$SNAPDGLIBLIBS" != "x"; then
				AC_DEFINE(SUPPORT_SNAPPED_CLIENTS)
				ENABLE_SNAPPED_CLIENTS="YES"
			fi
		fi
	else
		AC_MSG_RESULT(no)
	fi
fi

AC_MSG_CHECKING(for Snap support)
if test "x$ENABLE_SNAPPED_CLIENTS" != "xNO"; then
	if test "x$ENABLE_SNAPPED_CUPSD" != "xNO"; then
		AC_MSG_RESULT(yes: cupsd + clients)
	else
		AC_MSG_RESULT(yes: clients only)
	fi
else
	AC_MSG_RESULT(no)
fi

AC_SUBST(APPARMORLIBS)
AC_SUBST(SNAPDGLIBLIBS)
