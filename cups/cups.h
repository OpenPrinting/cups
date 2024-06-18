//
// API definitions for CUPS.
//
// Copyright © 2020-2024 by OpenPrinting.
// Copyright © 2007-2020 by Apple Inc.
// Copyright © 1997-2007 by Easy Software Products.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

#ifndef _CUPS_CUPS_H_
#  define _CUPS_CUPS_H_
#  include <stdbool.h>
#  include <sys/types.h>
#  if defined(_WIN32) && !defined(__CUPS_SSIZE_T_DEFINED)
#    define __CUPS_SSIZE_T_DEFINED
#    include <stddef.h>
// Windows does not support the ssize_t type, so map it to __int64...
typedef __int64 ssize_t;			// @private@
#  endif // _WIN32 && !__CUPS_SSIZE_T_DEFINED
#  include "file.h"
#  include "ipp.h"
#  include "language.h"
#  include "pwg.h"
#  ifdef __cplusplus
extern "C" {
#  endif // __cplusplus


//
// Constants...
//

#  define CUPS_VERSION			2.0500
#  define CUPS_VERSION_MAJOR		2
#  define CUPS_VERSION_MINOR		5
#  define CUPS_VERSION_PATCH		0

#  define CUPS_BC_FD			3
					// Back-channel file descriptor for select/poll
#  define CUPS_DATE_ANY			(time_t)-1
#  define CUPS_EXCLUDE_NONE		(const char *)0
#  define CUPS_FORMAT_AUTO		"application/octet-stream"
#  define CUPS_FORMAT_COMMAND		"application/vnd.cups-command"
#  define CUPS_FORMAT_JPEG		"image/jpeg"
#  define CUPS_FORMAT_PDF		"application/pdf"
#  define CUPS_FORMAT_POSTSCRIPT	"application/postscript"
#  define CUPS_FORMAT_RAW		"application/vnd.cups-raw"
#  define CUPS_FORMAT_TEXT		"text/plain"
#  define CUPS_HTTP_DEFAULT		(http_t *)0
#  define CUPS_INCLUDE_ALL		(const char *)0
#  define CUPS_JOBID_ALL		-1
#  define CUPS_JOBID_CURRENT		0
#  define CUPS_LENGTH_VARIABLE		(ssize_t)0
#  define CUPS_TIMEOUT_DEFAULT		0

// Options and values
#  define CUPS_COPIES			"copies"
#  define CUPS_COPIES_SUPPORTED		"copies-supported"

#  define CUPS_FINISHINGS		"finishings"
#  define CUPS_FINISHINGS_SUPPORTED	"finishings-supported"

#  define CUPS_FINISHINGS_BIND		"7"
#  define CUPS_FINISHINGS_COVER		"6"
#  define CUPS_FINISHINGS_FOLD		"10"
#  define CUPS_FINISHINGS_NONE		"3"
#  define CUPS_FINISHINGS_PUNCH		"5"
#  define CUPS_FINISHINGS_STAPLE	"4"
#  define CUPS_FINISHINGS_TRIM		"11"

#  define CUPS_MEDIA			"media"
#  define CUPS_MEDIA_READY		"media-ready"
#  define CUPS_MEDIA_SUPPORTED		"media-supported"

#  define CUPS_MEDIA_3X5		"na_index-3x5_3x5in"
#  define CUPS_MEDIA_4X6		"na_index-4x6_4x6in"
#  define CUPS_MEDIA_5X7		"na_5x7_5x7in"
#  define CUPS_MEDIA_8X10		"na_govt-letter_8x10in"
#  define CUPS_MEDIA_A3			"iso_a3_297x420mm"
#  define CUPS_MEDIA_A4			"iso_a4_210x297mm"
#  define CUPS_MEDIA_A5			"iso_a5_148x210mm"
#  define CUPS_MEDIA_A6			"iso_a6_105x148mm"
#  define CUPS_MEDIA_ENV10		"na_number-10_4.125x9.5in"
#  define CUPS_MEDIA_ENVDL		"iso_dl_110x220mm"
#  define CUPS_MEDIA_LEGAL		"na_legal_8.5x14in"
#  define CUPS_MEDIA_LETTER		"na_letter_8.5x11in"
#  define CUPS_MEDIA_PHOTO_L		"oe_photo-l_3.5x5in"
#  define CUPS_MEDIA_SUPERBA3		"na_super-b_13x19in"
#  define CUPS_MEDIA_TABLOID		"na_ledger_11x17in"

#  define CUPS_MEDIA_SOURCE		"media-source"
#  define CUPS_MEDIA_SOURCE_SUPPORTED	"media-source-supported"

#  define CUPS_MEDIA_SOURCE_AUTO	"auto"
#  define CUPS_MEDIA_SOURCE_MANUAL	"manual"

#  define CUPS_MEDIA_TYPE		"media-type"
#  define CUPS_MEDIA_TYPE_SUPPORTED	"media-type-supported"

#  define CUPS_MEDIA_TYPE_AUTO		"auto"
#  define CUPS_MEDIA_TYPE_ENVELOPE	"envelope"
#  define CUPS_MEDIA_TYPE_LABELS	"labels"
#  define CUPS_MEDIA_TYPE_LETTERHEAD	"stationery-letterhead"
#  define CUPS_MEDIA_TYPE_PHOTO		"photographic"
#  define CUPS_MEDIA_TYPE_PHOTO_GLOSSY	"photographic-glossy"
#  define CUPS_MEDIA_TYPE_PHOTO_MATTE	"photographic-matte"
#  define CUPS_MEDIA_TYPE_PLAIN		"stationery"
#  define CUPS_MEDIA_TYPE_TRANSPARENCY	"transparency"

#  define CUPS_NUMBER_UP		"number-up"
#  define CUPS_NUMBER_UP_SUPPORTED	"number-up-supported"

#  define CUPS_ORIENTATION		"orientation-requested"
#  define CUPS_ORIENTATION_SUPPORTED	"orientation-requested-supported"

#  define CUPS_ORIENTATION_PORTRAIT	"3"
#  define CUPS_ORIENTATION_LANDSCAPE	"4"

#  define CUPS_PRINT_COLOR_MODE		"print-color-mode"
#  define CUPS_PRINT_COLOR_MODE_SUPPORTED "print-color-mode-supported"

#  define CUPS_PRINT_COLOR_MODE_AUTO	"auto"
#  define CUPS_PRINT_COLOR_MODE_BI_LEVEL "bi-level"
#  define CUPS_PRINT_COLOR_MODE_COLOR	"color"
#  define CUPS_PRINT_COLOR_MODE_MONOCHROME "monochrome"

#  define CUPS_PRINT_QUALITY		"print-quality"
#  define CUPS_PRINT_QUALITY_SUPPORTED	"print-quality-supported"

#  define CUPS_PRINT_QUALITY_DRAFT	"3"
#  define CUPS_PRINT_QUALITY_NORMAL	"4"
#  define CUPS_PRINT_QUALITY_HIGH	"5"

#  define CUPS_SIDES			"sides"
#  define CUPS_SIDES_SUPPORTED		"sides-supported"

#  define CUPS_SIDES_ONE_SIDED		"one-sided"
#  define CUPS_SIDES_TWO_SIDED_PORTRAIT	"two-sided-long-edge"
#  define CUPS_SIDES_TWO_SIDED_LANDSCAPE "two-sided-short-edge"


//
// Types and structures...
//

enum cups_credpurpose_e			// X.509 credential purposes
{
  CUPS_CREDPURPOSE_SERVER_AUTH = 0x01,		// serverAuth
  CUPS_CREDPURPOSE_CLIENT_AUTH = 0x02,		// clientAuth
  CUPS_CREDPURPOSE_CODE_SIGNING = 0x04,		// codeSigning
  CUPS_CREDPURPOSE_EMAIL_PROTECTION = 0x08,	// emailProtection
  CUPS_CREDPURPOSE_TIME_STAMPING = 0x10,	// timeStamping
  CUPS_CREDPURPOSE_OCSP_SIGNING = 0x20,		// OCSPSigning
  CUPS_CREDPURPOSE_ALL = 0x3f			// All purposes
};
typedef unsigned cups_credpurpose_t;	// Combined X.509 credential purposes for @link cupsCreateCredentials@ and @link cupsCreateCredentialsRequest@

typedef enum cups_credtype_e		// X.509 credential types for @link cupsCreateCredentials@ and @link cupsCreateCredentialsRequest@
{
  CUPS_CREDTYPE_DEFAULT,			// Default type
  CUPS_CREDTYPE_RSA_2048_SHA256,		// RSA with 2048-bit keys and SHA-256 hash
  CUPS_CREDTYPE_RSA_3072_SHA256,		// RSA with 3072-bit keys and SHA-256 hash
  CUPS_CREDTYPE_RSA_4096_SHA256,		// RSA with 4096-bit keys and SHA-256 hash
  CUPS_CREDTYPE_ECDSA_P256_SHA256,		// ECDSA using the P-256 curve with SHA-256 hash
  CUPS_CREDTYPE_ECDSA_P384_SHA256,		// ECDSA using the P-384 curve with SHA-256 hash
  CUPS_CREDTYPE_ECDSA_P521_SHA256		// ECDSA using the P-521 curve with SHA-256 hash
} cups_credtype_t;

enum cups_credusage_e			// X.509 keyUsage flags
{
  CUPS_CREDUSAGE_DIGITAL_SIGNATURE = 0x001,	// digitalSignature
  CUPS_CREDUSAGE_NON_REPUDIATION = 0x002,	// nonRepudiation/contentCommitment
  CUPS_CREDUSAGE_KEY_ENCIPHERMENT = 0x004,	// keyEncipherment
  CUPS_CREDUSAGE_DATA_ENCIPHERMENT = 0x008,	// dataEncipherment
  CUPS_CREDUSAGE_KEY_AGREEMENT = 0x010,		// keyAgreement
  CUPS_CREDUSAGE_KEY_CERT_SIGN = 0x020,		// keyCertSign
  CUPS_CREDUSAGE_CRL_SIGN = 0x040,		// cRLSign
  CUPS_CREDUSAGE_ENCIPHER_ONLY = 0x080,		// encipherOnly
  CUPS_CREDUSAGE_DECIPHER_ONLY = 0x100,		// decipherOnly
  CUPS_CREDUSAGE_DEFAULT_CA = 0x061,		// Defaults for CA certs
  CUPS_CREDUSAGE_DEFAULT_TLS = 0x005,		// Defaults for TLS certs
  CUPS_CREDUSAGE_ALL = 0x1ff			// All keyUsage flags
};
typedef unsigned cups_credusage_t;	// Combined X.509 keyUsage flags for @link cupsCreateCredentials@ and @link cupsCreateCredentialsRequest@

enum cups_dest_flags_e			// Flags for @link cupsConnectDest@ and @link cupsEnumDests@
{
  CUPS_DEST_FLAGS_NONE = 0x00,			// No flags are set
  CUPS_DEST_FLAGS_UNCONNECTED = 0x01,		// There is no connection
  CUPS_DEST_FLAGS_MORE = 0x02,			// There are more destinations
  CUPS_DEST_FLAGS_REMOVED = 0x04,		// The destination has gone away
  CUPS_DEST_FLAGS_ERROR = 0x08,			// An error occurred
  CUPS_DEST_FLAGS_RESOLVING = 0x10,		// The destination address is being resolved
  CUPS_DEST_FLAGS_CONNECTING = 0x20,		// A connection is being established
  CUPS_DEST_FLAGS_CANCELED = 0x40,		// Operation was canceled
  CUPS_DEST_FLAGS_DEVICE = 0x80			// For @link cupsConnectDest@: Connect to device
};
typedef unsigned cups_dest_flags_t;	// Combined flags for @link cupsConnectDest@ and @link cupsEnumDests@

enum cups_media_flags_e			// Flags for @link cupsGetDestMediaByName@ and @link cupsGetDestMediaBySize@
{
  CUPS_MEDIA_FLAGS_DEFAULT = 0x00,		// Find the closest size supported by the printer
  CUPS_MEDIA_FLAGS_BORDERLESS = 0x01,		// Find a borderless size
  CUPS_MEDIA_FLAGS_DUPLEX = 0x02,		// Find a size compatible with 2-sided printing
  CUPS_MEDIA_FLAGS_EXACT = 0x04,		// Find an exact match for the size
  CUPS_MEDIA_FLAGS_READY = 0x08			// If the printer supports media sensing, find the size amongst the "ready" media.
};
typedef unsigned cups_media_flags_t;	// Combined flags for @link cupsGetDestMediaByName@ and @link cupsGetDestMediaBySize@

enum cups_ptype_e			// Printer type/capability flags
{
  CUPS_PTYPE_LOCAL = 0x0000,			// Local printer or class
  CUPS_PTYPE_CLASS = 0x0001,			// Printer class
  CUPS_PTYPE_REMOTE = 0x0002,			// Remote printer or class
  CUPS_PTYPE_BW = 0x0004,			// Can do B&W printing
  CUPS_PTYPE_COLOR = 0x0008,			// Can do color printing
  CUPS_PTYPE_DUPLEX = 0x0010,			// Can do two-sided printing
  CUPS_PTYPE_STAPLE = 0x0020,			// Can staple output
  CUPS_PTYPE_COPIES = 0x0040,			// Can do copies in hardware
  CUPS_PTYPE_COLLATE = 0x0080,			// Can quickly collate copies
  CUPS_PTYPE_PUNCH = 0x0100,			// Can punch output
  CUPS_PTYPE_COVER = 0x0200,			// Can cover output
  CUPS_PTYPE_BIND = 0x0400,			// Can bind output
  CUPS_PTYPE_SORT = 0x0800,			// Can sort output
  CUPS_PTYPE_SMALL = 0x1000,			// Can print on Letter/Legal/A4-size media
  CUPS_PTYPE_MEDIUM = 0x2000,			// Can print on Tabloid/B/C/A3/A2-size media
  CUPS_PTYPE_LARGE = 0x4000,			// Can print on D/E/A1/A0-size media
  CUPS_PTYPE_VARIABLE = 0x8000,			// Can print on rolls and custom-size media
  CUPS_PTYPE_DEFAULT = 0x20000,			// Default printer on network
  CUPS_PTYPE_FAX = 0x40000,			// Fax queue
  CUPS_PTYPE_REJECTING = 0x80000,		// Printer is rejecting jobs
  CUPS_PTYPE_NOT_SHARED = 0x200000,		// Printer is not shared
  CUPS_PTYPE_AUTHENTICATED = 0x400000,		// Printer requires authentication
  CUPS_PTYPE_COMMANDS = 0x800000,		// Printer supports maintenance commands
  CUPS_PTYPE_DISCOVERED = 0x1000000,		// Printer was discovered
  CUPS_PTYPE_SCANNER = 0x2000000,		// Scanner-only device
  CUPS_PTYPE_MFP = 0x4000000,			// Printer with scanning capabilities
  CUPS_PTYPE_3D = 0x8000000,			// Printer with 3D capabilities @exclude all@ @private@ @deprecated@
  CUPS_PTYPE_FOLD = 0x10000000,			// Can fold output @since CUPS 2.5@
  CUPS_PTYPE_OPTIONS = 0x1006fffc		// ~(CLASS | REMOTE | IMPLICIT | DEFAULT | FAX | REJECTING | DELETE | NOT_SHARED | AUTHENTICATED | COMMANDS | DISCOVERED) @private@
};
#  ifndef _CUPS_NO_DEPRECATED
#    define CUPS_PRINTER_LOCAL		CUPS_PTYPE_LOCAL
#    define CUPS_PRINTER_CLASS		CUPS_PTYPE_CLASS
#    define CUPS_PRINTER_REMOTE		CUPS_PTYPE_REMOTE
#    define CUPS_PRINTER_BW		CUPS_PTYPE_BW
#    define CUPS_PRINTER_COLOR		CUPS_PTYPE_COLOR
#    define CUPS_PRINTER_DUPLEX		CUPS_PTYPE_DUPLEX
#    define CUPS_PRINTER_STAPLE		CUPS_PTYPE_STAPLE
#    define CUPS_PRINTER_COPIES		CUPS_PTYPE_COPIES
#    define CUPS_PRINTER_COLLATE	CUPS_PTYPE_COLLATE
#    define CUPS_PRINTER_PUNCH		CUPS_PTYPE_PUNCH
#    define CUPS_PRINTER_COVER		CUPS_PTYPE_COVER
#    define CUPS_PRINTER_BIND		CUPS_PTYPE_BIND
#    define CUPS_PRINTER_SORT		CUPS_PTYPE_SORT
#    define CUPS_PRINTER_SMALL		CUPS_PTYPE_SMALL
#    define CUPS_PRINTER_MEDIUM		CUPS_PTYPE_MEDIUM
#    define CUPS_PRINTER_LARGE		CUPS_PTYPE_LARGE
#    define CUPS_PRINTER_VARIABLE	CUPS_PTYPE_VARIABLE
#    define CUPS_PRINTER_DEFAULT	CUPS_PTYPE_DEFAULT
#    define CUPS_PRINTER_FAX		CUPS_PTYPE_FAX
#    define CUPS_PRINTER_REJECTING	CUPS_PTYPE_REJECTING
#    define CUPS_PRINTER_NOT		CUPS_PTYPE_NOT
#    define CUPS_PRINTER_AUTHENTICATED	CUPS_PTYPE_AUTHENTICATED
#    define CUPS_PRINTER_COMMANDS	CUPS_PTYPE_COMMANDS
#    define CUPS_PRINTER_DISCOVERED	CUPS_PTYPE_DISCOVERED
#    define CUPS_PRINTER_SCANNER	CUPS_PTYPE_SCANNER
#    define CUPS_PRINTER_MFP		CUPS_PTYPE_MFP
#    define CUPS_PRINTER_3D		CUPS_PTYPE_3D
#    define CUPS_PRINTER_OPTIONS	CUPS_PTYPE_OPTIONS
#  endif // !_CUPS_NO_DEPRECATED
typedef unsigned cups_ptype_t;		// Combined printer type/capability flags

typedef enum cups_whichjobs_e		// Which jobs for @link cupsGetJobs@
{
  CUPS_WHICHJOBS_ALL = -1,		// All jobs
  CUPS_WHICHJOBS_ACTIVE,		// Pending/held/processing jobs
  CUPS_WHICHJOBS_COMPLETED		// Completed/canceled/aborted jobs
} cups_whichjobs_t;

typedef struct cups_option_s		// Printer Options
{
  char		*name;			// Name of option
  char		*value;			// Value of option
} cups_option_t;

typedef struct cups_dest_s		// Destination
{
  char		*name,			// Printer or class name
		*instance;		// Local instance name or `NULL`
  int		is_default;		// Is this printer the default?
  int		num_options;		// Number of options
  cups_option_t	*options;		// Options
} cups_dest_t;

typedef struct _cups_dinfo_s cups_dinfo_t;
					// Destination capability and status information @since CUPS 1.6/macOS 10.8@

typedef struct cups_job_s		// Job information
{
  int		id;			// The job ID
  char		*dest;			// Printer or class name
  char		*title;			// Title/job name
  char		*user;			// User that submitted the job
  char		*format;		// Document format
  ipp_jstate_t	state;			// Job state
  int		size;			// Size in kilobytes
  int		priority;		// Priority (1-100)
  time_t	completed_time;		// Time the job was completed
  time_t	creation_time;		// Time the job was created
  time_t	processing_time;	// Time the job was processed
} cups_job_t;

typedef struct cups_media_s		// Media information @since CUPS 2.5@
{
  char		media[128],		// Media name to use
		color[128],		// Media color (blank for any/auto)
		source[128],		// Media source (blank for any/auto)
		type[128];		// Media type (blank for any/auto)
  int		width,			// Width in hundredths of millimeters
		length,			// Length in hundredths of millimeters
		bottom,			// Bottom margin in hundredths of millimeters
		left,			// Left margin in hundredths of millimeters
		right,			// Right margin in hundredths of millimeters
		top;			// Top margin in hundredths of millimeters
} cups_media_t;

typedef struct cups_size_s		// Media Size @deprecated@
{
  char		media[128];		// Media name to use
  int		width,			// Width in hundredths of millimeters
		length,			// Length in hundredths of millimeters
		bottom,			// Bottom margin in hundredths of millimeters
		left,			// Left margin in hundredths of millimeters
		right,			// Right margin in hundredths of millimeters
		top;			// Top margin in hundredths of millimeters
} cups_size_t;

typedef int (*cups_client_cert_cb_t)(http_t *http, void *tls, cups_array_t *distinguished_names, void *user_data);
					// Client credentials callback @deprecated@

typedef bool (*cups_cert_san_cb_t)(const char *common_name, const char *subject_alt_name, void *user_data);
					// Certificate signing subjectAltName callback

typedef int (*cups_dest_cb_t)(void *user_data, unsigned flags, cups_dest_t *dest);
					// Destination enumeration callback @since CUPS 1.6/macOS 10.8@

#  ifdef __BLOCKS__
typedef int (^cups_dest_block_t)(unsigned flags, cups_dest_t *dest);
			      		// Destination enumeration block @deprecated@ @exclude all@
#  endif /* __BLOCKS__ */

typedef const char *(*cups_oauth_cb_t)(http_t *http, const char *realm, const char *scope, const char *resource, void *user_data);
					// OAuth callback @since CUPS 2.4@

typedef const char *(*cups_password_cb_t)(const char *prompt);
					// Password callback @deprecated@ @exclude all@

typedef const char *(*cups_password_cb2_t)(const char *prompt, http_t *http, const char *method, const char *resource, void *user_data);
					// New password callback @since CUPS 1.4/macOS 10.6@

typedef int (*cups_server_cert_cb_t)(http_t *http, void *tls, cups_array_t *certs, void *user_data);
					// Server credentials callback @deprecated@


//
// Functions...
//

extern int		cupsAddDest(const char *name, const char *instance, int num_dests, cups_dest_t **dests) _CUPS_PUBLIC;
extern int		cupsAddDestMediaOptions(http_t *http, cups_dest_t *dest, cups_dinfo_t *dinfo, unsigned flags, cups_size_t *size, int num_options, cups_option_t **options) _CUPS_DEPRECATED_MSG("Use cupsAddDestMediaOptions2 instead.");
extern int		cupsAddDestMediaOptions2(http_t *http, cups_dest_t *dest, cups_dinfo_t *dinfo, unsigned flags, cups_media_t *media, int num_options, cups_option_t **options) _CUPS_PUBLIC;
extern int		cupsAddIntegerOption(const char *name, int value, int num_options, cups_option_t **options) _CUPS_PUBLIC;
extern int		cupsAddOption(const char *name, const char *value, int num_options, cups_option_t **options) _CUPS_PUBLIC;
extern bool		cupsAreCredentialsValidForName(const char *common_name, const char *credentials);

extern ipp_status_t	cupsCancelDestJob(http_t *http, cups_dest_t *dest, int job_id) _CUPS_PUBLIC;
extern int		cupsCancelJob(const char *name, int job_id) _CUPS_PUBLIC;
extern ipp_status_t	cupsCancelJob2(http_t *http, const char *name, int job_id, int purge) _CUPS_PUBLIC;
extern int		cupsCheckDestSupported(http_t *http, cups_dest_t *dest, cups_dinfo_t *info, const char *option, const char *value) _CUPS_PUBLIC;
extern ipp_status_t	cupsCloseDestJob(http_t *http, cups_dest_t *dest, cups_dinfo_t *info, int job_id) _CUPS_PUBLIC;
extern size_t		cupsConcatString(char *dst, const char *src, size_t dstsize) _CUPS_PUBLIC;
extern http_t		*cupsConnectDest(cups_dest_t *dest, unsigned flags, int msec, int *cancel, char *resource, size_t resourcesize, cups_dest_cb_t cb, void *user_data) _CUPS_PUBLIC;
#  ifdef __BLOCKS__
extern http_t		*cupsConnectDestBlock(cups_dest_t *dest, unsigned flags, int msec, int *cancel, char *resource, size_t resourcesize, cups_dest_block_t block) _CUPS_DEPRECATED;
#  endif // __BLOCKS__
extern char		*cupsCopyCredentials(const char *path, const char *common_name) _CUPS_PUBLIC;
extern char		*cupsCopyCredentialsKey(const char *path, const char *common_name) _CUPS_PUBLIC;
extern char		*cupsCopyCredentialsRequest(const char *path, const char *common_name) _CUPS_PUBLIC;
extern int		cupsCopyDest(cups_dest_t *dest, int num_dests, cups_dest_t **dests) _CUPS_PUBLIC;
extern int		cupsCopyDestConflicts(http_t *http, cups_dest_t *dest, cups_dinfo_t *info, int num_options, cups_option_t *options, const char *new_option, const char *new_value, int *num_conflicts, cups_option_t **conflicts, int *num_resolved, cups_option_t **resolved) _CUPS_PUBLIC;
extern cups_dinfo_t	*cupsCopyDestInfo(http_t *http, cups_dest_t *dest) _CUPS_PUBLIC;
extern cups_dinfo_t	*cupsCopyDestInfo2(http_t *http, cups_dest_t *dest, cups_dest_flags_t dflags) _CUPS_PUBLIC;
extern size_t		cupsCopyString(char *dst, const char *src, size_t dstsize) _CUPS_PUBLIC;
extern bool		cupsCreateCredentials(const char *path, bool ca_cert, cups_credpurpose_t purpose, cups_credtype_t type, cups_credusage_t usage, const char *organization, const char *org_unit, const char *locality, const char *state_province, const char *country, const char *common_name, const char *email, size_t num_alt_names, const char * const *alt_names, const char *root_name, time_t expiration_date) _CUPS_PUBLIC;
extern bool		cupsCreateCredentialsRequest(const char *path, cups_credpurpose_t purpose, cups_credtype_t type, cups_credusage_t usage, const char *organization, const char *org_unit, const char *locality, const char *state_province, const char *country, const char *common_name, const char *email, size_t num_alt_names, const char * const *alt_names) _CUPS_PUBLIC;
extern ipp_status_t	cupsCreateDestJob(http_t *http, cups_dest_t *dest, cups_dinfo_t *info, int *job_id, const char *title, int num_options, cups_option_t *options) _CUPS_PUBLIC;
extern int		cupsCreateJob(http_t *http, const char *name, const char *title, int num_options, cups_option_t *options) _CUPS_PUBLIC;
extern int		cupsCreateTempFd(const char *prefix, const char *suffix, char *filename, size_t len) _CUPS_PUBLIC;
extern cups_file_t	*cupsCreateTempFile(const char *prefix, const char *suffix, char *filename, size_t len) _CUPS_PUBLIC;

extern int		cupsDoAuthentication(http_t *http, const char *method, const char *resource) _CUPS_PUBLIC;
extern ipp_t		*cupsDoFileRequest(http_t *http, ipp_t *request, const char *resource, const char *filename) _CUPS_PUBLIC;
extern ipp_t		*cupsDoIORequest(http_t *http, ipp_t *request, const char *resource, int infile, int outfile) _CUPS_PUBLIC;
extern ipp_t		*cupsDoRequest(http_t *http, ipp_t *request, const char *resource) _CUPS_PUBLIC;

extern ipp_attribute_t	*cupsEncodeOption(ipp_t *ipp, ipp_tag_t group_tag, const char *name, const char *value) _CUPS_PUBLIC;
extern void		cupsEncodeOptions(ipp_t *ipp, int num_options, cups_option_t *options) _CUPS_DEPRECATED_MSG("Use cupsEncodeOptions2 instead.");
extern void		cupsEncodeOptions2(ipp_t *ipp, int num_options, cups_option_t *options, ipp_tag_t group_tag) _CUPS_PUBLIC;
extern http_encryption_t cupsEncryption(void) _CUPS_DEPRECATED_MSG("Use cupsGetEncryption instead.");
extern int		cupsEnumDests(cups_dest_flags_t flags, int msec, int *cancel, cups_ptype_t type, cups_ptype_t mask, cups_dest_cb_t cb, void *user_data) _CUPS_PUBLIC;
#  ifdef __BLOCKS__
extern int		cupsEnumDestsBlock(unsigned flags, int msec, int *cancel, cups_ptype_t type, cups_ptype_t mask, cups_dest_block_t block) _CUPS_DEPRECATED;
#  endif // __BLOCKS__

extern ipp_attribute_t	*cupsFindDestDefault(http_t *http, cups_dest_t *dest, cups_dinfo_t *dinfo, const char *option) _CUPS_PUBLIC;
extern ipp_attribute_t	*cupsFindDestReady(http_t *http, cups_dest_t *dest, cups_dinfo_t *dinfo, const char *option) _CUPS_PUBLIC;
extern ipp_attribute_t	*cupsFindDestSupported(http_t *http, cups_dest_t *dest, cups_dinfo_t *dinfo, const char *option) _CUPS_PUBLIC;
extern ipp_status_t	cupsFinishDestDocument(http_t *http, cups_dest_t *dest, cups_dinfo_t *info) _CUPS_PUBLIC;
extern ipp_status_t	cupsFinishDocument(http_t *http, const char *name) _CUPS_PUBLIC;
extern ssize_t		cupsFormatString(char *buffer, size_t bufsize, const char *format, ...) _CUPS_FORMAT(3,4) _CUPS_PUBLIC;
extern ssize_t		cupsFormatStringv(char *buffer, size_t bufsize, const char *format, va_list ap) _CUPS_PUBLIC;
extern void		cupsFreeDestInfo(cups_dinfo_t *dinfo) _CUPS_PUBLIC;
extern void		cupsFreeDests(int num_dests, cups_dest_t *dests) _CUPS_PUBLIC;
extern void		cupsFreeJobs(int num_jobs, cups_job_t *jobs) _CUPS_PUBLIC;
extern void		cupsFreeOptions(int num_options, cups_option_t *options) _CUPS_PUBLIC;

extern int		cupsGetClasses(char ***classes) _CUPS_DEPRECATED_MSG("Use cupsEnumDests instead.");
extern time_t		cupsGetCredentialsExpiration(const char *credentials) _CUPS_PUBLIC;
extern char		*cupsGetCredentialsInfo(const char *credentials, char *buffer, size_t bufsize) _CUPS_PUBLIC;
extern http_trust_t	cupsGetCredentialsTrust(const char *path, const char *common_name, const char *credentials, bool require_ca) _CUPS_PUBLIC;
extern const char	*cupsGetDefault(void) _CUPS_PUBLIC;
extern const char	*cupsGetDefault2(http_t *http) _CUPS_PUBLIC;
extern cups_dest_t	*cupsGetDest(const char *name, const char *instance, int num_dests, cups_dest_t *dests) _CUPS_PUBLIC;
extern int		cupsGetDestMediaByIndex(http_t *http, cups_dest_t *dest, cups_dinfo_t *dinfo, int n, unsigned flags, cups_size_t *size) _CUPS_DEPRECATED_MSG("Use cupsGetDestMediaByIndex2 instead.");
extern bool		cupsGetDestMediaByIndex2(http_t *http, cups_dest_t *dest, cups_dinfo_t *dinfo, size_t n, unsigned flags, cups_media_t *media) _CUPS_PUBLIC;
extern int		cupsGetDestMediaByName(http_t *http, cups_dest_t *dest, cups_dinfo_t *dinfo, const char *name, unsigned flags, cups_size_t *size) _CUPS_DEPRECATED_MSG("Use cupsGetDestMediaByName2 instead.");
extern bool		cupsGetDestMediaByName2(http_t *http, cups_dest_t *dest, cups_dinfo_t *dinfo, const char *name, unsigned flags, cups_media_t *media) _CUPS_PUBLIC;
extern int		cupsGetDestMediaBySize(http_t *http, cups_dest_t *dest, cups_dinfo_t *dinfo, int width, int length, unsigned flags, cups_size_t *size) _CUPS_DEPRECATED_MSG("Use cupsGetDestMediaBySize2 instead.");
extern bool		cupsGetDestMediaBySize2(http_t *http, cups_dest_t *dest, cups_dinfo_t *dinfo, int width, int length, unsigned flags, cups_media_t *media) _CUPS_PUBLIC;
extern int		cupsGetDestMediaCount(http_t *http, cups_dest_t *dest, cups_dinfo_t *dinfo, unsigned flags) _CUPS_PUBLIC;
extern int		cupsGetDestMediaDefault(http_t *http, cups_dest_t *dest, cups_dinfo_t *dinfo, unsigned flags, cups_size_t *size) _CUPS_DEPRECATED_MSG("Use cupsGetDestMediaDefault2 instead.");
extern bool		cupsGetDestMediaDefault2(http_t *http, cups_dest_t *dest, cups_dinfo_t *dinfo, unsigned flags, cups_media_t *media) _CUPS_PUBLIC;
extern cups_dest_t	*cupsGetDestWithURI(const char *name, const char *uri) _CUPS_PUBLIC;
extern int		cupsGetDests(cups_dest_t **dests) _CUPS_PUBLIC;
extern int		cupsGetDests2(http_t *http, cups_dest_t **dests) _CUPS_PUBLIC;
extern http_encryption_t cupsGetEncryption(void) _CUPS_PUBLIC;
extern ipp_status_t	cupsGetError(void) _CUPS_PUBLIC;
extern const char	*cupsGetErrorString(void) _CUPS_PUBLIC;
extern http_status_t	cupsGetFile(http_t *http, const char *resource, const char *filename) _CUPS_PUBLIC;
extern http_status_t	cupsGetFd(http_t *http, const char *resource, int fd) _CUPS_PUBLIC;
extern int		cupsGetIntegerOption(const char *name, int num_options, cups_option_t *options) _CUPS_PUBLIC;
extern int		cupsGetJobs(cups_job_t **jobs, const char *name, int myjobs, int whichjobs) _CUPS_PUBLIC;
extern int		cupsGetJobs2(http_t *http, cups_job_t **jobs, const char *name, int myjobs, int whichjobs) _CUPS_PUBLIC;
extern cups_dest_t	*cupsGetNamedDest(http_t *http, const char *name, const char *instance) _CUPS_PUBLIC;
extern const char	*cupsGetOption(const char *name, int num_options, cups_option_t *options) _CUPS_PUBLIC;
extern const char	*cupsGetPassword(const char *prompt) _CUPS_DEPRECATED_MSG("Use cupsGetPassword instead.");
extern const char	*cupsGetPassword2(const char *prompt, http_t *http, const char *method, const char *resource) _CUPS_PUBLIC;
extern int		cupsGetPrinters(char ***printers) _CUPS_DEPRECATED_MSG("Use cupsEnumDests instead.");
extern unsigned		cupsGetRand(void) _CUPS_PUBLIC;
extern ipp_t		*cupsGetResponse(http_t *http, const char *resource) _CUPS_PUBLIC;
extern const char	*cupsGetServer(void) _CUPS_PUBLIC;
extern const char	*cupsGetUser(void) _CUPS_PUBLIC;
extern const char	*cupsGetUserAgent(void) _CUPS_PUBLIC;

extern ssize_t		cupsHashData(const char *algorithm, const void *data, size_t datalen, unsigned char *hash, size_t hashsize) _CUPS_PUBLIC;
extern const char	*cupsHashString(const unsigned char *hash, size_t hashsize, char *buffer, size_t bufsize) _CUPS_PUBLIC;
extern ssize_t		cupsHMACData(const char *algorithm, const unsigned char *key, size_t keylen, const void *data, size_t datalen, unsigned char *hash, size_t hashsize) _CUPS_PUBLIC;

extern ipp_status_t	cupsLastError(void) _CUPS_DEPRECATED_MSG("Use cupsGetError instead.");
extern const char	*cupsLastErrorString(void) _CUPS_DEPRECATED_MSG("Use cupsGetErrorString instead.");

extern const char	*cupsLocalizeDestMedia(http_t *http, cups_dest_t *dest, cups_dinfo_t *info, unsigned flags, cups_size_t *size) _CUPS_DEPRECATED_MSG("Use cupsLocalizeDestMedia2 instead.");
extern const char	*cupsLocalizeDestMedia2(http_t *http, cups_dest_t *dest, cups_dinfo_t *info, unsigned flags, cups_media_t *media) _CUPS_PUBLIC;
extern const char	*cupsLocalizeDestOption(http_t *http, cups_dest_t *dest, cups_dinfo_t *info, const char *option) _CUPS_PUBLIC;
extern const char	*cupsLocalizeDestValue(http_t *http, cups_dest_t *dest, cups_dinfo_t *info, const char *option, const char *value) _CUPS_PUBLIC;
extern char		*cupsLocalizeNotifySubject(cups_lang_t *lang, ipp_t *event) _CUPS_PUBLIC;
extern char		*cupsLocalizeNotifyText(cups_lang_t *lang, ipp_t *event) _CUPS_PUBLIC;

extern int		cupsMakeServerCredentials(const char *path, const char *common_name, int num_alt_names, const char **alt_names, time_t expiration_date) _CUPS_DEPRECATED_MSG("Use cupsCreateCredentials instead.");

extern char		*cupsNotifySubject(cups_lang_t *lang, ipp_t *event) _CUPS_DEPRECATED_MSG("Use cupsLocalizeNotifySubject instead.");
extern char		*cupsNotifyText(cups_lang_t *lang, ipp_t *event) _CUPS_DEPRECATED_MSG("Use cupsLocalizeNotifyText instead.");

extern int		cupsParseOptions(const char *arg, int num_options, cups_option_t **options) _CUPS_PUBLIC;
extern int		cupsPrintFile(const char *name, const char *filename, const char *title, int num_options, cups_option_t *options) _CUPS_PUBLIC;
extern int		cupsPrintFile2(http_t *http, const char *name, const char *filename, const char *title, int num_options, cups_option_t *options) _CUPS_PUBLIC;
extern int		cupsPrintFiles(const char *name, int num_files, const char **files, const char *title, int num_options, cups_option_t *options) _CUPS_PUBLIC;
extern int		cupsPrintFiles2(http_t *http, const char *name, int num_files, const char **files, const char *title, int num_options, cups_option_t *options) _CUPS_PUBLIC;
extern http_status_t	cupsPutFd(http_t *http, const char *resource, int fd) _CUPS_PUBLIC;
extern http_status_t	cupsPutFile(http_t *http, const char *resource, const char *filename) _CUPS_PUBLIC;

extern ssize_t		cupsReadResponseData(http_t *http, char *buffer, size_t length) _CUPS_PUBLIC;
extern int		cupsRemoveDest(const char *name, const char *instance, int num_dests, cups_dest_t **dests) _CUPS_PUBLIC;
extern int		cupsRemoveOption(const char *name, int num_options, cups_option_t **options) _CUPS_PUBLIC;

extern bool		cupsSaveCredentials(const char *path, const char *common_name, const char *credentials, const char *key) _CUPS_PUBLIC;
extern http_status_t	cupsSendRequest(http_t *http, ipp_t *request, const char *resource, size_t length) _CUPS_PUBLIC;
extern const char	*cupsServer(void) _CUPS_DEPRECATED_MSG("Use cupsGetServer instead.");
extern void		cupsSetClientCertCB(cups_client_cert_cb_t cb, void *user_data) _CUPS_DEPRECATED;
extern bool		cupsSetClientCredentials(const char *credentials, const char *key) _CUPS_PUBLIC;
extern int		cupsSetCredentials(cups_array_t *certs) _CUPS_DEPRECATED;
extern void		cupsSetDefaultDest(const char *name, const char *instance, int num_dests, cups_dest_t *dests) _CUPS_PUBLIC;
extern void		cupsSetDests(int num_dests, cups_dest_t *dests) _CUPS_PUBLIC;
extern int		cupsSetDests2(http_t *http, int num_dests, cups_dest_t *dests) _CUPS_PUBLIC;
extern void		cupsSetEncryption(http_encryption_t e) _CUPS_PUBLIC;
extern void		cupsSetOAuthCB(cups_oauth_cb_t cb, void *data) _CUPS_PUBLIC;
extern void		cupsSetPasswordCB(cups_password_cb_t cb) _CUPS_DEPRECATED_MSG("Use cupsSetPasswordCB2 instead.");
extern void		cupsSetPasswordCB2(cups_password_cb2_t cb, void *user_data) _CUPS_PUBLIC;
extern void		cupsSetServer(const char *server) _CUPS_PUBLIC;
extern void		cupsSetServerCertCB(cups_server_cert_cb_t cb, void *user_data) _CUPS_DEPRECATED;
extern int		cupsSetServerCredentials(const char *path, const char *common_name, int auto_create) _CUPS_PUBLIC;
extern void		cupsSetUser(const char *user) _CUPS_PUBLIC;
extern void		cupsSetUserAgent(const char *user_agent) _CUPS_PUBLIC;
extern bool		cupsSignCredentialsRequest(const char *path, const char *common_name, const char *request, const char *root_name, cups_credpurpose_t allowed_purpose, cups_credusage_t allowed_usage, cups_cert_san_cb_t cb, void *cb_data, time_t expiration_date) _CUPS_PUBLIC;
extern http_status_t	cupsStartDestDocument(http_t *http, cups_dest_t *dest, cups_dinfo_t *info, int job_id, const char *docname, const char *format, int num_options, cups_option_t *options, int last_document) _CUPS_PUBLIC;
extern http_status_t	cupsStartDocument(http_t *http, const char *name, int job_id, const char *docname, const char *format, int last_document) _CUPS_PUBLIC;

extern int		cupsTempFd(char *filename, int len) _CUPS_DEPRECATED_MSG("Use cupsCreateTempFd instead.");
extern char		*cupsTempFile(char *filename, int len) _CUPS_DEPRECATED_MSG("Use cupsCreateTempFd or cupsCreateTempFile instead.");
extern cups_file_t	*cupsTempFile2(char *filename, int len) _CUPS_DEPRECATED_MSG("Use cupsCreateTempFile instead.");

extern const char	*cupsUser(void) _CUPS_DEPRECATED_MSG("Use cupsGetUser instead.");
extern const char	*cupsUserAgent(void) _CUPS_DEPRECATED_MSG("Use cupsGetUserAgent instead.");

extern http_status_t	cupsWriteRequestData(http_t *http, const char *buffer, size_t length) _CUPS_PUBLIC;


#  ifdef __cplusplus
}
#  endif // __cplusplus
#endif // !_CUPS_CUPS_H_
