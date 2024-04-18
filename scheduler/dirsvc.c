/*
 * Directory services routines for the CUPS scheduler.
 *
 * Copyright © 2020-2024 by OpenPrinting.
 * Copyright © 2007-2018 by Apple Inc.
 * Copyright © 1997-2007 by Easy Software Products, all rights reserved.
 *
 * Licensed under Apache License v2.0.  See the file "LICENSE" for more
 * information.
 */

#include "cupsd.h"
#include <grp.h>

#if defined(HAVE_MDNSRESPONDER) && defined(__APPLE__)
#  include <nameser.h>
#  include <CoreFoundation/CoreFoundation.h>
#  include <SystemConfiguration/SystemConfiguration.h>
#endif /* HAVE_MDNSRESPONDER && __APPLE__ */


/*
 * Local functions...
 */

static char		*get_auth_info_required(cupsd_printer_t *p, char *buffer, size_t bufsize);
static int		dnssdBuildTxtRecord(cupsd_printer_t *p, cups_option_t **txt);
static void		dnssdErrorCB(void *cb_data, const char *message);
static void		dnssdRegisterCallback(cups_dnssd_service_t *service, void *cb_data, cups_dnssd_flags_t flags);
static void		dnssdRegisterPrinter(cupsd_printer_t *p);
static void		dnssdStop(void);


/*
 * 'cupsdDeregisterPrinter()' - Stop sending broadcast information for a
 *				local printer and remove any pending
 *                              references to remote printers.
 */

void
cupsdDeregisterPrinter(
    cupsd_printer_t *p,			/* I - Printer to register */
    int             removeit)		/* I - Printer being permanently removed */
{
 /*
  * Only deregister if browsing is enabled and it's a local printer...
  */

  cupsdLogMessage(CUPSD_LOG_DEBUG, "cupsdDeregisterPrinter(p=%p(%s), removeit=%d)", (void *)p, p->name, removeit);

  if (!Browsing || !p->shared || (p->type & (CUPS_PTYPE_REMOTE | CUPS_PTYPE_SCANNER)))
    return;

 /*
  * Announce the deletion...
  */

  if (removeit && (BrowseLocalProtocols & BROWSE_DNSSD) && DNSSDContext)
  {
    cupsDNSSDServiceDelete(p->dnssd);
    p->dnssd = NULL;
  }
}


/*
 * 'cupsdRegisterPrinter()' - Start sending broadcast information for a
 *                            printer or update the broadcast contents.
 */

void
cupsdRegisterPrinter(cupsd_printer_t *p)/* I - Printer */
{
  cupsdLogMessage(CUPSD_LOG_DEBUG, "cupsdRegisterPrinter(p=%p(%s))", (void *)p, p->name);

  if (!Browsing || !BrowseLocalProtocols || (p->type & (CUPS_PTYPE_REMOTE | CUPS_PTYPE_SCANNER)))
    return;

  if ((BrowseLocalProtocols & BROWSE_DNSSD) && DNSSDContext)
    dnssdRegisterPrinter(p);
}


/*
 * 'cupsdStartBrowsing()' - Start sending and receiving broadcast information.
 */

void
cupsdStartBrowsing(void)
{
  cupsd_printer_t	*p;		/* Current printer */


  if (!Browsing || !BrowseLocalProtocols)
    return;

  if (BrowseLocalProtocols & BROWSE_DNSSD)
  {
    if ((DNSSDContext = cupsDNSSDNew(dnssdErrorCB, /*cb_data*/NULL)) == NULL)
    {
      if (FatalErrors & CUPSD_FATAL_BROWSE)
	cupsdEndProcess(getpid(), 0);

      return;
    }

   /*
    * Set the computer name and register the web interface...
    */

    DNSSDPort = 0;
    cupsdUpdateDNSSDName();

   /*
    * Register the individual printers
    */

    for (p = (cupsd_printer_t *)cupsArrayFirst(Printers); p; p = (cupsd_printer_t *)cupsArrayNext(Printers))
    {
      if (!(p->type & (CUPS_PTYPE_REMOTE | CUPS_PTYPE_SCANNER)))
	dnssdRegisterPrinter(p);
    }
  }
}


