//
// DNS-SD API functions for CUPS.
//
// Copyright © 2022-2024 by OpenPrinting.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

#include "cups-private.h"
#include "dnssd.h"

#ifdef __APPLE__
#  include <nameser.h>
#  include <CoreFoundation/CoreFoundation.h>
#  include <SystemConfiguration/SystemConfiguration.h>
#endif // __APPLE__
#ifdef HAVE_MDNSRESPONDER
#  include <dns_sd.h>
#  if _WIN32
#    include <winsock2.h>
#    define poll WSAPoll
#  else
#    include <poll.h>
#  endif // _WIN32
#elif _WIN32
#  include <windns.h>
#else // HAVE_AVAHI
#  include <avahi-client/client.h>
#  include <avahi-client/lookup.h>
#  include <avahi-client/publish.h>
#  include <avahi-common/alternative.h>
#  include <avahi-common/domain.h>
#  include <avahi-common/error.h>
#  include <avahi-common/malloc.h>
#  include <avahi-common/simple-watch.h>
#  define AVAHI_DNS_TYPE_LOC 29		// Per RFC 1876
#  include <net/if.h>
#endif // HAVE_MDNSRESPONDER


//
// Private structures...
//

struct _cups_dnssd_s			// DNS-SD context
{
  cups_rwlock_t		rwlock;		// R/W lock for context
  size_t		config_changes;	// Number of hostname/network changes
  cups_dnssd_error_cb_t	cb;		// Error callback function
  void			*cb_data;	// Error callback data
  cups_array_t		*browses,	// Browse requests
			*queries,	// Query requests
			*resolves,	// Resolve requests
			*services;	// Registered services

#ifdef HAVE_MDNSRESPONDER
  DNSServiceRef		ref;		// Master service reference
  char			hostname[256];	// Current mDNS hostname
  DNSServiceRef		hostname_ref;	// Hostname monitoring reference
  cups_thread_t		monitor;	// Monitoring thread

#elif _WIN32

#else // HAVE_AVAHI
  cups_mutex_t		mutex;		// Avahi poll mutex
  bool			in_callback;	// Doing a callback?
  AvahiClient		*client;	// Avahi client connection
  AvahiSimplePoll	*poll;		// Avahi poll class
  cups_thread_t		monitor;	// Monitoring thread
  AvahiDomainBrowser	*dbrowser;	// Domain browser
  size_t		num_domains;	// Number of domains
  char			domains[32][256];// Domains
#endif // HAVE_MDNSRESPONDER
};

struct _cups_dnssd_browse_s		// DNS-SD browse request
{
  cups_dnssd_t		*dnssd;		// DNS-SD context
  cups_dnssd_browse_cb_t cb;		// Browse callback
  void			*cb_data;	// Browse callback data

#ifdef HAVE_MDNSRESPONDER
  DNSServiceRef		ref;		// Browse reference
#elif _WIN32
#else // HAVE_AVAHI
  size_t		num_browsers;	// Number of browsers
  AvahiServiceBrowser	*browsers[33];	// Browsers
#endif // HAVE_MDNSRESPONDER
};

struct _cups_dnssd_query_s		// DNS-SD query request
{
  cups_dnssd_t		*dnssd;		// DNS-SD context
  cups_dnssd_query_cb_t	cb;		// Query callback
  void			*cb_data;	// Query callback data

#ifdef HAVE_MDNSRESPONDER
  DNSServiceRef		ref;		// Query reference
#elif _WIN32
#else // HAVE_AVAHI
  AvahiRecordBrowser	*browser;	// Browser
#endif // HAVE_MDNSRESPONDER
};

struct _cups_dnssd_resolve_s		// DNS-SD resolve request
{
  cups_dnssd_t		*dnssd;		// DNS-SD context
  cups_dnssd_resolve_cb_t cb;		// Resolve callback
  void			*cb_data;	// Resolve callback data

#ifdef HAVE_MDNSRESPONDER
  DNSServiceRef		ref;		// Resolve reference
#elif _WIN32
#else // HAVE_AVAHI
  AvahiServiceResolver	*resolver;	// Resolver
#endif // HAVE_MDNSRESPONDER
};

struct _cups_dnssd_service_s		// DNS-SD service registration
{
  cups_dnssd_t		*dnssd;		// DNS-SD context
  char			*name;		// Service name
  uint32_t		if_index;	// Interface index
  cups_dnssd_service_cb_t cb;		// Service callback
  void			*cb_data;	// Service callback data
  unsigned char		loc[16];	// LOC record data
  bool			loc_set;	// Is the location data set?

#ifdef HAVE_MDNSRESPONDER
  size_t		num_refs;	// Number of service references
  DNSServiceRef		refs[16];	// Service references
  DNSRecordRef		loc_refs[16];	// Service location records
#elif _WIN32
#else // HAVE_AVAHI
  AvahiEntryGroup	*group;		// Group of services under this name
#endif // HAVE_MDNSRESPONDER
};


//
// Local functions...
//

static void		delete_browse(cups_dnssd_browse_t *browse);
static void		delete_query(cups_dnssd_query_t *query);
static void		delete_resolve(cups_dnssd_resolve_t *resolve);
static void		delete_service(cups_dnssd_service_t *service);
static void		report_error(cups_dnssd_t *dnssd, const char *message, ...) _CUPS_FORMAT(2,3);

#ifdef HAVE_MDNSRESPONDER
static void		*mdns_monitor(cups_dnssd_t *dnssd);
static void DNSSD_API	mdns_browse_cb(DNSServiceRef ref, DNSServiceFlags flags, uint32_t interfaceIndex, DNSServiceErrorType error, const char *name, const char *regtype, const char *domain, cups_dnssd_browse_t *browse);
static void DNSSD_API	mdns_hostname_cb(DNSServiceRef ref, DNSServiceFlags flags, uint32_t if_index, DNSServiceErrorType error, const char *fullname, uint16_t rrtype, uint16_t rrclass, uint16_t rdlen, const void *rdata, uint32_t ttl, cups_dnssd_t *dnssd);
static void DNSSD_API	mdns_query_cb(DNSServiceRef ref, DNSServiceFlags flags, uint32_t if_index, DNSServiceErrorType error, const char *name, uint16_t rrtype, uint16_t rrclass, uint16_t rdlen, const void *rdata, uint32_t ttl, cups_dnssd_query_t *query);
static void DNSSD_API	mdns_resolve_cb(DNSServiceRef ref, DNSServiceFlags flags, uint32_t if_index, DNSServiceErrorType error, const char *fullname, const char *host, uint16_t port, uint16_t txtlen, const unsigned char *txt, cups_dnssd_resolve_t *resolve);
static void DNSSD_API	mdns_service_cb(DNSServiceRef ref, DNSServiceFlags flags, DNSServiceErrorType error, const char *name, const char *regtype, const char *domain, cups_dnssd_service_t *service);
static const char	*mdns_strerror(DNSServiceErrorType errorCode);
static cups_dnssd_flags_t mdns_to_cups(DNSServiceFlags flags, DNSServiceErrorType error);

#elif _WIN32

#else // HAVE_AVAHI
static void		avahi_browse_cb(AvahiServiceBrowser *browser, AvahiIfIndex if_index, AvahiProtocol protocol, AvahiBrowserEvent event, const char *name, const char *type, const char *domain, AvahiLookupResultFlags flags, cups_dnssd_browse_t *browse);
static void		avahi_client_cb(AvahiClient *c, AvahiClientState state, cups_dnssd_t *dnssd);
static void		avahi_domain_cb(AvahiDomainBrowser *b, AvahiIfIndex interface, AvahiProtocol protocol, AvahiBrowserEvent event, const char *domain, AvahiLookupResultFlags flags, cups_dnssd_t *dnssd);
static AvahiIfIndex	avahi_if_index(uint32_t if_index);
static void		*avahi_monitor(cups_dnssd_t *dnssd);
static int		avahi_poll_cb(struct pollfd *ufds, unsigned int nfds, int timeout, cups_dnssd_t *dnssd);
static void		avahi_query_cb(AvahiRecordBrowser *browser, AvahiIfIndex if_index, AvahiProtocol protocol, AvahiBrowserEvent event, const char *fullName, uint16_t rrclass, uint16_t rrtype, const void *rdata, size_t rdlen, AvahiLookupResultFlags flags, cups_dnssd_query_t *query);
static void		avahi_resolve_cb(AvahiServiceResolver *resolver, AvahiIfIndex if_index, AvahiProtocol protocol, AvahiResolverEvent event, const char *name, const char *type, const char *domain, const char *host_name, const AvahiAddress *address, uint16_t port, AvahiStringList *txtrec, AvahiLookupResultFlags flags, cups_dnssd_resolve_t *resolve);
static void		avahi_service_cb(AvahiEntryGroup *srv, AvahiEntryGroupState state, cups_dnssd_service_t *service);
#endif // HAVE_MDNSRESPONDER


//
// 'cupsDNSSDAssembleFullName()' - Create a full service name from the instance
//                                 name, registration type, and domain.
//
// This function combines an instance name ("Example Name"), registration type
// ("_ipp._tcp"), and domain ("local.") to create a properly escaped full
// service name ("Example\032Name._ipp._tcp.local.").
//

bool					// O - `true` on success, `false` on failure
cupsDNSSDAssembleFullName(
    char       *fullname,		// I - Buffer for full name
    size_t     fullsize,		// I - Size of buffer
    const char *name,			// I - Service instance name
    const char *type,			// I - Registration type
    const char *domain)			// I - Domain
{
  if (!fullname || !fullsize || !name || !type)
    return (false);

#ifdef HAVE_MDNSRESPONDER
  if (fullsize < kDNSServiceMaxDomainName)
    return (false);

  return (DNSServiceConstructFullName(fullname, name, type, domain) == kDNSServiceErr_NoError);

#elif _WIN32
  return (false);

#else // HAVE_AVAHI
  return (!avahi_service_name_join(fullname, fullsize, name, type, domain));
#endif // HAVE_MDNSRESPONDER
}


//
// 'cupsDNSSDBrowseDelete()' - Cancel and delete a browse request.
//

void
cupsDNSSDBrowseDelete(
    cups_dnssd_browse_t *browse)	// I - Browse request
{
  if (browse)
  {
    cups_dnssd_t *dnssd = browse->dnssd;

    DEBUG_puts("2cupsDNSSDBrowseDelete: Write locking rwlock.");
    cupsRWLockWrite(&dnssd->rwlock);

    cupsArrayRemove(dnssd->browses, browse);

    DEBUG_puts("2cupsDNSSDBrowseDelete: Unlocking rwlock.");
    cupsRWUnlock(&dnssd->rwlock);
  }
}


//
// 'cupsDNSSDBrowseGetContext()' - Get the DNS-SD context for the browse request.
//

cups_dnssd_t *				// O - Context or `NULL`
cupsDNSSDBrowseGetContext(
    cups_dnssd_browse_t *browse)	// I - Browse request
{
  return (browse ? browse->dnssd : NULL);
}


