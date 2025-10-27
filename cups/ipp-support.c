//
// Internet Printing Protocol support functions for CUPS.
//
// Copyright © 2020-2025 by OpenPrinting.
// Copyright © 2007-2018 by Apple Inc.
// Copyright © 1997-2007 by Easy Software Products, all rights reserved.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

#include "cups-private.h"


//
// Local globals...
//

static const char * const ipp_states[] =// ipp_state_t strings
{
  "IPP_STATE_ERROR",
  "IPP_STATE_IDLE",
  "IPP_STATE_HEADER",
  "IPP_STATE_ATTRIBUTE",
  "IPP_STATE_DATA"
};
static const char * const ipp_status_oks[] =
{					// "OK" status codes; (name) = abandoned
  "successful-ok",
  "successful-ok-ignored-or-substituted-attributes",
  "successful-ok-conflicting-attributes",
  "successful-ok-ignored-subscriptions",
  "(successful-ok-ignored-notifications)",
  "successful-ok-too-many-events",
  "(successful-ok-but-cancel-subscription)",
  "successful-ok-events-complete"
};
static const char * const ipp_status_400s[] =
{					// Client errors; (name) = abandoned
  "client-error-bad-request",
  "client-error-forbidden",
  "client-error-not-authenticated",
  "client-error-not-authorized",
  "client-error-not-possible",
  "client-error-timeout",
  "client-error-not-found",
  "client-error-gone",
  "client-error-request-entity-too-large",
  "client-error-request-value-too-long",
  "client-error-document-format-not-supported",
  "client-error-attributes-or-values-not-supported",
  "client-error-uri-scheme-not-supported",
  "client-error-charset-not-supported",
  "client-error-conflicting-attributes",
  "client-error-compression-not-supported",
  "client-error-compression-error",
  "client-error-document-format-error",
  "client-error-document-access-error",
  "client-error-attributes-not-settable",
  "client-error-ignored-all-subscriptions",
  "client-error-too-many-subscriptions",
  "(client-error-ignored-all-notifications)",
  "(client-error-client-print-support-file-not-found)",
  "client-error-document-password-error",
  "client-error-document-permission-error",
  "client-error-document-security-error",
  "client-error-document-unprintable-error",
  "client-error-account-info-needed",
  "client-error-account-closed",
  "client-error-account-limit-reached",
  "client-error-account-authorization-failed",
  "client-error-not-fetchable"
};
static const char * const ipp_status_500s[] =
{					// Server errors
  "server-error-internal-error",
  "server-error-operation-not-supported",
  "server-error-service-unavailable",
  "server-error-version-not-supported",
  "server-error-device-error",
  "server-error-temporary-error",
  "server-error-not-accepting-jobs",
  "server-error-busy",
  "server-error-job-canceled",
  "server-error-multiple-document-jobs-not-supported",
  "server-error-printer-is-deactivated",
  "server-error-too-many-jobs",
  "server-error-too-many-documents"
};
static const char * const ipp_status_1000s[] =
{					// CUPS internal errors
  "cups-authentication-canceled",
  "cups-pki-error",
  "cups-upgrade-required",
  "cups-oauth"
};
static const char * const ipp_std_ops[] =
{
  // 0x0000 - 0x000f
  "0x0000",
  "0x0001",
  "Print-Job",				// RFC 8011
  "Print-URI",				// RFC 8011
  "Validate-Job",			// RFC 8011
  "Create-Job",				// RFC 8011
  "Send-Document",			// RFC 8011
  "Send-URI",				// RFC 8011
  "Cancel-Job",				// RFC 8011
  "Get-Job-Attributes",			// RFC 8011
  "Get-Jobs",				// RFC 8011
  "Get-Printer-Attributes",		// RFC 8011
  "Hold-Job",				// RFC 8011
  "Release-Job",			// RFC 8011
  "Restart-Job",			// RFC 8011
  "0x000f",

  // 0x0010 - 0x001f
  "Pause-Printer",			// RFC 8011
  "Resume-Printer",			// RFC 8011
  "Purge-Jobs",				// RFC 8011
  "Set-Printer-Attributes",		// RFC 3380
  "Set-Job-Attributes",			// RFC 3380
  "Get-Printer-Supported-Values",	// RFC 3380
  "Create-Printer-Subscriptions",	// RFC 3995
  "Create-Job-Subscriptions",		// RFC 3995
  "Get-Subscription-Attributes",	// RFC 3995
  "Get-Subscriptions",			// RFC 3995
  "Renew-Subscription",			// RFC 3995
  "Cancel-Subscription",		// RFC 3995
  "Get-Notifications",			// RFC 3996
  "(Send-Notifications)",
  "Get-Resource-Attributes",		// IPP System
  "(Get-Resource-Data)",

  // 0x0020 - 0x002f
  "Get-Resources",			// IPP System
  "(Get-Printer-Support-Files)",
  "Enable-Printer",			// RFC 3998
  "Disable-Printer",			// RFC 3998
  "Pause-Printer-After-Current-Job",	// RFC 3998
  "Hold-New-Jobs",			// RFC 3998
  "Release-Held-New-Jobs",		// RFC 3998
  "Deactivate-Printer",			// RFC 3998
  "Activate-Printer",			// RFC 3998
  "Restart-Printer",			// RFC 3998
  "Shutdown-Printer",			// RFC 3998
  "Startup-Printer",			// RFC 3998
  "Reprocess-Job",			// RFC 3998
  "Cancel-Current-Job",			// RFC 3998
  "Suspend-Current-Job",		// RFC 3998
  "Resume-Job",				// RFC 3998

  // 0x0030 - 0x003f
  "Promote-Job",			// RFC 3998
  "Schedule-Job-After",			// RFC 3998
  "0x0032",
  "Cancel-Document",			// IPP DocObject
  "Get-Document-Attributes",		// IPP DocObject
  "Get-Documents",			// IPP DocObject
  "Delete-Document",			// IPP DocObject
  "Set-Document-Attributes",		// IPP DocObject
  "Cancel-Jobs",			// IPP JobExt
  "Cancel-My-Jobs",			// IPP JobExt
  "Resubmit-Job",			// IPP JobExt
  "Close-Job",				// IPP JobExt
  "Identify-Printer",			// IPP NODRIVER
  "Validate-Document",			// IPP NODRIVER
  "Add-Document-Images",		// IPP Scan
  "Acknowledge-Document",		// IPP INFRA

  // 0x0040 - 0x004f
  "Acknowledge-Identify-Printer",	// IPP INFRA
  "Acknowledge-Job",			// IPP INFRA
  "Fetch-Document",			// IPP INFRA
  "Fetch-Job",				// IPP INFRA
  "Get-Output-Device-Attributes",	// IPP INFRA
  "Update-Active-Jobs",			// IPP INFRA
  "Deregister-Output-Device",		// IPP INFRA
  "Update-Document-Status",		// IPP INFRA
  "Update-Job-Status",			// IPP INFRA
  "Update-Output-Device-Attributes",	// IPP INFRA
  "Get-Next-Document-Data",		// IPP Scan
  "Allocate-Printer-Resources",		// IPP System
  "Create-Printer",			// IPP System
  "Deallocate-Printer-Resources",	// IPP System
  "Delete-Printer",			// IPP System
  "Get-Printers",			// IPP System

  // 0x0050 - 0x005f
  "Shutdown-One-Printer",		// IPP System
  "Startup-One-Printer",		// IPP System
  "Cancel-Resource",			// IPP System
  "Create-Resource",			// IPP System
  "Install-Resource",			// IPP System
  "Send-Resource-Data",			// IPP System
  "Set-Resource-Attributes",		// IPP System
  "Create-Resource-Subscriptions",	// IPP System
  "Create-System-Subscriptions",	// IPP System
  "Disable-All-Printers",		// IPP System
  "Enable-All-Printers",		// IPP System
  "Get-System-Attributes",		// IPP System
  "Get-System-Supported-Values",	// IPP System
  "Pause-All-Printers",			// IPP System
  "Pause-All-Printers-After-Current-Job",
					// IPP System
  "Register-Output-Device",		// IPP System

  // 0x0060 - 0x006a
  "Restart-System",			// IPP System
  "Resume-All-Printers",		// IPP System
  "Set-System-Attributes",		// IPP System
  "Shutdown-All-Printers",		// IPP System
  "Startup-All-Printers",		// IPP System
  "Get-Printer-Resources",		// IPP System
  "Get-User-Printer-Attributes",	// IPP EPX
  "Restart-One-Printer",		// IPP System
  "Acknowledge-Encrypted-Job-Attributes",
					// IPP TRUSTNOONE
  "Fetch-Encrypted-Job-Attributes",	// IPP TRUSTNOONE
  "Get-Encrypted-Job-Attributes"	// IPP TRUSTNOONE
};
static const char * const ipp_cups_ops[] =
{
  "CUPS-Get-Default",
  "CUPS-Get-Printers",
  "CUPS-Add-Modify-Printer",
  "CUPS-Delete-Printer",
  "CUPS-Get-Classes",
  "CUPS-Add-Modify-Class",
  "CUPS-Delete-Class",
  "CUPS-Accept-Jobs",
  "CUPS-Reject-Jobs",
  "CUPS-Set-Default",
  "CUPS-Get-Devices",
  "CUPS-Get-PPDs",
  "CUPS-Move-Job",
  "CUPS-Authenticate-Job",
  "CUPS-Get-PPD"
};
static const char * const ipp_cups_ops2[] =
{
  "CUPS-Get-Document",
  "CUPS-Create-Local-Printer"
};
static const char * const ipp_tag_names[] =
{					// Value/group tag names
  "zero",				// 0x00
  "operation-attributes-tag",		// 0x01
  "job-attributes-tag",			// 0x02
  "end-of-attributes-tag",		// 0x03
  "printer-attributes-tag",		// 0x04
  "unsupported-attributes-tag",		// 0x05
  "subscription-attributes-tag",	// 0x06 - RFC 3995
  "event-notification-attributes-tag",	// 0x07 - RFC 3995
  "resource-attributes-tag",		// 0x08 - IPP System
  "document-attributes-tag",		// 0x09 - IPP DocObject
  "system-attributes-tag",		// 0x0a - IPP System
  "0x0b",				// 0x0b
  "0x0c",				// 0x0c
  "0x0d",				// 0x0d
  "0x0e",				// 0x0e
  "0x0f",				// 0x0f
  "unsupported",			// 0x10
  "default",				// 0x11
  "unknown",				// 0x12
  "no-value",				// 0x13
  "0x14",				// 0x14
  "not-settable",			// 0x15 - RFC 3380
  "delete-attribute",			// 0x16 - RFC 3380
  "admin-define",			// 0x17 - RFC 3380
  "0x18",				// 0x18
  "0x19",				// 0x19
  "0x1a",				// 0x1a
  "0x1b",				// 0x1b
  "0x1c",				// 0x1c
  "0x1d",				// 0x1d
  "0x1e",				// 0x1e
  "0x1f",				// 0x1f
  "0x20",				// 0x20
  "integer",				// 0x21
  "boolean",				// 0x22
  "enum",				// 0x23
  "0x24",				// 0x24
  "0x25",				// 0x25
  "0x26",				// 0x26
  "0x27",				// 0x27
  "0x28",				// 0x28
  "0x29",				// 0x29
  "0x2a",				// 0x2a
  "0x2b",				// 0x2b
  "0x2c",				// 0x2c
  "0x2d",				// 0x2d
  "0x2e",				// 0x2e
  "0x2f",				// 0x2f
  "octetString",			// 0x30
  "dateTime",				// 0x31
  "resolution",				// 0x32
  "rangeOfInteger",			// 0x33
  "collection",				// 0x34
  "textWithLanguage",			// 0x35
  "nameWithLanguage",			// 0x36
  "endCollection",			// 0x37
  "0x38",				// 0x38
  "0x39",				// 0x39
  "0x3a",				// 0x3a
  "0x3b",				// 0x3b
  "0x3c",				// 0x3c
  "0x3d",				// 0x3d
  "0x3e",				// 0x3e
  "0x3f",				// 0x3f
  "0x40",				// 0x40
  "textWithoutLanguage",		// 0x41
  "nameWithoutLanguage",		// 0x42
  "0x43",				// 0x43
  "keyword",				// 0x44
  "uri",				// 0x45
  "uriScheme",				// 0x46
  "charset",				// 0x47
  "naturalLanguage",			// 0x48
  "mimeMediaType",			// 0x49
  "memberAttrName"			// 0x4a
};
static const char * const ipp_document_states[] =
{					// document-state-enums
  "pending",
  "4",
  "processing",
  "processing-stopped",			// IPP INFRA
  "canceled",
  "aborted",
  "completed"
};
static const char * const ipp_finishings[] =
{					// finishings enums
  "none",
  "staple",
  "punch",
  "cover",
  "bind",
  "saddle-stitch",
  "edge-stitch",
  "fold",
  "trim",
  "bale",
  "booklet-maker",
  "jog-offset",
  "coat",				// IPP FIN
  "laminate",				// IPP FIN
  "17",
  "18",
  "19",
  "staple-top-left",			// IPP FIN
  "staple-bottom-left",			// IPP FIN
  "staple-top-right",			// IPP FIN
  "staple-bottom-right",		// IPP FIN
  "edge-stitch-left",			// IPP FIN
  "edge-stitch-top",			// IPP FIN
  "edge-stitch-right",			// IPP FIN
  "edge-stitch-bottom",			// IPP FIN
  "staple-dual-left",			// IPP FIN
  "staple-dual-top",			// IPP FIN
  "staple-dual-right",			// IPP FIN
  "staple-dual-bottom",			// IPP FIN
  "staple-triple-left",			// IPP FIN
  "staple-triple-top",			// IPP FIN
  "staple-triple-right",		// IPP FIN
  "staple-triple-bottom",		// IPP FIN
  "36",
  "37",
  "38",
  "39",
  "40",
  "41",
  "42",
  "43",
  "44",
  "45",
  "46",
  "47",
  "48",
  "49",
  "bind-left",				// IPP FIN
  "bind-top",				// IPP FIN
  "bind-right",				// IPP FIN
  "bind-bottom",			// IPP FIN
  "54",
  "55",
  "56",
  "57",
  "58",
  "59",
  "trim-after-pages",			// IPP FIN
  "trim-after-documents",		// IPP FIN
  "trim-after-copies",			// IPP FIN
  "trim-after-job",			// IPP FIN
  "64",
  "65",
  "66",
  "67",
  "68",
  "69",
  "punch-top-left",			// IPP FIN
  "punch-bottom-left",			// IPP FIN
  "punch-top-right",			// IPP FIN
  "punch-bottom-right",			// IPP FIN
  "punch-dual-left",			// IPP FIN
  "punch-dual-top",			// IPP FIN
  "punch-dual-right",			// IPP FIN
  "punch-dual-bottom",			// IPP FIN
  "punch-triple-left",			// IPP FIN
  "punch-triple-top",			// IPP FIN
  "punch-triple-right",			// IPP FIN
  "punch-triple-bottom",		// IPP FIN
  "punch-quad-left",			// IPP FIN
  "punch-quad-top",			// IPP FIN
  "punch-quad-right",			// IPP FIN
  "punch-quad-bottom",			// IPP FIN
  "punch-multiple-left",		// IPP FIN
  "punch-multiple-top",			// IPP FIN
  "punch-multiple-right",		// IPP FIN
  "punch-multiple-bottom",		// IPP FIN
  "fold-accordion",			// IPP FIN
  "fold-double-gate",			// IPP FIN
  "fold-gate",				// IPP FIN
  "fold-half",				// IPP FIN
  "fold-half-z",			// IPP FIN
  "fold-left-gate",			// IPP FIN
  "fold-letter",			// IPP FIN
  "fold-parallel",			// IPP FIN
  "fold-poster",			// IPP FIN
  "fold-right-gate",			// IPP FIN
  "fold-z",				// IPP FIN
  "fold-engineering-z"			// IPP FIN
};
static const char * const ipp_finishings_vendor[] =
{
  // 0x40000000 to 0x4000000F
  "0x40000000",
  "0x40000001",
  "0x40000002",
  "0x40000003",
  "0x40000004",
  "0x40000005",
  "0x40000006",
  "0x40000007",
  "0x40000008",
  "0x40000009",
  "0x4000000A",
  "0x4000000B",
  "0x4000000C",
  "0x4000000D",
  "0x4000000E",
  "0x4000000F",
  // 0x40000010 to 0x4000001F
  "0x40000010",
  "0x40000011",
  "0x40000012",
  "0x40000013",
  "0x40000014",
  "0x40000015",
  "0x40000016",
  "0x40000017",
  "0x40000018",
  "0x40000019",
  "0x4000001A",
  "0x4000001B",
  "0x4000001C",
  "0x4000001D",
  "0x4000001E",
  "0x4000001F",
  // 0x40000020 to 0x4000002F
  "0x40000020",
  "0x40000021",
  "0x40000022",
  "0x40000023",
  "0x40000024",
  "0x40000025",
  "0x40000026",
  "0x40000027",
  "0x40000028",
  "0x40000029",
  "0x4000002A",
  "0x4000002B",
  "0x4000002C",
  "0x4000002D",
  "0x4000002E",
  "0x4000002F",
  // 0x40000030 to 0x4000003F
  "0x40000030",
  "0x40000031",
  "0x40000032",
  "0x40000033",
  "0x40000034",
  "0x40000035",
  "0x40000036",
  "0x40000037",
  "0x40000038",
  "0x40000039",
  "0x4000003A",
  "0x4000003B",
  "0x4000003C",
  "0x4000003D",
  "0x4000003E",
  "0x4000003F",
  // 0x40000040 - 0x4000004F
  "0x40000040",
  "0x40000041",
  "0x40000042",
  "0x40000043",
  "0x40000044",
  "0x40000045",
  "cups-punch-top-left",		// AirPrint
  "cups-punch-bottom-left",		// AirPrint
  "cups-punch-top-right",		// AirPrint
  "cups-punch-bottom-right",		// AirPrint
  "cups-punch-dual-left",		// AirPrint
  "cups-punch-dual-top",		// AirPrint
  "cups-punch-dual-right",		// AirPrint
  "cups-punch-dual-bottom",		// AirPrint
  "cups-punch-triple-left",		// AirPrint
  "cups-punch-triple-top",		// AirPrint
  // 0x40000050 - 0x4000005F
  "cups-punch-triple-right",		// AirPrint
  "cups-punch-triple-bottom",		// AirPrint
  "cups-punch-quad-left",		// AirPrint
  "cups-punch-quad-top",		// AirPrint
  "cups-punch-quad-right",		// AirPrint
  "cups-punch-quad-bottom",		// AirPrint
  "0x40000056",
  "0x40000057",
  "0x40000058",
  "0x40000059",
  "cups-fold-accordion",		// AirPrint
  "cups-fold-double-gate",		// AirPrint
  "cups-fold-gate",			// AirPrint
  "cups-fold-half",			// AirPrint
  "cups-fold-half-z",			// AirPrint
  "cups-fold-left-gate",		// AirPrint
  // 0x40000060 - 0x40000064
  "cups-fold-letter",			// AirPrint
  "cups-fold-parallel",			// AirPrint
  "cups-fold-poster",			// AirPrint
  "cups-fold-right-gate",		// AirPrint
  "cups-fold-z"				// AirPrint
};
static const char * const ipp_job_states[] =
{					// job-state enums
  "pending",
  "pending-held",
  "processing",
  "processing-stopped",
  "canceled",
  "aborted",
  "completed"
};
static const char * const ipp_orientation_requesteds[] =
{					// orientation-requested enums
  "portrait",
  "landscape",
  "reverse-landscape",
  "reverse-portrait",
  "none"
};
static const char * const ipp_print_qualities[] =
{					// print-quality enums
  "draft",
  "normal",
  "high"
};
static const char * const ipp_printer_states[] =
{					// printer-state enums
  "idle",
  "processing",
  "stopped"
};
static const char * const ipp_resource_states[] =
{					// resource-state enums
  "pending",
  "available",
  "installed",
  "canceled",
  "aborted"
};
static const char * const ipp_system_states[] =
{					// system-state enums
  "idle",
  "processing",
  "stopped"
};