/*
 * 'cupsdStopBrowsing()' - Stop sending and receiving broadcast information.
 */

void
cupsdStopBrowsing(void)
{
  if (!Browsing || !BrowseLocalProtocols)
    return;

 /*
  * Shut down browsing sockets...
  */

  if ((BrowseLocalProtocols & BROWSE_DNSSD) && DNSSDContext)
    dnssdStop();
}


/*
 * 'cupsdUpdateDNSSDName()' - Update the computer name we use for browsing...
 */

void
cupsdUpdateDNSSDName(void)
{
  char	name[1024];			/* Computer/host name */


 /*
  * Only share the web interface and printers when non-local listening is
  * enabled...
  */

  if (!DNSSDPort)
  {
   /*
    * Get the port we use for registrations.  If we are not listening on any
    * non-local ports, there is no sense sharing local printers via Bonjour...
    */

    cupsd_listener_t	*lis;		/* Current listening socket */

    for (lis = (cupsd_listener_t *)cupsArrayFirst(Listeners); lis; lis = (cupsd_listener_t *)cupsArrayNext(Listeners))
    {
      if (httpAddrLocalhost(&(lis->address)))
	continue;

      DNSSDPort = httpAddrPort(&(lis->address));
      break;
    }
  }

  if (!DNSSDPort)
    return;

 /*
  * Get the computer name...
  */

  if (cupsDNSSDCopyComputerName(DNSSDContext, name, sizeof(name)) && name[0])
    cupsdSetString(&DNSSDComputerName, name);

  if (!DNSSDComputerName)
  {
   /*
    * Use the ServerName instead...
    */

    cupsdLogMessage(CUPSD_LOG_DEBUG, "Using ServerName \"%s\" as computer name.", ServerName);
    cupsdSetString(&DNSSDComputerName, ServerName);
  }

 /*
  * Get the hostname...
  */

  if (cupsDNSSDCopyHostName(DNSSDContext, name, sizeof(name)))
    cupsdSetString(&DNSSDHostName, name);

  if (!DNSSDHostName)
  {
    if (strchr(ServerName, '.'))
      cupsdSetString(&DNSSDHostName, ServerName);
    else
      cupsdSetStringf(&DNSSDHostName, "%s.local", ServerName);

    cupsdLogMessage(CUPSD_LOG_INFO, "Defaulting to \"DNSSDHostName %s\".", DNSSDHostName);
  }

 /*
  * Then (re)register the web interface if enabled...
  */

  cupsDNSSDServiceDelete(DNSSDWebIF);
  DNSSDWebIF = NULL;

  if (BrowseWebIF)
  {
    char	webif[1024];		/* Web interface share name */

    if (DNSSDComputerName)
      snprintf(webif, sizeof(webif), "CUPS @ %s", DNSSDComputerName);
    else
      cupsCopyString(webif, "CUPS", sizeof(webif));

    DNSSDWebIF = cupsDNSSDServiceNew(DNSSDContext, CUPS_DNSSD_IF_INDEX_ANY, webif, /*cb*/NULL, /*cb_data*/NULL);
    cupsDNSSDServiceAdd(DNSSDWebIF, "_http._tcp", /*domain*/NULL, DNSSDHostName, (uint16_t)DNSSDPort, /*num_txt*/0, /*txt*/NULL);
    cupsDNSSDServicePublish(DNSSDWebIF);
  }
}


/*
 * 'dnssdBuildTxtRecord()' - Build a TXT record from printer info.
 */

