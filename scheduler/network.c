/*
 * Network interface functions for the CUPS scheduler.
 *
 * Copyright © 2020-2024 by OpenPrinting.
 * Copyright © 2007-2018 by Apple Inc.
 * Copyright © 1997-2006 by Easy Software Products, all rights reserved.
 *
 * Licensed under Apache License v2.0.  See the file "LICENSE" for more
 * information.
 */

/*
 * Include necessary headers.
 */

#include <cups/http-private.h>
#include "cupsd.h"
#include <cups/getifaddrs-internal.h>


/*
 * Local functions...
 */

static void	cupsdNetIFFree(void);
static int	compare_netif(cupsd_netif_t *a, cupsd_netif_t *b);


/*
 * 'cupsdNetIFFind()' - Find a network interface.
 */

cupsd_netif_t *				/* O - Network interface data */
cupsdNetIFFind(const char *name)	/* I - Name of interface */
{
  cupsd_netif_t	key;			/* Search key */


 /*
  * Update the interface list as needed...
  */

  if (NetIFUpdate)
    cupsdNetIFUpdate();

 /*
  * Search for the named interface...
  */

  strlcpy(key.name, name, sizeof(key.name));

  return ((cupsd_netif_t *)cupsArrayFind(NetIFList, &key));
}


/*
 * 'cupsdNetIFFree()' - Free the current network interface list.
 */

static void
cupsdNetIFFree(void)
{
  cupsd_netif_t	*current;		/* Current interface in array */


 /*
  * Loop through the interface list and free all the records...
  */

  for (current = (cupsd_netif_t *)cupsArrayFirst(NetIFList);
       current;
       current = (cupsd_netif_t *)cupsArrayNext(NetIFList))
  {
    cupsArrayRemove(NetIFList, current);
    free(current);
  }
}


/*
 * 'cupsdNetIFUpdate()' - Update the network interface list as needed...
 */

