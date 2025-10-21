//
// HTTP address routines for CUPS.
//
// Copyright © 2023-2025 by OpenPrinting.
// Copyright © 2007-2021 by Apple Inc.
// Copyright © 1997-2006 by Easy Software Products, all rights reserved.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

#include "cups-private.h"
#include <sys/stat.h>
#ifdef HAVE_RESOLV_H
#  include <resolv.h>
#endif // HAVE_RESOLV_H
#ifdef __APPLE__
#  include <CoreFoundation/CoreFoundation.h>
#  ifdef HAVE_SCDYNAMICSTORECOPYCOMPUTERNAME
#    include <SystemConfiguration/SystemConfiguration.h>
#  endif // HAVE_SCDYNAMICSTORECOPYCOMPUTERNAME
#endif // __APPLE__


//
// 'httpAddrAny()' - Check for the "any" address.
//
// @deprecated@ @exclude all@
//

int					// O - 1 if "any", 0 otherwise
httpAddrAny(const http_addr_t *addr)	// I - Address to check
{
  return (httpAddrIsAny(addr) ? 1 : 0);
}


//
// 'httpAddrClose()' - Close a socket created by @link httpAddrConnect@ or
//                     @link httpAddrListen@.
//
// Pass `NULL` for sockets created with @link httpAddrConnect2@ and the
// listen address for sockets created with @link httpAddrListen@.  This function
// ensures that domain sockets are removed when closed.
//
// @since CUPS 2.0/OS 10.10@
//

int						// O - 0 on success, -1 on failure
httpAddrClose(http_addr_t *addr,		// I - Listen address or `NULL`
              int         fd)			// I - Socket file descriptor
{
#ifdef _WIN32
  if (closesocket(fd))
#else
  if (close(fd))
#endif // _WIN32
    return (-1);

#ifdef AF_LOCAL
  if (addr && addr->addr.sa_family == AF_LOCAL)
    return (unlink(addr->un.sun_path));
#endif // AF_LOCAL

  return (0);
}


//
// 'httpAddrEqual()' - Compare two addresses.
//
// @deprecated@ @exclude all@
//

int					// O - 1 if equal, 0 if not
httpAddrEqual(const http_addr_t *addr1,	// I - First address
              const http_addr_t *addr2)	// I - Second address
{
  return (httpAddrIsEqual(addr1, addr2) ? 1 : 0);
}


//
// 'httpAddrIsAny()' - Check for the "any" address.
//
// @since CUPS 2.5@
//

bool					// O - `true` if "any" address, `false` otherwise
httpAddrIsAny(const http_addr_t *addr)	// I - Address to check
{
  if (!addr)
    return (false);

#ifdef AF_INET6
  if (addr->addr.sa_family == AF_INET6 && IN6_IS_ADDR_UNSPECIFIED(&(addr->ipv6.sin6_addr)))
    return (true);
#endif // AF_INET6

  if (addr->addr.sa_family == AF_INET && ntohl(addr->ipv4.sin_addr.s_addr) == 0x00000000)
    return (true);

  return (false);
}


//
// 'httpAddrIsEqual()' - Compare two addresses.
//
// @since CUPS 2.5@
//

bool					// O - `true` if equal, `false` if not
httpAddrIsEqual(
    const http_addr_t *addr1,		// I - First address
    const http_addr_t *addr2)		// I - Second address
{
  if (!addr1 && !addr2)
    return (1);

  if (!addr1 || !addr2)
    return (0);

  if (addr1->addr.sa_family != addr2->addr.sa_family)
    return (0);

#ifdef AF_LOCAL
  if (addr1->addr.sa_family == AF_LOCAL)
    return (!strcmp(addr1->un.sun_path, addr2->un.sun_path));
#endif // AF_LOCAL

#ifdef AF_INET6
  if (addr1->addr.sa_family == AF_INET6)
    return (!memcmp(&(addr1->ipv6.sin6_addr), &(addr2->ipv6.sin6_addr), 16));
#endif // AF_INET6

  return (addr1->ipv4.sin_addr.s_addr == addr2->ipv4.sin_addr.s_addr);
}


//
// 'httpAddrLength()' - Return the length of the address in bytes.
//
// @deprecated@ @exclude all@
//