//
// 'cupsDNSSDBrowseNew()' - Create a new DNS-SD browse request.
//
// This function creates a new DNS-SD browse request for the specified service
// types and optional domain and interface index.  The "types" argument can be a
// single service type ("_ipp._tcp") or a service type and comma-delimited list
// of sub-types ("_ipp._tcp,_print,_universal").
//
// Newly discovered services are reported using the required browse callback
// function, with the "flags" argument set to `CUPS_DNSSD_FLAGS_ADD` for newly
// discovered services, `CUPS_DNSSD_FLAGS_NONE` for removed services, or
// `CUPS_DNSSD_FLAGS_ERROR` on an error:
//
// ```
// void
// browse_cb(
//     cups_dnssd_browse_t *browse,
//     void                *cb_data,
//     cups_dnssd_flags_t  flags,
//     uint32_t            if_index,
//     const char          *name,
//     const char          *regtype,
//     const char          *domain)
// {
//     // Process added/removed service
// }
// ```
//

cups_dnssd_browse_t *			// O - Browse request or `NULL` on error
cupsDNSSDBrowseNew(
    cups_dnssd_t           *dnssd,	// I - DNS-SD context
    uint32_t               if_index,	// I - Interface index, `CUPS_DNSSD_IF_INDEX_ANY`, or `CUPS_DNSSD_IF_INDEX_LOCAL`
    const char             *types,	// I - Service types
    const char             *domain,	// I - Domain name or `NULL` for default
    cups_dnssd_browse_cb_t browse_cb,	// I - Browse callback function
    void                   *cb_data)	// I - Browse callback data
{
  cups_dnssd_browse_t	*browse;	// Browse request


  // Range check input...
  if (!dnssd || !types || !browse_cb)
    return (NULL);

  // Allocate memory for the browser...
  if ((browse = (cups_dnssd_browse_t *)calloc(1, sizeof(cups_dnssd_browse_t))) == NULL)
    return (NULL);

  browse->dnssd   = dnssd;
  browse->cb      = browse_cb;
  browse->cb_data = cb_data;

  DEBUG_puts("2cupsDNSSDBrowseNew: Write locking rwlock.");
  cupsRWLockWrite(&dnssd->rwlock);

  if (!dnssd->browses)
  {
    // Create an array of browsers...
    if ((dnssd->browses = cupsArrayNew3(NULL, NULL, NULL, 0, NULL, (cups_afree_cb_t)delete_browse)) == NULL)
    {
      // Unable to create...
      free(browse);
      browse = NULL;
      goto done;
    }
  }

#ifdef HAVE_MDNSRESPONDER
  DNSServiceErrorType error;		// Error, if any

  browse->ref = dnssd->ref;
  if ((error = DNSServiceBrowse(&browse->ref, kDNSServiceFlagsShareConnection, if_index, types, domain, (DNSServiceBrowseReply)mdns_browse_cb, browse)) != kDNSServiceErr_NoError)
  {
    report_error(dnssd, "Unable to create DNS-SD browse request: %s", mdns_strerror(error));
    free(browse);
    browse = NULL;
    goto done;
  }

#elif _WIN32

#else // HAVE_AVAHI
  if (!dnssd->in_callback)
  {
    DEBUG_puts("2cupsDNSSDBrowseNew: Locking mutex.");
    cupsMutexLock(&dnssd->mutex);
  }

  browse->num_browsers = 1;
  browse->browsers[0]  = avahi_service_browser_new(dnssd->client, avahi_if_index(if_index), AVAHI_PROTO_UNSPEC, types, /*domain*/NULL, /*flags*/0, (AvahiServiceBrowserCallback)avahi_browse_cb, browse);

  if (!browse->browsers[0])
  {
    report_error(dnssd, "Unable to create DNS-SD browse request: %s", avahi_strerror(avahi_client_errno(dnssd->client)));
    free(browse);
    browse = NULL;

    if (!dnssd->in_callback)
    {
      DEBUG_puts("2cupsDNSSDBrowseNew: Unlocking mutex.");
      cupsMutexUnlock(&dnssd->mutex);
    }

    goto done;
  }

  if (!domain && dnssd->num_domains > 0)
  {
    // Add browsers for all domains...
    size_t	i;			// Looping var

    for (i = 0; i < dnssd->num_domains; i ++)
    {
      if ((browse->browsers[browse->num_browsers] = avahi_service_browser_new(dnssd->client, avahi_if_index(if_index), AVAHI_PROTO_UNSPEC, types, dnssd->domains[i], /*flags*/0, (AvahiServiceBrowserCallback)avahi_browse_cb, browse)) != NULL)
        browse->num_browsers ++;
    }
  }

  if (!dnssd->in_callback)
  {
    DEBUG_puts("2cupsDNSSDBrowseNew: Unlocking mutex.");
    cupsMutexUnlock(&dnssd->mutex);

    avahi_simple_poll_wakeup(dnssd->poll);
  }
#endif // HAVE_MDNSRESPONDER

  DEBUG_printf("2cupsDNSSDBrowseNew: Adding browse=%p", (void *)browse);
  cupsArrayAdd(dnssd->browses, browse);

  done:

  DEBUG_puts("2cupsDNSSDBrowseNew: Unlocking rwlock.");
  cupsRWUnlock(&dnssd->rwlock);

  return (browse);
}



//
// 'cupsDNSSDCopyComputerName()' - Copy the current human-readable name for the system.
//
// This function copies the current human-readable name ("My Computer") to the
// provided buffer.  The "dnssd" parameter is a DNS-SD context created with
// @link cupsDNSSDNew@.  The "buffer" parameter points to a character array of
// at least 128 bytes and the "bufsize" parameter specifies the actual size of
// the array.
//

char *					// O - Computer name or `NULL` on error
cupsDNSSDCopyComputerName(
    cups_dnssd_t *dnssd,		// I - DNS-SD context
    char         *buffer,		// I - Computer name buffer
    size_t       bufsize)		// I - Size of computer name buffer (at least 128 bytes)
{
  // Range check input...
  if (buffer)
    *buffer = '\0';

  if (!dnssd || !buffer || bufsize < 128)
    return (NULL);

  // Copy the current computer name...
#ifdef __APPLE__
  SCDynamicStoreRef sc;			// Context for dynamic store
  CFStringEncoding nameEncoding;	// Encoding of computer name
  CFStringRef	nameRef;		// Computer name CFString

  if ((sc = SCDynamicStoreCreate(kCFAllocatorDefault, CFSTR("libcups"), NULL, NULL)) != NULL)
  {
    // Get the computer name from the dynamic store...
    if ((nameRef = SCDynamicStoreCopyComputerName(sc, &nameEncoding)) != NULL)
    {
      if (!CFStringGetCString(nameRef, buffer, (CFIndex)bufsize, kCFStringEncodingUTF8))
        *buffer = '\0';

      CFRelease(nameRef);
    }

    CFRelease(sc);
  }

#elif defined(HAVE_MDNSRESPONDER)
  char	*bufptr;			// Pointer into name

  DEBUG_puts("2cupsDNSSDCopyComputerName: Read locking rwlock.");
  cupsRWLockRead(&dnssd->rwlock);

  cupsCopyString(buffer, dnssd->hostname, bufsize);

  DEBUG_puts("2cupsDNSSDCopyComputerName: Unlocking rwlock.");
  cupsRWUnlock(&dnssd->rwlock);

  if ((bufptr = strchr(buffer, '.')) != NULL)
    *bufptr = '\0';

#else // HAVE_AVAHI
  cupsCopyString(buffer, avahi_client_get_host_name(dnssd->client), bufsize);
#endif // __APPLE__

  return (buffer);
}


//
// 'cupsDNSSDCopyHostName()' - Copy the current mDNS hostname for the system.
//
// This function copies the current mDNS hostname ("hostname.local") to the
// provided buffer.  The "dnssd" parameter is a DNS-SD context created with
// @link cupsDNSSDNew@.  The "buffer" parameter points to a character array of
// at least 70 bytes and the "bufsize" parameter specifies the actual size of
// the array.
//

char *					// O - mDNS hostname or `NULL` on error
cupsDNSSDCopyHostName(
    cups_dnssd_t *dnssd,		// I - DNS-SD context
    char         *buffer,		// I - Hostname buffer
    size_t       bufsize)		// I - Size of hostname buffer (at least 70 bytes)
{
  // Range check input...
  if (!dnssd || !buffer || bufsize < 70)
  {
    if (buffer)
      *buffer = '\0';

    return (NULL);
  }

  // Copy the current hostname...
#ifdef HAVE_MDNSRESPONDER
  DEBUG_puts("2cupsDNSSDCopyHostName: Read locking rwlock.");
  cupsRWLockRead(&dnssd->rwlock);

  cupsCopyString(buffer, dnssd->hostname, bufsize);

  DEBUG_puts("2cupsDNSSDCopyHostName: Unlocking rwlock.");
  cupsRWUnlock(&dnssd->rwlock);

#else // HAVE_AVAHI
  cupsCopyString(buffer, avahi_client_get_host_name_fqdn(dnssd->client), bufsize);
#endif // HAVE_MDNSRESPONDER

  return (buffer);
}


//
// 'cupsDNSSDDecodeTXT()' - Decode a TXT record into key/value pairs.
//
// This function converts the DNS TXT record encoding of key/value pairs into
// `cups_option_t` elements that can be accessed using the @link cupsGetOption@
// function and freed using the @link cupsFreeOptions@ function.
//

int					// O - Number of key/value pairs
cupsDNSSDDecodeTXT(
    const unsigned char *txtrec,	// I - TXT record data
    uint16_t            txtlen,		// I - TXT record length
    cups_option_t       **txt)		// O - Key/value pairs
{
  int		num_txt = 0;		// Number of key/value pairs
  unsigned char	keylen;			// Length of key/value
  char		key[256],		// Key/value buffer
		*value;			// Pointer to value
  const unsigned char *txtptr,		// Pointer into TXT record data
		*txtend;		// End of TXT record data


  // Range check input...
  if (txt)
    *txt = NULL;
  if (!txtrec || !txtlen || !txt)
    return (0);

  // Loop through the record...
  for (txtptr = txtrec, txtend = txtrec + txtlen; txtptr < txtend; txtptr += keylen)
  {
    // Format is a length byte followed by "key=value"
    keylen = *txtptr++;
    if (keylen == 0 || (txtptr + keylen) > txtend)
      break;				// Bogus length

    // Copy the data to a C string...
    memcpy(key, txtptr, keylen);
    key[keylen] = '\0';

    if ((value = strchr(key, '=')) != NULL)
    {
      // Got value separator, add it...
      *value++ = '\0';

      num_txt = cupsAddOption(key, value, num_txt, txt);
    }
    else
    {
      // No value, stop...
      break;
    }
  }

  // Return the number of pairs we parsed...
  return (num_txt);

}


//
// 'cupsDNSSDDelete()' - Delete a DNS-SD context and all its requests.
//

void
cupsDNSSDDelete(cups_dnssd_t *dnssd)	// I - DNS-SD context
{
  if (!dnssd)
    return;

  DEBUG_puts("2cupsDNSSDDelete: Write locking rwlock.");
  cupsRWLockWrite(&dnssd->rwlock);

  cupsArrayDelete(dnssd->browses);
  cupsArrayDelete(dnssd->queries);
  cupsArrayDelete(dnssd->resolves);
  cupsArrayDelete(dnssd->services);

  DEBUG_puts("2cupsDNSSDDelete: Unlocking rwlock.");
  cupsRWUnlock(&dnssd->rwlock);

#ifdef HAVE_MDNSRESPONDER
  cupsThreadCancel(dnssd->monitor);
  cupsThreadWait(dnssd->monitor);
  DNSServiceRefDeallocate(dnssd->ref);

#elif _WIN32

#else // HAVE_AVAHI
  avahi_domain_browser_free(dnssd->dbrowser);

  cupsThreadCancel(dnssd->monitor);
  cupsThreadWait(dnssd->monitor);

  avahi_simple_poll_free(dnssd->poll);
#endif // HAVE_MDNSRESPONDER

  cupsRWDestroy(&dnssd->rwlock);
  free(dnssd);
}