//
// Local functions...
//

static size_t	ipp_col_string(ipp_t *col, char *buffer, size_t bufsize);


//
// 'ippAttributeString()' - Convert the attribute's value to a string.
//
// This function converts an attribute's values into a string and returns the
// number of bytes that would be written, not including the trailing `nul`.  The
// buffer pointer can be NULL to get the required length, just like
// `(v)snprintf`.
//
// @since CUPS 1.6@
//

size_t					// O - Number of bytes less `nul`
ippAttributeString(
    ipp_attribute_t *attr,		// I - Attribute
    char            *buffer,		// I - String buffer or `NULL`
    size_t          bufsize)		// I - Size of string buffer
{
  int		i;			// Looping var
  char		*bufptr,		// Pointer into buffer
		*bufend,		// End of buffer
		temp[256];		// Temporary string
  const char	*ptr,			// Pointer into string
		*end;			// Pointer to end of string
  _ipp_value_t	*val;			// Current value


  // Range check input...
  if (!attr || !attr->name)
  {
    if (buffer)
      *buffer = '\0';

    return (0);
  }

  // Setup buffer pointers...
  bufptr = buffer;
  if (buffer)
    bufend = buffer + bufsize - 1;
  else
    bufend = NULL;

  // Loop through the values...
  for (i = attr->num_values, val = attr->values; i > 0; i --, val ++)
  {
    if (val > attr->values)
    {
      if (buffer && bufptr < bufend)
        *bufptr++ = ',';
      else
        bufptr ++;
    }

    switch (attr->value_tag & ~IPP_TAG_CUPS_CONST)
    {
      case IPP_TAG_ENUM :
          ptr = ippEnumString(attr->name, val->integer);

          if (buffer && bufptr < bufend)
            cupsCopyString(bufptr, ptr, (size_t)(bufend - bufptr + 1));

          bufptr += strlen(ptr);
          break;

      case IPP_TAG_INTEGER :
          if (buffer && bufptr < bufend)
            bufptr += snprintf(bufptr, (size_t)(bufend - bufptr + 1), "%d", val->integer);
          else
            bufptr += snprintf(temp, sizeof(temp), "%d", val->integer);
          break;

      case IPP_TAG_BOOLEAN :
          if (buffer && bufptr < bufend)
            cupsCopyString(bufptr, val->boolean ? "true" : "false", (size_t)(bufend - bufptr + 1));

          bufptr += val->boolean ? 4 : 5;
          break;

      case IPP_TAG_RANGE :
          if (buffer && bufptr < bufend)
            bufptr += snprintf(bufptr, (size_t)(bufend - bufptr + 1), "%d-%d", val->range.lower, val->range.upper);
          else
            bufptr += snprintf(temp, sizeof(temp), "%d-%d", val->range.lower, val->range.upper);
          break;

      case IPP_TAG_RESOLUTION :
	  if (val->resolution.xres == val->resolution.yres)
	  {
	    if (buffer && bufptr < bufend)
	      bufptr += snprintf(bufptr, (size_t)(bufend - bufptr + 1), "%d%s", val->resolution.xres, val->resolution.units == IPP_RES_PER_INCH ? "dpi" : "dpcm");
	    else
	      bufptr += snprintf(temp, sizeof(temp), "%d%s", val->resolution.xres, val->resolution.units == IPP_RES_PER_INCH ? "dpi" : "dpcm");
	  }
	  else if (buffer && bufptr < bufend)
            bufptr += snprintf(bufptr, (size_t)(bufend - bufptr + 1), "%dx%d%s", val->resolution.xres, val->resolution.yres, val->resolution.units == IPP_RES_PER_INCH ? "dpi" : "dpcm");
          else
            bufptr += snprintf(temp, sizeof(temp), "%dx%d%s", val->resolution.xres, val->resolution.yres, val->resolution.units == IPP_RES_PER_INCH ? "dpi" : "dpcm");
          break;

      case IPP_TAG_DATE :
          {
            unsigned year;		// Year

            year = ((unsigned)val->date[0] << 8) | (unsigned)val->date[1];

	    if (val->date[9] == 0 && val->date[10] == 0)
	      snprintf(temp, sizeof(temp), "%04u-%02u-%02uT%02u:%02u:%02uZ",
		       year, val->date[2], val->date[3], val->date[4],
		       val->date[5], val->date[6]);
	    else
	      snprintf(temp, sizeof(temp),
	               "%04u-%02u-%02uT%02u:%02u:%02u%c%02u%02u",
		       year, val->date[2], val->date[3], val->date[4],
		       val->date[5], val->date[6], val->date[8], val->date[9],
		       val->date[10]);

            if (buffer && bufptr < bufend)
              cupsCopyString(bufptr, temp, (size_t)(bufend - bufptr + 1));

            bufptr += strlen(temp);
          }
          break;

      case IPP_TAG_TEXT :
      case IPP_TAG_NAME :
      case IPP_TAG_KEYWORD :
      case IPP_TAG_CHARSET :
      case IPP_TAG_URI :
      case IPP_TAG_URISCHEME :
      case IPP_TAG_MIMETYPE :
      case IPP_TAG_LANGUAGE :
      case IPP_TAG_TEXTLANG :
      case IPP_TAG_NAMELANG :
	  if (!val->string.text)
	    break;

          for (ptr = val->string.text; *ptr; ptr ++)
          {
            if (*ptr == '\\' || *ptr == '\"' || *ptr == '[')
            {
              if (buffer && bufptr < bufend)
                *bufptr = '\\';
              bufptr ++;
            }

            if (buffer && bufptr < bufend)
              *bufptr = *ptr;
            bufptr ++;
          }

          if (val->string.language)
          {
           /*
            * Add "[language]" to end of string...
            */

            if (buffer && bufptr < bufend)
              *bufptr = '[';
            bufptr ++;

            if (buffer && bufptr < bufend)
              cupsCopyString(bufptr, val->string.language, (size_t)(bufend - bufptr));
            bufptr += strlen(val->string.language);

            if (buffer && bufptr < bufend)
              *bufptr = ']';
            bufptr ++;
          }
          break;

      case IPP_TAG_BEGIN_COLLECTION :
          if (buffer && bufptr < bufend)
            bufptr += ipp_col_string(val->collection, bufptr, (size_t)(bufend - bufptr + 1));
          else
            bufptr += ipp_col_string(val->collection, NULL, 0);
          break;

      case IPP_TAG_STRING :
          for (ptr = val->unknown.data, end = ptr + val->unknown.length;
               ptr < end; ptr ++)
          {
            if (*ptr == '\\' || _cups_isspace(*ptr))
            {
              if (buffer && bufptr < bufend)
                *bufptr = '\\';
              bufptr ++;

              if (buffer && bufptr < bufend)
                *bufptr = *ptr;
              bufptr ++;
            }
            else if (!isprint(*ptr & 255))
            {
              if (buffer && bufptr < bufend)
                bufptr += snprintf(bufptr, (size_t)(bufend - bufptr + 1), "\\%03o", *ptr & 255);
              else
                bufptr += snprintf(temp, sizeof(temp), "\\%03o", *ptr & 255);
            }
            else
            {
              if (buffer && bufptr < bufend)
                *bufptr = *ptr;
              bufptr ++;
            }
          }
          break;

      default :
          ptr = ippTagString(attr->value_tag);
          if (buffer && bufptr < bufend)
            cupsCopyString(bufptr, ptr, (size_t)(bufend - bufptr + 1));
          bufptr += strlen(ptr);
          break;
    }
  }

  // Nul-terminate and return...
  if (buffer && bufptr < bufend)
    *bufptr = '\0';
  else if (bufend)
    *bufend = '\0';

  return ((size_t)(bufptr - buffer));
}


