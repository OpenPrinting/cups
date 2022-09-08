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
#include "avahi.h"

// individual functions for browse and resolve

/*
    implementation of avahi_intialize, to create objects necessary for
    browse and resolve to work
    */

int avahiInitialize(AvahiPoll **avahi_poll, AvahiClient **avahi_client, void (*client_callback)(), int *err)
{

    /* allocate main loop object */
    if (!(*avahi_poll))
    {
        *avahi_poll = avahi_simple_poll_new();

        fprintf(stderr, "assigning avahi_poll = %p\n\n", *avahi_poll);

        if (!(*avahi_poll))
        {
            fprintf(stderr, "Failed to create simple poll object.\n");
            return 0;
        }
    }

    if (*avahi_poll){
        fprintf(stderr, "before setting poll %p\n", *avahi_poll);
        avahi_simple_poll_set_func(*avahi_poll, _pollCallback, NULL);
        fprintf(stderr, "after setting poll %p\n", *avahi_poll);
    }

        
    /* allocate a new client */
    *avahi_client = avahi_client_new(avahi_simple_poll_get(*avahi_poll), (AvahiClientFlags)0, *client_callback, *avahi_poll, err);

    if (!(*avahi_client))
    {
        fprintf(stderr, "Initialization Error, Failed to create client: %s\n", avahi_strerror(*err));
        return 0;
    }

    fprintf(stderr, "finished intialize with avahi_client = %p\n", *avahi_client);
    return 1;
}

// things to figure out yet
// 1. return type and error handling
// 2. more/specific parameters
void browseServices(AvahiClient **avahi_client, char *regtype, avahi_srv_t *service, cups_array_t *services, void (*browse_callback)(), int *err)
{
    // we may need to change domain parameter below, currently it is default(.local)
    if (avahi_service_browser_new(*avahi_client, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, regtype, NULL, (AvahiLookupFlags)0, browse_callback, services) == NULL)
    {
        *err = avahi_client_errno(*avahi_client);
    }
    else *err = 0;

    fprintf(stderr, "finishing browse_services\n");
}

void resolveServices(AvahiClient **avahi_client, avahi_srv_t *service, cups_array_t *services, rcb* ptr, int *err)
{
    fprintf(stderr, "inside resolveServices, avahi_client = %p\n", *avahi_client);

#ifdef HAVE_MDNSRESPONDER
    service->ref = dnssd_ref;
    err = DNSServiceResolve(&(service->ref),
                            kDNSServiceFlagsShareConnection, 0, service->name,
                            service->regtype, service->domain, resolveCallback,
                            service);

#elif defined(HAVE_AVAHI)
    service->ref = avahi_service_resolver_new(*avahi_client, AVAHI_IF_UNSPEC,
                                              AVAHI_PROTO_UNSPEC, service->name,
                                              service->regtype, service->domain,
                                              AVAHI_PROTO_UNSPEC, 0,
                                              *ptr, service);

    if(service->ref){
        fprintf(stderr, "service->ref = %p\n", service->ref);
    }

    // resolveCallback(NULL, 0, 0, 0, NULL, NULL, NULL, NULL, NULL, 0, NULL, 0, service);
    fprintf(stderr, "uri = %s\n", service->uri);

    if (service->ref)
        *err = 0;
    else
        *err = avahi_client_errno(avahi_client);
#endif /* HAVE_MDNSRESPONDER */
}

int _pollCallback(struct pollfd *pollfds,
                  unsigned int num_pollfds, int timeout,
                  void *context)
{
    fprintf(stderr, "inside poll_callback\n");
    return 1;
}