//
// 'cupsDNSSDGetConfigChanges()' - Get the number of host name/network
//                                 configuration changes seen.
//
// This function returns the number of host name or network configuration
// changes that have been seen since the context was created.  The value can be
// used to track when local services need to be updated.  Registered services
// will also get a callback with the `CUPS_DNSSD_FLAGS_HOST_CHANGE` bit set in
// the "flags" argument for host name changes and/or
// `CUPS_DNSSD_FLAGS_NETWORK_CHANGE` for network changes.
//

size_t					// O - Number of host name changes
cupsDNSSDGetConfigChanges(
    cups_dnssd_t *dnssd)		// I - DNS-SD context
{
  size_t	config_changes = 0;


  if (dnssd)
  {
    cupsRWLockRead(&dnssd->rwlock);
    config_changes = dnssd->config_changes;
    cupsRWUnlock(&dnssd->rwlock);
  }

  return (config_changes);
}


//
// 'cupsDNSSDNew()' - Create a new DNS-SD context.
//
// This function creates a new DNS-SD context for browsing, querying, resolving,
// and/or registering services.  Call @link cupsDNSSDDelete@ to stop any pending
// browses, queries, or resolves, unregister any services, and free the DNS-SD
// context.
//

cups_dnssd_t *				// O - DNS-SD context
cupsDNSSDNew(
    cups_dnssd_error_cb_t error_cb,	// I - Error callback function
    void                  *cb_data)	// I - Error callback data
{
  cups_dnssd_t	*dnssd;			// DNS-SD context


  DEBUG_printf("cupsDNSSDNew(error_cb=%p, cb_data=%p)", (void *)error_cb, cb_data);

  // Allocate memory...
  if ((dnssd = (cups_dnssd_t *)calloc(1, sizeof(cups_dnssd_t))) == NULL)
  {
    DEBUG_puts("2cupsDNSSDNew: Unable to allocate memory, returning NULL.");
    return (NULL);
  }

  // Save the error callback...
  dnssd->cb      = error_cb;
  dnssd->cb_data = cb_data;

  // Initialize the rwlock...
  cupsRWInit(&dnssd->rwlock);

  // Setup the DNS-SD connection and monitor thread...
#ifdef HAVE_MDNSRESPONDER
  DNSServiceErrorType error;		// Error code

  if ((error = DNSServiceCreateConnection(&dnssd->ref)) != kDNSServiceErr_NoError)
  {
    // Unable to create connection...
    report_error(dnssd, "Unable to initialize DNS-SD: %s", mdns_strerror(error));
    cupsDNSSDDelete(dnssd);
    DEBUG_puts("2cupsDNSSDNew: Unable to create DNS-SD thread - returning NULL.");
    return (NULL);
  }

  // Monitor for hostname changes...
  httpGetHostname(NULL, dnssd->hostname, sizeof(dnssd->hostname));
  dnssd->hostname_ref = dnssd->ref;
  if ((error = DNSServiceQueryRecord(&dnssd->hostname_ref, kDNSServiceFlagsShareConnection, kDNSServiceInterfaceIndexLocalOnly, "1.0.0.127.in-addr.arpa.", kDNSServiceType_PTR, kDNSServiceClass_IN, (DNSServiceQueryRecordReply)mdns_hostname_cb, dnssd)) != kDNSServiceErr_NoError)
  {
    report_error(dnssd, "Unable to query PTR record for local hostname: %s", mdns_strerror(error));
    dnssd->hostname_ref = NULL;
  }

  // Start the background monitoring thread...
  if ((dnssd->monitor = cupsThreadCreate((void *(*)(void *))mdns_monitor, dnssd)) == 0)
  {
    report_error(dnssd, "Unable to create DNS-SD thread: %s", strerror(errno));
    cupsDNSSDDelete(dnssd);
    DEBUG_puts("2cupsDNSSDNew: Unable to create DNS-SD thread - returning NULL.");
    return (NULL);
  }

  DEBUG_printf("2cupsDNSSDNew: dnssd->monitor=%p", (void *)dnssd->monitor);

#elif _WIN32

#else // HAVE_AVAHI
  int error;				// Error code

  // Initialize the mutex used to control access to the socket
  cupsMutexInit(&dnssd->mutex);

  // Create a polled interface for Avahi requests...
  if ((dnssd->poll = avahi_simple_poll_new()) == NULL)
  {
    // Unable to create the background thread...
    report_error(dnssd, "Unable to initialize DNS-SD: %s", strerror(errno));
    cupsDNSSDDelete(dnssd);
    DEBUG_puts("2cupsDNSSDNew: Unable to create simple poll - returning NULL.");
    return (NULL);
  }

  avahi_simple_poll_set_func(dnssd->poll, (AvahiPollFunc)avahi_poll_cb, dnssd);

  DEBUG_printf("2cupsDNSSDNew: dnssd->poll=%p", (void *)dnssd->poll);

  if ((dnssd->client = avahi_client_new(avahi_simple_poll_get(dnssd->poll), AVAHI_CLIENT_NO_FAIL, (AvahiClientCallback)avahi_client_cb, dnssd, &error)) == NULL)
  {
    // Unable to create the client...
    report_error(dnssd, "Unable to initialize DNS-SD: %s", avahi_strerror(error));
    avahi_simple_poll_free(dnssd->poll);
    cupsDNSSDDelete(dnssd);
    DEBUG_puts("2cupsDNSSDNew: Unable to create Avahi client - returning NULL.");
    return (NULL);
  }

  DEBUG_printf("2cupsDNSSDNew: dnssd->client=%p", (void *)dnssd->client);

  if ((dnssd->monitor = cupsThreadCreate((void *(*)(void *))avahi_monitor, dnssd)) == 0)
  {
    report_error(dnssd, "Unable to create DNS-SD thread: %s", strerror(errno));
    cupsDNSSDDelete(dnssd);
    DEBUG_puts("2cupsDNSSDNew: Unable to create DNS-SD thread - returning NULL.");
    return (NULL);
  }

  DEBUG_printf("2cupsDNSSDNew: dnssd->monitor=%p", (void *)dnssd->monitor);

  dnssd->dbrowser = avahi_domain_browser_new(dnssd->client, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, /*domain*/NULL, AVAHI_DOMAIN_BROWSER_BROWSE, /*flags*/0, (AvahiDomainBrowserCallback)avahi_domain_cb, dnssd);
#endif // HAVE_MDNSRESPONDER

  DEBUG_printf("2cupsDNSSDNew: Returning %p.", (void *)dnssd);

  return (dnssd);
}


//
// 'cupsDNSSDQueryDelete()' - Cancel and delete a query request.
//

void
cupsDNSSDQueryDelete(
    cups_dnssd_query_t *query)		// I - Query request
{
  if (query)
  {
    cups_dnssd_t *dnssd = query->dnssd;

    DEBUG_puts("2cupsDNSSDQueryDelete: Write locking rwlock.");
    cupsRWLockWrite(&dnssd->rwlock);

    cupsArrayRemove(dnssd->queries, query);

    DEBUG_puts("2cupsDNSSDQueryDelete: Unlocking rwlock.");
    cupsRWUnlock(&dnssd->rwlock);
  }
}


//
// 'cupsDNSSDQueryGetContext()' - Get the DNS-SD context for the query request.
//

cups_dnssd_t *				// O - DNS-SD context or `NULL`
cupsDNSSDQueryGetContext(
    cups_dnssd_query_t *query)		// I - Query request
{
  return (query ? query->dnssd : NULL);
}


//
// 'cupsDNSSDQueryNew()' - Create a new query request.
//
// This function creates a new DNS-SD query request for the specified full
// service name and DNS record type.  The "fullname" parameter specifies the
// full DNS name of the service (instance name, type, and domain) being queried.
// Responses to the query are reported using the required query callback
// function with the "flags" argument set to `CUPS_DNSSD_FLAGS_NONE` on success
// or `CUPS_DNSSD_FLAGS_ERROR` on error:
//
// ```
// void
// query_cb(
//     cups_dnssd_query_t *query,
//     void               *cb_data,
//     cups_dnssd_flags_t flags,
//     uint32_t           if_index,
//     const char         *fullname,
//     uint16_t           rrtype,
//     const void         *qdata,
//     uint16_t           qlen)
// {
//     // Process query record
// }
// ```
//

cups_dnssd_query_t *			// O - Query request or `NULL` on error
cupsDNSSDQueryNew(
    cups_dnssd_t          *dnssd,	// I - DNS-SD context
    uint32_t              if_index,	// I - Interface index or `CUPS_DNSSD_IF_INDEX_ANY` or `CUPS_DNSSD_IF_INDEX_LOCAL`
    const char            *fullname,	// I - Full DNS name including types and domain
    uint16_t              rrtype,	// I - Record type to query (`CUPS_DNSSD_RRTYPE_TXT`, etc.)
    cups_dnssd_query_cb_t query_cb,	// I - Query callback function
    void                  *cb_data)	// I - Query callback data
{
  cups_dnssd_query_t	*query;		// Query request


  DEBUG_printf("cupsDNSSDQueryNew(dnssd=%p, if_index=%u, fullname=\"%s\", rrtype=%u, query_cb=%p, cb_data=%p)", (void *)dnssd, if_index, fullname, rrtype, (void *)query_cb, cb_data);

  // Range check input...
  if (!dnssd || !fullname || !query_cb)
    return (NULL);

  // Allocate memory for the resolver...
  if ((query = (cups_dnssd_query_t *)calloc(1, sizeof(cups_dnssd_query_t))) == NULL)
    return (NULL);

  query->dnssd   = dnssd;
  query->cb      = query_cb;
  query->cb_data = cb_data;

  DEBUG_puts("2cupsDNSSDQueryNew: Write locking rwlock.");
  cupsRWLockWrite(&dnssd->rwlock);

  if (!dnssd->queries)
  {
    // Create an array of resolvers...
    DEBUG_puts("2cupsDNSSDQueryNew: Creating queries array.");
    if ((dnssd->queries = cupsArrayNew3(NULL, NULL, NULL, 0, NULL, (cups_afree_cb_t)delete_query)) == NULL)
    {
      // Unable to create...
      free(query);
      query = NULL;
      goto done;
    }
  }

#ifdef HAVE_MDNSRESPONDER
  DNSServiceErrorType error;		// Error, if any

  query->ref = dnssd->ref;
  if ((error = DNSServiceQueryRecord(&query->ref, kDNSServiceFlagsShareConnection, if_index, fullname, rrtype, kDNSServiceClass_IN, (DNSServiceQueryRecordReply)mdns_query_cb, query)) != kDNSServiceErr_NoError)
  {
    report_error(dnssd, "Unable to create DNS-SD query request: %s", mdns_strerror(error));
    free(query);
    query = NULL;
    goto done;
  }

#elif _WIN32

#else // HAVE_AVAHI
  if (!dnssd->in_callback)
  {
    DEBUG_puts("4avahi_poll_cb: Locking mutex.");
    cupsMutexLock(&dnssd->mutex);
  }

  query->browser = avahi_record_browser_new(dnssd->client, avahi_if_index(if_index), AVAHI_PROTO_UNSPEC, fullname, AVAHI_DNS_CLASS_IN, rrtype, 0, (AvahiRecordBrowserCallback)avahi_query_cb, query);

  if (!dnssd->in_callback)
  {
    DEBUG_puts("4avahi_poll_cb: Unlocking mutex.");
    cupsMutexUnlock(&dnssd->mutex);

    avahi_simple_poll_wakeup(dnssd->poll);
  }

  if (!query->browser)
  {
    report_error(dnssd, "Unable to create DNS-SD query request: %s", avahi_strerror(avahi_client_errno(dnssd->client)));
    free(query);
    query = NULL;
    goto done;
  }
#endif // HAVE_MDNSRESPONDER

  DEBUG_printf("2cupsDNSSDQueryNew: Adding query=%p", (void *)query);
  cupsArrayAdd(dnssd->queries, query);

  done:

  DEBUG_puts("2cupsDNSSDQueryNew: Unlocking rwlock.");
  cupsRWUnlock(&dnssd->rwlock);

  return (query);
}



