CHANGES - OpenPrinting CUPS 2.5b1 - (TBA)
==============================================

Changes in CUPS v2.5b1 (YYYY-MM-DD)
-----------------------------------

- Added multiple language support for IPP Everywhere.
- Added `cupsConcatString`, `cupsCopyString`, and `cupsFormatString` string
  APIs.
- Added `cupsRasterInitHeader` API.
- Added `httpConnectURI`, `httpGetCookieValue`, and `httpGetSecurity` APIs.
- Added `ippAddCredentialsString`, `ippGetFirstAttribute`,
  `ippGetNextAttribute`, `ippRestore`, and `ippSave` APIs.
- Added new DNS-SD APIs.
- Added new JSON APIs.
- Added new JWT APIs.
- Added new OAuth APIs.
- Added new WWW form APIs.
- Added "job-sheets-col" support (Issue #138)
- Added "--list-all" option for cupsfilter (Issue #194)
- Added support for wide-area DNS-SD with Avahi (Issue #319)
- Added `cupsCopyDestInfo2` API (Issue #586)
- Added OpenSSL support for `cupsHashData` (Issue #762)
- Added "job-presets-supported" support for CUPS shared printers (Issue #778)
- Added warning if the device has to do IPP request for 'all,media-col-database'
  in separate requests (Issue #829)
- Added a new argument value for `lpstat` argument '-W' - `successful` -
  to get successfully printed jobs (Issue #830)
- Added driver filter to web interface (Issue #848)
- Added support for PAM modules password-auth and system-auth (Issue #892)
- Added Docker support (Issue #929)
- Added a systemd slice to the systemd services included with the scheduler
- Added localizations for deprecated IPP attributes/options (Issue #1020)
- Added support for specifying permissions with the `cupsFileOpen` API.
- Added `cupsGetClock` API.
- Added `cupsParseOptions2` API with "end" argument.
- Added `cups-oauth` and `cups-x509` utilities (Issue #1184)
- Added `DNSSDComputerName` directive to "cupsd.conf" and updated cupsd to
  correctly update the mDNS hostname only if the `DNSSDHostName` directive is
  not specified (Issue #1217)
- Added `print-as-raster` printer and job attributes for forcing rasterization
  (Issue #1282)
- Added an "install" sub-command to the `cups-x509` command (Issue #1227)
- Added a "--user-agent" option to the `ipptool` command.
- Updated documentation (Issue #984, Issue #1086, Issue #1182)
- Updated translations (Issue #1146, Issue #1161, Issue #1164)
- Updated the configure script to default to installing to /usr/local.
- Updated CUPS to use the Windows mDNS APIs.
- Updated CUPS to require TLS support - OpenSSL, GNUTLS and LibreSSL are
  supported.
- Updated CUPS to require ZLIB.
- Updated CUPS to require support for `poll` API.
- Updated `cupsArray` APIs to support modern naming and types.
- Updated IPP Everywhere printer creation error reporting (Issue #347)
- Updated internal usage of CUPS array API to include callback pointer even when
  not used (Issue #674)
- Updated support for using keyword equivalents for enumerated values like
  "print-quality" (Issue #734)
- Updated `cups_enum_dests()` timeout for listing available IPP printers
  (Issue #751)
- Updated the `ippeveprinter` program to support the `-f` option with `-a`
  (Issue #759)
- Updated default destination documentation (Issue #819)
- Updated `httpAddrConnect2()` to handle `POLLHUP` together with `POLLIN` or
  `POLLOUT` (Issue #839)
- Updated the policies help document with the correct `Order` directive to deny
  access by default (Issue #844)
- Updated the "get-printer-attributes-suite.test" test file (Issue #909)
- Updated `cupsRasterReadPixels` and `cupsRasterWritePixels` to not try reading
  or writing if the number of bytes passed is 0 (Issue #914)
- Updated and documented the MIME typing buffering limit (Issue #925)
- Updated the maximum file descriptor limit for `cupsd` to 64k-1 (Issue #989)
- Updated `httpConnectAgain` to re-validate the server's X.509 certificate
  (Issue #1061)
- Updated the raster functions to report more issues via
  `cupsRasterGetErrorString`.
- Updated the `ipptool` utility to support the `--bearer-token` and
  `--client-name` options.
- Updated `cupsEnumDests` and `cupsGetDests` to support printer browsing and
  filtering options in client.conf (Issue #1180)
- Updated the CUPS web interface to make administrative tasks more discoverable
  (Issue #1207)
- Updated the `httpSetCookie` API to support multiple "Set-Cookie:" header
  values.
- Updated the setuid/gid checks in libcups to use `getauxval` on Linux to avoid
  potential security issues (Issue #1258)
- Deprecated the "page-border" Job Template attribute (Issue #1020)
- Removed the `cups-config` utility (use `pkg-config` instead)
- Fixed use-after-free in `cupsdAcceptClient()` when we log warning during error
  handling (fixes CVE-2023-34241)
- Fixed hanging of `lpstat` on Solaris (Issue #156)
- Fixed mapping of PPD InputSlot, MediaType, and OutputBin values (Issue #238)
- Fixed Digest authentication support (Issue #260)
- Fixed document-unprintable-error support (Issue #391)
- Fixed the web interface not showing an error for a non-existent printer
  (Issue #423)
- Fixed extensive looping in scheduler (Issue #604)
- Fixed printing multiple files on specific printers (Issue #643)
- Fixed printing of jobs with job name longer than 255 chars on older printers
  (Issue #644)
- Fixed segfault in `cupsGetNamedDest()` when trying to get default printer, but
  the default printer is not set (Issue #719)
- Fixed ready media support for iOS 17+ (Issue #738)
- Fixed delays in lpd backend (Issue #741)
- Fixed purging job files via `cancel -x` (Issue #742)
- Fixed RFC 1179 port reserving behavior in LPD backend (Issue #743)
- Fixed a bug in the PPD command interpretation code (Issue #768)
- Fixed hanging of `lpstat` on IBM AIX (Issue #773)
- Fixed printing to stderr if we can't open cups-files.conf (Issue #777)
- Fixed memory leak when unloading a job (Issue #813)
- Fixed memory leak when creating color profiles (Issue #814)
- Fixed punch finisher support for IPP Everywhere printers (Issue #821)
- Fixed staple and bind finisher support for IPP Everywhere printers
  (Issue #1073)
- Fixed crash in `scan_ps()` if incoming argument is NULL (Issue #831)
- Fixed setting job state reasons for successful jobs (Issue #832)
- Fixed infinite loop in IPP backend if hostname is IP address with Kerberos
  (Issue #838)
- Fixed crash in `ppdEmitString()` if there is no record for page size `Custom`
  (Issue #849)
- Fixed reporting `media-source-supported` when sharing printer which has
  numbers as strings instead of keywords as `InputSlot` values (Issue #859)
- Fixed IPP backend to support the "print-scaling" option with IPP printers
  (Issue #862)
- Fixed potential race condition for the creation of temporary queues
  (Issue #871)
- Fixed Oki 407 freeze when printing larger jobs (Issue #877)
- Fixed `httpGets` timeout handling (Issue #879)
- Fixed checking for required attributes during PPD generation (Issue #890)
- Fixed incorrect error message with HTTP/IPP related errors (Issue #893)
- Fixed pwg-raster-document-resolution-supported and urf-supported values
  (Issue #901)
- Fixed encoding of IPv6 addresses in HTTP requests (Issue #903)
- Fixed encoding of `IPP_TAG_EXTENSION` values in IPP messages (Issue #913)
- Fixed sending response headers to client (Issue #927)
- Fixed `Host` header regression (Issue #967)
- Fixed DNS-SD lookups of local services with Avahi (Issue #970)
- Fixed debug printfs PID substitution support (Issue #1066)
- Fixed `ServerToken None` in scheduler (Issue #1111)
- Fixed CGI program initialization and validation of form checkbox and text
  fields.
- Fixed finishing support in ippeveps.
- Fixed non-quick copy of collection values.
- Fixed TLS negotiation using OpenSSL with servers that require the TLS SNI
  extension.
- Fixed error handling when reading a mixed `1setOf` attribute.
- Fixed how `ippeveprinter` responds to an unsupported request character set.
- Fixed a recursion issue in `ippReadIO`.
- Fixed verbose listing of `lpstat -l -e` when permanent queue has the same name
  as network discovered (Issue #1120)
- Fixed validation of dateTime values with time zones more than UTC+11
  (Issue #1201)
- Fixed job cleanup after daemon restart (Issue #1315)
- Fixed unreachable block in IPP backend (Issue #1351)
- Fixed memory leak in _cupsConvertOptions (Issue #1354)
- Fixed missing write check in `cupsFileOpen/Fd` (Issue #1360)
- Fixed error recovery when scanning for PPDs in `cups-driverd` (Issue #1416)
- Removed hash support for SHA2-512-224 and SHA2-512-256.
- Removed `mantohtml` script for generating html pages (use
  `https://www.msweet.org/mantohtml/`)
- Removed SSPI and Security.framework support (CDSA)
