CHANGES - OpenPrinting CUPS 2.5b1 - (TBA)
==============================================

Changes in CUPS v2.5b1 (TBA)
----------------------------

- Added multiple language support for IPP Everywhere.
- Added `cupsConcatString`, `cupsCopyString`, and `cupsFormatString` string
  APIs.
- Added new `cupsRasterInitHeader` API.
- Added `httpConnectURI` API.
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
- Updated documents (Issue #984)
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
- Deprecated the "page-border" Job Template attribute (Issue #1020)
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
- Fixed CGI program initialization and validation of form checkbox and text
  fields.
- Fixed finishing support in ippeveps.
- Fixed non-quick copy of collection values.
- Fixed TLS negotiation using OpenSSL with servers that require the TLS SNI
  extension.
- Fixed error handling when reading a mixed `1setOf` attribute.
- Fixed how `ippeveprinter` responds to an unsupported request character set.
- Fixed a recursion issue in `ippReadIO`.
- Removed hash support for SHA2-512-224 and SHA2-512-256.
- Removed `mantohtml` script for generating html pages (use
  `https://www.msweet.org/mantohtml/`)
- Removed SSPI and Security.framework support (CDSA)
