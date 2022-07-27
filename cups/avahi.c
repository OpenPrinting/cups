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

/*
    implementation of avahi_intialize, to create objects necessary for
    browse and resolve to work
*/
int avahiInitialize(AvahiPoll **avahi_poll, AvahiClient **avahi_client, void (*_clientCallback)(AvahiClient*, AvahiClientState, void *), void *data, int *err)
{
   
    /* allocate main loop object */
    if (!(*avahi_poll))
    {
        *avahi_poll = avahi_simple_poll_new();

        if (!(*avahi_poll))
        {
            fprintf(stderr, "%s: Failed to create simple poll object.\n");
            return (0);
        }
    }

    if (*avahi_poll)
        avahi_simple_poll_set_func(*avahi_poll, _pollCallback, NULL);

    /* allocate a new client */
    *avahi_client = avahi_client_new(avahi_simple_poll_get(*avahi_poll), (AvahiClientFlags)0, *_clientCallback, *avahi_poll, err);

    if (!(*avahi_client))
    {
        fprintf(stderr, "Initialization Error, Failed to create client: %s\n", avahi_strerror(*err));
        return (0);
    }

    return (1);
}

void browseServices(AvahiClient **avahi_client, service_data_t *service, int *err)
{

    AvahiServiceBrowser *sb = NULL;
    int avahi_client_result = 0;

    sb = avahi_service_browser_new(*avahi_client, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, service->regtype, service->domain, (AvahiLookupFlags)0, _browseCallback, service->data);

    if (!sb)
    {   
       *err = avahi_client_errno(*avahi_client);
    }
    else {
        *err = 0;
    }

}

//TODO : optimization
void resolveServices(AvahiClient **avahi_client, service_data_t *service, int *err)
{
    
    service->ref = avahi_service_resolver_new(*avahi_client, AVAHI_IF_UNSPEC,
                                              AVAHI_PROTO_UNSPEC, service->name,
                                              service->regtype, service->domain,
                                              AVAHI_PROTO_UNSPEC, 0,
                                              _resolveCallback, service->data);
    if (service->ref)
        *err = 0;
    else
        *err = avahi_client_errno(avahi_client);

}