//
// 'ippCreateRequestedArray()' - Create a CUPS array of attribute names from the
//                               given requested-attributes attribute.
//
// This function creates a (sorted) CUPS array of attribute names matching the
// list of "requested-attribute" values supplied in an IPP request.  All IANA-
// registered values are supported in addition to the CUPS IPP extension
// attributes.
//
// The "request" argument specifies the request message that was read from
// the client.
//
// `NULL` is returned if all attributes should be returned.  Otherwise, the
// result is a sorted array of attribute names, where
// `cupsArrayFind(array, "attribute-name")` will return a non-`NULL` pointer.
// The array must be freed using @link cupsArrayDelete@.
//
// @since CUPS 1.7@
//

cups_array_t *				// O - CUPS array or `NULL` if all
ippCreateRequestedArray(ipp_t *request)	// I - IPP request
{
  size_t		i, j,		// Looping vars
			count;		// Number of values
  bool			added;		// Was name added?
  ipp_op_t		op;		// IPP operation code
  ipp_attribute_t	*requested;	// requested-attributes attribute
  cups_array_t		*ra;		// Requested attributes array
  const char		*value;		// Current value
  // The following lists come from the current IANA IPP registry of attributes
  static const char * const document_description[] =
  {					// document-description group
    "compression",
    "copies-actual",
    "cover-back-actual",
    "cover-front-actual",
    "current-page-order",
    "date-time-at-completed",
    "date-time-at-creation",
    "date-time-at-processing",
    "detailed-status-messages",
    "document-access-errors",
    "document-charset",
    "document-format",
    "document-format-details",		// IPP JobExt
    "document-format-detected",		// IPP JobExt
    "document-job-id",
    "document-job-uri",
    "document-message",
    "document-metadata",
    "document-name",
    "document-natural-language",
    "document-number",
    "document-printer-uri",
    "document-state",
    "document-state-message",
    "document-state-reasons",
    "document-uri",
    "document-uuid",			// IPP NODRIVER
    "errors-count",			// IPP JobExt
    "finishings-actual",
    "finishings-col-actual",
    "force-front-side-actual",
    "imposition-template-actual",
    "impressions",
    "impressions-col",
    "impressions-completed",
    "impressions-completed-col",
    "impressions-completed-current-copy",
    "insert-sheet-actual",
    "k-octets",
    "k-octets-processed",
    "last-document",
    "materials-col-actual",		// IPP 3D
    "media-actual",
    "media-col-actual",
    "media-input-tray-check-actual",
    "media-sheets",
    "media-sheets-col",
    "media-sheets-completed",
    "media-sheets-completed-col",
    "more-info",
    "multiple-object-handling-actual",	// IPP 3D
    "number-up-actual",
    "orientation-requested-actual",
    "output-bin-actual",
    "output-device-assigned",
    "overrides-actual",
    "page-delivery-actual",
    "page-order-received-actual",
    "page-ranges-actual",
    "pages",
    "pages-col",
    "pages-completed",
    "pages-completed-col",
    "pages-completed-current-copy",
    "platform-temperature-actual",	// IPP 3D
    "presentation-direction-number-up-actual",
    "print-accuracy-actual",		// IPP 3D
    "print-base-actual",		// IPP 3D
    "print-color-mode-actual",
    "print-content-optimize-actual",	// IPP JobExt
    "print-objects-actual",		// IPP 3D
    "print-quality-actual",
    "print-rendering-intent-actual",
    "print-scaling-actual",		// IPP Paid Printing
    "print-supports-actual",		// IPP 3D
    "printer-resolution-actual",
    "printer-up-time",
    "separator-sheets-actual",
    "sheet-completed-copy-number",
    "sides-actual",
    "time-at-completed",
    "time-at-creation",
    "time-at-processing",
    "warnings-count",			// IPP JobExt
    "x-image-position-actual",
    "x-image-shift-actual",
    "x-side1-image-shift-actual",
    "x-side2-image-shift-actual",
    "y-image-position-actual",
    "y-image-shift-actual",
    "y-side1-image-shift-actual",
    "y-side2-image-shift-actual"
  };
  static const char * const document_template[] =
  {					// document-template group
    "baling-type-supported",		// IPP FIN
    "baling-when-supported",		// IPP FIN
    "binding-reference-edge-supported",	// IPP FIN
    "binding-type-supported",		// IPP FIN
    "chamber-humidity",			// IPP 3D
    "chamber-humidity-default",		// IPP 3D
    "chamber-humidity-supported",	// IPP 3D
    "chamber-temperature",		// IPP 3D
    "chamber-temperature-default",	// IPP 3D
    "chamber-temperature-supported",	// IPP 3D
    "coating-sides-supported",		// IPP FIN
    "coating-type-supported",		// IPP FIN
    "copies",
    "copies-default",
    "copies-supported",
    "cover-back",			// IPP PPX
    "cover-back-default",		// IPP PPX
    "cover-back-supported",		// IPP PPX
    "cover-front",			// IPP PPX
    "cover-front-default",		// IPP PPX
    "cover-front-supported",		// IPP PPX
    "covering-name-supported",		// IPP FIN
    "feed-orientation",
    "feed-orientation-default",
    "feed-orientation-supported",
    "finishing-template-supported",	// IPP FIN
    "finishings",
    "finishings-col",			// IPP FIN
    "finishings-col-database",		// IPP FIN
    "finishings-col-default",		// IPP FIN
    "finishings-col-ready",		// IPP FIN
    "finishings-col-supported",		// IPP FIN
    "finishings-default",
    "finishings-ready",
    "finishings-supported",
    "folding-direction-supported",	// IPP FIN
    "folding-offset-supported",		// IPP FIN
    "folding-reference-edge-supported",	// IPP FIN
    "force-front-side",			// IPP PPX
    "force-front-side-default",		// IPP PPX
    "force-front-side-supported",	// IPP PPX
    "imposition-template",		// IPP PPX
    "imposition-template-default",	// IPP PPX
    "imposition-template-supported",	// IPP PPX
    "insert-count-supported",		// IPP PPX
    "insert-sheet",			// IPP PPX
    "insert-sheet-default",		// IPP PPX
    "insert-sheet-supported",		// IPP PPX
    "laminating-sides-supported",	// IPP FIN
    "laminating-type-supported",	// IPP FIN
    "material-amount-units-supported",	// IPP 3D
    "material-diameter-supported",	// IPP 3D
    "material-purpose-supported",	// IPP 3D
    "material-rate-supported",		// IPP 3D
    "material-rate-units-supported",	// IPP 3D
    "material-shell-thickness-supported",
					// IPP 3D
    "material-temperature-supported",	// IPP 3D
    "material-type-supported",		// IPP 3D
    "materials-col",			// IPP 3D
    "materials-col-database",		// IPP 3D
    "materials-col-default",		// IPP 3D
    "materials-col-ready",		// IPP 3D
    "materials-col-supported",		// IPP 3D
    "max-materials-col-supported",	// IPP 3D
    "max-page-ranges-supported",
    "max-stitching-locations-supported",// IPP FIN
    "media",
    "media-back-coating-supported",	// IPP JobExt
    "media-bottom-margin-supported",	// IPP JobExt
    "media-col",			// IPP JobExt
    "media-col-default",		// IPP JobExt
    "media-col-ready",			// IPP JobExt
    "media-col-supported",		// IPP JobExt
    "media-color-supported",		// IPP JobExt
    "media-default",
    "media-front-coating-supported",	// IPP JobExt
    "media-grain-supported",		// IPP JobExt
    "media-hole-count-supported",	// IPP JobExt
    "media-info-supported",		// IPP JobExt
    "media-input-tray-check",		// IPP PPX
    "media-input-tray-check-default",	// IPP PPX
    "media-input-tray-check-supported",	// IPP PPX
    "media-key-supported",		// IPP JobExt
    "media-left-margin-supported",	// IPP JobExt
    "media-order-count-supported",	// IPP JobExt
    "media-overprint",			// IPP NODRIVER
    "media-overprint-distance-supported",
					// IPP NODRIVER
    "media-overprint-method-supported",	// IPP NODRIVER
    "media-overprint-supported",	// IPP NODRIVER
    "media-pre-printed-supported",	// IPP JobExt
    "media-ready",
    "media-recycled-supported",		// IPP JobExt
    "media-right-margin-supported",	// IPP JobExt
    "media-size-supported",		// IPP JobExt
    "media-source-supported",		// IPP JobExt
    "media-supported",
    "media-thickness-supported",	// IPP JobExt
    "media-top-margin-supported",	// IPP JobExt
    "media-type-supported",		// IPP JobExt
    "media-weight-metric-supported",	// IPP JobExt
    "multiple-document-handling",
    "multiple-document-handling-default",
    "multiple-document-handling-supported",
    "multiple-object-handling",		// IPP 3D
    "multiple-object-handling-default",	// IPP 3D
    "multiple-object-handling-supported",
					// IPP 3D
    "number-up",
    "number-up-default",
    "number-up-supported",
    "orientation-requested",
    "orientation-requested-default",
    "orientation-requested-supported",
    "output-device",			// IPP JobExt
    "output-device-supported",		// IPP JobExt
    "output-mode",			// CUPS extension
    "output-mode-default",		// CUPS extension
    "output-mode-supported",		// CUPS extension
    "overrides",
    "overrides-supported",
    "page-delivery",			// IPP PPX
    "page-delivery-default",		// IPP PPX
    "page-delivery-supported",		// IPP PPX
    "page-ranges",
    "page-ranges-supported",
    "platform-temperature",		// IPP 3D
    "platform-temperature-default",	// IPP 3D
    "platform-temperature-supported",	// IPP 3D
    "preferred-attributes-supported",	// IPP NODRIVER
    "presentation-direction-number-up",	// IPP PPX
    "presentation-direction-number-up-default",
					// IPP PPX
    "presentation-direction-number-up-supported",
					// IPP PPX
    "print-accuracy",			// IPP 3D
    "print-accuracy-default",		// IPP 3D
    "print-accuracy-supported",		// IPP 3D
    "print-base",			// IPP 3D
    "print-base-default",		// IPP 3D
    "print-base-supported",		// IPP 3D
    "print-color-mode",			// IPP NODRIVER
    "print-color-mode-default",		// IPP NODRIVER
    "print-color-mode-supported",	// IPP NODRIVER
    "print-content-optimize",		// IPP JobExt
    "print-content-optimize-default",	// IPP JobExt
    "print-content-optimize-supported",	// IPP JobExt
    "print-objects",			// IPP 3D
    "print-objects-default",		// IPP 3D
    "print-objects-supported",		// IPP 3D
    "print-processing-attributes-supported",
					// IPP NODRIVER
    "print-quality",
    "print-quality-default",
    "print-quality-supported",
    "print-rendering-intent",		// IPP NODRIVER
    "print-rendering-intent-default",	// IPP NODRIVER
    "print-rendering-intent-supported",	// IPP NODRIVER
    "print-scaling",			// IPP NODRIVER
    "print-scaling-default",		// IPP NODRIVER
    "print-scaling-supported",		// IPP NODRIVER
    "print-supports",			// IPP 3D
    "print-supports-default",		// IPP 3D
    "print-supports-supported",		// IPP 3D
    "printer-resolution",
    "printer-resolution-default",
    "printer-resolution-supported",
    "punching-hole-diameter-configured",// IPP FIN
    "punching-locations-supported",	// IPP FIN
    "punching-offset-supported",	// IPP FIN
    "punching-reference-edge-supported",// IPP FIN
    "separator-sheets",			// IPP PPX
    "separator-sheets-default",		// IPP PPX
    "separator-sheets-supported",	// IPP PPX
    "separator-sheets-type-supported",	// IPP PPX
    "sides",
    "sides-default",
    "sides-supported",
    "stitching-angle-supported",	// IPP FIN
    "stitching-locations-supported",	// IPP FIN
    "stitching-method-supported",	// IPP FIN
    "stitching-offset-supported",	// IPP FIN
    "stitching-reference-edge-supported",
					// IPP FIN
    "x-image-position",			// IPP PPX
    "x-image-position-default",		// IPP PPX
    "x-image-position-supported",	// IPP PPX
    "x-image-shift",			// IPP PPX
    "x-image-shift-default",		// IPP PPX
    "x-image-shift-supported",		// IPP PPX
    "x-side1-image-shift",		// IPP PPX
    "x-side1-image-shift-default",	// IPP PPX
    "x-side1-image-shift-supported",	// IPP PPX
    "x-side2-image-shift",		// IPP PPX
    "x-side2-image-shift-default",	// IPP PPX
    "x-side2-image-shift-supported",	// IPP PPX
    "y-image-position",			// IPP PPX
    "y-image-position-default",		// IPP PPX
    "y-image-position-supported",	// IPP PPX
    "y-image-shift",			// IPP PPX
    "y-image-shift-default",		// IPP PPX
    "y-image-shift-supported",		// IPP PPX
    "y-side1-image-shift",		// IPP PPX
    "y-side1-image-shift-default",	// IPP PPX
    "y-side1-image-shift-supported",	// IPP PPX
    "y-side2-image-shift",		// IPP PPX
    "y-side2-image-shift-default",	// IPP PPX
    "y-side2-image-shift-supported"	// IPP PPX
  };
  static const char * const job_description[] =
  {					// job-description group
    "chamber-humidity-actual",		// IPP 3D
    "chamber-temperature-actual",	// IPP 3D
    "compression-supplied",
    "copies-actual",
    "cover-back-actual",
    "cover-front-actual",
    "current-page-order",
    "date-time-at-completed",
    "date-time-at-completed-estimated",	// IPP PPX
    "date-time-at-creation",
    "date-time-at-processing",
    "date-time-at-processing-estimated",// IPP PPX
    "destination-statuses",
    "document-charset-supplied",
    "document-digital-signature-supplied",
    "document-format-details-supplied",
    "document-format-supplied",
    "document-message-supplied",
    "document-metadata",
    "document-name-supplied",
    "document-natural-language-supplied",
    "document-overrides-actual",
    "errors-count",
    "finishings-actual",
    "finishings-col-actual",
    "force-front-side-actual",
    "imposition-template-actual",
    "impressions-completed-current-copy",
    "insert-sheet-actual",
    "job-account-id-actual",
    "job-accounting-sheets-actual",
    "job-accounting-user-id-actual",
    "job-attribute-fidelity",
    "job-charge-info",			// CUPS extension
    "job-detailed-status-message",
    "job-document-access-errors",
    "job-error-sheet-actual",
    "job-hold-until-actual",
    "job-id",
    "job-impressions",
    "job-impressions-col",
    "job-impressions-completed",
    "job-impressions-completed-col",
    "job-k-octets",
    "job-k-octets-processed",
    "job-mandatory-attributes",
    "job-media-progress",		// CUPS extension
    "job-media-sheets",
    "job-media-sheets-col",
    "job-media-sheets-completed",
    "job-media-sheets-completed-col",
    "job-message-from-operator",
    "job-more-info",
    "job-name",
    "job-originating-host-name",	// CUPS extension
    "job-originating-user-name",
    "job-originating-user-uri",		// IPP NODRIVER
    "job-pages",
    "job-pages-col",
    "job-pages-completed",
    "job-pages-completed-col",
    "job-pages-completed-current-copy",
    "job-printer-state-message",	// CUPS extension
    "job-printer-state-reasons",	// CUPS extension
    "job-printer-up-time",
    "job-printer-uri",
    "job-priority-actual",
    "job-resource-ids",			// IPP System
    "job-save-printer-make-and-model",
    "job-sheet-message-actual",
    "job-sheets-actual",
    "job-sheets-col-actual",
    "job-state",
    "job-state-message",
    "job-state-reasons",
    "job-storage",			// IPP EPX
    "job-uri",
    "job-uuid",				// IPP NODRIVER
    "materials-col-actual",		// IPP 3D
    "media-actual",
    "media-col-actual",
    "media-check-input-tray-actual",
    "multiple-document-handling-actual",
    "multiple-object-handling-actual",	// IPP 3D
    "number-of-documents",
    "number-of-intervening-jobs",
    "number-up-actual",
    "orientation-requested-actual",
    "original-requesting-user-name",
    "output-bin-actual",
    "output-device-assigned",
    "output-device-job-state",		// IPP INFRA
    "output-device-job-state-message",	// IPP INFRA
    "output-device-job-state-reasons",	// IPP INFRA
    "output-device-uuid-assigned",	// IPP INFRA
    "overrides-actual",
    "page-delivery-actual",
    "page-order-received-actual",
    "page-ranges-actual",
    "parent-job-id",			// IPP EPX
    "parent-job-uuid",			// IPP EPX
    "platform-temperature-actual",	// IPP 3D
    "presentation-direction-number-up-actual",
    "print-accuracy-actual",		// IPP 3D
    "print-base-actual",		// IPP 3D
    "print-color-mode-actual",
    "print-content-optimize-actual",
    "print-objects-actual",		// IPP 3D
    "print-quality-actual",
    "print-rendering-intent-actual",
    "print-scaling-actual",		// IPP Paid Printing
    "print-supports-actual",		// IPP 3D
    "printer-resolution-actual",
    "separator-sheets-actual",
    "sheet-collate-actual",
    "sheet-completed-copy-number",
    "sheet-completed-document-number",
    "sides-actual",
    "time-at-completed",
    "time-at-completed-estimated",	// IPP PPX
    "time-at-creation",
    "time-at-processing",
    "time-at-processing-estimated",	// IPP PPX
    "warnings-count",			// IPP JobExt
    "x-image-position-actual",
    "x-image-shift-actual",
    "x-side1-image-shift-actual",
    "x-side2-image-shift-actual",
    "y-image-position-actual",
    "y-image-shift-actual",
    "y-side1-image-shift-actual",
    "y-side2-image-shift-actual"
  };
  static const char * const job_template[] =
  {					// job-template group
    "accuracy-units-supported",		// IPP 3D
    "baling-type-supported",		// IPP FIN
    "baling-when-supported",		// IPP FIN
    "binding-reference-edge-supported",	// IPP FIN
    "binding-type-supported",		// IPP FIN
    "chamber-humidity",			// IPP 3D
    "chamber-humidity-default",		// IPP 3D
    "chamber-humidity-supported",	// IPP 3D
    "chamber-temperature",		// IPP 3D
    "chamber-temperature-default",	// IPP 3D
    "chamber-temperature-supported",	// IPP 3D
    "coating-sides-supported",		// IPP FIN
    "coating-type-supported",		// IPP FIN
    "confirmation-sheet-print",		// IPP FaxOut
    "confirmation-sheet-print-default",
    "copies",
    "copies-default",
    "copies-supported",
    "cover-back",			// IPP PPX
    "cover-back-default",		// IPP PPX
    "cover-back-supported",		// IPP PPX
    "cover-front",			// IPP PPX
    "cover-front-default",		// IPP PPX
    "cover-front-supported",		// IPP PPX
    "cover-sheet-info",			// IPP FaxOut
    "cover-sheet-info-default",		// IPP FaxOut
    "cover-sheet-info-supported",	// IPP FaxOut
    "covering-name-supported",		// IPP FIN
    "destination-uri-schemes-supported",// IPP FaxOut
    "destination-uris",			// IPP FaxOut
    "destination-uris-supported",
    "feed-orientation",
    "feed-orientation-default",
    "feed-orientation-supported",
    "finishings",
    "finishings-col",			// IPP FIN
    "finishings-col-database",		// IPP FIN
    "finishings-col-default",		// IPP FIN
    "finishings-col-ready",		// IPP FIN
    "finishings-col-supported",		// IPP FIN
    "finishings-default",
    "finishings-ready",
    "finishings-supported",
    "folding-direction-supported",	// IPP FIN
    "folding-offset-supported",		// IPP FIN
    "folding-reference-edge-supported",	// IPP FIN
    "force-front-side",			// IPP PPX
    "force-front-side-default",		// IPP PPX
    "force-front-side-supported",	// IPP PPX
    "imposition-template",		// IPP PPX
    "imposition-template-default",	// IPP PPX
    "imposition-template-supported",	// IPP PPX
    "insert-count-supported",		// IPP PPX
    "insert-sheet",			// IPP PPX
    "insert-sheet-default",		// IPP PPX
    "insert-sheet-supported",		// IPP PPX
    "job-account-id",			// IPP JobExt
    "job-account-id-default",		// IPP JobExt
    "job-account-id-supported",		// IPP JobExt
    "job-accounting-output-bin-supported",
					// IPP PPX
    "job-accounting-sheets",		// IPP PPX
    "job-accounting-sheets-default",	// IPP PPX
    "job-accounting-sheets-supported",	// IPP PPX
    "job-accounting-sheets-type-supported",
					// IPP PPX
    "job-accounting-user-id",		// IPP JobExt
    "job-accounting-user-id-default",	// IPP JobExt
    "job-accounting-user-id-supported",	// IPP JobExt
    "job-cancel-after",			// IPP EPX
    "job-cancel-after-default",		// IPP EPX
    "job-cancel-after-supported",	// IPP EPX
    "job-complete-before",		// IPP PPX
    "job-complete-before-supported",	// IPP PPX
    "job-complete-before-time",		// IPP PPX
    "job-complete-before-time-supported",
					// IPP PPX
    "job-delay-output-until",		// IPP JobExt
    "job-delay-output-until-default",	// IPP JobExt
    "job-delay-output-until-supported",	// IPP JobExt
    "job-delay-output-until-time",	// IPP JobExt
    "job-delay-output-until-time-default",
					// IPP JobExt
    "job-delay-output-until-time-supported",
					// IPP JobExt
    "job-error-action",			// IPP NODRIVER
    "job-error-action-default",		// IPP NODRIVER
    "job-error-action-supported",	// IPP NODRIVER
    "job-error-sheet",			// IPP PPX
    "job-error-sheet-default",		// IPP PPX
    "job-error-sheet-supported",	// IPP PPX
    "job-error-sheet-type-supported",	// IPP PPX
    "job-error-sheet-when-supported",	// IPP PPX
    "job-hold-until",
    "job-hold-until-default",
    "job-hold-until-supported",
    "job-hold-until-time",		// IPP JobExt
    "job-hold-until-time-default",	// IPP JobExt
    "job-hold-until-time-supported",	// IPP JobExt
    "job-message-to-operator",		// IPP PPX
    "job-message-to-operator-supported",// IPP PPX
    "job-phone-number",			// IPP PPX
    "job-phone-number-default",		// IPP PPX
    "job-phone-number-supported",	// IPP PPX
    "job-priority",
    "job-priority-default",
    "job-priority-supported",
    "job-recipient-name",		// IPP PPX
    "job-recipient-name-supported",	// IPP PPX
    "job-retain-until",			// IPP JobExt
    "job-retain-until-default",		// IPP JobExt
    "job-retain-until-interval",	// IPP JobExt
    "job-retain-until=interval-default",// IPP JobExt
    "job-retain-until-interval-supported",
					// IPP JobExt
    "job-retain-until-supported",	// IPP JobExt
    "job-retain-until-time",		// IPP JobExt
    "job-retain-until-time-supported",	// IPP JobExt
    "job-sheet-message",		// IPP PPX
    "job-sheet-message-supported",	// IPP PPX
    "job-sheets",
    "job-sheets-col",			// IPP JobExt
    "job-sheets-col-default",		// IPP JobExt
    "job-sheets-col-supported",		// IPP JobExt
    "job-sheets-default",
    "job-sheets-supported",
    "laminating-sides-supported",	// IPP FIN
    "laminating-type-supported",	// IPP FIN
    "logo-uri-schemes-supported",	// IPP FaxOut
    "material-amount-units-supported",	// IPP 3D
    "material-diameter-supported",	// IPP 3D
    "material-purpose-supported",	// IPP 3D
    "material-rate-supported",		// IPP 3D
    "material-rate-units-supported",	// IPP 3D
    "material-shell-thickness-supported",
					// IPP 3D
    "material-temperature-supported",	// IPP 3D
    "material-type-supported",		// IPP 3D
    "materials-col",			// IPP 3D
    "materials-col-database",		// IPP 3D
    "materials-col-default",		// IPP 3D
    "materials-col-ready",		// IPP 3D
    "materials-col-supported",		// IPP 3D
    "max-materials-col-supported",	// IPP 3D
    "max-page-ranges-supported",
    "max-stitching-locations-supported",// IPP FIN
    "media",
    "media-back-coating-supported",	// IPP JobExt
    "media-bottom-margin-supported",	// IPP JobExt
    "media-col",			// IPP JobExt
    "media-col-default",		// IPP JobExt
    "media-col-ready",			// IPP JobExt
    "media-col-supported",		// IPP JobExt
    "media-color-supported",		// IPP JobExt
    "media-default",
    "media-front-coating-supported",	// IPP JobExt
    "media-grain-supported",		// IPP JobExt
    "media-hole-count-supported",	// IPP JobExt
    "media-info-supported",		// IPP JobExt
    "media-input-tray-check",		// IPP PPX
    "media-input-tray-check-default",	// IPP PPX
    "media-input-tray-check-supported",	// IPP PPX
    "media-key-supported",		// IPP JobExt
    "media-left-margin-supported",	// IPP JobExt
    "media-order-count-supported",	// IPP JobExt
    "media-overprint",			// IPP NODRIVER
    "media-overprint-distance-supported",
					// IPP NODRIVER
    "media-overprint-method-supported",	// IPP NODRIVER
    "media-overprint-supported",	// IPP NODRIVER
    "media-pre-printed-supported",	// IPP JobExt
    "media-ready",
    "media-recycled-supported",		// IPP JobExt
    "media-right-margin-supported",	// IPP JobExt
    "media-size-supported",		// IPP JobExt
    "media-source-supported",		// IPP JobExt
    "media-supported",
    "media-thickness-supported",	// IPP JobExt
    "media-top-margin-supported",	// IPP JobExt
    "media-type-supported",		// IPP JobExt
    "media-weight-metric-supported",	// IPP JobExt
    "multiple-document-handling",
    "multiple-document-handling-default",
    "multiple-document-handling-supported",
    "multiple-object-handling",		// IPP 3D
    "multiple-object-handling-default",	// IPP 3D
    "multiple-object-handling-supported",
					// IPP 3D
    "number-of-retries",		// IPP FaxOut
    "number-of-retries-default",
    "number-of-retries-supported",
    "number-up",
    "number-up-default",
    "number-up-supported",
    "orientation-requested",
    "orientation-requested-default",
    "orientation-requested-supported",
    "output-bin",
    "output-bin-default",
    "output-bin-supported",
    "output-device",			// IPP JobExt
    "output-device-supported",		// IPP JobExt
    "output-mode",			// CUPS extension
    "output-mode-default",		// CUPS extension
    "output-mode-supported",		// CUPS extension
    "overrides",
    "overrides-supported",
    "page-delivery",			// IPP PPX
    "page-delivery-default",		// IPP PPX
    "page-delivery-supported",		// IPP PPX
    "page-ranges",
    "page-ranges-supported",
    "platform-temperature",		// IPP 3D
    "platform-temperature-default",	// IPP 3D
    "platform-temperature-supported",	// IPP 3D
    "preferred-attributes-supported",	// IPP NODRIVER
    "presentation-direction-number-up",	// IPP PPX
    "presentation-direction-number-up-default",
					// IPP PPX
    "presentation-direction-number-up-supported",
					// IPP PPX
    "print-accuracy",			// IPP 3D
    "print-accuracy-default",		// IPP 3D
    "print-accuracy-supported",		// IPP 3D
    "print-base",			// IPP 3D
    "print-base-default",		// IPP 3D
    "print-base-supported",		// IPP 3D
    "print-color-mode",			// IPP NODRIVER
    "print-color-mode-default",		// IPP NODRIVER
    "print-color-mode-supported",	// IPP NODRIVER
    "print-content-optimize",		// IPP JobExt
    "print-content-optimize-default",	// IPP JobExt
    "print-content-optimize-supported",	// IPP JobExt
    "print-objects",			// IPP 3D
    "print-objects-default",		// IPP 3D
    "print-objects-supported",		// IPP 3D
    "print-processing-attributes-supported",
					// IPP NODRIVER
    "print-quality",
    "print-quality-default",
    "print-quality-supported",
    "print-rendering-intent",		// IPP NODRIVER
    "print-rendering-intent-default",	// IPP NODRIVER
    "print-rendering-intent-supported",	// IPP NODRIVER
    "print-scaling",			// IPP NODRIVER
    "print-scaling-default",		// IPP NODRIVER
    "print-scaling-supported",		// IPP NODRIVER
    "print-supports",			// IPP 3D
    "print-supports-default",		// IPP 3D
    "print-supports-supported",		// IPP 3D
    "printer-resolution",
    "printer-resolution-default",
    "printer-resolution-supported",
    "proof-copies",			// IPP EPX
    "proof-copies-supported",		// IPP EPX
    "proof-print",			// IPP EPX
    "proof-print-default",		// IPP EPX
    "proof-print-supported"		// IPP EPX
    "punching-hole-diameter-configured",// IPP FIN
    "punching-locations-supported",	// IPP FIN
    "punching-offset-supported",	// IPP FIN
    "punching-reference-edge-supported",// IPP FIN
    "retry-interval",			// IPP FaxOut
    "retry-interval-default",
    "retry-interval-supported",
    "retry-timeout",			// IPP FaxOut
    "retry-timeout-default",
    "retry-timeout-supported",
    "separator-sheets",			// IPP PPX
    "separator-sheets-default",		// IPP PPX
    "separator-sheets-supported",	// IPP PPX
    "separator-sheets-type-supported",	// IPP PPX
    "sides",
    "sides-default",
    "sides-supported",
    "stitching-angle-supported",	// IPP FIN
    "stitching-locations-supported",	// IPP FIN
    "stitching-method-supported",	// IPP FIN
    "stitching-offset-supported",	// IPP FIN
    "stitching-reference-edge-supported",
					// IPP FIN
    "x-image-position",			// IPP PPX
    "x-image-position-default",		// IPP PPX
    "x-image-position-supported",	// IPP PPX
    "x-image-shift",			// IPP PPX
    "x-image-shift-default",		// IPP PPX
    "x-image-shift-supported",		// IPP PPX
    "x-side1-image-shift",		// IPP PPX
    "x-side1-image-shift-default",	// IPP PPX
    "x-side1-image-shift-supported",	// IPP PPX
    "x-side2-image-shift",		// IPP PPX
    "x-side2-image-shift-default",	// IPP PPX
    "x-side2-image-shift-supported",	// IPP PPX
    "y-image-position",			// IPP PPX
    "y-image-position-default",		// IPP PPX
    "y-image-position-supported",	// IPP PPX
    "y-image-shift",			// IPP PPX
    "y-image-shift-default",		// IPP PPX
    "y-image-shift-supported",		// IPP PPX
    "y-side1-image-shift",		// IPP PPX
    "y-side1-image-shift-default",	// IPP PPX
    "y-side1-image-shift-supported",	// IPP PPX
    "y-side2-image-shift",		// IPP PPX
    "y-side2-image-shift-default",	// IPP PPX
    "y-side2-image-shift-supported"	// IPP PPX
  };
  static const char * const printer_description[] =
  {					// printer-description group
    "auth-info-required",		// CUPS extension
    "chamber-humidity-current",		// IPP 3D
    "chamber-temperature-current",	// IPP 3D
    "charset-configured",
    "charset-supported",
    "color-supported",
    "compression-supported",
    "device-service-count",
    "device-uri",			// CUPS extension
    "device-uuid",
    "document-charset-default",		// IPP JobExt
    "document-charset-supported",	// IPP JobExt
    "document-creation-attributes-supported",
    "document-format-default",
    "document-format-details-supported",// IPP JobExt
    "document-format-preferred",	// AirPrint extension
    "document-format-supported",
    "document-format-varying-attributes",
    "document-natural-language-default",// IPP JobExt
    "document-natural-language-supported",
					// IPP JobExt
    "document-password-supported",	// IPP NODRIVER
    "document-privacy-attributes",	// IPP Privacy Attributes
    "document-privacy-scope",		// IPP Privacy Attributes
    "generated-natural-language-supported",
    "identify-actions-default",		// IPP NODRIVER
    "identify-actions-supported",	// IPP NODRIVER
    "input-source-supported",		// IPP FaxOut
    "ipp-features-supported",		// IPP NODRIVER
    "ipp-versions-supported",
    "ippget-event-life",		// RFC 3995
    "job-authorization-uri-supported",	// CUPS extension
    "job-constraints-supported",	// IPP NODRIVER
    "job-creation-attributes-supported",// IPP JobExt
    "job-history-attributes-configured",// IPP JobExt
    "job-history-attributes-supported",	// IPP JobExt
    "job-ids-supported",		// IPP JobExt
    "job-impressions-supported",
    "job-k-limit",			// CUPS extension
    "job-k-octets-supported",
    "job-mandatory-attributes-supported",
					// IPP JobExt
    "job-media-sheets-supported",
    "job-page-limit",			// CUPS extension
    "job-pages-per-set-supported",	// IPP FIN
    "job-password-encryption-supported",// IPP EPX
    "job-password-length-supported",	// IPP EPX
    "job-password-repertoire-configured",
					// IPP EPX
    "job-password-repertoire-supported",// IPP EPX
    "job-password-supported",		// IPP EPX
    "job-presets-supported",		// IPP NODRIVER
    "job-privacy-attributes",		// IPP Privacy Attributes
    "job-privacy-scope",		// IPP Privacy Attributes
    "job-quota-period",			// CUPS extension
    "job-release-action-default",	// IPP EPX
    "job-release-action-supported",	// IPP EPX
    "job-resolvers-supported",		// IPP NODRIVER
    "job-settable-attributes-supported",// RFC 3380
    "job-spooling-supported",		// IPP JobExt
    "job-storage-access-supported",	// IPP EPX
    "job-storage-disposition-supported",// IPP EPX
    "job-storage-group-supported",	// IPP EPX
    "job-storage-supported",		// IPP EPX
    "job-triggers-supported",		// IPP NODRIVER
    "jpeg-features-supported",		// IPP NODRIVER
    "jpeg-k-octets-supported",		// IPP NODRIVER
    "jpeg-x-dimension-supported",	// IPP NODRIVER
    "jpeg-y-dimension-supported",	// IPP NODRIVER
    "landscape-orientation-requested-preferred",
					// AirPrint extension
    "marker-change-time",		// CUPS extension
    "marker-colors",			// CUPS extension
    "marker-high-levels",		// CUPS extension
    "marker-levels",			// CUPS extension
    "marker-low-levels",		// CUPS extension
    "marker-message",			// CUPS extension
    "marker-names",			// CUPS extension
    "marker-types",			// CUPS extension
    "member-names",			// CUPS extension
    "member-uris",			// CUPS extension
    "mopria-certified",			// Mopria extension
    "multiple-destination-uris-supported",
					// IPP FaxOut
    "multiple-document-jobs-supported",
    "multiple-operation-time-out",
    "multiple-operation-time-out-action",
					// IPP NODRIVER
    "natural-language-configured",
    "operations-supported",
    "output-device-uuid-supported",	// IPP INFRA
    "pages-per-minute",
    "pages-per-minute-color",
    "pdf-k-octets-supported",		// CUPS extension
    "pdf-features-supported",		// IPP 3D
    "pdf-versions-supported",		// CUPS extension
    "pdl-override-supported",
    "platform-shape",			// IPP 3D
    "pkcs7-document-format-supported",	// IPP TRUSTNOONE
    "port-monitor",			// CUPS extension
    "port-monitor-supported",		// CUPS extension
    "preferred-attributes-supported",
    "printer-alert",
    "printer-alert-description",
    "printer-camera-image-uri",		// IPP 3D
    "printer-charge-info",
    "printer-charge-info-uri",
    "printer-commands",			// CUPS extension
    "printer-config-change-date-time",
    "printer-config-change-time",
    "printer-config-changes",		// IPP System
    "printer-contact-col",		// IPP System
    "printer-current-time",
    "printer-detailed-status-messages",	// IPP EPX
    "printer-device-id",
    "printer-dns-sd-name",		// CUPS extension
    "printer-driver-installer",
    "printer-fax-log-uri",		// IPP FaxOut
    "printer-fax-modem-info",		// IPP FaxOut
    "printer-fax-modem-name",		// IPP FaxOut
    "printer-fax-modem-number",		// IPP FaxOut
    "printer-finisher",			// IPP FIN
    "printer-finisher-description",	// IPP FIN
    "printer-finisher-supplies",	// IPP FIN
    "printer-finisher-supplies-description",
					// IPP FIN
    "printer-firmware-name",		// PWG 5110.1
    "printer-firmware-patches",		// PWG 5110.1
    "printer-firmware-string-version",	// PWG 5110.1
    "printer-firmware-version",		// PWG 5110.1
    "printer-geo-location",
    "printer-get-attributes-supported",
    "printer-icc-profiles",
    "printer-icons",
    "printer-id",			// IPP System
    "printer-info",
    "printer-input-tray",		// IPP NODRIVER
    "printer-is-accepting-jobs",
    "printer-is-shared",		// CUPS extension
    "printer-is-temporary",		// CUPS extension
    "printer-kind",			// IPP Paid Printing
    "printer-location",
    "printer-make-and-model",
    "printer-mandatory-job-attributes",
    "printer-message-date-time",
    "printer-message-from-operator",
    "printer-message-time",
    "printer-more-info",
    "printer-more-info-manufacturer",
    "printer-name",
    "printer-organization",
    "printer-organizational-unit",
    "printer-output-tray",		// IPP NODRIVER
    "printer-pkcs7-public-key",		// IPP TRUSTNOONE
    "printer-pkcs7-repertoire-configured",
					// IPP TRUSTNOONE
    "printer-pkcs7-repertoire-supported",
					// IPP TRUSTNOONE
    "printer-service-type",		// IPP System
    "printer-settable-attributes-supported",
					// RFC 3380
    "printer-service-contact-col",	// IPP EPX
    "printer-state",
    "printer-state-change-date-time",
    "printer-state-change-time",
    "printer-state-message",
    "printer-state-reasons",
    "printer-storage",			// IPP EPX
    "printer-storage-description",	// IPP EPX
    "printer-strings-languages-supported",
					// IPP NODRIVER
    "printer-strings-uri",		// IPP NODRIVER
    "printer-supply",
    "printer-supply-description",
    "printer-supply-info-uri",
    "printer-type",			// CUPS extension
    "printer-up-time",
    "printer-uri-supported",
    "printer-uuid",
    "printer-wifi-ssid",		// AirPrint extension
    "printer-wifi-state",		// AirPrint extension
    "printer-xri-supported",
    "proof-copies-supported",		// IPP EPX
    "proof-print-copies-supported",	// IPP EPX
    "pwg-raster-document-resolution-supported",
					// PWG Raster
    "pwg-raster-document-sheet-back",	// PWG Raster
    "pwg-raster-document-type-supported",
					// PWG Raster
    "queued-job-count",
    "reference-uri-schemes-supported",
    "repertoire-supported",
    "requesting-user-name-allowed",	// CUPS extension
    "requesting-user-name-denied",	// CUPS extension
    "requesting-user-uri-supported",
    "smi2699-auth-print-group",		// PWG ippserver extension
    "smi2699-auth-proxy-group",		// PWG ippserver extension
    "smi2699-device-command",		// PWG ippserver extension
    "smi2699-device-format",		// PWG ippserver extension
    "smi2699-device-name",		// PWG ippserver extension
    "smi2699-device-uri",		// PWG ippserver extension
    "subordinate-printers-supported",
    "subscription-privacy-attributes",	// IPP Privacy Attributes
    "subscription-privacy-scope",	// IPP Privacy Attributes
    "trimming-offset-supported",	// IPP FIN
    "trimming-reference-edge-supported",// IPP FIN
    "trimming-type-supported",		// IPP FIN
    "trimming-when-supported",		// IPP FIN
    "urf-supported",			// AirPrint
    "uri-authentication-supported",
    "uri-security-supported",
    "which-jobs-supported",		// IPP JobExt
    "xri-authentication-supported",
    "xri-security-supported",
    "xri-uri-scheme-supported"
  };
  static const char * const resource_description[] =
  {					// resource-description group - IPP System
    "resource-info",
    "resource-name"
  };
  static const char * const resource_status[] =
  {					// resource-status group - IPP System
    "date-time-at-canceled",
    "date-time-at-creation",
    "date-time-at-installed",
    "resource-data-uri",
    "resource-format",
    "resource-id",
    "resource-k-octets",
    "resource-state",
    "resource-state-message",
    "resource-state-reasons",
    "resource-string-version",
    "resource-type",
    "resource-use-count",
    "resource-uuid",
    "resource-version",
    "time-at-canceled",
    "time-at-creation",
    "time-at-installed"
  };
  static const char * const resource_template[] =
  {					// resource-template group - IPP System
    "resource-format",
    "resource-format-supported",
    "resource-info",
    "resource-name",
    "resource-type",
    "resource-type-supported"
  };
  static const char * const subscription_description[] =
  {					// subscription-description group
    "notify-job-id",
    "notify-lease-expiration-time",
    "notify-printer-up-time",
    "notify-printer-uri",
    "notify-resource-id",		// IPP System
    "notify-system-uri",		// IPP System
    "notify-sequence-number",
    "notify-subscriber-user-name",
    "notify-subscriber-user-uri",
    "notify-subscription-id",
    "notify-subscription-uuid"		// IPP NODRIVER
  };
  static const char * const subscription_template[] =
  {					// subscription-template group
    "notify-attributes",
    "notify-attributes-supported",
    "notify-charset",
    "notify-events",
    "notify-events-default",
    "notify-events-supported",
    "notify-lease-duration",
    "notify-lease-duration-default",
    "notify-lease-duration-supported",
    "notify-max-events-supported",
    "notify-natural-language",
    "notify-pull-method",
    "notify-pull-method-supported",
    "notify-recipient-uri",
    "notify-schemes-supported",
    "notify-time-interval",
    "notify-user-data"
  };
  static const char * const system_description[] =
  {					// system-description group - IPP System
    "charset-configured",
    "charset-supported",
    "document-format-supported",
    "generated-natural-language-supported",
    "ipp-features-supported",
    "ipp-versions-supported",
    "ippget-event-life",
    "multiple-document-printers-supported",
    "natural-language-configured",
    "notify-attributes-supported",
    "notify-events-default",
    "notify-events-supported",
    "notify-lease-duration-default",
    "notify-lease-duration-supported",
    "notify-max-events-supported",
    "notify-pull-method-supported",
    "operations-supported",
    "power-calendar-policy-col",
    "power-event-policy-col",
    "power-timeout-policy-col",
    "printer-creation-attributes-supported",
    "printer-service-type-supported",
    "resource-format-supported",
    "resource-type-supported",
    "resource-settable-attributes-supported",
    "smi2699-auth-group-supported",	// PWG ippserver extension
    "smi2699-device-command-supported",	// PWG ippserver extension
    "smi2699-device-format-format",	// PWG ippserver extension
    "smi2699-device-uri-schemes-supported",
					// PWG ippserver extension
    "system-contact-col",
    "system-current-time",
    "system-default-printer-id",
    "system-geo-location",
    "system-info",
    "system-location",
    "system-mandatory-printer-attributes",
    "system-make-and-model",
    "system-message-from-operator",
    "system-name",
    "system-owner-col",
    "system-settable-attributes-supported",
    "system-strings-languages-supported",
    "system-strings-uri",
    "system-xri-supported"
  };
  static const char * const system_status[] =
  {					// system-status group - IPP System
    "power-log-col",
    "power-state-capabilities-col",
    "power-state-counters-col",
    "power-state-monitor-col",
    "power-state-transitions-col",
    "system-config-change-date-time",
    "system-config-change-time",
    "system-config-changes",
    "system-configured-printers",
    "system-configured-resources",
    "system-firmware-name",
    "system-firmware-patches",
    "system-firmware-string-version",
    "system-firmware-version",
    "system-impressions-completed",
    "system-impressions-completed-col",
    "system-media-sheets-completed",
    "system-media-sheets-completed-col",
    "system-pages-completed",
    "system-pages-completed-col",
    "system-resident-application-name",
    "system-resident-application-patches",
    "system-resident-application-string-version",
    "system-resident-application-version",
    "system-serial-number",
    "system-state",
    "system-state-change-date-time",
    "system-state-change-time",
    "system-state-message",
    "system-state-reasons",
    "system-time-source-configured",
    "system-up-time",
    "system-user-application-name",
    "system-user-application-patches",
    "system-user-application-string-version",
    "system-user-application-version",
    "system-uuid",
    "xri-authentication-supported",
    "xri-security-supported",
    "xri-uri-scheme-supported"
  };


  // Get the requested-attributes attribute...
  op = ippGetOperation(request);

  if ((requested = ippFindAttribute(request, "requested-attributes", IPP_TAG_KEYWORD)) == NULL)
  {
    // The Get-Jobs operation defaults to "job-id" and "job-uri", and
    // Get-Documents defaults to "document-number", while all others default to
    // "all"...
    if (op == IPP_OP_GET_JOBS)
    {
      ra = cupsArrayNew(_cupsArrayStrcmp, NULL);
      cupsArrayAdd(ra, "job-id");
      cupsArrayAdd(ra, "job-uri");

      return (ra);
    }
    else if (op == IPP_OP_GET_DOCUMENTS)
    {
      ra = cupsArrayNew(_cupsArrayStrcmp, NULL);
      cupsArrayAdd(ra, "document-number");

      return (ra);
    }
    else
    {
      return (NULL);
    }
  }

  // If the attribute contains a single "all" keyword, return NULL...
  count = ippGetCount(requested);
  if (count == 1 && !strcmp(ippGetString(requested, 0, NULL), "all"))
    return (NULL);

  // Create an array using "strcmp" as the comparison function...
  ra = cupsArrayNew(_cupsArrayStrcmp, NULL);

  for (i = 0; i < count; i ++)
  {
    added = false;
    value = ippGetString(requested, i, NULL);

    if (!strcmp(value, "document-description") || (!strcmp(value, "all") && (op == IPP_OP_GET_JOB_ATTRIBUTES || op == IPP_OP_GET_JOBS || op == IPP_OP_GET_DOCUMENT_ATTRIBUTES || op == IPP_OP_GET_DOCUMENTS)))
    {
      for (j = 0; j < (sizeof(document_description) / sizeof(document_description[0])); j ++)
        cupsArrayAdd(ra, (void *)document_description[j]);

      added = true;
    }

    if (!strcmp(value, "document-template") || !strcmp(value, "all"))
    {
      for (j = 0; j < (sizeof(document_template) / sizeof(document_template[0])); j ++)
        cupsArrayAdd(ra, (void *)document_template[j]);

      added = true;
    }

    if (!strcmp(value, "job-description") || (!strcmp(value, "all") && (op == IPP_OP_GET_JOB_ATTRIBUTES || op == IPP_OP_GET_JOBS)))
    {
      for (j = 0; j < (sizeof(job_description) / sizeof(job_description[0])); j ++)
        cupsArrayAdd(ra, (void *)job_description[j]);

      added = true;
    }

    if (!strcmp(value, "job-template") || (!strcmp(value, "all") && (op == IPP_OP_GET_JOB_ATTRIBUTES || op == IPP_OP_GET_JOBS || op == IPP_OP_GET_PRINTER_ATTRIBUTES || op == IPP_OP_GET_OUTPUT_DEVICE_ATTRIBUTES)))
    {
      for (j = 0; j < (sizeof(job_template) / sizeof(job_template[0])); j ++)
        cupsArrayAdd(ra, (void *)job_template[j]);

      added = true;
    }

    if (!strcmp(value, "printer-description") || (!strcmp(value, "all") && (op == IPP_OP_GET_PRINTER_ATTRIBUTES || op == IPP_OP_GET_OUTPUT_DEVICE_ATTRIBUTES || op == IPP_OP_GET_PRINTERS || op == IPP_OP_CUPS_GET_DEFAULT || op == IPP_OP_CUPS_GET_PRINTERS || op == IPP_OP_CUPS_GET_CLASSES)))
    {
      for (j = 0; j < (sizeof(printer_description) / sizeof(printer_description[0])); j ++)
        cupsArrayAdd(ra, (void *)printer_description[j]);

      added = true;
    }

    if (!strcmp(value, "resource-description") || (!strcmp(value, "all") && (op == IPP_OP_GET_RESOURCE_ATTRIBUTES || op == IPP_OP_GET_RESOURCES)))
    {
      for (j = 0; j < (sizeof(resource_description) / sizeof(resource_description[0])); j ++)
        cupsArrayAdd(ra, (void *)resource_description[j]);

      added = true;
    }

    if (!strcmp(value, "resource-status") || (!strcmp(value, "all") && (op == IPP_OP_GET_RESOURCE_ATTRIBUTES || op == IPP_OP_GET_RESOURCES)))
    {
      for (j = 0; j < (sizeof(resource_status) / sizeof(resource_status[0])); j ++)
        cupsArrayAdd(ra, (void *)resource_status[j]);

      added = true;
    }

    if (!strcmp(value, "resource-template") || (!strcmp(value, "all") && (op == IPP_OP_GET_RESOURCE_ATTRIBUTES || op == IPP_OP_GET_RESOURCES || op == IPP_OP_GET_SYSTEM_ATTRIBUTES)))
    {
      for (j = 0; j < (sizeof(resource_template) / sizeof(resource_template[0])); j ++)
        cupsArrayAdd(ra, (void *)resource_template[j]);

      added = true;
    }

    if (!strcmp(value, "subscription-description") || (!strcmp(value, "all") && (op == IPP_OP_GET_SUBSCRIPTION_ATTRIBUTES || op == IPP_OP_GET_SUBSCRIPTIONS)))
    {
      for (j = 0; j < (sizeof(subscription_description) / sizeof(subscription_description[0])); j ++)
        cupsArrayAdd(ra, (void *)subscription_description[j]);

      added = true;
    }

    if (!strcmp(value, "subscription-template") || (!strcmp(value, "all") && (op == IPP_OP_GET_SUBSCRIPTION_ATTRIBUTES || op == IPP_OP_GET_SUBSCRIPTIONS)))
    {
      for (j = 0; j < (sizeof(subscription_template) / sizeof(subscription_template[0])); j ++)
        cupsArrayAdd(ra, (void *)subscription_template[j]);

      added = true;
    }

    if (!strcmp(value, "system-description") || (!strcmp(value, "all") && op == IPP_OP_GET_SYSTEM_ATTRIBUTES))
    {
      for (j = 0; j < (sizeof(system_description) / sizeof(system_description[0])); j ++)
        cupsArrayAdd(ra, (void *)system_description[j]);

      added = true;
    }

    if (!strcmp(value, "system-status") || (!strcmp(value, "all") && op == IPP_OP_GET_SYSTEM_ATTRIBUTES))
    {
      for (j = 0; j < (sizeof(system_status) / sizeof(system_status[0])); j ++)
        cupsArrayAdd(ra, (void *)system_status[j]);

      added = true;
    }

    if (!added)
      cupsArrayAdd(ra, (void *)value);
  }

  return (ra);
}


