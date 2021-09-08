/*
 * Directory services definitions for the CUPS scheduler.
 *
 * Copyright © 2021-2022 by OpenPrinting.
 * Copyright © 2007-2017 by Apple Inc.
 * Copyright © 1997-2007 by Easy Software Products, all rights reserved.
 *
 * Licensed under Apache License v2.0.  See the file "LICENSE" for more
 * information.
 */

#ifndef _DIRSVC_H
#define _DIRSVC_H
/*
 * Browse protocols...
 */

#define BROWSE_DNSSD	1		/* DNS Service Discovery (aka Bonjour) */
#define BROWSE_ALL	1		/* All protocols */


/*
 * Globals...
 */

VAR int			Browsing	VALUE(TRUE),
					/* Whether or not browsing is enabled */
			BrowseWebIF	VALUE(FALSE),
					/* Whether the web interface is advertised */
			BrowseLocalProtocols
					VALUE(BROWSE_ALL);
					/* Protocols to support for local printers */
#ifdef HAVE_DNSSD
VAR char		*DNSSDComputerName VALUE(NULL),
					/* Computer/server name */
			*DNSSDHostName	VALUE(NULL),
					/* Hostname */
			*DNSSDSubTypes VALUE(NULL);
					/* Bonjour registration subtypes */
VAR cups_array_t	*DNSSDAlias	VALUE(NULL);
					/* List of dynamic ServerAlias's */
VAR int			DNSSDPort	VALUE(0);
					/* Port number to register */
VAR cups_array_t	*DNSSDPrinters	VALUE(NULL);
					/* Printers we have registered */
#  ifdef HAVE_MDNSRESPONDER
VAR DNSServiceRef	DNSSDMaster	VALUE(NULL);
					/* Master DNS-SD service reference */
#  else /* HAVE_AVAHI */
VAR AvahiThreadedPoll	*DNSSDMaster	VALUE(NULL);
					/* Master polling interface for Avahi */
VAR AvahiClient		*DNSSDClient	VALUE(NULL);
					/* Client information */
#  endif /* HAVE_MDNSRESPONDER */
VAR cupsd_srv_t		WebIFSrv	VALUE(NULL);
					/* Service reference for the web interface */
#endif /* HAVE_DNSSD */


/*
 * Prototypes...
 */

extern void	cupsdDeregisterPrinter(cupsd_printer_t *p, int removeit);
extern void	cupsdRegisterPrinter(cupsd_printer_t *p);
extern void	cupsdStartBrowsing(void);
extern void	cupsdStopBrowsing(void);
#ifdef HAVE_DNSSD
extern void	cupsdUpdateDNSSDName(void);
#endif /* HAVE_DNSSD */
#endif /* _DIRSVC_H */