//
// 'cupsDNSSDResolveDelete()' - Cancel and free a resolve request.
//

void
cupsDNSSDResolveDelete(
    cups_dnssd_resolve_t *res)		// I - Resolve request
{
  if (res)
  {
    cups_dnssd_t *dnssd = res->dnssd;

    DEBUG_puts("2cupsDNSSDResolveDelete: Write locking rwlock.");
    cupsRWLockWrite(&dnssd->rwlock);

    cupsArrayRemove(dnssd->resolves, res);

    DEBUG_puts("2cupsDNSSDResolveDelete: Unlocking rwlock.");
    cupsRWUnlock(&dnssd->rwlock);
  }
}


//
// 'cupsDNSSDResolveGetContext()' - Get the DNS-SD context for the resolve request.
//

cups_dnssd_t *				// O - DNS-SD context or `NULL`
cupsDNSSDResolveGetContext(
    cups_dnssd_resolve_t *resolve)	// I - Resolve request
{
  return (resolve ? resolve->dnssd : NULL);
}


//
// 'cupsDNSSDResolveNew()' - Create a new DNS-SD resolve request.
//
// This function creates a new DNS-SD resolver for the specified instance name,
// service type, and optional domain and interface index.  Resikved services
// are reported using the required resolve callback function, with the "flags"
// argument set to `CUPS_DNSSD_FLAGS_NONE` on success or
// `CUPS_DNSSD_FLAGS_ERROR` on error:
//
// ```
// void
// resolve_cb(
//     cups_dnssd_resolve_t *resolve,
//     void                 *cb_data,
//     cups_dnssd_flags_t   flags,
//     uint32_t             if_index,
//     const char           *fullname,
//     const char           *host,
//     uint16_t             port,
//     size_t               num_txt,
//     cups_option_t        *txt)
// {
//     // Process resolved service
// }
// ```
//

cups_dnssd_resolve_t *			// O - Resolve request or `NULL` on error
cupsDNSSDResolveNew(
    cups_dnssd_t            *dnssd,	// I - DNS-SD context
    uint32_t                if_index,	// I - Interface index or `CUPS_DNSSD_IF_INDEX_ANY` or `CUPS_DNSSD_IF_INDEX_LOCAL`
    const char              *name,	// I - Service name
    const char              *type,	// I - Service type
    const char              *domain,	// I - Domain name or `NULL` for default
    cups_dnssd_resolve_cb_t resolve_cb,	// I - Resolve callback function
    void                    *cb_data)	// I - Resolve callback data
{
  cups_dnssd_resolve_t	*resolve;	// Resolve request


  DEBUG_printf("cupsDNSSDResolveNew(dnssd=%p, if_index=%u, name=\"%s\", type=\"%s\", domain=\"%s\", resolve_cb=%p, cb_data=%p)", (void *)dnssd, (unsigned)if_index, name, type, domain, (void *)resolve_cb, cb_data);

  // Range check input...
  if (!dnssd || !name || !type || !resolve_cb)
  {
    DEBUG_puts("2cupsDNSSDResolveNew: Bad arguments, returning NULL.");
    return (NULL);
  }

  // Allocate memory for the resolver...
  if ((resolve = (cups_dnssd_resolve_t *)calloc(1, sizeof(cups_dnssd_resolve_t))) == NULL)
  {
    DEBUG_printf("2cupsDNSSDResolveNew: Unable to allocate memory: %s", strerror(errno));
    return (NULL);
  }

  resolve->dnssd   = dnssd;
  resolve->cb      = resolve_cb;
  resolve->cb_data = cb_data;

#ifdef HAVE_MDNSRESPONDER
  DNSServiceErrorType error;		// Error, if any

  resolve->ref = dnssd->ref;
  if ((error = DNSServiceResolve(&resolve->ref, kDNSServiceFlagsShareConnection, if_index, name, type, domain, (DNSServiceResolveReply)mdns_resolve_cb, resolve)) != kDNSServiceErr_NoError)
  {
    report_error(dnssd, "Unable to create DNS-SD query request: %s", mdns_strerror(error));
    free(resolve);
    return (NULL);
  }

#elif _WIN32

#else // HAVE_AVAHI
  if (!dnssd->in_callback)
  {
    DEBUG_puts("2cupsDNSSDResolveNew: Locking mutex.");
    cupsMutexLock(&dnssd->mutex);
  }

  resolve->resolver = avahi_service_resolver_new(dnssd->client, avahi_if_index(if_index), AVAHI_PROTO_UNSPEC, name, type, domain, AVAHI_PROTO_UNSPEC, /*flags*/0, (AvahiServiceResolverCallback)avahi_resolve_cb, resolve);

  if (!dnssd->in_callback)
  {
    DEBUG_puts("2cupsDNSSDResolveNew: Unlocking mutex.");
    cupsMutexUnlock(&dnssd->mutex);

    avahi_simple_poll_wakeup(dnssd->poll);
  }

  if (!resolve->resolver)
  {
    report_error(dnssd, "Unable to create DNS-SD resolve request: %s", avahi_strerror(avahi_client_errno(dnssd->client)));
    free(resolve);
    return (NULL);
  }
#endif // HAVE_MDNSRESPONDER

  DEBUG_puts("2cupsDNSSDResolveNew: Write locking rwlock.");
  cupsRWLockWrite(&dnssd->rwlock);

  if (!dnssd->resolves)
  {
    // Create an array of resolvers...
    DEBUG_puts("2cupsDNSSDResolveNew: Creating resolver array.");
    if ((dnssd->resolves = cupsArrayNew3(NULL, NULL, NULL, 0, NULL, (cups_afree_cb_t)delete_resolve)) == NULL)
    {
      // Unable to create...
      DEBUG_printf("2cupsDNSSDResolveNew: Unable to allocate memory: %s", strerror(errno));
      free(resolve);
      resolve = NULL;

      goto done;
    }
  }

  DEBUG_printf("2cupsDNSSDResolveNew: Adding resolver %p.", (void *)resolve);
  cupsArrayAdd(dnssd->resolves, resolve);

  done:

  DEBUG_puts("2cupsDNSSDResolveNew: Unlocking rwlock.");
  cupsRWUnlock(&dnssd->rwlock);

  return (resolve);
}


//
// 'cupsDNSSDSeparateFullName()' - Separate a full service name into an instance
//                                 name, registration type, and domain.
//
// This function separates a full service name such as
// "Example\032Name._ipp._tcp.local.") into its instance name ("Example Name"),
// registration type ("_ipp._tcp"), and domain ("local.").
//

bool					// O - `true` on success, `false` on error
cupsDNSSDSeparateFullName(
    const char *fullname,		// I - Full service name
    char       *name,			// I - Instance name buffer
    size_t     namesize,		// I - Size of instance name buffer
    char       *type,			// I - Registration type buffer
    size_t     typesize,		// I - Size of registration type buffer
    char       *domain,			// I - Domain name buffer
    size_t     domainsize)		// I - Size of domain name buffer
{
  // Range check input..
  if (!fullname || !name || !namesize || !type || !typesize || !domain || !domainsize)
  {
    if (name)
      *name = '\0';
    if (type)
      *type = '\0';
    if (domain)
      *domain = '\0';

    return (false);
  }

#if _WIN32 || defined(HAVE_MDNSRESPONDER)
  bool	ret = true;			// Return value
  char	*ptr,				// Pointer into name/type/domain
	*end;				// Pointer to end of name/type/domain

  // Get the service name...
  for (ptr = name, end = name + namesize - 1; *fullname; fullname ++)
  {
    if (*fullname == '.')
    {
      // Service type separator...
      break;
    }
    else if (*fullname == '\\' && isdigit(fullname[1] & 255) && isdigit(fullname[2] & 255) && isdigit(fullname[3] & 255))
    {
      // Escaped character
      if (ptr < end)
        *ptr++ = (fullname[1] - '0') * 100 + (fullname[2] - '0') * 10 + fullname[3] - '0';
      else
        ret = false;

      fullname += 3;
    }
    else if (ptr < end)
      *ptr++ = *fullname;
    else
      ret = false;
  }
  *ptr = '\0';

  if (*fullname)
    fullname ++;

  // Get the type...
  for (ptr = type, end = type + typesize - 1; *fullname; fullname ++)
  {
    if (*fullname == '.' && fullname[1] != '_')
    {
      // Service type separator...
      break;
    }
    else if (*fullname == '\\' && isdigit(fullname[1] & 255) && isdigit(fullname[2] & 255) && isdigit(fullname[3] & 255))
    {
      // Escaped character
      if (ptr < end)
        *ptr++ = (fullname[1] - '0') * 100 + (fullname[2] - '0') * 10 + fullname[3] - '0';
      else
        ret = false;

      fullname += 3;
    }
    else if (ptr < end)
      *ptr++ = *fullname;
    else
      ret = false;
  }
  *ptr = '\0';

  if (*fullname)
    fullname ++;

  // Get the domain...
  for (ptr = domain, end = domain + domainsize - 1; *fullname; fullname ++)
  {
    if (*fullname == '\\' && isdigit(fullname[1] & 255) && isdigit(fullname[2] & 255) && isdigit(fullname[3] & 255))
    {
      // Escaped character
      if (ptr < end)
        *ptr++ = (fullname[1] - '0') * 100 + (fullname[2] - '0') * 10 + fullname[3] - '0';
      else
        ret = false;

      fullname += 3;
    }
    else if (ptr < end)
      *ptr++ = *fullname;
    else
      ret = false;
  }
  *ptr = '\0';

  return (ret);

#else // HAVE_AVAHI
  return (!avahi_service_name_split(fullname, name, namesize, type, typesize, domain, domainsize));
#endif // _WIN32 || HAVE_MDNSRESPONDER
}


//
// 'cupsDNSSDServiceAdd()' - Add a service instance.
//
// This function adds a service instance for the specified service types,
// domain, host, and port.  The "types" argument can be a single service type
// ("_ipp._tcp") or a service type and comma-delimited list of sub-types
// ("_ipp._tcp,_print,_universal").
//
// Call the @link cupsDNSSDServicePublish@ function after all service instances
// have been added.
//