//
// 'ippEnumString()' - Return a string corresponding to the enum value.
//

const char *				// O - Enum string
ippEnumString(const char *attrname,	// I - Attribute name
              int        enumvalue)	// I - Enum value
{
  _cups_globals_t *cg = _cupsGlobals();	// Pointer to library globals


  // Check for standard enum values...
  if (!strcmp(attrname, "document-state") && enumvalue >= 3 && enumvalue < (3 + (int)(sizeof(ipp_document_states) / sizeof(ipp_document_states[0]))))
    return (ipp_document_states[enumvalue - 3]);
  else if (!strcmp(attrname, "finishings") || !strcmp(attrname, "finishings-actual") || !strcmp(attrname, "finishings-default") || !strcmp(attrname, "finishings-ready") || !strcmp(attrname, "finishings-supported") || !strcmp(attrname, "job-finishings") || !strcmp(attrname, "job-finishings-default") || !strcmp(attrname, "job-finishings-supported"))
  {
    if (enumvalue >= 3 && enumvalue < (3 + (int)(sizeof(ipp_finishings) / sizeof(ipp_finishings[0]))))
      return (ipp_finishings[enumvalue - 3]);
    else if (enumvalue >= 0x40000000 && enumvalue < (0x40000000 + (int)(sizeof(ipp_finishings_vendor) / sizeof(ipp_finishings_vendor[0]))))
      return (ipp_finishings_vendor[enumvalue - 0x40000000]);
  }
  else if (!strcmp(attrname, "job-state") && enumvalue >= IPP_JSTATE_PENDING && enumvalue <= IPP_JSTATE_COMPLETED)
    return (ipp_job_states[enumvalue - IPP_JSTATE_PENDING]);
  else if (!strcmp(attrname, "operations-supported"))
    return (ippOpString((ipp_op_t)enumvalue));
  else if ((!strcmp(attrname, "orientation-requested") || !strcmp(attrname, "orientation-requested-actual") || !strcmp(attrname, "orientation-requested-default") || !strcmp(attrname, "orientation-requested-supported")) && enumvalue >= 3 && enumvalue < (3 + (int)(sizeof(ipp_orientation_requesteds) / sizeof(ipp_orientation_requesteds[0]))))
    return (ipp_orientation_requesteds[enumvalue - 3]);
  else if ((!strcmp(attrname, "print-quality") || !strcmp(attrname, "print-quality-actual") || !strcmp(attrname, "print-quality-default") || !strcmp(attrname, "print-quality-supported")) && enumvalue >= 3 && enumvalue < (3 + (int)(sizeof(ipp_print_qualities) / sizeof(ipp_print_qualities[0]))))
    return (ipp_print_qualities[enumvalue - 3]);
  else if (!strcmp(attrname, "printer-state") && enumvalue >= IPP_PSTATE_IDLE && enumvalue <= IPP_PSTATE_STOPPED)
    return (ipp_printer_states[enumvalue - IPP_PSTATE_IDLE]);
  else if (!strcmp(attrname, "resource-state") && enumvalue >= IPP_RSTATE_PENDING && enumvalue <= IPP_RSTATE_ABORTED)
    return (ipp_resource_states[enumvalue - IPP_RSTATE_PENDING]);
  else if (!strcmp(attrname, "system-state") && enumvalue >= IPP_SSTATE_IDLE && enumvalue <= IPP_SSTATE_STOPPED)
    return (ipp_system_states[enumvalue - IPP_SSTATE_IDLE]);

  // Not a standard enum value, just return the decimal equivalent...
  snprintf(cg->ipp_unknown, sizeof(cg->ipp_unknown), "%d", enumvalue);
  return (cg->ipp_unknown);
}


