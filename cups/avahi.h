/*
 * header file for modulation of discovery process
 
 * Copyright © 2021-2022 by OpenPrinting.
 * Copyright © 2020 by the IEEE-ISTO Printer Working Group
 * Copyright © 2008-2018 by Apple Inc.
 *
 * Licensed under Apache License v2.0.  See the file "LICENSE" for more
 * information.
 */

/*
 * Include necessary headers.
 */

#include <stddef.h>
#include <stdio.h>
#include <cups/cups-private.h>

#define _CUPS_NO_DEPRECATED
#include <errno.h>

#define HAVE_AVAHI 1

#ifdef _WIN32
#include <process.h>
#include <sys/timeb.h>
#else
#include <sys/wait.h>
#endif /* _WIN32 */
#include <regex.h>
#ifdef HAVE_MDNSRESPONDER
#include <dns_sd.h>
#elif defined(HAVE_AVAHI)
#include <avahi-client/client.h>
#include <avahi-client/lookup.h>
#include <avahi-common/simple-watch.h>
#include <avahi-common/domain.h>
#include <avahi-common/error.h>
#include <avahi-common/malloc.h>
#define kDNSServiceMaxDomainName AVAHI_DOMAIN_NAME_MAX
#endif /* HAVE_MDNSRESPONDER */


typedef struct avahi_srv_s		/* Service information */
{
#ifdef HAVE_MDNSRESPONDER
  DNSServiceRef	ref;			/* Service reference for query */
#elif defined(HAVE_AVAHI)
  AvahiServiceResolver *ref;		/* Resolver */
  //TODO
  /*why dont we keep service browser reference as well ?*/
#endif /* HAVE_MDNSRESPONDER */
  char		*name,			/* Service name */
		*domain,		/* Domain name */
		*regtype,		/* Registration type */
		*fullName,		/* Full name */
		*host,			/* Hostname */
		*resource,		/* Resource path */
		*uri;			/* URI */
  int		num_txt;		/* Number of TXT record keys */
  cups_option_t	*txt;			/* TXT record keys */
  int		port,			/* Port number */
		is_local,		/* Is a local service? */
		is_processed,		/* Did we process the service? */
		is_resolved;		/* Got the resolve data? */
} avahi_srv_t;

/*
	declare callbacks
*/

#ifdef HAVE_MDNSRESPONDER
static void DNSSD_API	(*_browseCallback)(DNSServiceRef sdRef, DNSServiceFlags flags, uint32_t interfaceIndex, DNSServiceErrorType errorCode, const char *serviceName, const char *regtype, const char *replyDomain, void *context) _CUPS_NONNULL(1,5,6,7,8);
static void DNSSD_API	(*_browseLocalCallback)(DNSServiceRef sdRef, DNSServiceFlags flags, uint32_t interfaceIndex, DNSServiceErrorType errorCode, const char *serviceName, const char *regtype, const char *replyDomain, void *context) _CUPS_NONNULL(1,5,6,7,8);
#elif defined(HAVE_AVAHI)
static void		(*_browseCallback)(AvahiServiceBrowser *browser,
					AvahiIfIndex interface,
					AvahiProtocol protocol,
					AvahiBrowserEvent event,
					const char *serviceName,
					const char *regtype,
					const char *replyDomain,
					AvahiLookupResultFlags flags,
					void *context);
static void		(*_clientCallback)(AvahiClient *client,
					AvahiClientState state,
					void *context);
#endif /* HAVE_MDNSRESPONDER */


#ifdef HAVE_MDNSRESPONDER
static void DNSSD_API	(*_resolveCallback)(DNSServiceRef sdRef, DNSServiceFlags flags, uint32_t interfaceIndex, DNSServiceErrorType errorCode, const char *fullName, const char *hostTarget, uint16_t port, uint16_t txtLen, const unsigned char *txtRecord, void *context) _CUPS_NONNULL(1,5,6,9, 10);
#elif defined(HAVE_AVAHI)
static int		(*_pollCallback)(struct pollfd *pollfds,
			              unsigned int num_pollfds, int timeout,
			              void *context);
static void		(*_resolveCallback)(AvahiServiceResolver *res,
					 AvahiIfIndex interface,
					 AvahiProtocol protocol,
					 AvahiResolverEvent event,
					 const char *serviceName,
					 const char *regtype,
					 const char *replyDomain,
					 const char *host_name,
					 const AvahiAddress *address,
					 uint16_t port,
					 AvahiStringList *txt,
					 AvahiLookupResultFlags flags,
					 void *context);
#endif /* HAVE_MDNSRESPONDER */

/*
	functions prototypes for browse and resolve API functions
*/
static int avahiInitialize(AvahiPoll **avahi_poll, AvahiClient **avahi_client, void (*client_callback)(AvahiClient*, AvahiClientState, void *), int *err);
static void browseServices(AvahiClient **avahi_client, char *regtype, char *domain, cups_array_t *services, int *err);
static void resolveServices(AvahiClient **avahi_client, avahi_srv_t *service, int *err);