bool					// O - `true` on success, `false` on failure
cupsDNSSDServiceAdd(
    cups_dnssd_service_t *service,	// I - Service
    const char           *types,	// I - Service types
    const char           *domain,	// I - Domain name or `NULL` for default
    const char           *host,		// I - Host name or `NULL` for default
    uint16_t             port,		// I - Port number or `0` for none
    int                  num_txt,	// I - Number of TXT record values
    cups_option_t        *txt)		// I - TXT record values
{
  bool		ret = true;		// Return value
  int		i;			// Looping var


  DEBUG_printf("cupsDNSSDServiceAdd(service=%p, types=\"%s\", domain=\"%s\", host=\"%s\", port=%u, num_txt=%d, txt=%p)", (void *)service, types, domain, host, port, num_txt, (void *)txt);

  // Range check input...
  if (!service || !types)
    return (false);

#ifdef HAVE_MDNSRESPONDER
  DNSServiceErrorType	error;		// Error, if any
  TXTRecordRef		txtrec,		// TXT record
			*txtptr = NULL;	// Pointer to TXT record, if any

  // Limit number of services with this name...
  if (service->num_refs >= (sizeof(service->refs) / sizeof(service->refs[0])))
  {
    report_error(service->dnssd, "Unable to create DNS-SD service registration: Too many services with this name.");
    ret = false;
    goto done;
  }

  // Create the TXT record as needed...
  if (num_txt)
  {
    TXTRecordCreate(&txtrec, 1024, NULL);
    for (i = 0; i < num_txt; i ++)
      TXTRecordSetValue(&txtrec, txt[i].name, (uint8_t)strlen(txt[i].value), txt[i].value);

    txtptr = &txtrec;
  }

  service->refs[service->num_refs] = service->dnssd->ref;
  if ((error = DNSServiceRegister(service->refs + service->num_refs, kDNSServiceFlagsShareConnection | kDNSServiceFlagsNoAutoRename, service->if_index, service->name, types, domain, host, htons(port), txtptr ? TXTRecordGetLength(txtptr) : 0, txtptr ? TXTRecordGetBytesPtr(txtptr) : NULL, (DNSServiceRegisterReply)mdns_service_cb, service)) != kDNSServiceErr_NoError)
  {
    if (txtptr)
      TXTRecordDeallocate(txtptr);

    report_error(service->dnssd, "Unable to create DNS-SD service registration: %s", mdns_strerror(error));
    ret = false;
    goto done;
  }

  if (txtptr)
    TXTRecordDeallocate(txtptr);

  if (service->loc_set)
  {
    if ((error = DNSServiceAddRecord(service->refs[service->num_refs], service->loc_refs + service->num_refs, 0, kDNSServiceType_LOC, sizeof(service->loc), service->loc, 0)) != kDNSServiceErr_NoError)
    {
      report_error(service->dnssd, "Unable to add DNS-SD service location data: %s", mdns_strerror(error));
    }
  }

  service->num_refs ++;

#elif _WIN32

#else // HAVE_AVAHI
  int			error;		// Error code
  AvahiStringList	*txtrec = NULL;	// TXT record string list
  char			*regtype,	// Registration type
			*subtypes;	// Subtypes (if any)

  // Avahi has trouble with hostnames for services that are only available on
  // the loopback interface/localhost...
  if (service->if_index == CUPS_DNSSD_IF_INDEX_LOCAL || (host && !_cups_strcasecmp(host, "localhost")))
    host = NULL;

  // Build the string list from the TXT array...
  for (i = 0; i < num_txt; i ++)
    txtrec = avahi_string_list_add_printf(txtrec, "%s=%s", txt[i].name, txt[i].value);

  // Copy the registration type...
  if ((regtype = strdup(types)) == NULL)
  {
    report_error(service->dnssd, "Unable to duplicate registration types: %s", strerror(errno));
    ret = false;
    goto done;
  }

  if ((subtypes = strchr(regtype, ',')) != NULL)
    *subtypes++ = '\0';

  // Add the service entry...
  if ((error = avahi_entry_group_add_service_strlst(service->group, avahi_if_index(service->if_index), AVAHI_PROTO_UNSPEC, /*flags*/0, service->name, regtype, domain, host, port, txtrec)) < 0)
  {
    report_error(service->dnssd, "Unable to register '%s.%s': %s", service->name, regtype, avahi_strerror(error));
    ret = false;
  }
  else if (subtypes)
  {
    char	subtype[256];		// Subtype string
    char 	*start, *end;		// Pointers into sub-types...

    DEBUG_printf("cupsDNSSDServiceAdd: Registered '%s.%s.%s'.", service->name, regtype, domain);

    for (start = subtypes; ret && start && *start; start = end)
    {
      if ((end = strchr(start, ',')) != NULL)
	*end++ = '\0';
      else
	end = start + strlen(start);

      snprintf(subtype, sizeof(subtype), "%s._sub.%s", start, regtype);
      if ((error = avahi_entry_group_add_service_subtype(service->group, avahi_if_index(service->if_index), AVAHI_PROTO_UNSPEC, /*flags*/0, service->name, regtype, domain, subtype)) < 0)
      {
        report_error(service->dnssd, "Unable to register '%s.%s': %s", service->name, subtype, avahi_strerror(error));
        ret = false;
      }

      DEBUG_printf("cupsDNSSDServiceAdd: Registered '%s.%s.%s'.", service->name, subtype, domain);
    }
  }

  free(regtype);

  if (txtrec)
    avahi_string_list_free(txtrec);
#endif // HAVE_MDNSRESPONDER

  done:

  DEBUG_printf("2cupsDNSSDServiceAdd: Returning %s.", ret ? "true" : "false");
  return (ret);
}


//
// 'cupsDNSSDServiceDelete()' - Cancel and free a service registration.
//

void
cupsDNSSDServiceDelete(
    cups_dnssd_service_t *service)	// I - Service
{
  DEBUG_printf("cupsDNSSDServiceDelete(service=%p)", (void *)service);

  if (service)
  {
    cups_dnssd_t *dnssd = service->dnssd;

    DEBUG_puts("2cupsDNSSDServiceDelete: Write locking rwlock.");
    cupsRWLockWrite(&dnssd->rwlock);

    cupsArrayRemove(dnssd->services, service);

    DEBUG_puts("2cupsDNSSDServiceDelete: Unlocking rwlock.");
    cupsRWUnlock(&dnssd->rwlock);
  }
}


//
// 'cupsDNSSDServiceGetContext()' - Get the DNS-SD context for the service
//                                  registration.
//

cups_dnssd_t *				// O - DNS-SD context or `NULL`
cupsDNSSDServiceGetContext(
    cups_dnssd_service_t *service)	// I - Service registration
{
  return (service ? service->dnssd : NULL);
}


//
// 'cupsDNSSDServiceGetName()' - Get the service instance name for the service registration.
//

const char *				// O - Service instance name
cupsDNSSDServiceGetName(
    cups_dnssd_service_t *service)	// I - Service registration
{
  return (service ? service->name : NULL);
}


//
// 'cupsDNSSDServiceNew()' - Create a new named service.
//
// This function creates a new DNS-SD service registration for the given service
// instance name and interface.  Specific services using the name are added
// using the @link cupsDNSSDServiceAdd@ function.
//
// The required service callback is called for select events, with the "flags"
// argument set to `CUPS_DNSSD_FLAGS_NONE` for a successful registration,
// `CUPS_DNSSD_FLAGS_COLLISION` when there is a name collision, or
// `CUPS_DNSSD_FLAGS_ERROR` when there is a problem completing the service
// registration.
//

cups_dnssd_service_t *			// O - Service or `NULL` on error
cupsDNSSDServiceNew(
    cups_dnssd_t            *dnssd,	// I - DNS-SD context
    uint32_t                if_index,	// I - Interface index, `CUPS_DNSSD_IF_INDEX_ANY`, or `CUPS_DNSSD_IF_INDEX_LOCAL`
    const char              *name,	// I - Name of service
    cups_dnssd_service_cb_t cb,		// I - Service registration callback function
    void                    *cb_data)	// I - Service registration callback data
{
  cups_dnssd_service_t	*service;	// Service registration


  DEBUG_printf("cupsDNSSDServiceNew(dnssd=%p, if_index=%u, name=\"%s\", cb=%p, cb_data=%p)", (void *)dnssd, (unsigned)if_index, name, (void *)cb, cb_data);

  // Range check input...
  if (!dnssd || !name || !cb)
    return (NULL);

  // Allocate memory for the service...
  if ((service = (cups_dnssd_service_t *)calloc(1, sizeof(cups_dnssd_service_t))) == NULL)
    return (NULL);

  service->dnssd    = dnssd;
  service->cb       = cb;
  service->cb_data  = cb_data;
  service->name     = strdup(name);
  service->if_index = if_index;

#ifdef HAVE_MDNSRESPONDER
#elif _WIN32
#else // HAVE_AVAHI
  service->group = avahi_entry_group_new(dnssd->client, (AvahiEntryGroupCallback)avahi_service_cb, service);

  if (!service->group)
  {
    report_error(dnssd, "Unable to create DNS-SD service registration: %s", avahi_strerror(avahi_client_errno(dnssd->client)));
    free(service->name);
    free(service);
    service = NULL;
    return (NULL);
  }

  DEBUG_printf("2cupsDNSSDServiceNew: service->group=%p", service->group);
#endif // HAVE_MDNSRESPONDER

  DEBUG_puts("2cupsDNSSDServiceNew: Write locking rwlock.");
  cupsRWLockWrite(&dnssd->rwlock);

  if (!dnssd->services)
  {
    // Create an array of services...
    if ((dnssd->services = cupsArrayNew3(NULL, NULL, NULL, 0, NULL, (cups_afree_cb_t)delete_service)) == NULL)
    {
      // Unable to create...
      free(service->name);
      free(service);
      service = NULL;
      goto done;
    }
  }

  DEBUG_printf("2cupsDNSSDServiceNew: Adding service %p.", (void *)service);
  cupsArrayAdd(dnssd->services, service);

  done:

  DEBUG_puts("2cupsDNSSDServiceNew: Unlocking rwlock.");
  cupsRWUnlock(&dnssd->rwlock);

  DEBUG_printf("2cupsDNSSDServiceNew: Returning %p.", (void *)service);
  return (service);
}


//
// 'cupsDNSSDServicePublish()' - Publish a service.
//
// This function publishes the DNS-SD services added using the
// @link cupsDNSSDServiceAdd@ function.
//

bool					// O - `true` on success, `false` on failure
cupsDNSSDServicePublish(
    cups_dnssd_service_t *service)	// I - Service
{
  bool		ret = true;		// Return value


  DEBUG_printf("cupsDNSSDServicePublish(service=%p)", (void *)service);

#if _WIN32
  (void)service;
#elif defined(HAVE_MDNSRESPONDER)
  (void)service;
#else // HAVE_AVAHI
  avahi_entry_group_commit(service->group);
  avahi_simple_poll_wakeup(service->dnssd->poll);
#endif // _WIN32

  DEBUG_printf("2cupsDNSSDServicePublish: Returning %s.", ret ? "true" : "false");
  return (ret);
}


//
// 'cupsDNSSDServiceSetLocation()' - Set the geolocation (LOC record) of a
//                                   service.
//
// This function sets the geolocation of a service using a 'geo:' URI (RFC 5870)
// of the form
// 'geo:LATITUDE,LONGITUDE[,ALTITUDE][;crs=CRSLABEL][;u=UNCERTAINTY]'.  The
// specified coordinates and uncertainty are converted into a DNS LOC record
// for the service name label.  Only the "wgs84" CRSLABEL string is supported.
//
// You must call this function prior to @link cupsDNSSDServiceAdd@.
//

