CHANGES - OpenPrinting CUPS 2.5b1 - (TBA)
==============================================

Changes in CUPS v2.5b1 (TBA)
----------------------------

- Added `cupsDNSSD` APIs.
- Added `cupsConcatString` and `cupsCopyString` string APIs.
- Added new APIs for form, JSON, JWT, IPP, and raster setup.
- Added OpenSSL support for `cupsHashData` (Issue #762)
- Added warning if the device has to do IPP request for 'all,media-col-database'
  in separate requests (Issue #829)
- Added a new argument value for `lpstat` argument '-W' - `successful` -
  to get successfully printed jobs (Issue #830)
- Added driver filter to web interface (Issue #848)
- Added support for PAM modules password-auth and system-auth (Issue #892)
- Updated CUPS to require TLS support - OpenSSL, GNUTLS and LibreSSL are
  supported.
- Updated CUPS to require ZLIB.
- Updated CUPS to require support for `poll` API.
- Updated `cupsArray` APIs to support modern naming and types.
- Updated `cups_enum_dests()` timeout for listing available IPP printers
  (Issue #751)
- Updated `httpAddrConnect2()` to handle `POLLHUP` together with `POLLIN` or
  `POLLOUT` (Issue #839)
- Updated `cupsRasterReadPixels` and `cupsRasterWritePixels` to not try reading
  or writing if the number of bytes passed is 0 (Issue #914)
- Fixed use-after-free in `cupsdAcceptClient()` when we log warning during error
  handling (fixes CVE-2023-34241)
- Fixed hanging of `lpstat` on Solaris (Issue #156)
- Fixed Digest authentication support (Issue #260)
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
- Fixed Oki 407 freeze when printing larger jobs (Issue #877)
- Fixed checking for required attributes during PPD generation (Issue #890)
- Fixed encoding of IPv6 addresses in HTTP requests (Issue #903)
- Fixed encoding of `IPP_TAG_EXTENSION` values in IPP messages (Issue #913)
- Removed hash support for SHA2-512-224 and SHA2-512-256.
- Removed `mantohtml` script for generating html pages (use
  `https://www.msweet.org/mantohtml/`)
- Removed SSPI and Security.framework support (CDSA)