static int				/* O - Number of TXT key/value pairs */
dnssdBuildTxtRecord(
    cupsd_printer_t *p,			/* I - Printer information */
    cups_option_t   **txt)		/* O - TXT key/value pairs */
{
  int		i,			/* Looping var */
		num_txt;		/* Number of TXT key/value pairs */
  char		admin_hostname[256],	/* Hostname for admin page */
		rp[256],		/* RP value */
		value[256],		/* TXT value */
                *ptr;                   /* Pointer in string */
  cupsd_listener_t *lis;                /* Current listener */
  const char    *admin_scheme = "http"; /* Admin page URL scheme */
  ipp_attribute_t *urf_supported;	/* urf-supported attribute */


 /*
  * Load up the key value pairs...
  */

  num_txt = 0;
  *txt    = NULL;

  num_txt = cupsAddOption("txtvers", "1", num_txt, txt);
  num_txt = cupsAddOption("qtotal", "1", num_txt, txt);

  snprintf(rp, sizeof(rp), "%s/%s", (p->type & CUPS_PTYPE_CLASS) ? "classes" : "printers", p->name);
  num_txt = cupsAddOption("rp", rp, num_txt, txt);

  num_txt = cupsAddOption("ty", p->make_model ? p->make_model : "Unknown", num_txt, txt);

 /*
  * Get the hostname for the admin page...
  */

  if (strchr(DNSSDHostName, '.'))
  {
   /*
    * Use the provided hostname, but make sure it ends with a period...
    */

    if ((ptr = DNSSDHostName + strlen(DNSSDHostName) - 1) >= DNSSDHostName && *ptr == '.')
      cupsCopyString(admin_hostname, DNSSDHostName, sizeof(admin_hostname));
    else
      snprintf(admin_hostname, sizeof(admin_hostname), "%s.", DNSSDHostName);
  }
  else
  {
   /*
    * Unqualified hostname gets ".local." added to it...
    */

    snprintf(admin_hostname, sizeof(admin_hostname), "%s.local.", DNSSDHostName);
  }

 /*
  * Get the URL scheme for the admin page...
  */

  for (lis = (cupsd_listener_t *)cupsArrayFirst(Listeners); lis; lis = (cupsd_listener_t *)cupsArrayNext(Listeners))
  {
    if (lis->encryption != HTTP_ENCRYPTION_NEVER)
    {
      admin_scheme = "https";
      break;
    }
  }

  httpAssembleURIf(HTTP_URI_CODING_ALL, value, sizeof(value), admin_scheme,  NULL, admin_hostname, DNSSDPort, "/%s/%s", (p->type & CUPS_PTYPE_CLASS) ? "classes" : "printers", p->name);

  num_txt = cupsAddOption("adminurl", value, num_txt, txt);

  if (p->location)
    num_txt = cupsAddOption("note", p->location, num_txt, txt);

  num_txt = cupsAddOption("priority", "0", num_txt, txt);

  num_txt = cupsAddOption("product", p->pc && p->pc->product ? p->pc->product : "Unknown", num_txt, txt);

  num_txt = cupsAddOption("pdl", p->pdl ? p->pdl : "application/postscript", num_txt, txt);

  if (get_auth_info_required(p, value, sizeof(value)))
    num_txt = cupsAddOption("air", value, num_txt, txt);

  num_txt = cupsAddOption("UUID", p->uuid + 9, num_txt, txt);

  num_txt = cupsAddOption("TLS", "1.3", num_txt, txt);

  if ((urf_supported = ippFindAttribute(p->ppd_attrs, "urf-supported", IPP_TAG_KEYWORD)) != NULL)
  {
    int urf_count = ippGetCount(urf_supported);
					// Number of URF values

    value[0] = '\0';
    for (i = 0, ptr = value; i < urf_count; i ++)
    {
      const char *keyword = ippGetString(urf_supported, i, NULL);

      if (ptr > value && ptr < (value + sizeof(value) - 1))
	*ptr++ = ',';

      cupsCopyString(ptr, keyword, sizeof(value) - (size_t)(ptr - value));
      ptr += strlen(ptr);

      if (ptr >= (value + sizeof(value) - 1))
	break;
    }

    num_txt = cupsAddOption("URF", value, num_txt, txt);
  }

  num_txt = cupsAddOption("mopria-certified", "1.3", num_txt, txt);

  if (p->type & CUPS_PTYPE_FAX)
  {
    num_txt = cupsAddOption("Fax", "T", num_txt, txt);
    num_txt = cupsAddOption("rfo", rp, num_txt, txt);
  }

  if (p->type & CUPS_PTYPE_COLOR)
    num_txt = cupsAddOption("Color", (p->type & CUPS_PTYPE_COLOR) ? "T" : "F", num_txt, txt);

  if (p->type & CUPS_PTYPE_DUPLEX)
    num_txt = cupsAddOption("Duplex", (p->type & CUPS_PTYPE_DUPLEX) ? "T" : "F", num_txt, txt);

  if (p->type & CUPS_PTYPE_STAPLE)
    num_txt = cupsAddOption("Staple", (p->type & CUPS_PTYPE_STAPLE) ? "T" : "F", num_txt, txt);

  if (p->type & CUPS_PTYPE_COPIES)
    num_txt = cupsAddOption("Copies", (p->type & CUPS_PTYPE_COPIES) ? "T" : "F", num_txt, txt);

  if (p->type & CUPS_PTYPE_COLLATE)
    num_txt = cupsAddOption("Collate", (p->type & CUPS_PTYPE_COLLATE) ? "T" : "F", num_txt, txt);

  if (p->type & CUPS_PTYPE_PUNCH)
    num_txt = cupsAddOption("Punch", (p->type & CUPS_PTYPE_PUNCH) ? "T" : "F", num_txt, txt);

  if (p->type & CUPS_PTYPE_BIND)
    num_txt = cupsAddOption("Bind", (p->type & CUPS_PTYPE_BIND) ? "T" : "F", num_txt, txt);

  if (p->type & CUPS_PTYPE_SORT)
    num_txt = cupsAddOption("Sort", (p->type & CUPS_PTYPE_SORT) ? "T" : "F", num_txt, txt);

  if (p->type & CUPS_PTYPE_MFP)
    num_txt = cupsAddOption("Scan", (p->type & CUPS_PTYPE_MFP) ? "T" : "F", num_txt, txt);

  snprintf(value, sizeof(value), "0x%X", p->type | CUPS_PTYPE_REMOTE);
  num_txt = cupsAddOption("printer-type", value, num_txt, txt);

  return (num_txt);
}