bool					// O - `true` on success, `false` on failure
cupsDNSSDServiceSetLocation(
    cups_dnssd_service_t *service,	// I - Service
    const char           *geo_uri)	// I - Geolocation as a 'geo:' URI
{
  bool		ret = true;		// Return value
  const char	*geo_ptr;		// Pointer into 'geo;' URI
  double	lat = 0.0, lon = 0.0;	// Latitude and longitude in degrees
  double	alt = 0.0;		// Altitude in meters
  double	u = 5.0;		// Uncertainty in meters
  unsigned int	lat_ksec, lon_ksec;	// Latitude and longitude in thousandths of arc seconds, biased by 2^31
  unsigned int	alt_cm;			// Altitude in centimeters, biased by 10,000,000cm
  unsigned char	prec;			// Precision value


  // Range check input...
  if (!service || !geo_uri)
    return (false);

  // See if this is a WGS-84 coordinate...
  if ((geo_ptr = strstr(geo_uri, ";crs=")) != NULL && strncmp(geo_ptr + 5, "wgs84", 5))
  {
    // Not WGS-84...
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Only WGS-84 coordinates are supported."), true);
    return (false);
  }

  // Pull apart the "geo:" URI and convert to the integer representation for
  // the LOC record...
  sscanf(geo_uri, "geo:%lf,%lf,%lf", &lat, &lon, &alt);
  lat_ksec = (unsigned)((int)(lat * 3600000.0) + 0x40000000 + 0x40000000);
  lon_ksec = (unsigned)((int)(lon * 3600000.0) + 0x40000000 + 0x40000000);
  alt_cm   = (unsigned)((int)(alt * 100.0) + 10000000);

  if ((geo_ptr = strstr(geo_uri, ";u=")) != NULL)
    u = strtod(geo_ptr + 3, NULL);

  if (u < 0.0)
    u = 0.0;

  for (prec = 0, u = u * 100.0; u >= 10.0 && prec < 15; u = u * 0.01)
    prec ++;

  if (u < 10.0)
    prec |= (unsigned char)((int)u << 4);
  else
    prec |= (unsigned char)0x90;

  // Build the LOC record...
  service->loc[0]  = 0x00;		// Version
  service->loc[1]  = 0x51;		// Size (50cm)
  service->loc[2]  = prec;		// Horizontal precision
  service->loc[3]  = prec;		// Vertical precision

  service->loc[4]  = (unsigned char)(lat_ksec >> 24);
					// Latitude (32-bit big-endian)
  service->loc[5]  = (unsigned char)(lat_ksec >> 16);
  service->loc[6]  = (unsigned char)(lat_ksec >> 8);
  service->loc[7]  = (unsigned char)(lat_ksec);

  service->loc[8]  = (unsigned char)(lon_ksec >> 24);
					// Latitude (32-bit big-endian)
  service->loc[9]  = (unsigned char)(lon_ksec >> 16);
  service->loc[10] = (unsigned char)(lon_ksec >> 8);
  service->loc[11] = (unsigned char)(lon_ksec);

  service->loc[12] = (unsigned char)(alt_cm >> 24);
					// Altitude (32-bit big-endian)
  service->loc[13] = (unsigned char)(alt_cm >> 16);
  service->loc[14] = (unsigned char)(alt_cm >> 8);
  service->loc[15] = (unsigned char)(alt_cm);

  service->loc_set = true;

#ifdef HAVE_MDNSRESPONDER
  // Add LOC record in cupsDNSSDServiceAdd()

#elif _WIN32
  // Add LOC record in cupsDNSSDServiceAdd()

#else // HAVE_AVAHI
  // Add LOC record now...
  int error;				// Error code

  if ((error = avahi_entry_group_add_record(service->group, avahi_if_index(service->if_index), AVAHI_PROTO_UNSPEC, /*flags*/0, service->name, AVAHI_DNS_CLASS_IN, AVAHI_DNS_TYPE_LOC, /*ttl*/75 * 60, service->loc, sizeof(service->loc))) < 0)
  {
    report_error(service->dnssd, "Unable to register LOC record for '%s': %s", service->name, avahi_strerror(error));
    ret = false;
  }
#endif // HAVE_MDNSRESPONDER

  return (ret);
}


//
// 'delete_browse()' - Delete a browse request.
//

static void
delete_browse(
    cups_dnssd_browse_t *browse)	// I - Browse request
{
#ifdef HAVE_MDNSRESPONDER
  DNSServiceRefDeallocate(browse->ref);

#elif _WIN32

#else // HAVE_AVAHI
  size_t	i;			// Looping var

  for (i = 0; i < browse->num_browsers; i ++)
    avahi_service_browser_free(browse->browsers[i]);
#endif // HAVE_MDNSRESPONDER

  free(browse);
}


//
// 'delete_query()' - Delete a query request.
//

static void
delete_query(
    cups_dnssd_query_t *query)		// I - Query request
{
#ifdef HAVE_MDNSRESPONDER
  DNSServiceRefDeallocate(query->ref);

#elif _WIN32

#else // HAVE_AVAHI
  avahi_record_browser_free(query->browser);
#endif // HAVE_MDNSRESPONDER
}


//
// 'delete_resolve()' - Delete a resolve request.
//

static void
delete_resolve(
    cups_dnssd_resolve_t *resolve)	// I - Resolve request
{
#ifdef HAVE_MDNSRESPONDER
  DNSServiceRefDeallocate(resolve->ref);

#elif _WIN32

#else // HAVE_AVAHI
  avahi_service_resolver_free(resolve->resolver);
#endif // HAVE_MDNSRESPONDER

}


//
// 'delete_service()' - Delete a service registration.
//

static void
delete_service(
    cups_dnssd_service_t *service)	// I - Service
{
  free(service->name);

#ifdef HAVE_MDNSRESPONDER
  size_t	i;			// Looping var

  for (i = 0; i < service->num_refs; i ++)
    DNSServiceRefDeallocate(service->refs[i]);

#elif _WIN32

#else // HAVE_AVAHI
  avahi_entry_group_free(service->group);
#endif // HAVE_MDNSRESPONDER

  free(service);
}


//
// 'report_error()' - Report an error.
//

static void
report_error(cups_dnssd_t *dnssd,	// I - DNS-SD context
             const char   *message,	// I - printf-style message string
             ...)			// I - Additional arguments as needed
{
  va_list	ap;			// Pointer to arguments
  char		buffer[8192];		// Formatted message


  // Format the message...
  va_start(ap, message);
  vsnprintf(buffer, sizeof(buffer), message, ap);
  va_end(ap);

  DEBUG_printf("cupsDNSSD:report_error: %s", buffer);

  // Send it...
  if (dnssd->cb)
    (dnssd->cb)(dnssd->cb_data, buffer);
  else
    fprintf(stderr, "%s\n", buffer);
}


#ifdef HAVE_MDNSRESPONDER
//
// 'mdns_browse_cb()' - Handle DNS-SD browse callbacks from mDNSResponder.
//

static void
mdns_browse_cb(
    DNSServiceRef       ref,		// I - Service reference
    DNSServiceFlags     flags,		// I - Browse flags
    uint32_t            if_index,	// I - Interface index
    DNSServiceErrorType error,		// I - Error code, if any
    const char          *name,		// I - Service name
    const char          *regtype,	// I - Registration type
    const char          *domain,	// I - Domain
    cups_dnssd_browse_t *browse)	// I - Browse request
{
  char	temp[256],			// Temporary string
	*tempptr;			// Pointer into temporary string


  (void)ref;

  if (error != kDNSServiceErr_NoError)
    report_error(browse->dnssd, "DNS-SD browse error: %s", mdns_strerror(error));

  // Strip trailing dot from registration/service type...
  cupsCopyString(temp, regtype, sizeof(temp));
  if ((tempptr = temp + strlen(temp) - 1) >= temp && *tempptr == '.')
    *tempptr = '\0';

  // Strip leading dot from domain...
  if (domain && *domain == '.')
    domain ++;				// Eliminate leading period

  // Call the browse callback...
  (browse->cb)(browse, browse->cb_data, mdns_to_cups(flags, error), if_index, name, temp, domain);
}


//
// 'mdns_hostname_cb()' - Track changes to the mDNS hostname...
//

static void DNSSD_API
mdns_hostname_cb(
    DNSServiceRef       ref,		// I - Service reference (unsued)
    DNSServiceFlags     flags,		// I - Flags (unused)
    uint32_t            if_index,	// I - Interface index (unused)
    DNSServiceErrorType error,		// I - Error code, if any
    const char          *fullname,	// I - Search name (unused)
    uint16_t            rrtype,		// I - Record type (unused)
    uint16_t            rrclass,	// I - Record class (unused)
    uint16_t            rdlen,		// I - Record data length
    const void          *rdata,		// I - Record data
    uint32_t            ttl,		// I - Time-to-live (unused)
    cups_dnssd_t        *dnssd)		// I - DNS-SD context
{
  uint8_t	*rdataptr,		// Pointer into record data
		lablen;			// Length of current label
  char		temp[1024],		// Temporary hostname string
		*tempptr;		// Pointer into temporary string


  (void)ref;
  (void)flags;
  (void)if_index;
  (void)fullname;
  (void)rrtype;
  (void)rrclass;
  (void)ttl;

  // Check for errors...
  if (error != kDNSServiceErr_NoError)
    return;

  // Copy the hostname from the PTR record...
  for (rdataptr = (uint8_t *)rdata, tempptr = temp; rdlen > 0 && tempptr < (temp + sizeof(temp) - 2); rdlen -= lablen, rdataptr += lablen)
  {
    lablen = *rdataptr++;
    rdlen --;

    if (!rdlen || rdlen < lablen)
      break;

    if (tempptr > temp)
      *tempptr++ = '.';

    if (lablen < (sizeof(temp) - (size_t)(tempptr - temp)))
    {
      memcpy(tempptr, rdataptr, lablen);
      tempptr += lablen;
    }
  }

  *tempptr = '\0';

  // Ignore localhost...
  if (!strcmp(temp, "localhost"))
    return;

  // Look for changes to the hostname...
  DEBUG_puts("4mdns_hostname_cb: Write locking rwlock.");
  cupsRWLockWrite(&dnssd->rwlock);
  if (strcmp(temp, dnssd->hostname))
  {
    cups_dnssd_service_t *service;	// Current service

    // Copy the new hostname...
    cupsCopyString(dnssd->hostname, temp, sizeof(dnssd->hostname));
    dnssd->config_changes ++;

    // Notify services of the change...
    for (service = (cups_dnssd_service_t *)cupsArrayGetFirst(dnssd->services); service; service = (cups_dnssd_service_t *)cupsArrayGetNext(dnssd->services))
      (service->cb)(service, service->cb_data, CUPS_DNSSD_FLAGS_HOST_CHANGE);
  }
  DEBUG_puts("4mdns_hostname_cb: Unlocking rwlock.");
  cupsRWUnlock(&dnssd->rwlock);
}


//
// 'mdns_monitor()' - Monitor DNS-SD messages from mDNSResponder.
//