//
// 'ippEnumValue()' - Return the value associated with a given enum string.
//

int					// O - Enum value or `-1` if unknown
ippEnumValue(const char *attrname,	// I - Attribute name
             const char *enumstring)	// I - Enum string
{
  size_t	i,			// Looping var
		num_strings;		// Number of strings to compare
  const char * const *strings;		// Strings to compare


  // If the string is just a number, return it...
  if (isdigit(*enumstring & 255))
    return ((int)strtol(enumstring, NULL, 0));

  // Otherwise look up the string...
  if (!strcmp(attrname, "document-state"))
  {
    num_strings = sizeof(ipp_document_states) / sizeof(ipp_document_states[0]);
    strings     = ipp_document_states;
  }
  else if (!strcmp(attrname, "finishings") ||
	   !strcmp(attrname, "finishings-actual") ||
	   !strcmp(attrname, "finishings-default") ||
	   !strcmp(attrname, "finishings-ready") ||
	   !strcmp(attrname, "finishings-supported"))
  {
    for (i = 0; i < (sizeof(ipp_finishings_vendor) / sizeof(ipp_finishings_vendor[0])); i ++)
    {
      if (!strcmp(enumstring, ipp_finishings_vendor[i]))
	return (i + 0x40000000);
    }

    num_strings = sizeof(ipp_finishings) / sizeof(ipp_finishings[0]);
    strings     = ipp_finishings;
  }
  else if (!strcmp(attrname, "job-state"))
  {
    num_strings = sizeof(ipp_job_states) / sizeof(ipp_job_states[0]);
    strings     = ipp_job_states;
  }
  else if (!strcmp(attrname, "operations-supported"))
  {
    return (ippOpValue(enumstring));
  }
  else if (!strcmp(attrname, "orientation-requested") ||
           !strcmp(attrname, "orientation-requested-actual") ||
           !strcmp(attrname, "orientation-requested-default") ||
           !strcmp(attrname, "orientation-requested-supported"))
  {
    num_strings = sizeof(ipp_orientation_requesteds) / sizeof(ipp_orientation_requesteds[0]);
    strings     = ipp_orientation_requesteds;
  }
  else if (!strcmp(attrname, "print-quality") ||
           !strcmp(attrname, "print-quality-actual") ||
           !strcmp(attrname, "print-quality-default") ||
           !strcmp(attrname, "print-quality-supported"))
  {
    num_strings = sizeof(ipp_print_qualities) / sizeof(ipp_print_qualities[0]);
    strings     = ipp_print_qualities;
  }
  else if (!strcmp(attrname, "printer-state"))
  {
    num_strings = sizeof(ipp_printer_states) / sizeof(ipp_printer_states[0]);
    strings     = ipp_printer_states;
  }
  else if (!strcmp(attrname, "resource-state"))
  {
    num_strings = sizeof(ipp_resource_states) / sizeof(ipp_resource_states[0]);
    strings     = ipp_resource_states;
  }
  else if (!strcmp(attrname, "system-state"))
  {
    num_strings = sizeof(ipp_system_states) / sizeof(ipp_system_states[0]);
    strings     = ipp_system_states;
  }
  else
  {
    return (-1);
  }

  for (i = 0; i < num_strings; i ++)
  {
    if (!strcmp(enumstring, strings[i]))
      return (i + 3);
  }

  return (-1);
}