int					// O - Length in bytes
httpAddrLength(const http_addr_t *addr)	// I - Address
{
  return ((int)httpAddrGetLength(addr));
}


//
// 'httpAddrGetLength()' - Return the length of the address in bytes.
//
// @since CUPS 2.5@
//

size_t					// O - Length in bytes
httpAddrGetLength(
    const http_addr_t *addr)		// I - Address
{
  if (!addr)
    return (0);

#ifdef AF_INET6
  if (addr->addr.sa_family == AF_INET6)
    return (sizeof(addr->ipv6));
  else
#endif // AF_INET6
#ifdef AF_LOCAL
  if (addr->addr.sa_family == AF_LOCAL)
    return ((size_t)(offsetof(struct sockaddr_un, sun_path) + strlen(addr->un.sun_path) + 1));
  else
#endif // AF_LOCAL
  if (addr->addr.sa_family == AF_INET)
    return (sizeof(addr->ipv4));
  else
    return (0);
}


//
// 'httpAddrListen()' - Create a listening socket bound to the specified
//                      address and port.
//
// @since CUPS 1.7@
//

int					// O - Socket or -1 on error
httpAddrListen(http_addr_t *addr,	// I - Address to bind to
               int         port)	// I - Port number to bind to
{
  int		fd = -1,		// Socket
		val,			// Socket value
                status;			// Bind status


  // Range check input...
  if (!addr || port < 0)
    return (-1);

  // Make sure the network stack is initialized...
  httpInitialize();

  // Create the socket and set options...
  if ((fd = socket(addr->addr.sa_family, SOCK_STREAM, 0)) < 0)
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, strerror(errno), 0);
    return (-1);
  }

  val = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, CUPS_SOCAST &val, sizeof(val));

#ifdef IPV6_V6ONLY
  if (addr->addr.sa_family == AF_INET6)
    setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, CUPS_SOCAST &val, sizeof(val));
#endif // IPV6_V6ONLY

  // Bind the socket...
#ifdef AF_LOCAL
  if (addr->addr.sa_family == AF_LOCAL)
  {
    mode_t	mask;			// Umask setting

    // Remove any existing domain socket file...
    if ((status = unlink(addr->un.sun_path)) < 0)
    {
      if (errno == ENOENT)
	status = 0;
      else
	DEBUG_printf("1httpAddrListen: Unable to unlink \"%s\": %s", addr->un.sun_path, strerror(errno));
    }

    if (!status)
    {
      // Save the current umask and set it to 0 so that all users can access
      // the domain socket...
      mask = umask(0);

      // Bind the domain socket...
      if ((status = bind(fd, (struct sockaddr *)addr, (socklen_t)httpAddrLength(addr))) < 0)
	DEBUG_printf("1httpAddrListen: Unable to bind domain socket \"%s\": %s", addr->un.sun_path, strerror(errno));

      // Restore the umask...
      umask(mask);
    }
  }
  else
#endif // AF_LOCAL
  {
    httpAddrSetPort(addr, port);

    if ((status = bind(fd, (struct sockaddr *)addr, (socklen_t)httpAddrLength(addr))) < 0)
      DEBUG_printf("1httpAddrListen: Unable to bind network socket: %s", strerror(errno));
  }

  if (status)
  {
    DEBUG_printf("1httpAddrListen: Unable to listen on socket: %s", strerror(errno));
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, strerror(errno), 0);

    close(fd);

    return (-1);
  }

  // Listen...
  if (listen(fd, INT_MAX))
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, strerror(errno), 0);

    close(fd);

    return (-1);
  }

  // Close on exec...
#ifndef _WIN32
  fcntl(fd, F_SETFD, fcntl(fd, F_GETFD) | FD_CLOEXEC);
#endif // !_WIN32

#ifdef SO_NOSIGPIPE
  // Disable SIGPIPE for this socket.
  val = 1;
  setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, CUPS_SOCAST &val, sizeof(val));
#endif // SO_NOSIGPIPE

  return (fd);
}


//
// 'httpAddrLocalhost()' - Check for the local loopback address.
//
// @deprecated@ @exclude all@
//