static void *				// O - Return value (always `NULL`)
mdns_monitor(cups_dnssd_t *dnssd)	// I - DNS-SD context
{
  DNSServiceErrorType	error;		// Current error
  struct pollfd		polldata;	// Polling data

  polldata.fd     = DNSServiceRefSockFD(dnssd->ref);
  polldata.events = POLLERR | POLLHUP | POLLIN;

  for (;;)
  {
#  ifndef _WIN32
    if (poll(&polldata, 1, 1000) < 0 && errno != EINTR && errno != EAGAIN)
      break;

    if (!(polldata.revents & POLLIN))
      continue;
#  endif // !_WIN32

    if ((error = DNSServiceProcessResult(dnssd->ref)) != kDNSServiceErr_NoError)
    {
      report_error(dnssd, "Unable to read response from DNS-SD service: %s", mdns_strerror(error));
      break;
    }
  }

  return (NULL);
}


//
// 'mdns_query_cb()' - Handle DNS-SD query callbacks from mDNSResponder.
//

static void
mdns_query_cb(
    DNSServiceRef       ref,		// I - Service reference
    DNSServiceFlags     flags,		// I - Query flags
    uint32_t            if_index,	// I - Interface index
    DNSServiceErrorType error,		// I - Error code, if any
    const char          *name,		// I - Service name
    uint16_t            rrtype,		// I - Record type
    uint16_t            rrclass,	// I - Record class
    uint16_t            rdlen,		// I - Response length
    const void          *rdata,		// I - Response data
    uint32_t            ttl,		// I - Time-to-live value
    cups_dnssd_query_t  *query)		// I - Query request
{
  (void)ref;
  (void)rrclass;
  (void)ttl;

  if (error != kDNSServiceErr_NoError)
    report_error(query->dnssd, "DNS-SD query error: %s", mdns_strerror(error));

  (query->cb)(query, query->cb_data, mdns_to_cups(flags, error), if_index, name, rrtype, rdata, rdlen);
}


//
// 'mdns_resolve_cb()' - Handle DNS-SD resolution callbacks from mDNSResponder.
//

static void
mdns_resolve_cb(
    DNSServiceRef        ref,		// I - Service reference
    DNSServiceFlags      flags,		// I - Registration flags
    uint32_t             if_index,	// I - Interface index
    DNSServiceErrorType  error,		// I - Error code, if any
    const char           *fullname,	// I - Full name of service
    const char           *host,		// I - Hostname of service
    uint16_t             port,		// I - Port number in network byte order
    uint16_t             txtlen,	// I - Length of TXT record
    const unsigned char  *txtrec,	// I - TXT record
    cups_dnssd_resolve_t *resolve)	// I - Resolve request
{
  int		num_txt;		// Number of TXT key/value pairs
  cups_option_t	*txt;			// TXT key/value pairs


  (void)ref;

  if (error != kDNSServiceErr_NoError)
    report_error(resolve->dnssd, "DNS-SD resolve error: %s", mdns_strerror(error));

  num_txt = cupsDNSSDDecodeTXT(txtrec, txtlen, &txt);

  (resolve->cb)(resolve, resolve->cb_data, mdns_to_cups(flags, error), if_index, fullname, host, ntohs(port), num_txt, txt);

  cupsFreeOptions(num_txt, txt);
}


//
// 'mdns_service_cb()' - Handle DNS-SD service registration callbacks from
//                       mDNSResponder.
//

static void
mdns_service_cb(
    DNSServiceRef        ref,		// I - Service reference
    DNSServiceFlags      flags,		// I - Registration flags
    DNSServiceErrorType  error,		// I - Error code, if any
    const char           *name,		// I - Service name
    const char           *regtype,	// I - Registration type
    const char           *domain,	// I - Domain
    cups_dnssd_service_t *service)	// I - Service registration
{
  (void)ref;
  (void)name;
  (void)regtype;
  (void)domain;

  if (error != kDNSServiceErr_NoError)
    report_error(service->dnssd, "DNS-SD service registration error: %s", mdns_strerror(error));

  (service->cb)(service, service->cb_data, mdns_to_cups(flags, error));
}


//
// 'mdns_strerror()' - Convert an error code to a string.
//

static const char *			// O - Error message
mdns_strerror(
    DNSServiceErrorType errorCode)	// I - Error code
{
  switch (errorCode)
  {
    case kDNSServiceErr_NoError :
        return ("No error");

    case kDNSServiceErr_Unknown :
    default :
        return ("Unknown error");

    case kDNSServiceErr_NoSuchName :
        return ("Name not found");

    case kDNSServiceErr_NoMemory :
        return ("Out of memory");

    case kDNSServiceErr_BadParam :
        return ("Bad parameter");

    case kDNSServiceErr_BadReference :
        return ("Bad service reference");

    case kDNSServiceErr_BadState :
        return ("Bad state");

    case kDNSServiceErr_BadFlags :
        return ("Bad flags argument");

    case kDNSServiceErr_Unsupported :
        return ("Unsupported feature");

    case kDNSServiceErr_NotInitialized :
        return ("Not initialized");

    case kDNSServiceErr_AlreadyRegistered :
        return ("Name already registered");

    case kDNSServiceErr_NameConflict :
        return ("Name conflicts");

    case kDNSServiceErr_Invalid :
        return ("Invalid argument");

    case kDNSServiceErr_Firewall :
        return ("Firewall prevents access");

    case kDNSServiceErr_Incompatible :
        return ("Client library incompatible with background daemon");

    case kDNSServiceErr_BadInterfaceIndex :
        return ("Bad interface index");

    case kDNSServiceErr_Refused :
        return ("Connection refused");

    case kDNSServiceErr_NoSuchRecord :
        return ("DNS record not found");

    case kDNSServiceErr_NoAuth :
        return ("No authoritative answer");

    case kDNSServiceErr_NoSuchKey :
        return ("TXT record key not found");

    case kDNSServiceErr_NATTraversal :
        return ("Unable to traverse via NAT");

    case kDNSServiceErr_DoubleNAT :
        return ("Double NAT is in use");

    case kDNSServiceErr_BadTime :
        return ("Bad time value");

    case kDNSServiceErr_BadSig :
        return ("Bad signal");

    case kDNSServiceErr_BadKey :
        return ("Bad TXT record key");

    case kDNSServiceErr_Transient :
        return ("Transient error");

    case kDNSServiceErr_ServiceNotRunning :
        return ("Background daemon not running");

    case kDNSServiceErr_NATPortMappingUnsupported :
        return ("NAT doesn't support PCP, NAT-PMP or UPnP");

    case kDNSServiceErr_NATPortMappingDisabled :
        return ("NAT supports PCP, NAT-PMP or UPnP, but it's disabled by the administrator");

    case kDNSServiceErr_NoRouter :
        return ("No router configured, probably no network connectivity");

    case kDNSServiceErr_PollingMode :
        return ("Polling error");

    case kDNSServiceErr_Timeout :
        return ("Timeout");

#if !_WIN32 // Bonjour SDK for Windows doesn't define this...
    case kDNSServiceErr_DefunctConnection :
        return ("Connection lost");
#endif // !_WIN32
  }
}


//
// 'mdns_to_cups()' - Convert mDNSResponder flags to CUPS DNS-SD flags...
//

static cups_dnssd_flags_t		// O - CUPS DNS-SD flags
mdns_to_cups(
    DNSServiceFlags     flags,		// I - mDNSResponder flags
    DNSServiceErrorType error)		// I - mDNSResponder error code
{
  cups_dnssd_flags_t	cups_flags = CUPS_DNSSD_FLAGS_NONE;
					// CUPS DNS-SD flags


  if (flags & kDNSServiceFlagsAdd)
    cups_flags |= CUPS_DNSSD_FLAGS_ADD;
  if (flags & kDNSServiceFlagsMoreComing)
    cups_flags |= CUPS_DNSSD_FLAGS_MORE;
  if (error != kDNSServiceErr_NoError)
    cups_flags |= CUPS_DNSSD_FLAGS_ERROR;

  return (cups_flags);
}


#elif _WIN32


#else // HAVE_AVAHI
#  ifdef DEBUG
static const char * const avahi_events[] =	// Event names
{
  "AVAHI_BROWSER_NEW",
  "AVAHI_BROWSER_REMOVE",
  "AVAHI_BROWSER_CACHE_EXHAUSTED",
  "AVAHI_BROWSER_ALL_FOR_NOW",
  "AVAHI_BROWSER_FAILURE"
};
#  endif // DEBUG


//
// 'avahi_browse_cb()' - Handle browse callbacks from Avahi.
//

static void
avahi_browse_cb(
    AvahiServiceBrowser    *browser,	// I - Avahi browser
    AvahiIfIndex           if_index,	// I - Interface index
    AvahiProtocol          protocol,	// I - Network protocol (unused)
    AvahiBrowserEvent      event,	// I - What happened
    const char             *name,	// I - Service name
    const char             *type,	// I - Service type
    const char             *domain,	// I - Domain
    AvahiLookupResultFlags flags,	// I - Flags
    cups_dnssd_browse_t    *browse)	// I - CUPS browse request
{
  cups_dnssd_flags_t	cups_flags;	// CUPS DNS-SD flags


  (void)protocol;
  (void)flags;

  switch (event)
  {
    case AVAHI_BROWSER_NEW :
        cups_flags = CUPS_DNSSD_FLAGS_ADD;
        break;
    case AVAHI_BROWSER_REMOVE :
        cups_flags = CUPS_DNSSD_FLAGS_NONE;
        break;
    case AVAHI_BROWSER_FAILURE :
        cups_flags = CUPS_DNSSD_FLAGS_ERROR;
        break;

    default :
        // Other events don't get passed through...
        return;
  }

  browse->dnssd->in_callback = true;
  (browse->cb)(browse, browse->cb_data, cups_flags, (uint32_t)if_index, name, type, domain);
  browse->dnssd->in_callback = false;
}


//
// 'avahi_client_cb()' - Client callback for Avahi.
//
// Called whenever the client or server state changes...
//

static void
avahi_client_cb(
    AvahiClient      *c,		// I - Client
    AvahiClientState state,		// I - Current state
    cups_dnssd_t     *dnssd)		// I - DNS-SD context
{
  if (!c)
    return;

  if (state == AVAHI_CLIENT_FAILURE)
  {
    if (avahi_client_errno(c) == AVAHI_ERR_DISCONNECTED)
      report_error(dnssd, "Avahi server crashed.");
  }
  else if (state == AVAHI_CLIENT_S_RUNNING)
  {
    // Let the services know the hostname has changed...
    cups_dnssd_service_t *service;	// Current service

    DEBUG_puts("4avahi_client_cb: Write locking rwlock.");
    cupsRWLockWrite(&dnssd->rwlock);

    dnssd->config_changes ++;

    for (service = (cups_dnssd_service_t *)cupsArrayGetFirst(dnssd->services); service; service = (cups_dnssd_service_t *)cupsArrayGetNext(dnssd->services))
      (service->cb)(service, service->cb_data, CUPS_DNSSD_FLAGS_HOST_CHANGE);

    DEBUG_puts("4avahi_client_cb: Unlocking rwlock.");
    cupsRWUnlock(&dnssd->rwlock);
  }
}


//
// 'avahi_domain_cb()' - Domain callback.
//

