dnl Optional dependencies for the local GnuTLS SSLOptions tests.
AC_ARG_ENABLE([test-deps], AS_HELP_STRING([--enable-test-deps], [require optional test dependencies]), [], [enable_test_deps=no])
AS_CASE([$enable_test_deps], [yes|no], [], [AC_MSG_ERROR([--enable-test-deps accepts yes or no])])

SSL_OPTIONS_TEST=no
AS_IF([test "$with_tls" = gnutls], [
    AC_PATH_PROG([TEST_PYTHON], [python3])
    AC_PATH_PROG([TEST_OPENSSL], [openssl])
    AS_IF([test -n "$TEST_PYTHON" -a -n "$TEST_OPENSSL"], [
        AC_MSG_CHECKING([whether test Python supports TLS 1.3])
        AS_IF(["$TEST_PYTHON" -c 'import ssl, sys; sys.exit(not ssl.HAS_TLSv1_3)' >/dev/null 2>&1], [
            AC_MSG_RESULT([yes])
            SSL_OPTIONS_TEST=yes
        ], [AC_MSG_RESULT([no])])
    ])
    AS_IF([test "$SSL_OPTIONS_TEST" = no], [
        AS_IF([test "$enable_test_deps" = yes], [
            AC_MSG_ERROR([GnuTLS SSLOptions tests require Python 3 with TLS 1.3 support and the openssl command])
        ], [AC_MSG_NOTICE([GnuTLS SSLOptions tests will be skipped: optional dependencies unavailable])])
    ])
])
AC_SUBST([TEST_PYTHON])
AC_SUBST([TEST_OPENSSL])
AC_SUBST([SSL_OPTIONS_TEST])
