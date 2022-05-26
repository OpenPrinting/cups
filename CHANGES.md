CHANGES - OpenPrinting CUPS 2.4.2 - 2022-05-26
==============================================

Changes in CUPS v2.4.2 (26th May 2022)
--------------------------------------

- Fixed certificate strings comparison for Local authorization (CVE-2022-26691)
- The `cupsFileOpen` function no longer opens files for append in read-write
  mode (Issue #291)
- The cupsd daemon removed processing temporary queue (Issue #364)
- Fixed delay in IPP backend if GNUTLS is used and endpoint doesn't confirm
  closing the connection (Issue #365)
- Fixed conditional jump based on uninitialized value in cups/ppd.c (Issue #329)
- Fixed CSS related issues in CUPS Web UI (Issue #344)
- Fixed copyright in CUPS Web UI trailer template (Issue #346)
- mDNS hostname in device uri is not resolved when installaling a permanent
  IPP Everywhere queue (Issues #340, #343)
- The `lpstat` command now reports when the scheduler is not running
  (Issue #352)
- Updated the man pages concerning the `-h` option (Issue #357)
- Re-added LibreSSL/OpenSSL support (Issue #362)
- Updated the Solaris smf service file (Issue #368)
- Fixed a regression in lpoptions option support (Issue #370)
- The scheduler now regenerates the PPD cache information after changing the
  "cupsd.conf" file (Issue #371)
- Updated the scheduler to set "auth-info-required" to "username,password" if a
  backend reports it needs authentication info but doesn't set a method for
  authentication (Issue #373)
- Updated the configure script to look for the OpenSSL library the old way if
  pkg-config is not available (Issue #375)
- Fixed the prototype for the `httpWriteResponse` function (Issue #380)
- Brought back minimal AIX support (Issue #389)
- `cupsGetResponse` did not always set the last error.
- Fixed a number of old references to the Apple CUPS web page.
- Restored the default/generic printer icon file for the web interface.
- Removed old stylesheet classes that are no longer used by the web
  interface.


Changes in CUPS v2.4.1 (27th January 2020)
------------------------------------------

- The default color mode now is now configurable and defaults to the printer's
  reported default mode (Issue #277)
- Configuration script now checks linking for -Wl,-pie flags (Issue #303)
- Fixed memory leaks - in testi18n (Issue #313), in `cups_enum_dests()`
  (Issue #317), in `_cupsEncodeOption()` and `http_tls_upgrade()` (Issue #322)
- Fixed missing bracket in de/index.html (Issue #299)
- Fixed typos in configuration scripts (Issues #304, #316)
- Removed remaining legacy code for `RIP_MAX_CACHE` environment variable
  (Issue #323)
- Removed deprecated directives from cupsctl and cups-files.conf (Issue #300)
- Removed `purge-jobs` legacy code from CGI scripts and templates (Issue #325)


Changes in CUPS v2.4.0 (29th November 2021)
-------------------------------------------

- Added configure option --with-idle-exit-timeout (Issue #294)
- Added --with-systemd-timeoutstartsec configure option (Issue #298)
- DigestOptions now are applied for MD5 Digest authentication defined
  by RFC 2069 as well (Issue #287)
- Fixed compilation on Solaris (Issue #293)
- Fixed and improved German translations (Issue #296, Issue #297)


Changes in CUPS v2.4rc1 (12th November 2021)
--------------------------------------------

- Added warning and debug messages when loading printers
 if the queue is raw or with driver (Issue #286)
- Compilation now uses -fstack-protector-strong if available (Issue #285)


Changes in CUPS v2.4b1 (27th October 2021)
------------------------------------------

- Added support for CUPS running in a Snapcraft snap.
- Added basic OAuth 2.0 client support (Issue #100)
- Added support for AirPrint and Mopria clients (Issue #105)
- Added configure support for specifying systemd dependencies in the CUPS
  service file (Issue #144)
- Added several features and improvements to `ipptool` (Issue #153)
- Added a JSON output mode for `ipptool`.
- The `ipptool` command now correctly reports an error when a test file cannot
  be found.
- CUPS library now uses thread safe `getpwnam_r` and `getpwuid_r` functions
  (Issue #274)
- Fixed Kerberos authentication for the web interface (Issue #19)
- The ZPL sample driver now supports more "standard" label sizes (Issue #70)
- Fixed reporting of printer instances when enumerating and when no options are
  set for the main instance (Issue #71)
- Reverted USB read limit enforcement change from CUPS 2.2.12 (Issue #72)
- The IPP backend did not return the correct status code when a job was canceled
  at the printer/server (Issue #74)
- The `testlang` unit test program now loops over all of the available locales
  by default (Issue #85)
- The `cupsfilter` command now shows error messages when options are used
  incorrectly (Issue #88)
- The PPD functions now treat boolean values as case-insensitive (Issue #106)
- Temporary queue names no longer end with an underscore (Issue #110)
- The USB backend now runs as root (Issue #121)
- Added pkg-config file for libcups (Issue #122)
- Fixed a PPD memory leak caused by emulator definitions (Issue #124)
- Fixed a `DISPLAY` bug in `ipptool` (Issue #139)
- The scheduler now includes the `[Job N]` prefix for job log messages, even
  when using syslog logging (Issue #154)
- Added support for locales using the GB18030 character set (Issue #159)
- `httpReconnect2` did not reset the socket file descriptor when the TLS
  negotiation failed (Apple #5907)
- `httpUpdate` did not reset the socket file descriptor when the TLS
  negotiation failed (Apple #5915)
- The IPP backend now retries Validate-Job requests (Issue #132)
- Now show better error messages when a driver interface program fails to
  provide a PPD file (Issue #148)
- Added dark mode support to the CUPS web interface (Issue #152)
- Added a workaround for Solaris in `httpAddrConnect2` (Issue #156)
- Fixed an interaction between `--remote-admin` and `--remote-any` for the
  `cupsctl` command (Issue #158)
- Now use a 60 second timeout for reading USB backchannel data (Issue #160)
- The USB backend now tries harder to find a serial number (Issue #170)
- Fixed `@IF(name)` handling in `cupsd.conf` (Apple #5918)
- Fixed documentation and added examples for CUPS' limited CGI support
  (Apple #5940)
- Fixed the `lpc` command prompt (Apple #5946)
- Now always pass "localhost" in the `Host:` header when talking over a domain
  socket or the loopback interface (Issue #185)
- Fixed a job history update issue in the scheduler (Issue #187)
- Fixed `job-pages-per-set` value for duplex print jobs.
- Fixed an edge case in `ippReadIO` to make sure that only complete attributes
  and values are retained on an error (Issue #195)
- Hardened `ippReadIO` to prevent invalid IPP messages from being propagated
  (Issue #195, Issue #196)
- The scheduler now supports the "everywhere" model directly (Issue #201)
- Fixed some IPP Everywhere option mapping problems (Issue #238)
- Fixed support for "job-hold-until" with the Restart-Job operation (Issue #250)
- Fixed the default color/grayscale presets for IPP Everywhere PPDs (Issue #262)
- Fixed support for the 'offline-report' state for all USB backends (Issue #264)
- Documentation fixes (Issue #92, Issue #163, Issue #177, Issue #184)
- Localization updates (Issue #123, Issue #129, Issue #134, Issue #146,
  Issue #164)
- USB quirk updates (Issue #192, Issue #270, Apple #5766, Apple #5838,
  Apple #5843, Apple #5867)
- Web interface updates (Issue #142, Issue #218)
- The `ippeveprinter` tool now automatically uses an available port.
- Fixed several Windows TLS and hashing issues.
- Deprecated cups-config (Issue #97)
- Deprecated Kerberos (`AuthType Negotiate`) authentication (Issue #98)
- Removed support for the (long deprecated and unused) `FontPath`,
  `ListenBackLog`, `LPDConfigFile`, `KeepAliveTimeout`, `RIPCache`, and
  `SMBConfigFile` directives in `cupsd.conf` and `cups-files.conf`.
- Stubbed out deprecated `httpMD5` functions.
- Add test for undefined page ranges during printing.


CUPS v2.3.3op2 (February 1, 2021)
---------------------------------

- Security: Fixed a buffer (read) overflow in the `ippReadIO` function
  (CVE-2020-10001)
- Clarified the documentation for the "Listen" directive (Issue #53)
- Fixed duplicate ColorModel entries for AirPrint printers (Issue 59)
- Fixed directory/permission defaults for Debian kfreebsd-based systems
  (Issue #60, Issue #61)
- Fixed crash bug in `ppdOpen` (Issue #64, Issue #78)
- Fixed regression in `snprintf` emulation function (Issue #67)
- The scheduler's systemd service file now waits for the nslcd service to start
  (Issue #69)
- The libusb-based USB backend now uses a simpler read timer implementation to
  avoid a regression in a previous change (Issue #72)
- The PPD caching code now only tracks the `APPrinterIconPath` value on macOS
  (Issue #73)
- Fixed segfault in help.cgi when searching in man pages (Issue #81)
- Root certificates were incorrectly stored in "~/.cups/ssl".


CUPS v2.3.3op1 (November 27, 2020)
----------------------------------

- The automated test suite can now be activated using `make test` for
  consistency with other projects and CI environments - the old `make check`
  continues to work as well, and the previous test server behavior can be
  accessed by running `make testserver`.
- ippeveprinter now supports multiple icons and strings files.
- ippeveprinter now uses the system's FQDN with Avahi.
- ippeveprinter now supports Get-Printer-Attributes on "/".
- ippeveprinter now uses a deterministic "printer-uuid" value.
- ippeveprinter now uses system sounds on macOS for Identify-Printer.
- Updated ippfind to look for files in "~/Desktop" on Windows.
- Updated ippfind to honor `SKIP-XXX` directives with `PAUSE`.
- Updated IPP Everywhere support to work around printers that only advertise
  color raster support but really also support grayscale (Issue #1)
- ipptool now supports DNS-SD URIs like `ipps://My%20Printer._ipps._tcp.local`
  (Issue #5)
- The scheduler now allows root backends to have world read permissions but not
  world execute permissions (Issue #21)
- Failures to bind IPv6 listener sockets no longer cause errors if IPv6 is
  disabled on the host (Issue #25)
- The SNMP backend now supports the HP and Ricoh vendor MIBs (Issue #28)
- The scheduler no longer includes a timestamp in files it writes (Issue #29)
- The systemd service names are now "cups.service" and "cups-lpd.service"
  (Issue #30, Issue #31)
- The scheduler no longer adds the local hostname to the ServerAlias list
  (Issue #32)
- Added `LogFileGroup` directive in "cups-files.conf" to control the group
  owner of log files (Issue #34)
- Added `--with-max-log-size` configure option (Issue #35)
- Added `--enable-sync-on-close` configure option (Issue #37)
- Added `--with-error-policy` configure option (Issue #38)
- IPP Everywhere PPDs could have an "unknown" default InputSlot (Issue #44)
- The `httpAddrListen` function now uses a listen backlog of 128.
- Added USB quirks (Apple issue #5789, #5823, #5831)
- Fixed IPP Everywhere v1.1 conformance issues in ippeveprinter.
- Fixed DNS-SD name collision support in ippeveprinter.
- Fixed compiler and code analyzer warnings.
- Fixed TLS support on Windows.
- Fixed ippfind sub-type searches with Avahi.
- Fixed the default hostname used by ippeveprinter on macOS.
- Fixed resolution of local IPP-USB printers with Avahi.
- Fixed coverity issues (Issue #2)
- Fixed `httpAddrConnect` issues (Issue #3)
- Fixed web interface device URI issue (Issue #4)
- Fixed lp/lpr "printer/class not found" error reporting (Issue #6)
- Fixed xinetd support for LPD clients (Issue #7)
- Fixed libtool build issue (Issue #11)
- Fixed a memory leak in the scheduler (Issue #12)
- Fixed a potential integer overflow in the PPD hashing code (Issue #13)
- Fixed output-bin and print-quality handling issues (Issue #18)
- Fixed PPD options getting mapped to odd IPP values like "tray---4" (Issue #23)
- Fixed remote access to the cupsd.conf and log files (Issue #24)
- Fixed the automated test suite when running in certain build/CI environments
  (Issue #25)
- Fixed a logging regression caused by a previous change for Apple issue #5604
  (Issue #25)
- Fixed fax phone number handling with GNOME (Issue #40)
- Fixed potential rounding error in rastertopwg filter (Issue #41)
- Fixed the "uri-security-supported" value from the scheduler (Issue #42)
- Fixed IPP backend crash bug with "printer-alert" values (Issue #43)
- Removed old Solaris inetconv(1m) reference in cups-lpd man page (Issue #46)
- Fixed default options that incorrectly use the "custom" prefix (Issue #48)
- Fixed a memory leak when resolving DNS-SD URIs (Issue #49)
- Fixed systemd status reporting by adopting the notify interface (Issue #51)
- Fixed crash in rastertopwg (Apple issue #5773)
- Fixed cupsManualCopies values in IPP Everywhere PPDs (Apple issue #5807)