//
// 'dnssdErrorCB()' - DNS-SD error callback.
//

static void
dnssdErrorCB(void       *cb_data,	// I - Callback data (unused)
             const char *message)	// I - Error message
{
  (void)cb_data;

  cupsdLogMessage(CUPSD_LOG_ERROR, "[DNS-SD] %s", message);
}


/*
 * 'dnssdRegisterCallback()' - Service registration callback.
 */

static void
dnssdRegisterCallback(
    cups_dnssd_service_t *service,	// I - Service
    void                 *cb_data,	// I - Callback data (printer)
    cups_dnssd_flags_t   flags)		// I - Registration flags
{
  cupsd_printer_t *p = (cupsd_printer_t *)cb_data;
					// Current printer
  const char	*reg_name;		// Updated service name


  if (flags & CUPS_DNSSD_FLAGS_ERROR)
    return;

  if (!p)
    return;

  reg_name = cupsDNSSDServiceGetName(service);

  if ((!p->reg_name || _cups_strcasecmp(reg_name, p->reg_name)))
  {
    cupsdLogMessage(CUPSD_LOG_INFO, "Using service name \"%s\" for \"%s\".", reg_name, p->name);

    cupsArrayRemove(DNSSDPrinters, p);
    cupsdSetString(&p->reg_name, reg_name);
    cupsArrayAdd(DNSSDPrinters, p);

    LastEvent |= CUPSD_EVENT_PRINTER_MODIFIED;
  }
}