void
cupsdNetIFUpdate(void)
{
  int			match;		/* Matching address? */
  cupsd_listener_t	*lis;		/* Listen address */
  cupsd_netif_t		*temp;		/* New interface */
  struct ifaddrs	*addrs,		/* Interface address list */
			*addr;		/* Current interface address */
  char			hostname[1024];	/* Hostname for address */
  size_t		hostlen;	/* Length of hostname */


 /*
  * Only update the list if we need to...
  */

  if (!NetIFUpdate)
    return;

  NetIFUpdate = 0;

 /*
  * Free the old interfaces...
  */

  cupsdNetIFFree();

 /*
  * Make sure we have an array...
  */

  if (!NetIFList)
    NetIFList = cupsArrayNew((cups_array_func_t)compare_netif, NULL);

  if (!NetIFList)
    return;

 /*
  * Grab a new list of interfaces...
  */

  if (getifaddrs(&addrs) < 0)
  {
    cupsdLogMessage(CUPSD_LOG_DEBUG, "cupsdNetIFUpdate: Unable to get interface list - %s", strerror(errno));
    return;
  }

  for (addr = addrs; addr != NULL; addr = addr->ifa_next)
  {
   /*
    * See if this interface address is IPv4 or IPv6...
    */

    if (addr->ifa_addr == NULL ||
        (addr->ifa_addr->sa_family != AF_INET
#ifdef AF_INET6
	 && addr->ifa_addr->sa_family != AF_INET6
#endif
	) ||
        addr->ifa_netmask == NULL || addr->ifa_name == NULL)
    {
      cupsdLogMessage(CUPSD_LOG_DEBUG, "cupsdNetIFUpdate: Ignoring \"%s\".", addr->ifa_name);
      continue;
    }

   /*
    * Try looking up the hostname for the address as needed...
    */

    if (HostNameLookups)
      httpAddrLookup((http_addr_t *)(addr->ifa_addr), hostname,
                     sizeof(hostname));
    else
    {
     /*
      * Map the default server address and localhost to the server name
      * and localhost, respectively; for all other addresses, use the
      * numeric address...
      */

      if (httpAddrLocalhost((http_addr_t *)(addr->ifa_addr)))
        strlcpy(hostname, "localhost", sizeof(hostname));
      else
	httpAddrString((http_addr_t *)(addr->ifa_addr), hostname,
		       sizeof(hostname));
    }

   /*
    * Create a new address element...
    */

    hostlen = strlen(hostname);
    if ((temp = calloc(1, sizeof(cupsd_netif_t) + hostlen)) == NULL)
    {
      cupsdLogMessage(CUPSD_LOG_DEBUG, "cupsdNetIFUpdate: Unable to allocate memory for interface.");
      break;
    }

   /*
    * Copy all of the information...
    */

    strlcpy(temp->name, addr->ifa_name, sizeof(temp->name));
    temp->hostlen = hostlen;
    memcpy(temp->hostname, hostname, hostlen + 1);

    if (addr->ifa_addr->sa_family == AF_INET)
    {
     /*
      * Copy IPv4 addresses...
      */

      memcpy(&(temp->address), addr->ifa_addr, sizeof(struct sockaddr_in));
      memcpy(&(temp->mask), addr->ifa_netmask, sizeof(struct sockaddr_in));

      if (addr->ifa_dstaddr)
	memcpy(&(temp->broadcast), addr->ifa_dstaddr,
	       sizeof(struct sockaddr_in));
    }
#ifdef AF_INET6
    else
    {
     /*
      * Copy IPv6 addresses...
      */

      memcpy(&(temp->address), addr->ifa_addr, sizeof(struct sockaddr_in6));
      memcpy(&(temp->mask), addr->ifa_netmask, sizeof(struct sockaddr_in6));

      if (addr->ifa_dstaddr)
	memcpy(&(temp->broadcast), addr->ifa_dstaddr,
	       sizeof(struct sockaddr_in6));
    }
#endif /* AF_INET6 */

    if (!(addr->ifa_flags & IFF_POINTOPOINT) &&
        !httpAddrLocalhost(&(temp->address)))
      temp->is_local = 1;

   /*
    * Determine which port to use when advertising printers...
    */

    for (lis = (cupsd_listener_t *)cupsArrayFirst(Listeners);
         lis;
	 lis = (cupsd_listener_t *)cupsArrayNext(Listeners))
    {
      match = 0;

      if (httpAddrAny(&(lis->address)))
        match = 1;
      else if (addr->ifa_addr->sa_family == AF_INET &&
               lis->address.addr.sa_family == AF_INET &&
               (lis->address.ipv4.sin_addr.s_addr &
	        temp->mask.ipv4.sin_addr.s_addr) ==
	           (temp->address.ipv4.sin_addr.s_addr &
		    temp->mask.ipv4.sin_addr.s_addr))
        match = 1;
#ifdef AF_INET6
      else if (addr->ifa_addr->sa_family == AF_INET6 &&
               lis->address.addr.sa_family == AF_INET6 &&
               (lis->address.ipv6.sin6_addr.s6_addr[0] &
	        temp->mask.ipv6.sin6_addr.s6_addr[0]) ==
		   (temp->address.ipv6.sin6_addr.s6_addr[0] &
		    temp->mask.ipv6.sin6_addr.s6_addr[0]) &&
               (lis->address.ipv6.sin6_addr.s6_addr[1] &
	        temp->mask.ipv6.sin6_addr.s6_addr[1]) ==
		   (temp->address.ipv6.sin6_addr.s6_addr[1] &
		    temp->mask.ipv6.sin6_addr.s6_addr[1]) &&
               (lis->address.ipv6.sin6_addr.s6_addr[2] &
	        temp->mask.ipv6.sin6_addr.s6_addr[2]) ==
		   (temp->address.ipv6.sin6_addr.s6_addr[2] &
		    temp->mask.ipv6.sin6_addr.s6_addr[2]) &&
               (lis->address.ipv6.sin6_addr.s6_addr[3] &
	        temp->mask.ipv6.sin6_addr.s6_addr[3]) ==
		   (temp->address.ipv6.sin6_addr.s6_addr[3] &
		    temp->mask.ipv6.sin6_addr.s6_addr[3]))
        match = 1;
#endif /* AF_INET6 */

      if (match)
      {
        temp->port = httpAddrPort(&(lis->address));
	break;
      }
    }

   /*
    * Add it to the array...
    */

    cupsArrayAdd(NetIFList, temp);

    cupsdLogMessage(CUPSD_LOG_DEBUG, "cupsdNetIFUpdate: \"%s\" = %s:%d",
                    temp->name, temp->hostname, temp->port);
  }

  freeifaddrs(addrs);
}


/*
 * 'compare_netif()' - Compare two network interfaces.
 */

static int				/* O - Result of comparison */
compare_netif(cupsd_netif_t *a,		/* I - First network interface */
              cupsd_netif_t *b)		/* I - Second network interface */
{
  return (strcmp(a->name, b->name));
}
