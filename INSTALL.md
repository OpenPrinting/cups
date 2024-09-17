Building and Installing OpenPrinting CUPS
=========================================

This file describes how to compile and install CUPS from source code.  For more
information on CUPS see the file called `README.md`.

Using CUPS requires additional third-party support software and printer drivers.
These are typically included with your operating system distribution.


Before You Begin
----------------

You'll need ANSI-compliant C and C++ compilers, plus a make program and POSIX-
compliant shell (/bin/sh).  The GNU compiler tools and Bash work well and we
have tested the current CUPS code against several versions of Clang and GCC with
excellent results.

The makefiles used by the project should work with POSIX-compliant versions of
`make`.  We've tested them with GNU make as well as several vendor make programs.
BSD users should use GNU make (`gmake`) since BSD make is not POSIX-compliant
and does not support the `include` directive.

Besides these tools you'll want ZLIB for compression support, Avahi for mDNS
support, LIBUSB for USB printing support, the GNU TLS, LibreSSL, or OpenSSL
libraries for encryption support on platforms other than iOS, macOS, or Windows,
and PAM for authentication support.  CUPS will compile and run without these,
however you'll miss out on many of the features provided by CUPS.

> Note: Kerberos support is deprecated starting with CUPS 2.4.0 and will be
> removed in a future version of CUPS.  To build CUPS with Kerberos support,
> specify the "--enable-gssapi" configure option below.

On a stock Ubuntu install, the following command will install the required
prerequisites:

    sudo apt-get install autoconf build-essential \
         avahi-daemon libavahi-client-dev \
         libssl-dev libkrb5-dev libnss-mdns libpam-dev \
         libsystemd-dev libusb-1.0-0-dev zlib1g-dev


For Fedora you can install these packages:

    sudo dnf install autoconf make automake gcc gcc-c++ \
         avahi avahi-devel \
         openssl-devel krb5-devel krb5-libs nss-mdns pam-devel \
         systemd-devel libusb1-devel zlib-devel


Configuration
-------------

CUPS uses GNU autoconf, so you should find the usual "configure" script in the
main CUPS source directory.  To configure CUPS for your system, type:

    ./configure

The default installation will put the CUPS software in the "/etc", "/usr", and
"/var" directories on your system, which will overwrite any existing printing
commands on your system.  Use the `--prefix` option to install the CUPS software
in another location:

    ./configure --prefix=/some/directory

To see a complete list of configuration options, use the `--help` option:

    ./configure --help

If any of the dependent libraries are not installed in a system default location
(typically "/usr/include" and "/usr/lib") you'll need to set the `CFLAGS`,
`CPPFLAGS`, `CXXFLAGS`, `DSOFLAGS`, and `LDFLAGS` environment variables prior to
running configure:

    setenv CFLAGS "-I/some/directory"
    setenv CPPFLAGS "-I/some/directory"
    setenv CXXFLAGS "-I/some/directory"
    setenv DSOFLAGS "-L/some/directory"
    setenv LDFLAGS "-L/some/directory"
    ./configure ...

or:

    CFLAGS="-I/some/directory" \
    CPPFLAGS="-I/some/directory" \
    CXXFLAGS="-I/some/directory" \
    DSOFLAGS="-L/some/directory" \
    LDFLAGS="-L/some/directory" \
    ./configure ...

The `--enable-debug` option compiles CUPS with debugging information enabled.
Additional debug logging support can be enabled using the
`--enable-debug-printfs` option - these debug messages are enabled using the
`CUPS_DEBUG_xxx` environment variables at run-time.

CUPS also includes an extensive set of unit tests that can be used to find and
diagnose a variety of common problems - use the "--enable-unit-tests" configure
option to run them at build time.

Once you have configured things, just type:

    make

or if you have FreeBSD, NetBSD, or OpenBSD type:

    gmake

to build the software.


Testing the Software
--------------------

Aside from the built-in unit tests, CUPS includes an automated test framework
for testing the entire printing system.  To run the tests, just type:

    make test

or if you have FreeBSD, NetBSD, or OpenBSD type:

    gmake test

The test framework runs a copy of the CUPS scheduler (cupsd) on port 8631 in
"/tmp/cups-$USER" and produces a nice HTML report of the results.


Installing the Software
-----------------------

Once you have built the software you need to install it.  The "install" target
provides a quick way to install the software on your local system:

    make install

or for FreeBSD, NetBSD, or OpenBSD:

    gmake install

Use the `BUILDROOT` variable to install to an alternate root directory:

    make BUILDROOT=/some/other/root/directory install

You can also build binary packages that can be installed on other machines using
the RPM spec file ("packaging/cups.spec") or EPM list file
("packaging/cups.list").  The latter also supports building of binary RPMs, so
it may be more convenient to use.

You can find the RPM software at <http://www.rpm.org/>.

The EPM software is available at <https://jimjag.github.io/epm/>.


Creating Binary Distributions With Epm
--------------------------------------

The top level makefile supports generation of many types of binary distributions
using EPM.  To build a binary distribution type:

    make FORMAT

or

    gmake FORMAT

for FreeBSD, NetBSD, and OpenBSD.  The "FORMAT" target is one of the following:

- "epm": Builds a script + tarfile package
- "bsd": Builds a *BSD package
- "deb": Builds a Debian package
- "pkg": Builds a Solaris package
- "rpm": Builds a RPM package
- "slackware": Build a Slackware package


Getting Debug Logging From CUPS
-------------------------------

When configured with the `--enable-debug-printfs` option, CUPS compiles in
additional debug logging support in the scheduler, CUPS API, and CUPS Imaging
API.  The following environment variables are used to enable and control debug
logging:

- `CUPS_DEBUG_FILTER`: Specifies a POSIX regular expression to control which
  messages are logged.
- `CUPS_DEBUG_LEVEL`: Specifies a number from 0 to 9 to control the verbosity of
  the logging. The default level is 1.
- `CUPS_DEBUG_LOG`: Specifies a log file to use.  Specify the name "-" to send
  the messages to stderr.  Prefix a filename with "+" to append to an existing
  file.  You can include a single "%d" in the filename to embed the current
  process ID.