int					// O - 1 if local host, 0 otherwise
httpAddrLocalhost(
    const http_addr_t *addr)		// I - Address to check
{
  return (httpAddrIsLocalhost(addr) ? 1 : 0);
}


//
// 'httpAddrIsLocalhost()' - Check for the local loopback address.
//
// @since CUPS 2.5@
//

bool					// O - `true` if local host, `false` otherwise
httpAddrIsLocalhost(
    const http_addr_t *addr)		// I - Address to check
{
  if (!addr)
    return (true);

#ifdef AF_INET6
  if (addr->addr.sa_family == AF_INET6 && IN6_IS_ADDR_LOOPBACK(&(addr->ipv6.sin6_addr)))
    return (true);
#endif // AF_INET6

#ifdef AF_LOCAL
  if (addr->addr.sa_family == AF_LOCAL)
    return (true);
#endif // AF_LOCAL

  if (addr->addr.sa_family == AF_INET && (ntohl(addr->ipv4.sin_addr.s_addr) & 0xff000000) == 0x7f000000)
    return (true);

  return (false);
}


//
// 'httpAddrLookup()' - Lookup the hostname associated with the address.
//
// @since CUPS 1.2@
//

char *					// O - Host name
httpAddrLookup(
    const http_addr_t *addr,		// I - Address to lookup
    char              *name,		// I - Host name buffer
    int               namelen)		// I - Size of name buffer
{
  int			error;		// Any error from getnameinfo
  _cups_globals_t	*cg = _cupsGlobals();
					// Global data


  DEBUG_printf("httpAddrLookup(addr=%p, name=%p, namelen=%d)", (void *)addr, (void *)name, namelen);

  // Range check input...
  if (!addr || !name || namelen <= 2)
  {
    if (name && namelen >= 1)
      *name = '\0';

    return (NULL);
  }

#ifdef AF_LOCAL
  if (addr->addr.sa_family == AF_LOCAL)
  {
    cupsCopyString(name, addr->un.sun_path, (size_t)namelen);
    return (name);
  }
#endif // AF_LOCAL

  // Optimize lookups for localhost/loopback addresses...
  if (httpAddrLocalhost(addr))
  {
    cupsCopyString(name, "localhost", (size_t)namelen);
    return (name);
  }

#ifdef HAVE_RES_INIT
  // STR #2920: Initialize resolver after failure in cups-polld
  //
  // If the previous lookup failed, re-initialize the resolver to prevent
  // temporary network errors from persisting.  This *should* be handled by
  // the resolver libraries, but apparently the glibc folks do not agree.
  //
  // We set a flag at the end of this function if we encounter an error that
  // requires reinitialization of the resolver functions.  We then call
  // res_init() if the flag is set on the next call here or in httpAddrLookup().
  if (cg->need_res_init)
  {
    res_init();

    cg->need_res_init = 0;
  }
#endif // HAVE_RES_INIT

  // STR #2486: httpAddrLookup() fails when getnameinfo() returns EAI_AGAIN
  //
  // FWIW, I think this is really a bug in the implementation of
  // getnameinfo(), but falling back on httpAddrString() is easy to do...
  if ((error = getnameinfo(&addr->addr, (socklen_t)httpAddrLength(addr), name, (socklen_t)namelen, NULL, 0, 0)) != 0)
  {
    if (error == EAI_FAIL)
      cg->need_res_init = 1;

    return (httpAddrGetString(addr, name, (size_t)namelen));
  }

  DEBUG_printf("1httpAddrLookup: returning \"%s\"...", name);

  return (name);
}


//
// 'httpAddrFamily()' - Get the address family of an address.
//
// @deprecated@ @exclude all@
//

int					// O - Address family
httpAddrFamily(http_addr_t *addr)	// I - Address
{
  return (httpAddrGetFamily(addr));
}


//
// 'httpAddrGetFamily()' - Get the address family of an address.
//

int					// O - Address family
httpAddrGetFamily(http_addr_t *addr)	// I - Address
{
  if (addr)
    return (addr->addr.sa_family);
  else
    return (0);
}


//
// 'httpAddrPort()' - Get the port number associated with an address.
//
// @deprecated@ @exclude all@
//

