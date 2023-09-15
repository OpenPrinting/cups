CHANGES - OpenPrinting CUPS 2.5b1 - (TBA)
==============================================

Changes in CUPS v2.5b1 (TBA)
----------------------------

- Added `cupsDNSSD` APIs.
- Added `cupsConcatString` and `cupsCopyString` string APIs.
- Added OpenSSL support for `cupsHashData` (Issue #762)
- Updated `cupsArray` APIs.
- Fixed Digest authentication support (Issue #260)
- Fixed delays in lpd backend (Issue #741)
- Fixed extensive looping in scheduler (Issue #604)
- Fixed hanging of `lpstat` on IBM AIX (Issue #773)
- Fixed hanging of `lpstat` on Solaris (Issue #156)
- Fixed segfault in `cupsGetNamedDest()` when trying to get default printer, but
  the default printer is not set (Issue #719)
- Fixed RFC 1179 port reserving behavior in LPD backend (Issue #743)
- Fixed printing multiple files on specific printers (Issue #643)
- Fixed printing to stderr if we can't open cups-files.conf (Issue #777)
- Fixed purging job files via `cancel -x` (Issue #742)
- Fixed use-after-free in `cupsdAcceptClient()` when we log warning during error
  handling (fixes CVE-2023-34241)
- Fixed a bug in the PPD command interpretation code (Issue #768)