/*
 * 'dnssdRegisterPrinter()' - Start sending broadcast information for a printer
 *		              or update the broadcast contents.
 */

static void
dnssdRegisterPrinter(
    cupsd_printer_t *p)			/* I - Printer */
{
  char		name[256],		/* Service name */
		regtype[256];		/* Registration type(s) */
  int		num_txt;		/* Number of IPP(S) TXT key/value pairs */
  cups_option_t	*txt;			/* IPP(S) TXT key/value pairs */
  bool		status;			/* Registration status */


  cupsdLogMessage(CUPSD_LOG_DEBUG2, "dnssdRegisterPrinter(%s)", p->name);

 /*
  * Remove the current registrations if we have them and then return if
  * per-printer sharing was just disabled...
  */

  cupsDNSSDServiceDelete(p->dnssd);
  p->dnssd = NULL;

  if (!p->shared)
    return;

 /*
  * Set the registered name as needed; the registered name takes the form of
  * "<printer-info> @ <computer name>"...
  */

  if (!p->reg_name)
  {
    if (p->info && strlen(p->info) > 0)
    {
      if (DNSSDComputerName)
	snprintf(name, sizeof(name), "%s @ %s", p->info, DNSSDComputerName);
      else
	cupsCopyString(name, p->info, sizeof(name));
    }
    else if (DNSSDComputerName)
    {
      snprintf(name, sizeof(name), "%s @ %s", p->name, DNSSDComputerName);
    }
    else
    {
      cupsCopyString(name, p->name, sizeof(name));
    }
  }
  else
  {
    cupsCopyString(name, p->reg_name, sizeof(name));
  }

 /*
  * Register IPP and LPD...
  *
  * We always must register the "_printer" service type in order to reserve
  * our name, but use port number 0 so that we don't have clients using LPD...
  */

  p->dnssd = cupsDNSSDServiceNew(DNSSDContext, CUPS_DNSSD_IF_INDEX_ANY, name, dnssdRegisterCallback, p);
  status   = p->dnssd != NULL;

  // LPD placeholder
  status &= cupsDNSSDServiceAdd(p->dnssd, "_printer._tcp", /*domain*/NULL, DNSSDHostName, /*port*/0, /*num_txt*/0, /*txt*/NULL);

  // IPP service
  num_txt = dnssdBuildTxtRecord(p, &txt);

  if (p->type & CUPS_PTYPE_FAX)
  {
    if (DNSSDSubTypes)
      snprintf(regtype, sizeof(regtype), "_fax-ipp._tcp,%s", DNSSDSubTypes);
    else
      cupsCopyString(regtype, "_fax-ipp._tcp", sizeof(regtype));
  }
  else
  {
    if (DNSSDSubTypes)
      snprintf(regtype, sizeof(regtype), "_ipp._tcp,%s", DNSSDSubTypes);
    else
      cupsCopyString(regtype, "_ipp._tcp", sizeof(regtype));
  }

  status &= cupsDNSSDServiceAdd(p->dnssd, regtype, /*domain*/NULL, DNSSDHostName, (uint16_t)DNSSDPort, num_txt, txt);

  // IPPS service
  if (DNSSDSubTypes)
    snprintf(regtype, sizeof(regtype), "_ipps._tcp,%s", DNSSDSubTypes);
  else
    cupsCopyString(regtype, "_ipps._tcp", sizeof(regtype));

  status &= cupsDNSSDServiceAdd(p->dnssd, regtype, /*domain*/NULL, DNSSDHostName, (uint16_t)DNSSDPort, num_txt, txt);

  cupsFreeOptions(num_txt, txt);

  if (status)
  {
    // Save the registered name and add the printer to the array of DNS-SD
    // printers...
    cupsdSetString(&p->reg_name, name);
    cupsArrayAdd(DNSSDPrinters, p);

    cupsdLogMessage(CUPSD_LOG_DEBUG2, "dnssdRegisterPrinter: Registered \"%s\" as \"%s\".", p->name, name);
  }
  else
  {
    // Registration failed for this printer...
    cupsDNSSDServiceDelete(p->dnssd);
    p->dnssd = NULL;

    cupsdLogMessage(CUPSD_LOG_DEBUG2, "dnssdRegisterPrinter: Unable to register \"%s\" as \"%s\".", p->name, name);
  }
}