//
// 'ippErrorString()' - Return a name for the given status code.
//

const char *				// O - Text string
ippErrorString(ipp_status_t error)	// I - Error status
{
  _cups_globals_t *cg = _cupsGlobals();	// Pointer to library globals


  // See if the error code is a known value...
  if (error >= IPP_STATUS_OK && error <= IPP_STATUS_OK_EVENTS_COMPLETE)
    return (ipp_status_oks[error]);
  else if (error == IPP_STATUS_REDIRECTION_OTHER_SITE)
    return ("redirection-other-site");
  else if (error == IPP_STATUS_CUPS_SEE_OTHER)
    return ("cups-see-other");
  else if (error >= IPP_STATUS_ERROR_BAD_REQUEST && error <= IPP_STATUS_ERROR_ACCOUNT_AUTHORIZATION_FAILED)
    return (ipp_status_400s[error - IPP_STATUS_ERROR_BAD_REQUEST]);
  else if (error >= IPP_STATUS_ERROR_INTERNAL && error <= IPP_STATUS_ERROR_TOO_MANY_DOCUMENTS)
    return (ipp_status_500s[error - IPP_STATUS_ERROR_INTERNAL]);
  else if (error >= IPP_STATUS_ERROR_CUPS_AUTHENTICATION_CANCELED && error <= IPP_STATUS_ERROR_CUPS_OAUTH)
    return (ipp_status_1000s[error - IPP_STATUS_ERROR_CUPS_AUTHENTICATION_CANCELED]);

  // No, build an "0xxxxx" error string...
  snprintf(cg->ipp_unknown, sizeof(cg->ipp_unknown), "0x%04x", error);

  return (cg->ipp_unknown);
}


