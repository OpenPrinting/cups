CHANGES - OpenPrinting CUPS
===========================


Changes in CUPS v2.4.13 (YYYY-MM-DD)
------------------------------------

- Updated the scheduler to send the "printer-added" or "printer-modified" events
  whenever an IPP Everywhere PPD is installed (Issue #1244)
- Updated the scheduler to send the "printer-modified" event whenever the system
  default printer is changed (Issue #1246)
- Fixed a memory leak in `httpClose` (Issue #1223)
- Fixed missing commas in `ippCreateRequestedArray` (Issue #1234)
- Fixed subscription issues in the scheduler and D-Bus notifier (Issue #1235)
- Fixed support for IPP/PPD options with periods or underscores (Issue #1249)


Changes in CUPS v2.4.12 (2025-04-08)
------------------------------------

- GnuTLS follows system crypto policies now (Issue #1105)
- Added `NoSystem` SSLOptions value (Issue #1130)
- Now we raise alert for certificate issues (Issue #1194)
- Added Kyocera USB quirk (Issue #1198)
- The scheduler now logs a job's debugging history if the backend fails
  (Issue #1205)
- Fixed a potential timing issue with `cupsEnumDests` (Issue #1084)
- Fixed a potential "lost PPD" condition in the scheduler (Issue #1109)
- Fixed a compressed file error handling bug (Issue #1070)
- Fixed a bug in the make-and-model whitespace trimming code (Issue #1096)
- Fixed a removal of IPP Everywhere permanent queue if installation failed
  (Issue #1102)
- Fixed `ServerToken None` in scheduler (Issue #1111)
- Fixed invalid IPP keyword values created from PPD option names (Issue #1118)
- Fixed handling of "media" and "PageSize" in the same print request
  (Issue #1125)
- Fixed client raster printing from macOS (Issue #1143)
- Fixed the default User-Agent string.
- Fixed a recursion issue in `ippReadIO`.
- Fixed handling incorrect radix in `scan_ps()` (Issue #1188)
- Fixed validation of dateTime values with time zones more than UTC+11
  (Issue #1201)
- Fixed attributes returned by the Create-Xxx-Subscriptions requests
  (Issue #1204)
- Fixed `ippDateToTime` when using a non GMT/UTC timezone (Issue #1208)
- Fixed `job-completed` event notifications for jobs that are cancelled before
  started (Issue #1209)
- Fixed DNS-SD discovery with `ippfind` (Issue #1211)


Changes in CUPS v2.4.11 (2024-09-30)
------------------------------------

- Updated the maximum file descriptor limit for `cupsd` to 64k-1 (Issue #989)
- Fixed `lpoptions -d` with a discovered but not added printer (Issue #833)
- Fixed incorrect error message for HTTP/IPP errors (Issue #893)
- Fixed JobPrivateAccess and SubscriptionPrivateAccess support for "all"
  (Issue #990)
- Fixed issues with cupsGetDestMediaByXxx (Issue #993)
- Fixed adding and modifying of printers via the web interface (Issue #998)
- Fixed HTTP PeerCred authentication for domain users (Issue #1001)
- Fixed checkbox support (Issue #1008)
- Fixed printer state notifications (Issue #1013)
- Fixed IPP Everywhere printer setup (Issue #1033)


Changes in CUPS v2.4.10 (2024-06-18)
------------------------------------

- Fixed error handling when reading a mixed `1setOf` attribute.
- Fixed scheduler start if there is only domain socket to listen on (Issue #985)


Changes in CUPS v2.4.9 (2024-06-11)
-----------------------------------

- Fixed domain socket handling (CVE-2024-35235)
- Fixed creating of `cupsUrfSupported` PPD keyword (Issue #952)
- Fixed searching for destinations in web ui (Issue #954)
- Fixed TLS negotiation using OpenSSL with servers that require the TLS SNI
  extension.
- Really raised `cups_enum_dests()` timeout for listing available IPP printers
  (Issue #751)...
- Fixed `Host` header regression (Issue #967)
- Fixed DNS-SD lookups of local services with Avahi (Issue #970)
- Fixed listing jobs in destinations in web ui. (Apple issue #6204)
- Fixed showing search query in web ui help page. (Issue #977)


Changes in CUPS v2.4.8 (2024-04-26)
-----------------------------------

- Added warning if the device has to be asked for 'all,media-col-database'
  separately (Issue #829)
- Added new value for 'lpstat' option '-W' - successfull - for getting
  successfully printed jobs (Issue #830)
- Added support for PAM modules password-auth and system-auth (Issue #892)
- Updated IPP Everywhere printer creation error reporting (Issue #347)
- Updated and documented the MIME typing buffering limit (Issue #925)
- Now report an error for temporary printer defaults with lpadmin (Issue #237)
- Fixed mapping of PPD InputSlot, MediaType, and OutputBin values (Issue #238)
- Fixed "document-unprintable-error" handling (Issue #391)
- Fixed the web interface not showing an error for a non-existent printer
  (Issue #423)
- Fixed printing of jobs with job name longer than 255 chars on older printers
  (Issue #644)
- Really backported fix for Issue #742
- Fixed `cupsCopyDestInfo` device connection detection (Issue #586)
- Fixed "Upgrade" header handling when there is no TLS support (Issue #775)
- Fixed memory leak when unloading a job (Issue #813)
- Fixed memory leak when creating color profiles (Issue #815)
- Fixed a punch finishing bug in the IPP Everywhere support (Issue #821)
- Fixed crash in `scan_ps()` if incoming argument is NULL (Issue #831)
- Fixed setting job state reasons for successful jobs (Issue #832)
- Fixed infinite loop in IPP backend if hostname is IP address with Kerberos
  (Issue #838)
- Added additional check on socket if `revents` from `poll()` returns POLLHUP
  together with POLLIN or POLLOUT in `httpAddrConnect2()` (Issue #839)
- Fixed crash in `ppdEmitString()` if `size` is NULL (Issue #850)
- Fixed reporting `media-source-supported` when sharing printer which has
  numbers as strings instead of keywords as `InputSlot` values (Issue #859)
- Fixed IPP backend to support the "print-scaling" option with IPP printers
  (Issue #862)
- Fixed potential race condition for the creation of temporary queues
  (Issue #871)
- Fixed `httpGets` timeout handling (Issue #879)
- Fixed checking for required attributes during PPD generation (Issue #890)
- Fixed encoding of IPv6 addresses in HTTP requests (Issue #903)
- Fixed sending response headers to client (Issue #927)
- Fixed CGI program initialization and validation of form checkbox and text
  fields.


Changes in CUPS v2.4.7 (2023-09-20)
-----------------------------------

- CVE-2023-4504 - Fixed Heap-based buffer overflow when reading Postscript
  in PPD files
- Added OpenSSL support for cupsHashData (Issue #762)
- Fixed delays in lpd backend (Issue #741)
- Fixed extensive logging in scheduler (Issue #604)
- Fixed hanging of `lpstat` on IBM AIX (Issue #773)
- Fixed hanging of `lpstat` on Solaris (Issue #156)
- Fixed printing to stderr if we can't open cups-files.conf (Issue #777)
- Fixed purging job files via `cancel -x` (Issue #742)
- Fixed RFC 1179 port reserving behavior in LPD backend (Issue #743)
- Fixed a bug in the PPD command interpretation code (Issue #768)
- Fixed Oki 407 freeze when printing larger jobs (Issue #877)


Changes in CUPS v2.4.6 (2023-06-22)
-----------------------------------

- CVE-2023-34241: Fixed use-after-free when logging warnings in case of failures
  in `cupsdAcceptClient()`.
- Fixed linking error on old MacOS (Issue #715)
- Fixed printing multiple files on specific printers (Issue #643)


Changes in CUPS v2.4.5 (2023-06-13)
-----------------------------------

- Fixed corruption of locally saved certificates (Issue #724)


Changes in CUPS v2.4.4 (2023-06-06)
-----------------------------------

- Fixed segfault in `cupsGetNamedDest()` when trying to get default printer, but
  the default printer is not set (Issue #719)


Changes in CUPS v2.4.3 (2023-06-01)
-----------------------------------

- CVE-2023-32360: Fixed default policy for CUPS-Get-Document operation
- CVE-2023-32324: Fixed possible heap buffer overflow in `_cups_strlcpy()`.
- Added a title with device uri for found network printers (Issues #402, #393)
- Added new media sizes defined by IANA (Issues #501)
- Added quirk for GoDEX label printers (Issue #440)
- Fixed `--enable-libtool-unsupported` (Issue #394)
- Fixed configuration on RISC-V machines (Issue #404)
- Fixed the `device_uri` invalid pointer for driverless printers with `.local`
  hostname (Issue #419)
- Fixed an OpenSSL crash bug (Issue #409)
- Fixed a potential SNMP OID value overflow issue (Issue #431)
- Fixed an OpenSSL certificate loading issue (Issue #465)
- Fixed Brazilian Portuguese translations (Issue #288)
- Fixed `cupsd` default keychain location when building with OpenSSL
  (Issue #529)
- Fixed default color settings for CMYK printers as well (Issue #500)
- Fixed duplicate PPD2IPP media-type names (Issue #688)
- Fixed InputSlot heuristic for photo sizes smaller than 5x7" if there is no
  media-source in the request (Issue #569)
- Fixed invalid memory access during generating IPP Everywhere queue
  (Issue #466)
- Fixed lprm if no destination is provided (Issue #457)
- Fixed memory leaks in `create_local_bg_thread()` (Issue #466)
- Fixed media size tolerance in `ippeveprinter` (Issue #487)
- Fixed passing command name without path into `ippeveprinter` (Issue #629)
- Fixed saving strings file path in `printers.conf` (Issue #710)
- Fixed TLS certificate generation bugs (Issue #652)
- `ippDeleteValues` would not delete the last value (Issue #556)
- Ignore some of IPP defaults if the application sends its PPD alternative
  (Issue #484)
- Make `Letter` the default size in `ippevepcl` (Issue #543)
- Now accessing Admin page in Web UI requires authentication (Issue #518)
- Now look for default printer on network if needed (Issue #452)
- Now we poll `media-col-database` separately if we fail at first (Issue #599)
- Now report fax attributes and values as needed (Issue #459)
- Now localize HTTP responses using the Content-Language value (Issue #426)
- Raised file size limit for importing PPD via Web UI (Issue #433)
- Raised maximum listen backlog size to INT MAX (Issue #626)
- Update print-color-mode if the printer is modified via ColorModel PPD option
  (Issue #451)
- Use localhost when printing via printer application (Issue #353)
- Write defaults into /etc/cups/lpoptions if we're root (Issue #456)


Changes in CUPS v2.4.2 (2022-05-26)
-----------------------------------

- Fixed certificate strings comparison for Local authorization (CVE-2022-26691)
- The `cupsFileOpen` function no longer opens files for append in read-write
  mode (Issue #291)
- The cupsd daemon removed processing temporary queue (Issue #364)
- Fixed delay in IPP backend if GNUTLS is used and endpoint doesn't confirm
  closing the connection (Issue #365)
- Fixed conditional jump based on uninitialized value in cups/ppd.c (Issue #329)
- Fixed CSS related issues in CUPS Web UI (Issue #344)
- Fixed copyright in CUPS Web UI trailer template (Issue #346)
- mDNS hostname in device uri is not resolved when installing a permanent
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


Changes in CUPS v2.4.1 (2022-01-27)
-----------------------------------

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


Changes in CUPS v2.4.0 (2021-11-29)
-----------------------------------

- Added configure option --with-idle-exit-timeout (Issue #294)
- Added --with-systemd-timeoutstartsec configure option (Issue #298)
- DigestOptions now are applied for MD5 Digest authentication defined
  by RFC 2069 as well (Issue #287)
- Fixed compilation on Solaris (Issue #293)
- Fixed and improved German translations (Issue #296, Issue #297)


Changes in CUPS v2.4rc1 (2021-11-12)
------------------------------------

- Added warning and debug messages when loading printers
 if the queue is raw or with driver (Issue #286)
- Compilation now uses -fstack-protector-strong if available (Issue #285)


Changes in CUPS v2.4b1 (2021-10-27)
-----------------------------------

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
- Fixed an integer overflow in the PWG media size name formatting code
  (Issue #668)
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
