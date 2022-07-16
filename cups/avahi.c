/*
 * implementation file using avahi discovery service APIS
 *
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
#include "avahi.h";

/*
 * Structures...
 */

typedef struct avahi_srv_s		/* Service information */
{
#ifdef HAVE_MDNSRESPONDER
  DNSServiceRef	ref;			/* Service reference for query */
#elif defined(HAVE_AVAHI)
  AvahiServiceResolver *ref;		/* Resolver */
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
 * Local globals...
 */

#ifdef HAVE_MDNSRESPONDER
static DNSServiceRef dnssd_ref;		/* Master service reference */
#elif defined(HAVE_AVAHI)
static AvahiClient *avahi_client = NULL;/* Client information */
static int	avahi_got_data = 0;	/* Got data from poll? */
static AvahiSimplePoll *avahi_poll = NULL; /* Poll information */
static AvahiServiceBrowser* sb;

#endif /* HAVE_MDNSRESPONDER */

static int	address_family = AF_UNSPEC;
					/* Address family for LIST */
static int	bonjour_error = 0;	/* Error browsing/resolving? */
static double	bonjour_timeout = 1.0;	/* Timeout in seconds */
static int	ipp_version = 20;	/* IPP version for LIST */


#ifdef HAVE_MDNSRESPONDER
static void DNSSD_API	resolve_callback(DNSServiceRef sdRef, DNSServiceFlags flags, uint32_t interfaceIndex, DNSServiceErrorType errorCode, const char *fullName, const char *hostTarget, uint16_t port, uint16_t txtLen, const unsigned char *txtRecord, void *context) _CUPS_NONNULL(1,5,6,9, 10);
#elif defined(HAVE_AVAHI)
static int		poll_callback(struct pollfd *pollfds,
			              unsigned int num_pollfds, int timeout,
			              void *context);
#endif /* HAVE_MDNSRESPONDER */

static int err = 0;
                    
// individual functions for browse and resolve

/*
    implementation of avahi_intialize, to create objects necessary for 
    browse and resolve to work
    */

AvahiClient* avahi_intialize(){
    AvahiClient *client = NULL;
    
    /* allocate main loop object */
    if (!(avahi_poll = avahi_simple_poll_new())) {
        fprintf(stderr, "Initialization Error, Failed to create simple poll object.\n");
        return NULL;
    }

    avahi_simple_poll_set_func(avahi_poll, poll_callback, NULL);

    int error;
    /* allocate a new client */
    client = avahi_client_new(avahi_simple_poll_get(avahi_poll), (AvahiClientFlags)0, client_callback, NULL, &error);

    if(!client){
        fprintf(stderr, "Initialization Error, Failed to create client: %s\n", avahi_strerror(error));
        return NULL;
    }

    return client;
}


//things to figure out yet
//1. return type and error handling
//2. more/specific parameters
void browse_services(char *regtype){

    //initialize a client object
    if(!avahi_client && (avahi_client = avahi_intialize() == NULL)){
        fprintf(stderr, "Initialization Error");
        return ;
    }

    //we may need to change domain parameter below, currently it is default(.local) 
    if (!sb && !(sb = avahi_service_browser_new(avahi_client, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, regtype, NULL, (AvahiLookupFlags)0, browse_callback, avahi_client))) {
        return ;
    }
    
    //assuming avahi_service_browser_new returns NULL on failure
    if(!sb){
      err = avahi_client_errno(avahi_client);
    }
    
    if (err)
    {
      _cupsLangPrintf(stderr, _("ippfind: Unable to browse or resolve: %s"),
                      dnssd_error_string(err));

      return ;
    }
}


void resolve_services(avahi_srv_t *service){
  if (getenv("IPPFIND_DEBUG"))
        fprintf(stderr, "Resolving name=\"%s\", regtype=\"%s\", domain=\"%s\"\n", service->name, service->regtype, service->domain);

#ifdef HAVE_MDNSRESPONDER
      service->ref = dnssd_ref;
      err          = DNSServiceResolve(&(service->ref),
                                       kDNSServiceFlagsShareConnection, 0, service->name,
				       service->regtype, service->domain, resolve_callback,
				       service);

#elif defined(HAVE_AVAHI)
      service->ref = avahi_service_resolver_new(avahi_client, AVAHI_IF_UNSPEC,
                                                AVAHI_PROTO_UNSPEC, service->name,
                                                service->regtype, service->domain,
                                                AVAHI_PROTO_UNSPEC, 0,
                                                resolve_callback, service);
      if (service->ref)
        err = 0;
      else
        err = avahi_client_errno(avahi_client);
#endif /* HAVE_MDNSRESPONDER */
}

/*
 * 'client_callback()' - Avahi client callback function.
 */

static void
client_callback(
    AvahiClient      *client,		/* I - Client information (unused) */
    AvahiClientState state,		/* I - Current state */
    void             *context)		/* I - User data (unused) */
{
  (void)client;
  (void)context;

 /*
  * If the connection drops, quit.
  */

  if (state == AVAHI_CLIENT_FAILURE)
  {
    fputs("DEBUG: Avahi connection failed.\n", stderr);
    bonjour_error = 1;
    avahi_simple_poll_quit(avahi_poll);
  }
}

#ifdef HAVE_AVAHI
/*
 * 'poll_callback()' - Wait for input on the specified file descriptors.
 *
 * Note: This function is needed because avahi_simple_poll_iterate is broken
 *       and always uses a timeout of 0 (!) milliseconds.
 *       (Avahi Ticket #364)
 */

static int				/* O - Number of file descriptors matching */
poll_callback(
    struct pollfd *pollfds,		/* I - File descriptors */
    unsigned int  num_pollfds,		/* I - Number of file descriptors */
    int           timeout,		/* I - Timeout in milliseconds (unused) */
    void          *context)		/* I - User data (unused) */
{
  int	val;				/* Return value */


  (void)timeout;
  (void)context;

  val = poll(pollfds, num_pollfds, 500);

  if (val > 0)
    avahi_got_data = 1;

  return (val);
}
#endif /* HAVE_AVAHI */

/*
 * 'get_service()' - Create or update a device.
 */

static avahi_srv_t *			/* O - Service */
avahi_get_service(cups_array_t *services,	/* I - Service array */
	    const char   *serviceName,	/* I - Name of service/device */
	    const char   *regtype,	/* I - Type of service */
	    const char   *replyDomain)	/* I - Service domain */
{
  avahi_srv_t	key,			/* Search key */
		*service;		/* Service */
  char		fullName[kDNSServiceMaxDomainName];
					/* Full name for query */


 /*
  * See if this is a new device...
  */

  key.name    = (char *)serviceName;
  key.regtype = (char *)regtype;

  for (service = cupsArrayFind(services, &key);
       service;
       service = cupsArrayNext(services))
    if (_cups_strcasecmp(service->name, key.name))
      break;
    else if (!strcmp(service->regtype, key.regtype))
      return (service);

 /*
  * Yes, add the service...
  */

  if ((service = calloc(sizeof(avahi_srv_t), 1)) == NULL)
    return (NULL);

  service->name     = strdup(serviceName);
  service->domain   = strdup(replyDomain);
  service->regtype  = strdup(regtype);

  cupsArrayAdd(services, service);

 /*
  * Set the "full name" of this service, which is used for queries and
  * resolves...
  */

#ifdef HAVE_MDNSRESPONDER
  DNSServiceConstructFullName(fullName, serviceName, regtype, replyDomain);
#else /* HAVE_AVAHI */
  avahi_service_name_join(fullName, kDNSServiceMaxDomainName, serviceName,
                          regtype, replyDomain);
#endif /* HAVE_MDNSRESPONDER */

  service->fullName = strdup(fullName);

  return (service);
}