int					// O - Port number
httpAddrPort(http_addr_t *addr)		// I - Address
{
  return (httpAddrGetPort(addr));
}


//
// 'httpAddrGetPort()' - Get the port number associated with an address.
//
// @since CUPS 2.5@
//

int					// O - Port number
httpAddrGetPort(http_addr_t *addr)	// I - Address
{
  if (!addr)
    return (-1);
#ifdef AF_INET6
  else if (addr->addr.sa_family == AF_INET6)
    return (ntohs(addr->ipv6.sin6_port));
#endif // AF_INET6
  else if (addr->addr.sa_family == AF_INET)
    return (ntohs(addr->ipv4.sin_port));
  else
    return (0);
}


//
// 'httpAddrSetPort()' - Set the port number associated with an address.
//
// @since CUPS 2.5@
//

void
httpAddrSetPort(http_addr_t *addr,	// I - Address
		int         port)	// I - Port
{
  if (!addr || port <= 0)
    return;

#ifdef AF_INET6
  if (addr->addr.sa_family == AF_INET6)
    addr->ipv6.sin6_port = htons(port);
  else
#endif // AF_INET6
  if (addr->addr.sa_family == AF_INET)
    addr->ipv4.sin_port = htons(port);
}


//
// 'httpAddrString()' - Convert an address to a numeric string.
//
// @deprecated@ @exclude all@
//

char *					// O - Numeric address string
httpAddrString(const http_addr_t *addr,	// I - Address to convert
               char              *s,	// I - String buffer
	       int               slen)	// I - Length of string
{
  return (httpAddrGetString(addr, s, (size_t)slen));
}


//
// 'httpAddrGetString()' - Convert an address to a numeric string.
//
// @since CUPS 2.5@
//

char *					// O - Numeric address string
httpAddrGetString(
    const http_addr_t *addr,		// I - Address to convert
    char              *s,		// I - String buffer
    size_t            slen)		// I - Length of string
{
  DEBUG_printf("httpAddrGetString(addr=%p, s=%p, slen=%u)", (void *)addr, (void *)s, (unsigned)slen);

  // Range check input...
  if (!addr || !s || slen <= 2)
  {
    if (s && slen >= 1)
      *s = '\0';

    return (NULL);
  }

#ifdef AF_LOCAL
  if (addr->addr.sa_family == AF_LOCAL)
  {
    if (addr->un.sun_path[0] == '/')
      cupsCopyString(s, addr->un.sun_path, (size_t)slen);
    else
      cupsCopyString(s, "localhost", (size_t)slen);
  }
  else
#endif // AF_LOCAL
  if (addr->addr.sa_family == AF_INET)
  {
    unsigned temp;			// Temporary address

    temp = ntohl(addr->ipv4.sin_addr.s_addr);

    snprintf(s, slen, "%d.%d.%d.%d", (temp >> 24) & 255, (temp >> 16) & 255, (temp >> 8) & 255, temp & 255);
  }
#ifdef AF_INET6
  else if (addr->addr.sa_family == AF_INET6)
  {
    char	*sptr,			// Pointer into string
		temps[64];		// Temporary string for address

    if (getnameinfo(&addr->addr, (socklen_t)httpAddrLength(addr), temps, sizeof(temps), NULL, 0, NI_NUMERICHOST))
    {
      // If we get an error back, then the address type is not supported
      // and we should zero out the buffer...
      s[0] = '\0';

      return (NULL);
    }
    else if ((sptr = strchr(temps, '%')) != NULL)
    {
      // Convert "%zone" to "+zone" to match URI form...
      *sptr = '+';
    }

    // Add "[v1." and "]" around IPv6 address to convert to URI form.
    snprintf(s, slen, "[v1.%s]", temps);
  }
#endif // AF_INET6
  else
  {
    cupsCopyString(s, "UNKNOWN", slen);
  }

  DEBUG_printf("1httpAddrGetString: returning \"%s\"...", s);

  return (s);
}


//
// 'httpGetAddress()' - Get the address of the connected peer of a connection.
//
// For connections created with @link httpConnect2@, the address is for the
// server.  For connections created with @link httpAccept@, the address is for
// the client.
//
// Returns `NULL` if the socket is currently unconnected.
//
// @since CUPS 2.0/OS 10.10@
//