//
// 'ippErrorValue()' - Return a status code for the given name.
//
// @since CUPS 1.2@
//

ipp_status_t				// O - IPP status code
ippErrorValue(const char *name)		// I - Name
{
  size_t	i;			// Looping var


  for (i = 0; i < (sizeof(ipp_status_oks) / sizeof(ipp_status_oks[0])); i ++)
  {
    if (!_cups_strcasecmp(name, ipp_status_oks[i]))
      return ((ipp_status_t)i);
  }

  if (!_cups_strcasecmp(name, "redirection-other-site"))
    return (IPP_STATUS_REDIRECTION_OTHER_SITE);

  if (!_cups_strcasecmp(name, "cups-see-other"))
    return (IPP_STATUS_CUPS_SEE_OTHER);

  for (i = 0; i < (sizeof(ipp_status_400s) / sizeof(ipp_status_400s[0])); i ++)
  {
    if (!_cups_strcasecmp(name, ipp_status_400s[i]))
      return ((ipp_status_t)(i + 0x400));
  }

  for (i = 0; i < (sizeof(ipp_status_500s) / sizeof(ipp_status_500s[0])); i ++)
  {
    if (!_cups_strcasecmp(name, ipp_status_500s[i]))
      return ((ipp_status_t)(i + 0x500));
  }

  for (i = 0; i < (sizeof(ipp_status_1000s) / sizeof(ipp_status_1000s[0])); i ++)
  {
    if (!_cups_strcasecmp(name, ipp_status_1000s[i]))
      return ((ipp_status_t)(i + 0x1000));
  }

  return ((ipp_status_t)-1);
}


