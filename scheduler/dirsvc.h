/*
 * Directory services definitions for the CUPS scheduler.
 *
 * Copyright © 2020-2024 by OpenPrinting.
 * Copyright © 2007-2017 by Apple Inc.
 * Copyright © 1997-2007 by Easy Software Products, all rights reserved.
 *
 * Licensed under Apache License v2.0.  See the file "LICENSE" for more
 * information.
 */

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
VAR cups_dnssd_t	*DNSSDContext	VALUE(NULL);
					/* DNS-SD context */
VAR cups_array_t	*DNSSDPrinters	VALUE(NULL);
					/* Printers we have registered */
VAR cups_dnssd_service_t *DNSSDWebIF	VALUE(NULL);
					/* Web interface service */


/*
 * Prototypes...
 */

extern void	cupsdDeregisterPrinter(cupsd_printer_t *p, int removeit);
extern void	cupsdRegisterPrinter(cupsd_printer_t *p);
extern void	cupsdStartBrowsing(void);
extern void	cupsdStopBrowsing(void);
extern void	cupsdUpdateDNSSDName(void);