http_addr_t *				// O - Connected address or `NULL`
httpGetAddress(http_t *http)		// I - HTTP connection
{
  if (http)
    return (http->hostaddr);
  else
    return (NULL);
}


//
// 'httpGetHostByName()' - Lookup a hostname or IPv4 address, and return
//                         address records for the specified name.
//
// @deprecated@ @exclude all@
//

struct hostent *			// O - Host entry
httpGetHostByName(const char *name)	// I - Hostname or IP address
{
  const char		*nameptr;	// Pointer into name
  unsigned		ip[4];		// IP address components
  _cups_globals_t	*cg = _cupsGlobals();
  					// Pointer to library globals


  DEBUG_printf("httpGetHostByName(name=\"%s\")", name);

  // Avoid lookup delays and configuration problems when connecting
  // to the localhost address...
  if (!strcmp(name, "localhost"))
    name = "127.0.0.1";

 /*
  * This function is needed because some operating systems have a
  * buggy implementation of gethostbyname() that does not support
  * IP addresses.  If the first character of the name string is a
  * number, then sscanf() is used to extract the IP components.
  * We then pack the components into an IPv4 address manually,
  * since the inet_aton() function is deprecated.  We use the
  * htonl() macro to get the right byte order for the address.
  *
  * We also support domain sockets when supported by the underlying
  * OS...
  */

#ifdef AF_LOCAL
  if (name[0] == '/')
  {
    // A domain socket address, so make an AF_LOCAL entry and return it...
    cg->hostent.h_name      = (char *)name;
    cg->hostent.h_aliases   = NULL;
    cg->hostent.h_addrtype  = AF_LOCAL;
    cg->hostent.h_length    = (int)strlen(name) + 1;
    cg->hostent.h_addr_list = cg->ip_ptrs;
    cg->ip_ptrs[0]          = (char *)name;
    cg->ip_ptrs[1]          = NULL;

    DEBUG_puts("1httpGetHostByName: returning domain socket address...");

    return (&cg->hostent);
  }
#endif // AF_LOCAL

  for (nameptr = name; isdigit(*nameptr & 255) || *nameptr == '.'; nameptr ++);

  if (!*nameptr)
  {
    // We have an IPv4 address; break it up and provide the host entry
    // to the caller.
    if (sscanf(name, "%u.%u.%u.%u", ip, ip + 1, ip + 2, ip + 3) != 4)
      return (NULL);			// Must have 4 numbers

    if (ip[0] > 255 || ip[1] > 255 || ip[2] > 255 || ip[3] > 255)
      return (NULL);			// Invalid byte ranges!

    cg->ip_addr = htonl((ip[0] << 24) | (ip[1] << 16) | (ip[2] << 8) | ip[3]);

    // Fill in the host entry and return it...
    cg->hostent.h_name      = (char *)name;
    cg->hostent.h_aliases   = NULL;
    cg->hostent.h_addrtype  = AF_INET;
    cg->hostent.h_length    = 4;
    cg->hostent.h_addr_list = cg->ip_ptrs;
    cg->ip_ptrs[0]          = (char *)&(cg->ip_addr);
    cg->ip_ptrs[1]          = NULL;

    DEBUG_puts("1httpGetHostByName: returning IPv4 address...");

    return (&cg->hostent);
  }
  else
  {
    // Use the gethostbyname() function to get the IPv4 address for the name...
    DEBUG_puts("1httpGetHostByName: returning domain lookup address(es)...");

    return (gethostbyname(name));
  }
}


//
// 'httpGetHostname()' - Get the FQDN for the connection or local system.
//
// When "http" points to a connected socket, return the hostname or
// address that was used in the call to httpConnect() or httpConnectEncrypt(),
// or the address of the client for the connection from httpAcceptConnection().
// Otherwise, return the FQDN for the local system using both gethostname()
// and gethostbyname() to get the local hostname with domain.
//
// @since CUPS 1.2@
//

