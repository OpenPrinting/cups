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

int avahi_initialize(AvahiPoll **avahi_poll, AvahiClient **avahi_client, void (*client_callback)(AvahiClient*, AvahiClientState, void *), int *err)
{
   
    /* allocate main loop object */
    if (!(*avahi_poll))
    {
        *avahi_poll = avahi_simple_poll_new();

        // if(avahi_poll){

        // }

        if (!(*avahi_poll))
        {
            fprintf(stderr, "%s: Failed to create simple poll object.\n");
            return 0;
        }
    }

    if (*avahi_poll)
        avahi_simple_poll_set_func(*avahi_poll, poll_callback, NULL);

    int error;
    /* allocate a new client */
    *avahi_client = avahi_client_new(avahi_simple_poll_get(*avahi_poll), (AvahiClientFlags)0, *client_callback, *avahi_poll, err);

    if (!(*avahi_client))
    {
        fprintf(stderr, "Initialization Error, Failed to create client: %s\n", avahi_strerror(*err));
        return 0;
    }

    return 1;
}

void browse_services(AvahiClient **avahi_client, char *regtype, char *domain, cups_array_t *services, int *err)
{

    AvahiServiceBrowser *sb = NULL;
    int avahi_client_result = 0;

    // we may need to change domain parameter below, currently it is default(.local)
    sb = avahi_service_browser_new(*avahi_client, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, regtype, domain, (AvahiLookupFlags)0, browse_callback, services);

    // assuming avahi_service_browser_new returns NULL on failure
   if (!sb)
    {   
       *err = avahi_client_errno(*avahi_client);
    }
    else {
        *err = 0;
    }

}

//TODO
void resolve_services(AvahiClient **avahi_client, avahi_srv_t *service, int *err)
{
    if (getenv("IPPFIND_DEBUG"))
        fprintf(stderr, "Resolving name=\"%s\", regtype=\"%s\", domain=\"%s\"\n", service->name, service->regtype, service->domain);

#ifdef HAVE_MDNSRESPONDER
    service->ref = dnssd_ref;
    err = DNSServiceResolve(&(service->ref),
                            kDNSServiceFlagsShareConnection, 0, service->name,
                            service->regtype, service->domain, resolve_callback,
                            service);

#elif defined(HAVE_AVAHI)
    service->ref = avahi_service_resolver_new(*avahi_client, AVAHI_IF_UNSPEC,
                                              AVAHI_PROTO_UNSPEC, service->name,
                                              service->regtype, service->domain,
                                              AVAHI_PROTO_UNSPEC, 0,
                                              resolve_callback, service);
    if (service->ref)
        *err = 0;
    else
        *err = avahi_client_errno(avahi_client);
#endif /* HAVE_MDNSRESPONDER */

}