/*
 * 'dnssdStop()' - Stop all DNS-SD registrations.
 */

static void
dnssdStop(void)
{
  cupsd_printer_t	*p;		/* Current printer */


 /*
  * De-register the individual printers
  */

  for (p = (cupsd_printer_t *)cupsArrayFirst(Printers); p; p = (cupsd_printer_t *)cupsArrayNext(Printers))
  {
    cupsDNSSDServiceDelete(p->dnssd);
    p->dnssd = NULL;
  }

 /*
  * Shutdown the rest of the service refs...
  */

  cupsDNSSDServiceDelete(DNSSDWebIF);
  DNSSDWebIF = NULL;

  cupsDNSSDDelete(DNSSDContext);
  DNSSDContext = NULL;

  cupsArrayDelete(DNSSDPrinters);
  DNSSDPrinters = NULL;

  DNSSDPort = 0;
}


/*
 * 'get_auth_info_required()' - Get the auth-info-required value to advertise.
 */

static char *				/* O - String or NULL if none */
get_auth_info_required(
    cupsd_printer_t *p,			/* I - Printer */
    char            *buffer,		/* I - Value buffer */
    size_t          bufsize)		/* I - Size of value buffer */
{
  cupsd_location_t *auth;		/* Pointer to authentication element */
  char		resource[1024];		/* Printer/class resource path */


 /*
  * If auth-info-required is set for this printer, return that...
  */

  if (p->num_auth_info_required > 0 && strcmp(p->auth_info_required[0], "none"))
  {
    int		i;			/* Looping var */
    char	*bufptr;		/* Pointer into buffer */

    for (i = 0, bufptr = buffer; i < p->num_auth_info_required; i ++)
    {
      if (bufptr >= (buffer + bufsize - 2))
	break;

      if (i)
	*bufptr++ = ',';

      cupsCopyString(bufptr, p->auth_info_required[i], bufsize - (size_t)(bufptr - buffer));
      bufptr += strlen(bufptr);
    }

    return (buffer);
  }

 /*
  * Figure out the authentication data requirements to advertise...
  */

  if (p->type & CUPS_PTYPE_CLASS)
    snprintf(resource, sizeof(resource), "/classes/%s", p->name);
  else
    snprintf(resource, sizeof(resource), "/printers/%s", p->name);

  if ((auth = cupsdFindBest(resource, HTTP_STATE_POST)) == NULL ||
      auth->type == CUPSD_AUTH_NONE)
    auth = cupsdFindPolicyOp(p->op_policy_ptr, IPP_OP_PRINT_JOB);

  if (auth)
  {
    int	auth_type;			/* Authentication type */

    if ((auth_type = auth->type) == CUPSD_AUTH_DEFAULT)
      auth_type = cupsdDefaultAuthType();

    switch (auth_type)
    {
      case CUPSD_AUTH_NONE :
          return (NULL);

      case CUPSD_AUTH_NEGOTIATE :
	  cupsCopyString(buffer, "negotiate", bufsize);
	  break;

      default :
	  cupsCopyString(buffer, "username,password", bufsize);
	  break;
    }

    return (buffer);
  }

  return ("none");
}