const char *				// O - FQDN for connection or system
httpGetHostname(http_t *http,		// I - HTTP connection or NULL
                char   *s,		// I - String buffer for name
                int    slen)		// I - Size of buffer
{
  DEBUG_printf("httpGetHostname(http=%p, s=%p, slen=%d)", (void *)http, (void *)s, slen);

  if (http)
  {
    DEBUG_printf("1httpGetHostname: http->hostname=\"%s\"", http->hostname);

    if (!s || slen <= 1)
    {
      if (http->hostname[0] == '/')
	return ("localhost");
      else
	return (http->hostname);
    }
    else if (http->hostname[0] == '/')
    {
      cupsCopyString(s, "localhost", (size_t)slen);
    }
    else
    {
      cupsCopyString(s, http->hostname, (size_t)slen);
    }
  }
  else
  {
    // Get the hostname...
    if (!s || slen <= 1)
      return (NULL);

    if (gethostname(s, (size_t)slen) < 0)
      cupsCopyString(s, "localhost", (size_t)slen);

    DEBUG_printf("1httpGetHostname: gethostname() returned \"%s\".", s);

    if (!strchr(s, '.'))
    {
#ifdef HAVE_SCDYNAMICSTORECOPYCOMPUTERNAME
      // The hostname is not a FQDN, so use the local hostname from the
      // SystemConfiguration framework...
      SCDynamicStoreRef	sc = SCDynamicStoreCreate(kCFAllocatorDefault, CFSTR("libcups"), NULL, NULL);
					// System configuration data
      CFStringRef	local = sc ? SCDynamicStoreCopyLocalHostName(sc) : NULL;
					// Local host name
      char		localStr[1024];	// Local host name C string

      if (local && CFStringGetCString(local, localStr, sizeof(localStr), kCFStringEncodingUTF8))
      {
        // Append ".local." to the hostname we get...
        snprintf(s, (size_t)slen, "%s.local.", localStr);
        DEBUG_printf("1httpGetHostname: SCDynamicStoreCopyLocalHostName() returned \"%s\".", s);
      }

      if (local)
        CFRelease(local);
      if (sc)
        CFRelease(sc);

#else
      // The hostname is not a FQDN, so look it up...
      struct hostent	*host;		// Host entry to get FQDN

      if ((host = gethostbyname(s)) != NULL && host->h_name)
      {
        // Use the resolved hostname...
	cupsCopyString(s, host->h_name, (size_t)slen);
        DEBUG_printf("1httpGetHostname: gethostbyname() returned \"%s\".", s);
      }
#endif // HAVE_SCDYNAMICSTORECOPYCOMPUTERNAME
    }

    // Make sure .local hostnames end with a period...
    if (strlen(s) > 6 && !strcmp(s + strlen(s) - 6, ".local"))
      cupsConcatString(s, ".", (size_t)slen);
  }

  // Convert the hostname to lowercase as needed...
  if (s[0] != '/')
  {
    char	*ptr;			// Pointer into string

    for (ptr = s; *ptr; ptr ++)
      *ptr = (char)_cups_tolower((int)*ptr);
  }

  // Return the hostname with as much domain info as we have...
  return (s);
}


//
// 'httpResolveHostname()' - Resolve the hostname of the HTTP connection
//                           address.
//
// @since CUPS 2.0/OS 10.10@
//

const char *				// O - Resolved hostname or `NULL`
httpResolveHostname(http_t *http,	// I - HTTP connection
                    char   *buffer,	// I - Hostname buffer or `NULL` to use HTTP buffer
                    size_t bufsize)	// I - Size of buffer
{
  if (!http)
    return (NULL);

  if (isdigit(http->hostname[0] & 255) || http->hostname[0] == '[')
  {
    char	temp[1024];		// Temporary string

    if (httpAddrLookup(http->hostaddr, temp, sizeof(temp)))
      cupsCopyString(http->hostname, temp, sizeof(http->hostname));
    else
      return (NULL);
  }

  if (buffer)
  {
    if (http->hostname[0] == '/')
      cupsCopyString(buffer, "localhost", bufsize);
    else
      cupsCopyString(buffer, http->hostname, bufsize);

    return (buffer);
  }
  else if (http->hostname[0] == '/')
  {
    return ("localhost");
  }
  else
  {
    return (http->hostname);
  }
}