//
// 'ippGetPort()' - Return the default IPP port number.
//
// @since CUPS 2.5@
//

int					// O - Port number
ippGetPort(void)
{
  _cups_globals_t *cg = _cupsGlobals();	// Pointer to library globals


  DEBUG_puts("ippPort()");

  if (!cg->client_conf_loaded)
    _cupsSetDefaults();

  DEBUG_printf("1ippPort: Returning %d...", cg->ipp_port);

  return (cg->ipp_port);
}


//
// 'ippOpString()' - Return a name for the given operation id.
//
// @since CUPS 1.2@
//

const char *				// O - Name
ippOpString(ipp_op_t op)		// I - Operation ID
{
  _cups_globals_t *cg = _cupsGlobals();	// Pointer to library globals


  // See if the operation ID is a known value...
  if (op >= IPP_OP_PRINT_JOB && op < (ipp_op_t)(sizeof(ipp_std_ops) / sizeof(ipp_std_ops[0])))
    return (ipp_std_ops[op]);
  else if (op == IPP_OP_PRIVATE)
    return ("windows-ext");
  else if (op >= IPP_OP_CUPS_GET_DEFAULT && op <= IPP_OP_CUPS_GET_PPD)
    return (ipp_cups_ops[op - IPP_OP_CUPS_GET_DEFAULT]);
  else if (op >= IPP_OP_CUPS_GET_DOCUMENT && op <= IPP_OP_CUPS_CREATE_LOCAL_PRINTER)
    return (ipp_cups_ops2[op - IPP_OP_CUPS_GET_DOCUMENT]);

  // No, build an "0xxxxx" operation string...
  snprintf(cg->ipp_unknown, sizeof(cg->ipp_unknown), "0x%04x", op);

  return (cg->ipp_unknown);
}


//
// 'ippOpValue()' - Return an operation id for the given name.
//
// @since CUPS 1.2@
//

ipp_op_t				// O - Operation ID
ippOpValue(const char *name)		// I - Textual name
{
  size_t	i;			// Looping var


  if (!strncmp(name, "0x", 2))
    return ((ipp_op_t)strtol(name + 2, NULL, 16));

  for (i = 0; i < (sizeof(ipp_std_ops) / sizeof(ipp_std_ops[0])); i ++)
  {
    if (!_cups_strcasecmp(name, ipp_std_ops[i]))
      return ((ipp_op_t)i);
  }

  if (!_cups_strcasecmp(name, "windows-ext"))
    return (IPP_OP_PRIVATE);

  for (i = 0; i < (sizeof(ipp_cups_ops) / sizeof(ipp_cups_ops[0])); i ++)
  {
    if (!_cups_strcasecmp(name, ipp_cups_ops[i]))
      return ((ipp_op_t)(i + 0x4001));
  }

  for (i = 0; i < (sizeof(ipp_cups_ops2) / sizeof(ipp_cups_ops2[0])); i ++)
  {
    if (!_cups_strcasecmp(name, ipp_cups_ops2[i]))
      return ((ipp_op_t)(i + 0x4027));
  }

  if (!_cups_strcasecmp(name, "Create-Job-Subscription"))
    return (IPP_OP_CREATE_JOB_SUBSCRIPTIONS);

  if (!_cups_strcasecmp(name, "Create-Printer-Subscription"))
    return (IPP_OP_CREATE_PRINTER_SUBSCRIPTIONS);

  if (!_cups_strcasecmp(name, "CUPS-Add-Class"))
    return (IPP_OP_CUPS_ADD_MODIFY_CLASS);

  if (!_cups_strcasecmp(name, "CUPS-Add-Printer"))
    return (IPP_OP_CUPS_ADD_MODIFY_PRINTER);

  return (IPP_OP_CUPS_INVALID);
}


//
// 'ippPort()' - Return the default IPP port number.
//
// @deprecated@ @exclude all@
//

int					// O - Port number
ippPort(void)
{
  return (ippGetPort());
}


//
// 'ippSetPort()' - Set the default port number.
//

void
ippSetPort(int p)			// I - Port number to use
{
  DEBUG_printf("ippSetPort(p=%d)", p);

  _cupsGlobals()->ipp_port = p;
}


//
// 'ippStateString()' - Return the name corresponding to a state value.
//
// @since CUPS 2.0/OS 10.10@
//

const char *				// O - State name
ippStateString(ipp_state_t state)	// I - State value
{
  if (state >= IPP_STATE_ERROR && state <= IPP_STATE_DATA)
    return (ipp_states[state - IPP_STATE_ERROR]);
  else
    return ("UNKNOWN");
}


//
// 'ippTagString()' - Return the tag name corresponding to a tag value.
//
// The returned names are defined in RFC 8011 and the IANA IPP Registry.
//
// @since CUPS 1.4@
//

const char *				// O - Tag name
ippTagString(ipp_tag_t tag)		// I - Tag value
{
  tag &= IPP_TAG_CUPS_MASK;

  if (tag < (ipp_tag_t)(sizeof(ipp_tag_names) / sizeof(ipp_tag_names[0])))
    return (ipp_tag_names[tag]);
  else
    return ("UNKNOWN");
}


//
// 'ippTagValue()' - Return the tag value corresponding to a tag name.
//
// The tag names are defined in RFC 8011 and the IANA IPP Registry.
//
// @since CUPS 1.4@
//

ipp_tag_t				// O - Tag value
ippTagValue(const char *name)		// I - Tag name
{
  size_t	i;			// Looping var


  for (i = 0; i < (sizeof(ipp_tag_names) / sizeof(ipp_tag_names[0])); i ++)
  {
    if (!_cups_strcasecmp(name, ipp_tag_names[i]))
      return ((ipp_tag_t)i);
  }

  if (!_cups_strcasecmp(name, "operation"))
    return (IPP_TAG_OPERATION);
  else if (!_cups_strcasecmp(name, "job"))
    return (IPP_TAG_JOB);
  else if (!_cups_strcasecmp(name, "printer"))
    return (IPP_TAG_PRINTER);
  else if (!_cups_strcasecmp(name, "unsupported"))
    return (IPP_TAG_UNSUPPORTED_GROUP);
  else if (!_cups_strcasecmp(name, "subscription"))
    return (IPP_TAG_SUBSCRIPTION);
  else if (!_cups_strcasecmp(name, "event"))
    return (IPP_TAG_EVENT_NOTIFICATION);
  else if (!_cups_strcasecmp(name, "language"))
    return (IPP_TAG_LANGUAGE);
  else if (!_cups_strcasecmp(name, "mimetype"))
    return (IPP_TAG_MIMETYPE);
  else if (!_cups_strcasecmp(name, "name"))
    return (IPP_TAG_NAME);
  else if (!_cups_strcasecmp(name, "text"))
    return (IPP_TAG_TEXT);
  else if (!_cups_strcasecmp(name, "begCollection"))
    return (IPP_TAG_BEGIN_COLLECTION);
  else
    return (IPP_TAG_ZERO);
}


//
// 'ipp_col_string()' - Convert a collection to a string.
//

static size_t				// O - Number of bytes
ipp_col_string(ipp_t  *col,		// I - Collection attribute
               char   *buffer,		// I - Buffer or NULL
               size_t bufsize)		// I - Size of buffer
{
  char			*bufptr,	// Position in buffer
			*bufend,	// End of buffer
			prefix = '{',	// Prefix character
			temp[256];	// Temporary string
  ipp_attribute_t	*attr;		// Current member attribute


  if (!col)
  {
    if (buffer)
      *buffer = '\0';

    return (0);
  }

  bufptr = buffer;
  bufend = buffer + bufsize - 1;

  for (attr = col->attrs; attr; attr = attr->next)
  {
    if (!attr->name)
      continue;

    if (buffer && bufptr < bufend)
      *bufptr = prefix;
    bufptr ++;
    prefix = ' ';

    if (buffer && bufptr < bufend)
      bufptr += snprintf(bufptr, (size_t)(bufend - bufptr + 1), "%s=", attr->name);
    else
      bufptr += strlen(attr->name) + 1;

    if (buffer && bufptr < bufend)
      bufptr += ippAttributeString(attr, bufptr, (size_t)(bufend - bufptr + 1));
    else
      bufptr += ippAttributeString(attr, temp, sizeof(temp));
  }

  if (prefix == '{')
  {
    if (buffer && bufptr < bufend)
      *bufptr = prefix;
    bufptr ++;
  }

  if (buffer && bufptr < bufend)
    *bufptr = '}';
  bufptr ++;

  return ((size_t)(bufptr - buffer));
}