static void
avahi_domain_cb(
    AvahiDomainBrowser     *b,		// I - Browser (not used)
    AvahiIfIndex           interface,	// I - Network interface (not used)
    AvahiProtocol          protocol,	// I - Protocol (not used)
    AvahiBrowserEvent      event,	// I - Event
    const char             *domain,	// I - Domain name
    AvahiLookupResultFlags flags,	// I - Lookup flags (not used)
    cups_dnssd_t           *dnssd)	// I - DNS-SD context
{
  size_t	i;			// Looping var


  (void)b;
  (void)interface;
  (void)protocol;
  (void)flags;

  DEBUG_printf("3avahi_domain_cb(..., event=%s, domain=\"%s\", ...)", avahi_events[event], domain);

  if (!domain || !strcmp(domain, "."))
    return;

  DEBUG_puts("4avahi_domain_cb: Write locking rwlock.");
  cupsRWLockWrite(&dnssd->rwlock);

  if (event == AVAHI_BROWSER_NEW)
  {
    // Add a domain - see if the domain is new to us...
    for (i = 0; i < dnssd->num_domains; i ++)
    {
      if (!_cups_strcasecmp(dnssd->domains[i], domain))
        break;
    }

    if (i >= dnssd->num_domains && dnssd->num_domains < (sizeof(dnssd->domains) / sizeof(dnssd->domains[0])))
    {
      // New, copy the domain name...
      cupsCopyString(dnssd->domains[i], domain, sizeof(dnssd->domains[0]));
      dnssd->num_domains ++;
      DEBUG_printf("4avahi_domain_cb: Added domain \"%s\", num_domains=%u", domain, (unsigned)dnssd->num_domains);
    }
  }
  else if (event == AVAHI_BROWSER_REMOVE)
  {
    // Remove the domain...
    for (i = 0; i < dnssd->num_domains; i ++)
    {
      if (!_cups_strcasecmp(dnssd->domains[i], domain))
      {
        dnssd->num_domains --;
        if (i < dnssd->num_domains)
          memmove(dnssd->domains[i], dnssd->domains[i + 1], (dnssd->num_domains - i) * sizeof(dnssd->domains[0]));

        DEBUG_printf("4avahi_domain_cb: Removed domain \"%s\", num_domains=%u", domain, (unsigned)dnssd->num_domains);
        break;
      }
    }
  }

  DEBUG_puts("4avahi_domain_cb: Unlocking rwlock.");
  cupsRWUnlock(&dnssd->rwlock);
}


//
// 'avahi_if_index()' - Convert the DNS-SD interface index to an Avahi interface index.
//

static AvahiIfIndex			// O - Avahi interface index
avahi_if_index(uint32_t if_index)	// I - DNS-SD interface index
{
  if (if_index == CUPS_DNSSD_IF_INDEX_ANY)
    return (AVAHI_IF_UNSPEC);
  else if (if_index == CUPS_DNSSD_IF_INDEX_LOCAL)
    return (if_nametoindex("lo"));
  else
    return ((int)if_index);
}


//
// 'avahi_monitor()' - Background thread for Avahi.
//

static void *				// O - Exit status
avahi_monitor(cups_dnssd_t *dnssd)	// I - DNS-SD context
{
  DEBUG_printf("3avahi_monitor(dnssd=%p)", (void *)dnssd);

  DEBUG_puts("4avahi_monitor: Locking mutex.");
  cupsMutexLock(&dnssd->mutex);

  DEBUG_puts("4avahi_monitor: Running poll loop.");
  avahi_simple_poll_loop(dnssd->poll);

  DEBUG_puts("4avahi_monitor: Unlocking mutex.");
  cupsMutexUnlock(&dnssd->mutex);

  return (NULL);
}


//
// 'avahi_poll_cb()' - Poll callback for Avahi event handler...
//

static int				// O - Number of file descriptors or `-1` on error
avahi_poll_cb(struct pollfd *ufds,	// I - File descriptors for poll
              unsigned int  nfds,	// I - Number of file descriptors
              int           timeout,	// I - Timeout in milliseconds
              cups_dnssd_t  *dnssd)	// I - DNS-SD context
{
  int	ret;				// Return value


  DEBUG_printf("3avahi_poll_cb(ufds=%p, nfds=%u, timeout=%d, dnssd=%p)", (void *)ufds, nfds, timeout, (void *)dnssd);

  DEBUG_puts("4avahi_poll_cb: Unlocking mutex.");
  cupsMutexUnlock(&dnssd->mutex);

  DEBUG_puts("4avahi_poll_cb: Polling sockets...");
  ret = poll(ufds, nfds, timeout);
  DEBUG_printf("4avahi_poll_cb: poll() returned %d...", ret);

  DEBUG_puts("4avahi_poll_cb: Locking mutex.");
  cupsMutexLock(&dnssd->mutex);

  return (ret);
}


//
// 'avahi_query_cb()' - Query callback for Avahi.
//

static void
avahi_query_cb(
    AvahiRecordBrowser     *browser,	// I - Browser
    AvahiIfIndex           if_index,	// I - Interface index
    AvahiProtocol          protocol,	// I - Network protocol (not used)
    AvahiBrowserEvent      event,	// I - What happened
    const char             *fullname,	// I - Full service name
    uint16_t               rrclass,	// I - Record class (not used)
    uint16_t               rrtype,	// I - Record type
    const void             *rdata,	// I - Record data
    size_t                 rdlen,	// I - Size of record data
    AvahiLookupResultFlags flags,	// I - Flags
    cups_dnssd_query_t     *query)	// I - Query request
{
  (void)browser;
  (void)protocol;
  (void)rrclass;

  DEBUG_printf("3avahi_query_cb(..., event=%s, fullname=\"%s\", ..., query=%p)", avahi_events[event], fullname, query);

  query->dnssd->in_callback = true;
  (query->cb)(query, query->cb_data, event == AVAHI_BROWSER_FAILURE ? CUPS_DNSSD_FLAGS_ERROR : event == AVAHI_BROWSER_NEW ? CUPS_DNSSD_FLAGS_ADD : CUPS_DNSSD_FLAGS_NONE, (uint32_t)if_index, fullname, rrtype, rdata, rdlen);
  query->dnssd->in_callback = false;
}


//
// 'avahi_resolve_cb()' - Resolver callback for Avahi.
//

static void
avahi_resolve_cb(
    AvahiServiceResolver   *resolver,	// I - Service resolver
    AvahiIfIndex           if_index,	// I - Interface index
    AvahiProtocol          protocol,	// I - Network protocol (not used)
    AvahiResolverEvent     event,	// I - What happened
    const char             *name,	// I - Service name
    const char             *type,	// I - Service type
    const char             *domain,	// I - Domain
    const char             *host,	// I - Host name
    const AvahiAddress     *address,	// I - Address
    uint16_t               port,	// I - Port number
    AvahiStringList        *txtrec,	// I - TXT record
    AvahiLookupResultFlags flags,	// I - Flags
    cups_dnssd_resolve_t   *resolve)	// I - Resolve request
{
  AvahiStringList *txtpair;		// Current pair
  size_t	num_txt = 0;		// Number of TXT key/value pairs
  cups_option_t	*txt = NULL;		// TXT key/value pairs
  char		fullname[1024];		// Full service name


  DEBUG_printf("3avahi_resolve_cb(resolver=%p, if_index=%d, protocol=%d, event=%s, name=\"%s\", type=\"%s\", domain=\"%s\", host=\"%s\", address=%p, port=%u, txtrec=%p, flags=%u, resolve=%p)", (void *)resolver, if_index, protocol, avahi_events[event], name, type, domain, host, (void *)address, (unsigned)port, (void *)txtrec, (unsigned)flags, (void *)resolve);

  if (!resolver)
    return;

  (void)resolver;
  (void)protocol;
  (void)flags;

  // Map the addresses "127.0.0.1" (IPv4) and "::1" (IPv6) to "localhost" to work around a well-known Avahi registration bug for local-only services (Issue #970)
  if (address->proto == AVAHI_PROTO_INET && address->data.ipv4.address == htonl(0x7f000001))
  {
    DEBUG_puts("4avahi_resolve_cb: Mapping 127.0.0.1 to localhost.");
    host = "localhost";
  }
  else if (address->proto == AVAHI_PROTO_INET6 && address->data.ipv6.address[0] == 0 && address->data.ipv6.address[1] == 0 && address->data.ipv6.address[2] == 0 && address->data.ipv6.address[3] == 0 && address->data.ipv6.address[4] == 0 && address->data.ipv6.address[5] == 0 && address->data.ipv6.address[6] == 0 && address->data.ipv6.address[7] == 0 && address->data.ipv6.address[8] == 0 && address->data.ipv6.address[9] == 0 && address->data.ipv6.address[10] == 0 && address->data.ipv6.address[11] == 0 && address->data.ipv6.address[12] == 0 && address->data.ipv6.address[13] == 0 && address->data.ipv6.address[14] == 0 && address->data.ipv6.address[15] == 1)
  {
    DEBUG_puts("4avahi_resolve_cb: Mapping ::1 to localhost.");
    host = "localhost";
  }

  // Convert TXT key/value pairs into CUPS option array...
  for (txtpair = txtrec; txtpair; txtpair = avahi_string_list_get_next(txtpair))
  {
    char *key, *value;			// Key and value

    avahi_string_list_get_pair(txtpair, &key, &value, NULL);

    DEBUG_printf("4avahi_resolve_cb: txt[%u].name=\"%s\", .value=\"%s\"", (unsigned)num_txt, key, value);
    num_txt = cupsAddOption(key, value, num_txt, &txt);

    avahi_free(key);
    avahi_free(value);
  }
  DEBUG_printf("4avahi_resolve_cb: num_txt=%u", (unsigned)num_txt);

  // Create a full name for the service...
  cupsDNSSDAssembleFullName(fullname, sizeof(fullname), name, type, domain);
  DEBUG_printf("4avahi_resolve_cb: fullname=\"%s\"", fullname);

  // Do the resolve callback and free the TXT record stuff...
  (resolve->cb)(resolve, resolve->cb_data, event == AVAHI_RESOLVER_FAILURE ? CUPS_DNSSD_FLAGS_ERROR : CUPS_DNSSD_FLAGS_NONE, (uint32_t)if_index, fullname, host, port, num_txt, txt);

  cupsFreeOptions(num_txt, txt);
}


//
// 'avahi_service_cb()' - Service callback for Avahi.
//

static void
avahi_service_cb(
    AvahiEntryGroup      *srv,		// I - Service
    AvahiEntryGroupState state,		// I - Registration state
    cups_dnssd_service_t *service)	// I - Service registration
{
#  ifdef DEBUG
  static const char * const avahi_states[] =
  {					// AvahiEntryGroupState strings
    "AVAHI_ENTRY_GROUP_UNCOMMITED",
    "AVAHI_ENTRY_GROUP_REGISTERING",
    "AVAHI_ENTRY_GROUP_ESTABLISHED",
    "AVAHI_ENTRY_GROUP_COLLISION",
    "AVAHI_ENTRY_GROUP_FAILURE"
  };
#  endif // DEBUG


  (void)srv;

  DEBUG_printf("3avahi_service_cb(srv=%p, state=%s, service=%p)", srv, avahi_states[state], service);

  (service->cb)(service, service->cb_data, state == AVAHI_ENTRY_GROUP_COLLISION ? CUPS_DNSSD_FLAGS_COLLISION : CUPS_DNSSD_FLAGS_NONE);
}
#endif // HAVE_MDNSRESPONDER
