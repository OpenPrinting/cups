//
// HTTP routines for CUPS.
//
// Copyright © 2022-2025 by OpenPrinting.
// Copyright © 2007-2021 by Apple Inc.
// Copyright © 1997-2007 by Easy Software Products, all rights reserved.
//
// This file contains Kerberos support code, copyright 2006 by
// Jelmer Vernooij.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

#include "cups-private.h"
#include <fcntl.h>
#include <math.h>
#ifdef _WIN32
#  include <tchar.h>
#else
#  include <poll.h>
#  include <signal.h>
#  include <sys/time.h>
#  include <sys/resource.h>
#endif // _WIN32
#include <zlib.h>


//
// Local functions...
//

static void		http_add_field(http_t *http, http_field_t field, const char *value, int append);
static void		http_content_coding_finish(http_t *http);
static void		http_content_coding_start(http_t *http, const char *value);
static http_t		*http_create(const char *host, int port, http_addrlist_t *addrlist, int family, http_encryption_t encryption, int blocking, _http_mode_t mode);
#ifdef DEBUG
static void		http_debug_hex(const char *prefix, const char *buffer, int bytes);
#endif // DEBUG
static ssize_t		http_read(http_t *http, char *buffer, size_t length);
static ssize_t		http_read_buffered(http_t *http, char *buffer, size_t length);
static ssize_t		http_read_chunk(http_t *http, char *buffer, size_t length);
static bool		http_send(http_t *http, http_state_t request, const char *uri);
static ssize_t		http_write(http_t *http, const char *buffer, size_t length);
static ssize_t		http_write_chunk(http_t *http, const char *buffer, size_t length);
static off_t		http_set_length(http_t *http);
static void		http_set_timeout(int fd, double timeout);
static void		http_set_wait(http_t *http);
static bool		http_tls_upgrade(http_t *http);


//
// Local globals...
//

static const char * const http_fields[] =
			{
			  "Accept-Language",
			  "Accept-Ranges",
			  "Authorization",
			  "Connection",
			  "Content-Encoding",
			  "Content-Language",
			  "Content-Length",
			  "Content-Location",
			  "Content-MD5",
			  "Content-Range",
			  "Content-Type",
			  "Content-Version",
			  "Date",
			  "Host",
			  "If-Modified-Since",
			  "If-Unmodified-since",
			  "Keep-Alive",
			  "Last-Modified",
			  "Link",
			  "Location",
			  "Range",
			  "Referer",
			  "Retry-After",
			  "Transfer-Encoding",
			  "Upgrade",
			  "User-Agent",
			  "WWW-Authenticate",
			  "Accept-Encoding",
			  "Allow",
			  "Server",
			  "Authentication-Info",
			  "Access-Control-Allow-Credentials",
			  "Access-Control-Allow-Headers",
			  "Access-Control-Allow-Methods",
			  "Access-Control-Allow-Origin",
			  "Access-Control-Expose-Headers",
			  "Access-Control-Max-Age",
			  "Access-Control-Request-Headers",
			  "Access-Control-Request-Method",
			  "Optional-WWW-Authenticate",
			  "Origin",
			  "OSCORE",
			  "Strict-Transport-Security",
			  "Accept"
			};


//
// 'httpAcceptConnection()' - Accept a new HTTP client connection.
//
// This function accepts a new HTTP client connection from the specified
// listening socket "fd".  The "blocking" argument specifies whether the new
// HTTP connection is blocking.
//
// @since CUPS 1.7@
//

http_t *				// O - HTTP connection or `NULL`
httpAcceptConnection(int fd,		// I - Listen socket file descriptor
                     int blocking)	// I - 1 if the connection should be blocking, 0 otherwise
{
  http_t		*http;		// HTTP connection
  http_addrlist_t	addrlist;	// Dummy address list
  socklen_t		addrlen;	// Length of address
  int			val;		// Socket option value


  // Range check input...
  if (fd < 0)
    return (NULL);

  // Create the client connection...
  memset(&addrlist, 0, sizeof(addrlist));

  if ((http = http_create(NULL, 0, &addrlist, AF_UNSPEC, HTTP_ENCRYPTION_IF_REQUESTED, blocking, _HTTP_MODE_SERVER)) == NULL)
    return (NULL);

  // Accept the client and get the remote address...
  addrlen = sizeof(http_addr_t);

  if ((http->fd = accept(fd, (struct sockaddr *)&(http->addrlist->addr), &addrlen)) < 0)
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, strerror(errno), 0);
    httpClose(http);

    return (NULL);
  }

  http->hostaddr = &(http->addrlist->addr);

  if (httpAddrIsLocalhost(http->hostaddr))
    cupsCopyString(http->hostname, "localhost", sizeof(http->hostname));
  else
    httpAddrGetString(http->hostaddr, http->hostname, sizeof(http->hostname));

#ifdef SO_NOSIGPIPE
  // Disable SIGPIPE for this socket.
  val = 1;
  setsockopt(http->fd, SOL_SOCKET, SO_NOSIGPIPE, CUPS_SOCAST &val, sizeof(val));
#endif // SO_NOSIGPIPE

  // Using TCP_NODELAY improves responsiveness, especially on systems with a
  // slow loopback interface.  Since we write large buffers when sending print
  // files and requests, there shouldn't be any performance penalty for this...
  val = 1;
  setsockopt(http->fd, IPPROTO_TCP, TCP_NODELAY, CUPS_SOCAST &val, sizeof(val));

#ifdef FD_CLOEXEC
  // Close this socket when starting another process...
  fcntl(http->fd, F_SETFD, FD_CLOEXEC);
#endif // FD_CLOEXEC

  return (http);
}


//
// 'httpAddCredential()' - Allocates and adds a single credential to an array.
//
// @deprecated@ @exclude all@
//

int					// O - 0 on success, -1 on error
httpAddCredential(
    cups_array_t *credentials,		// I - Credentials array
    const void   *data,			// I - PEM-encoded X.509 data
    size_t       datalen)		// I - Length of data
{
  (void)credentials;
  (void)data;
  (void)datalen;

  return (-1);
}


//
// 'httpBlocking()' - Set blocking/non-blocking behavior on a connection.
//
// @deprecated@
//

void
httpBlocking(http_t *http,		// I - HTTP connection
             int    b)			// I - 1 = blocking, 0 = non-blocking
{
  httpSetBlocking(http, b != 0);
}


//
// 'httpCheck()' - Check to see if there is a pending response from the server.
//
// @deprecated@
//

int					// O - 0 = no data, 1 = data available
httpCheck(http_t *http)			// I - HTTP connection
{
  return (httpWait(http, 0));
}


//
// 'httpClearCookie()' - Clear the cookie value(s).
//
// @since CUPS 1.1.19@
//

void
httpClearCookie(http_t *http)		// I - HTTP connection
{
  if (!http)
    return;

  free(http->cookie);
  http->cookie = NULL;
}


//
// 'httpClearFields()' - Clear HTTP request/response fields.
//

void
httpClearFields(http_t *http)		// I - HTTP connection
{
  http_field_t	field;			// Current field


  DEBUG_printf("httpClearFields(http=%p)", (void *)http);

  if (http)
  {
    memset(http->_fields, 0, sizeof(http->_fields));

    for (field = HTTP_FIELD_ACCEPT_LANGUAGE; field < HTTP_FIELD_ACCEPT_ENCODING; field ++)
    {
      if (!http->fields[field])
        continue;

      if (http->fields[field] != http->_fields[field])
        free(http->fields[field]);

      http->fields[field] = NULL;
    }

    for (; field < HTTP_FIELD_MAX; field ++)
    {
      if (!http->fields[field])
        continue;

      free(http->fields[field]);
      http->fields[field] = NULL;
    }

    if (http->mode == _HTTP_MODE_CLIENT)
    {
      if (http->hostname[0] == '/')
	httpSetField(http, HTTP_FIELD_HOST, "localhost");
      else
	httpSetField(http, HTTP_FIELD_HOST, http->hostname);
    }

    http->expect = (http_status_t)0;
  }
}


//
// 'httpClose()' - Close a HTTP connection.
//

void
httpClose(http_t *http)			// I - HTTP connection
{
  http_field_t	field;			// Current field
#ifdef HAVE_GSSAPI
  OM_uint32	minor_status;		// Minor status code
#endif // HAVE_GSSAPI


  DEBUG_printf("httpClose(http=%p)", (void *)http);

  //
  // Range check input...
  //

  if (!http)
    return;

  //
  // Close any open connection...
  //

  _httpDisconnect(http);

  //
  // Free memory used...
  //

  httpAddrFreeList(http->addrlist);

  free(http->cookie);

#ifdef HAVE_GSSAPI
  if (http->gssctx != GSS_C_NO_CONTEXT)
    gss_delete_sec_context(&minor_status, &http->gssctx, GSS_C_NO_BUFFER);

  if (http->gssname != GSS_C_NO_NAME)
    gss_release_name(&minor_status, &http->gssname);
#endif // HAVE_GSSAPI

#ifdef HAVE_AUTHORIZATION_H
  if (http->auth_ref)
    AuthorizationFree(http->auth_ref, kAuthorizationFlagDefaults);
#endif // HAVE_AUTHORIZATION_H

  for (field = HTTP_FIELD_ACCEPT_LANGUAGE; field < HTTP_FIELD_MAX; field ++)
  {
    free(http->default_fields[field]);
    if (field >= HTTP_FIELD_ACCEPT_ENCODING || http->fields[field] != http->_fields[field])
      free(http->fields[field]);
  }

  if (http->authstring && http->authstring != http->_authstring)
    free(http->authstring);

  _httpFreeCredentials(http->tls_credentials);

  free(http);
}


//
// 'httpCompareCredentials()' - Compare two sets of X.509 credentials.
//
// @deprecated@ @exclude all@
//

int					// O - 1 if they match, 0 if they do not
httpCompareCredentials(
    cups_array_t *cred1,		// I - First set of X.509 credentials
    cups_array_t *cred2)		// I - Second set of X.509 credentials
{
  (void)cred1;
  (void)cred2;

  return (0);
}


//
// 'httpConnect()' - Connect to a HTTP server.
//
// This function is deprecated - use @link httpConnect2@ instead.
//
// @deprecated@ @exclude all@
//

http_t *				// O - New HTTP connection
httpConnect(const char *host,		// I - Host to connect to
            int        port)		// I - Port number
{
  return (httpConnect2(host, port, NULL, AF_UNSPEC, HTTP_ENCRYPTION_IF_REQUESTED, 1, 30000, NULL));
}


//
// 'httpConnect2()' - Connect to a HTTP server.
//
// This function creates a connection to a HTTP server.  The "host" and "port"
// arguments specify a hostname or IP address and port number to use while the
// "addrlist" argument specifies a list of addresses to use or `NULL` to do a
// fresh lookup.  The "family" argument specifies the address family to use -
// `AF_UNSPEC` to try both IPv4 and IPv6, `AF_INET` for IPv4, or `AF_INET6` for
// IPv6.
//
// The "encryption" argument specifies whether to encrypt the connection and the
// "blocking" argument specifies whether to use blocking behavior when reading
// or writing data.
//
// The "msec" argument specifies how long to try to connect to the server or `0`
// to just create an unconnected `http_t` object.  The "cancel" argument
// specifies an integer variable that can be set to a non-zero value to cancel
// the connection process.
//
// @since CUPS 1.7@
//

http_t *				// O - New HTTP connection
httpConnect2(
    const char        *host,		// I - Host to connect to
    int               port,		// I - Port number
    http_addrlist_t   *addrlist,	// I - List of addresses or `NULL` to lookup
    int               family,		// I - Address family to use or `AF_UNSPEC` for any
    http_encryption_t encryption,	// I - Type of encryption to use
    int               blocking,		// I - 1 for blocking connection, 0 for non-blocking
    int               msec,		// I - Connection timeout in milliseconds, 0 means don't connect
    int               *cancel)		// I - Pointer to "cancel" variable
{
  http_t	*http;			// New HTTP connection


  DEBUG_printf("httpConnect2(host=\"%s\", port=%d, addrlist=%p, family=%d, encryption=%d, blocking=%d, msec=%d, cancel=%p)", host, port, (void *)addrlist, family, encryption, blocking, msec, (void *)cancel);

  // Create the HTTP structure...
  if ((http = http_create(host, port, addrlist, family, encryption, blocking, _HTTP_MODE_CLIENT)) == NULL)
    return (NULL);

  // Optionally connect to the remote system...
  if (msec == 0 || httpConnectAgain(http, msec, cancel))
    return (http);

  // Could not connect to any known address - bail out!
  httpClose(http);

  return (NULL);
}


//
// 'httpConnectAgain()' - Reconnect to a HTTP server with timeout and optional cancel variable.
//

bool					// O - `true` on success, `false` on failure
httpConnectAgain(http_t *http,		// I - HTTP connection
	         int    msec,		// I - Timeout in milliseconds
	         int    *cancel)	// I - Pointer to "cancel" variable
{
  http_addrlist_t	*addr;		// Connected address
  char			*orig_creds;	// Original peer credentials
#ifdef DEBUG
  http_addrlist_t	*current;	// Current address
  char			temp[256];	// Temporary address string
#endif // DEBUG


  DEBUG_printf("httpConnectAgain(http=%p, msec=%d, cancel=%p)", (void *)http, msec, (void *)cancel);

  if (!http)
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, strerror(EINVAL), 0);
    return (false);
  }

  orig_creds = httpCopyPeerCredentials(http);

  if (http->tls)
  {
    DEBUG_puts("1httpConnectAgain: Shutting down TLS...");
    _httpTLSStop(http);
  }

  // Close any previously open socket...
  if (http->fd >= 0)
  {
    DEBUG_printf("1httpConnectAgain: Closing socket %d...", http->fd);

    httpAddrClose(NULL, http->fd);

    http->fd = -1;
  }

  // Reset all state (except fields, which may be reused)...
  http->state           = HTTP_STATE_WAITING;
  http->version         = HTTP_VERSION_1_1;
  http->keep_alive      = HTTP_KEEPALIVE_OFF;
  memset(&http->_hostaddr, 0, sizeof(http->_hostaddr));
  http->data_encoding   = HTTP_ENCODING_FIELDS;
  http->_data_remaining = 0;
  http->used            = 0;
  http->data_remaining  = 0;
  http->hostaddr        = NULL;
  http->wused           = 0;

  // Connect to the server...
#ifdef DEBUG
  for (current = http->addrlist; current; current = current->next)
    DEBUG_printf("1httpConnectAgain: Address %s:%d", httpAddrString(&(current->addr), temp, sizeof(temp)), httpAddrPort(&(current->addr)));
#endif // DEBUG

  if ((addr = httpAddrConnect2(http->addrlist, &(http->fd), msec, cancel)) == NULL)
  {
    // Unable to connect...
#ifdef _WIN32
    http->error  = WSAGetLastError();
#else
    http->error  = errno;
#endif // _WIN32
    http->status = HTTP_STATUS_ERROR;

    DEBUG_printf("1httpConnectAgain: httpAddrConnect failed: %s", strerror(http->error));

    free(orig_creds);

    return (false);
  }

  DEBUG_printf("1httpConnectAgain: New socket=%d", http->fd);

  if (http->timeout_value > 0)
    http_set_timeout(http->fd, http->timeout_value);

  http->hostaddr = &(addr->addr);
  http->error    = 0;

  if (http->encryption == HTTP_ENCRYPTION_ALWAYS)
  {
    // Always do encryption via TLS.
    if (!_httpTLSStart(http))
    {
      httpAddrClose(NULL, http->fd);
      http->fd = -1;

      free(orig_creds);

      return (false);
    }
  }
  else if (http->encryption == HTTP_ENCRYPTION_REQUIRED && !http->tls_upgrade)
  {
    if (!http_tls_upgrade(http))
    {
      free(orig_creds);

      return (false);
    }
  }

  DEBUG_printf("1httpConnectAgain: Connected to %s:%d...", httpAddrGetString(http->hostaddr, temp, sizeof(temp)), httpAddrGetPort(http->hostaddr));

  if (orig_creds)
  {
    char *new_creds = httpCopyPeerCredentials(http);
					// New peer credentials

    if (!new_creds || (strcmp(orig_creds, new_creds) && cupsGetCredentialsTrust(/*path*/NULL, http->hostname, new_creds, /*require_ca*/true) != HTTP_TRUST_OK))
    {
      // New and old credentials don't match and the new cert doesn't validate...
      _httpDisconnect(http);

      free(orig_creds);
      free(new_creds);

      return (false);
    }

    free(new_creds);
  }

  free(orig_creds);

  return (true);
}


//
// 'httpConnectEncrypt()' - Connect to a HTTP server using encryption.
//
// This function is now deprecated. Please use the @link httpConnect2@ function
// instead.
//
// @deprecated@ @exclude all@
//

http_t *				// O - New HTTP connection
httpConnectEncrypt(
    const char        *host,		// I - Host to connect to
    int               port,		// I - Port number
    http_encryption_t encryption)	// I - Type of encryption to use
{
  DEBUG_printf("httpConnectEncrypt(host=\"%s\", port=%d, encryption=%d)", host, port, encryption);

  return (httpConnect2(host, port, NULL, AF_UNSPEC, encryption, 1, 30000, NULL));
}


//
// 'httpConnectURI()' - Connect to a HTTP service using a URI.
//
// This function creates a connection to a HTTP server.  The "uri" argument
// specifies a "http", "https", "ipp", or "ipps" URI for the service.
//
// The resource path for the service is returned in the buffer pointed to by
// the "resource" argument of size "rsize".
//
// The "msec" argument specifies how long to try to connect to the server or `0`
// to just create an unconnected `http_t` object.  The "cancel" argument
// specifies an integer variable that can be set to a non-zero value to cancel
// the connection process.
//
// The "require_ca" argument specifies whether to verify that the service
// connection is using a CA-signed X.509 certificate.
//
// @since CUPS 2.5@
//

http_t *				// O - New HTTP connection
httpConnectURI(const char *uri,		// I - Service to connect to
               char       *host,	// I - Host name buffer (`NULL` for don't care)
               size_t     hsize,	// I - Size of host name buffer
               int        *port,	// O - Port number (`NULL` for don't care)
               char       *resource,	// I - Resource path buffer (`NULL` for don't care)
               size_t     rsize,	// I - Size of resource path buffer
               bool       blocking,	// I - `true` for blocking connection, `false` for non-blocking
               int        msec,		// I - Connection timeout in milliseconds, `0` means don't connect
               int        *cancel,	// I - Pointer to "cancel" variable or `NULL` for none
               bool       require_ca)	// I - `true` to require a CA-signed X.509 certificate
{
  http_t	*http;			// New HTTP connection
  char		scheme[32],		// URI scheme
		userpass[32],		// URI username:password
		lhost[256],		// URI host (local copy)
		lresource[256];		// URI resource (local copy)
  int		lport;			// URI port (local copy)
  http_encryption_t encryption;		// Type of encryption to use
  http_uri_status_t uri_status;		// URI separation status


  DEBUG_printf("httpConnectURI(uri=\"%s\", host=%p, hsize=%u, port=%p, resource=%p, rsize=%u, blocking=%d, msec=%d, cancel=%p, require_ca=%s)", uri, (void *)host, (unsigned)hsize, (void *)port, (void *)resource, (unsigned)rsize, blocking, msec, (void *)cancel, require_ca ? "true" : "false");

  // Range check input...
  if (!uri)
  {
    if (host)
      *host = '\0';

    if (port)
      *port = 0;

    if (resource)
      *resource = '\0';

    return (NULL);
  }

  if (!host)
  {
    host  = lhost;
    hsize = sizeof(lhost);
  }

  if (!port)
    port = &lport;

  if (!resource)
  {
    resource = lresource;
    rsize    = sizeof(lresource);
  }

  // Get the URI components...
  if ((uri_status = httpSeparateURI(HTTP_URI_CODING_ALL, uri, scheme, sizeof(scheme), userpass, sizeof(userpass), host, hsize, port, resource, rsize)) < HTTP_URI_STATUS_OK)
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, httpURIStatusString(uri_status), 0);
    return (NULL);
  }

  DEBUG_printf("1httpConnectURI: scheme=\"%s\", host=\"%s\", port=%d, resource=\"%s\"", scheme, host, *port, resource);

  if (!strcmp(scheme, "https") || !strcmp(scheme, "ipps") || *port == 443)
    encryption = HTTP_ENCRYPTION_ALWAYS;
  else
    encryption = HTTP_ENCRYPTION_IF_REQUESTED;

  http = httpConnect2(host, *port, /*addrlist*/NULL, AF_UNSPEC, encryption, blocking, msec, cancel);

  if (httpIsEncrypted(http))
  {
    // Validate trust with service...
    char	*creds;			// Peer credentials...
    http_trust_t trust;			// Trust in credentials...

    creds = httpCopyPeerCredentials(http);
    trust = cupsGetCredentialsTrust(/*path*/NULL, host, creds, require_ca);

    if (trust == HTTP_TRUST_OK)
    {
      // Pin the trusted credentials (for TOFU)...
      cupsSaveCredentials(/*path*/NULL, host, creds, /*key*/NULL);
    }
    else if (trust != HTTP_TRUST_RENEWED)
    {
      // Don't allow the connection...
      httpClose(http);
      http = NULL;
    }

    free(creds);
  }

  return (http);
}


//
// 'httpDelete()' - Send a DELETE request to the server.
//
// @deprecated@ @exclude all@
//

int					// O - Status of call (0 = success)
httpDelete(http_t     *http,		// I - HTTP connection
           const char *uri)		// I - URI to delete
{
  return (http_send(http, HTTP_STATE_DELETE, uri) ? 0 : -1);
}


//
// '_httpDisconnect()' - Disconnect a HTTP connection.
//

void
_httpDisconnect(http_t *http)		// I - HTTP connection
{
  if (http->tls)
    _httpTLSStop(http);

  httpAddrClose(NULL, http->fd);

  http->fd = -1;
}


//
// 'httpEncryption()' - Set the required encryption on the link.
//
// @deprecated@ @exclude all@
//

int					// O - -1 on error, 0 on success
httpEncryption(http_t            *http,	// I - HTTP connection
               http_encryption_t e)	// I - New encryption preference
{
  return (httpSetEncryption(http, e) ? 0 : -1);
}


//
// 'httpError()' - Get the last error on a connection.
//
// @deprecated@ @exclude all@
//

int					// O - Error code (errno) value
httpError(http_t *http)			// I - HTTP connection
{
  return (httpGetError(http));
}


//
// 'httpFieldValue()' - Return the HTTP field enumeration value for a field name.
//

http_field_t				// O - Field index
httpFieldValue(const char *name)	// I - String name
{
  int	i;				// Looping var


  for (i = 0; i < HTTP_FIELD_MAX; i ++)
  {
    if (!_cups_strcasecmp(name, http_fields[i]))
      return ((http_field_t)i);
  }

  return (HTTP_FIELD_UNKNOWN);
}


//
// 'httpFlush()' - Flush data read from a HTTP connection.
//

void
httpFlush(http_t *http)			// I - HTTP connection
{
  char		buffer[8192];		// Junk buffer
  int		blocking;		// To block or not to block
  http_state_t	oldstate;		// Old state


  DEBUG_printf("httpFlush(http=%p), state=%s", (void *)http, httpStateString(http->state));

  // Nothing to do if we are in the "waiting" state...
  if (http->state == HTTP_STATE_WAITING)
    return;

  // Temporarily set non-blocking mode so we don't get stuck in httpRead()...
  blocking = http->blocking;
  http->blocking = 0;

  // Read any data we can...
  oldstate = http->state;
  while (httpRead2(http, buffer, sizeof(buffer)) > 0);

  // Restore blocking and reset the connection if we didn't get all of
  // the remaining data...
  http->blocking = blocking;

  if (http->state == oldstate && http->state != HTTP_STATE_WAITING &&
      http->fd >= 0)
  {
    // Didn't get the data back, so close the current connection.
    if (http->coding)
      http_content_coding_finish(http);

    DEBUG_puts("1httpFlush: Setting state to HTTP_STATE_WAITING and closing.");

    http->state = HTTP_STATE_WAITING;

    if (http->tls)
      _httpTLSStop(http);

    httpAddrClose(NULL, http->fd);

    http->fd = -1;
  }
}


//
// 'httpFlushWrite()' - Flush data written to a HTTP connection.
//
// @since CUPS 1.2@
//

int					// O - Bytes written or -1 on error
httpFlushWrite(http_t *http)		// I - HTTP connection
{
  ssize_t	bytes;			// Bytes written


  DEBUG_printf("httpFlushWrite(http=%p) data_encoding=%d", (void *)http, http ? http->data_encoding : 100);

  if (!http || !http->wused)
  {
    DEBUG_puts(http ? "1httpFlushWrite: Write buffer is empty." : "1httpFlushWrite: No connection.");
    return (0);
  }

  if (http->data_encoding == HTTP_ENCODING_CHUNKED)
    bytes = http_write_chunk(http, http->wbuffer, (size_t)http->wused);
  else
    bytes = http_write(http, http->wbuffer, (size_t)http->wused);

  http->wused = 0;

  DEBUG_printf("1httpFlushWrite: Returning %d, errno=%d.", (int)bytes, errno);

  return ((int)bytes);
}


//
// 'httpFreeCredentials()' - Free an array of credentials.
//
// @deprecated@ @exclude all@
//

void
httpFreeCredentials(
    cups_array_t *credentials)		// I - Array of credentials
{
  (void)credentials;
}


//
// 'httpGet()' - Send a GET request to the server.
//
// @deprecated@ @exclude all@
//

int					// O - Status of call (0 = success)
httpGet(http_t     *http,		// I - HTTP connection
        const char *uri)		// I - URI to get
{
  return (http_send(http, HTTP_STATE_GET, uri) ? 0 : -1);
}


//
// 'httpGetActivity()' - Get the most recent activity for a connection.
//
// The return value is the time in seconds of the last read or write.
//
// @since CUPS 2.0/OS 10.10@
//

time_t					// O - Time of last read or write
httpGetActivity(http_t *http)		// I - HTTP connection
{
  return (http ? http->activity : 0);
}


//
// 'httpGetAuthString()' - Get the current authorization string.
//
// The authorization string is set by @link cupsDoAuthentication@ and
// @link httpSetAuthString@.  Use @link httpGetAuthString@ to retrieve the
// string to use with @link httpSetField@ for the
// `HTTP_FIELD_AUTHORIZATION` value.
//
// @since CUPS 1.3@
//

char *					// O - Authorization string
httpGetAuthString(http_t *http)		// I - HTTP connection
{
  if (http)
    return (http->authstring);
  else
    return (NULL);
}


//
// 'httpGetBlocking()' - Get the blocking/non-blocking state of a connection.
//
// @since CUPS 1.2@
//

int					// O - 1 if blocking, 0 if non-blocking
httpGetBlocking(http_t *http)		// I - HTTP connection
{
  return (http ? http->blocking : 0);
}


//
// 'httpGetContentEncoding()' - Get a common content encoding, if any, between
//                              the client and server.
//
// This function uses the value of the Accepts-Encoding HTTP header and must be
// called after receiving a response from the server or a request from the
// client.  The value returned can be use in subsequent requests (for clients)
// or in the response (for servers) in order to compress the content stream.
//
// @since CUPS 1.7@
//

const char *				// O - Content-Coding value or `NULL` for the identity coding.
httpGetContentEncoding(http_t *http)	// I - HTTP connection
{
  if (http && http->fields[HTTP_FIELD_ACCEPT_ENCODING])
  {
    int		i;			// Looping var
    char	temp[HTTP_MAX_VALUE],	// Copy of Accepts-Encoding value
		*start,			// Start of coding value
		*end;			// End of coding value
    double	qvalue;			// "qvalue" for coding
    struct lconv *loc = localeconv();	// Locale data
    static const char * const codings[] =
    {					// Supported content codings
      "deflate",
      "gzip",
      "x-deflate",
      "x-gzip"
    };

    cupsCopyString(temp, http->fields[HTTP_FIELD_ACCEPT_ENCODING], sizeof(temp));

    for (start = temp; *start; start = end)
    {
      // Find the end of the coding name...
      qvalue = 1.0;
      end    = start;
      while (*end && *end != ';' && *end != ',' && !isspace(*end & 255))
        end ++;

      if (*end == ';')
      {
        // Grab the qvalue as needed...
        if (!strncmp(end, ";q=", 3))
          qvalue = _cupsStrScand(end + 3, NULL, loc);

        // Skip past all attributes...
        *end++ = '\0';
        while (*end && *end != ',' && !isspace(*end & 255))
          end ++;
      }
      else if (*end)
      {
        *end++ = '\0';
      }

      while (*end && isspace(*end & 255))
	end ++;

      // Check value if it matches something we support...
      if (qvalue <= 0.0)
        continue;

      for (i = 0; i < (int)(sizeof(codings) / sizeof(codings[0])); i ++)
      {
        if (!strcmp(start, codings[i]))
          return (codings[i]);
      }
    }
  }

  return (NULL);
}


//
// 'httpGetCookie()' - Get cookie data from the HTTP connection.
//
// This function returns any HTTP "Set-Cookie:" or "Cookie:" header data for the
// given HTTP connection as described in RFC 6265.  Use the
// @link httpGetCookieValue@ to get the value of a named "Cookie:" value.
//
// @since CUPS 1.1.19@
//

const char *				// O - Cookie data or `NULL`
httpGetCookie(http_t *http)		// I - HTTP connection
{
  return (http ? http->cookie : NULL);
}


//
// 'httpGetCookieValue()' - Get the value of a named cookie from the HTTP connection.
//
// This function copies the value of a named cookie in the HTTP "Cookie:" header
// for the given HTTP connection as described in RFC 6265.  Use the
// @link httpGetCookie@ function to get the original "Cookie:" string.
//
// @since CUPS 2.5@
//

char *					// O - Cookie value or `NULL` if not present
httpGetCookieValue(http_t     *http,	// I - HTTP connection
                   const char *name,	// I - Cookie name
                   char       *buffer,	// I - Value buffer
                   size_t     bufsize)	// I - Size of value buffer
{
  const char	*cookie;		// Cookie: header value
  char		current[128],		// Current name string
		*ptr,			// Pointer into name/buffer
		*end;			// End of name/buffer
  bool		match;			// Does the current name match?


  // Range check input...
  if (buffer)
    *buffer = '\0';

  if (!http || !http->cookie || !name || !buffer || bufsize < 2)
    return (NULL);

  // Loop through the cookie string...
  for (cookie = http->cookie; *cookie;)
  {
    // Skip leading whitespace...
    while (isspace(*cookie & 255))
      cookie ++;
    if (!*cookie)
      break;

    // Copy the name...
    for (ptr = current, end = current + sizeof(current) - 1; *cookie && *cookie != '=';)
    {
      if (ptr < end)
        *ptr++ = *cookie++;
      else
	cookie ++;
    }

    if (*cookie != '=')
      break;

    *ptr = '\0';
    match = !strcmp(current, name);
    cookie ++;

    // Then the value...
    if (*cookie == '\"')
    {
      // Copy quoted value...
      for (cookie ++, ptr = buffer, end = buffer + bufsize - 1; *cookie && *cookie != '\"';)
      {
        if (match && ptr < end)
	  *ptr++ = *cookie++;
	else
	  cookie ++;
      }

      if (*cookie == '\"')
        cookie ++;
      else
        match = false;
    }
    else
    {
      // Copy unquoted value...
      for (ptr = buffer, end = buffer + bufsize - 1; *cookie && *cookie != ';';)
      {
        if (match && ptr < end)
	  *ptr++ = *cookie++;
	else
	  cookie ++;
      }
    }

    if (match)
    {
      // Got the value we were looking for, nul-terminate and return...
      *ptr = '\0';
      return (buffer);
    }

    // Skip over separator...
    if (*cookie == ';')
      cookie ++;
  }

  // If we get here then we never found the cookie...
  return (NULL);
}


//
// 'httpGetEncryption()' - Get the current encryption mode of a connection.
//
// This function returns the encryption mode for the connection. Use the
// @link httpIsEncrypted@ function to determine whether a TLS session has
// been established.
//
// @since CUPS 2.0@
//

http_encryption_t			// O - Current encryption mode
httpGetEncryption(http_t *http)		// I - HTTP connection
{
  return (http ? http->encryption : HTTP_ENCRYPTION_IF_REQUESTED);
}


//
// 'httpGetError()' - Get the last error on a connection.
//
// @since CUPS 2.5@
//

int					// O - Error code (errno) value
httpGetError(http_t *http)		// I - HTTP connection
{
  if (http)
    return (http->error);
  else
    return (EINVAL);
}


//
// 'httpGetExpect()' - Get the value of the Expect header, if any.
//
// Returns `HTTP_STATUS_NONE` if there is no Expect header, otherwise
// returns the expected HTTP status code, typically `HTTP_STATUS_CONTINUE`.
//
// @since CUPS 1.7@
//

http_status_t				// O - Expect: status, if any
httpGetExpect(http_t *http)		// I - HTTP connection
{
  if (!http)
    return (HTTP_STATUS_ERROR);
  else
    return (http->expect);
}


//
// 'httpGetFd()' - Get the file descriptor associated with a connection.
//
// @since CUPS 1.2@
//

int					// O - File descriptor or -1 if none
httpGetFd(http_t *http)			// I - HTTP connection
{
  return (http ? http->fd : -1);
}


//
// 'httpGetField()' - Get a field value from a request/response.
//

const char *				// O - Field value
httpGetField(http_t       *http,	// I - HTTP connection
             http_field_t field)	// I - Field to get
{
  if (!http || field <= HTTP_FIELD_UNKNOWN || field >= HTTP_FIELD_MAX)
    return (NULL);
  else if (http->fields[field])
    return (http->fields[field]);
  else
    return ("");
}


//
// 'httpGetKeepAlive()' - Get the current Keep-Alive state of the connection.
//
// @since CUPS 2.0/OS 10.10@
//

http_keepalive_t			// O - Keep-Alive state
httpGetKeepAlive(http_t *http)		// I - HTTP connection
{
  return (http ? http->keep_alive : HTTP_KEEPALIVE_OFF);
}


//
// 'httpGetLength()' - Get the amount of data remaining from the Content-Length or Transfer-Encoding fields.
//
// This function is deprecated and will not return lengths larger than
// 2^31 - 1; use @link httpGetLength2@ instead.
//
// @deprecated@ @exclude all@
//

int					// O - Content length
httpGetLength(http_t *http)		// I - HTTP connection
{
  // Get the read content length and return the 32-bit value.
  if (http)
  {
    httpGetLength2(http);

    return (http->_data_remaining);
  }
  else
  {
    return (-1);
  }
}


//
// 'httpGetLength2()' - Get the amount of data remaining from the Content-Length or Transfer-Encoding fields.
//
// This function returns the complete content length, even for
// content larger than 2^31 - 1.
//
// @since CUPS 1.2@
//

off_t					// O - Content length
httpGetLength2(http_t *http)		// I - HTTP connection
{
  off_t			remaining;	// Remaining length


  DEBUG_printf("2httpGetLength2(http=%p), state=%s", (void *)http, http ? httpStateString(http->state) : "NONE");

  if (!http)
    return (-1);

  if (http->fields[HTTP_FIELD_TRANSFER_ENCODING] && !_cups_strcasecmp(http->fields[HTTP_FIELD_TRANSFER_ENCODING], "chunked"))
  {
    DEBUG_puts("4httpGetLength2: chunked request!");
    remaining = 0;
  }
  else
  {
    // The following is a hack for HTTP servers that don't send a
    // Content-Length or Transfer-Encoding field...
    //
    // If there is no Content-Length then the connection must close
    // after the transfer is complete...
    if (!http->fields[HTTP_FIELD_CONTENT_LENGTH] || !http->fields[HTTP_FIELD_CONTENT_LENGTH][0])
    {
      // Default content length is 0 for errors and certain types of operations,
      // and 2^31-1 for other successful requests...
      if (http->status >= HTTP_STATUS_MULTIPLE_CHOICES || http->state == HTTP_STATE_OPTIONS || (http->state == HTTP_STATE_GET && http->mode == _HTTP_MODE_SERVER) || http->state == HTTP_STATE_HEAD || (http->state == HTTP_STATE_PUT && http->mode == _HTTP_MODE_CLIENT) || http->state == HTTP_STATE_DELETE || http->state == HTTP_STATE_TRACE || http->state == HTTP_STATE_CONNECT)
        remaining = 0;
      else
        remaining = 2147483647;
    }
    else if ((remaining = strtoll(http->fields[HTTP_FIELD_CONTENT_LENGTH], NULL, 10)) < 0)
    {
      remaining = -1;
    }

    DEBUG_printf("4httpGetLength2: content_length=" CUPS_LLFMT, CUPS_LLCAST remaining);
  }

  return (remaining);
}


//
// 'httpGetPending()' - Get the number of bytes that are buffered for writing.
//
// @since CUPS 2.0/OS 10.10@
//

size_t					// O - Number of bytes buffered
httpGetPending(http_t *http)		// I - HTTP connection
{
  return (http ? (size_t)http->wused : 0);
}


//
// 'httpGetReady()' - Get the number of bytes that can be read without blocking.
//
// @since CUPS 2.0/OS 10.10@
//

size_t					// O - Number of bytes available
httpGetReady(http_t *http)		// I - HTTP connection
{
  if (!http)
    return (0);
  else if (http->used > 0)
    return ((size_t)http->used);
  else if (http->tls)
    return (_httpTLSPending(http));

  return (0);
}


//
// 'httpGetRemaining()' - Get the number of remaining bytes in the message body or current chunk.
//
// The @link httpIsChunked@ function can be used to determine whether the
// message body is chunked or fixed-length.
//
// @since CUPS 2.0/OS 10.10@
//

size_t					// O - Remaining bytes
httpGetRemaining(http_t *http)		// I - HTTP connection
{
  return (http ? (size_t)http->data_remaining : 0);
}


//
// 'httpGets()' - Get a line of text from a HTTP connection.
//
// @deprecated@ @exclude all@
//

char *					// O - Line or `NULL`
httpGets(char   *line,			// I - Line to read into
         int    length,			// I - Max length of buffer
	 http_t *http)			// I - HTTP connection
{
  return (httpGets2(http, line, (size_t)length));
}


//
// 'httpGets2()' - Get a line of text from a HTTP connection.
//

char *					// O - Line or `NULL`
httpGets2(http_t *http,			// I - HTTP connection
          char   *line,			// I - Line to read into
          size_t length)		// I - Max length of buffer
{
  char		*lineptr,		// Pointer into line
		*lineend,		// End of line
		*bufptr,		// Pointer into input buffer
	        *bufend;		// Pointer to end of buffer
  ssize_t	bytes;			// Number of bytes read
  int		eol;			// End-of-line?


  DEBUG_printf("2httpGets2(http=%p, line=%p, length=" CUPS_LLFMT ")", (void *)http, (void *)line, CUPS_LLCAST length);

  if (!http || !line || length <= 1)
    return (NULL);

  // Read a line from the buffer...
  http->error = 0;
  lineptr     = line;
  lineend     = line + length - 1;
  eol         = 0;

  while (lineptr < lineend)
  {
    // Pre-load the buffer as needed...
#ifdef _WIN32
    WSASetLastError(0);
#else
    errno = 0;
#endif // _WIN32

    while (http->used == 0)
    {
      // No newline; see if there is more data to be read...
      while (!_httpWait(http, http->wait_value, 1))
      {
	if (http->timeout_cb && (*http->timeout_cb)(http, http->timeout_data))
	  continue;

        DEBUG_puts("3httpGets2: Timed out!");
#ifdef _WIN32
        http->error = WSAETIMEDOUT;
#else
        http->error = ETIMEDOUT;
#endif // _WIN32
        return (NULL);
      }

      bytes = http_read(http, http->buffer + http->used, (size_t)(HTTP_MAX_BUFFER - http->used));

      DEBUG_printf("4httpGets2: read " CUPS_LLFMT " bytes.", CUPS_LLCAST bytes);

      if (bytes < 0)
      {
        // Nope, can't get a line this time...
#ifdef _WIN32
        DEBUG_printf("3httpGets2: recv() error %d!", WSAGetLastError());

        if (WSAGetLastError() == WSAEINTR)
        {
	  continue;
	}
	else if (WSAGetLastError() == WSAEWOULDBLOCK)
	{
	  if (http->timeout_cb && (*http->timeout_cb)(http, http->timeout_data))
	    continue;

	  http->error = WSAGetLastError();
	  return (NULL);
	}
	else if (WSAGetLastError() != http->error)
	{
	  http->error = WSAGetLastError();
	  continue;
	}

#else
        DEBUG_printf("3httpGets2: recv() error %d!", errno);

        if (errno == EINTR)
	{
	  continue;
	}
	else if (errno == EWOULDBLOCK || errno == EAGAIN)
	{
	  if (http->timeout_cb && (*http->timeout_cb)(http, http->timeout_data))
	    continue;
	  else if (!http->timeout_cb && errno == EAGAIN)
	    continue;

	  http->error = errno;
	  return (NULL);
	}
	else if (errno != http->error)
	{
	  http->error = errno;
	  continue;
	}
#endif // _WIN32

        return (NULL);
      }
      else if (bytes == 0)
      {
	http->error = EPIPE;

        return (NULL);
      }

      // Yup, update the amount used...
      http->used += (int)bytes;
    }

    // Now copy as much of the current line as possible...
    for (bufptr = http->buffer, bufend = http->buffer + http->used; lineptr < lineend && bufptr < bufend;)
    {
      if (*bufptr == 0x0a)
      {
        eol = 1;
	bufptr ++;
	break;
      }
      else if (*bufptr == 0x0d)
      {
        bufptr ++;
      }
      else
      {
	*lineptr++ = *bufptr++;
      }
    }

    http->used -= (int)(bufptr - http->buffer);
    if (http->used > 0)
      memmove(http->buffer, bufptr, (size_t)http->used);

    if (eol)
    {
      // End of line...
      http->activity = time(NULL);

      *lineptr = '\0';

      DEBUG_printf("3httpGets2: Returning \"%s\"", line);

      return (line);
    }
  }

  DEBUG_puts("3httpGets2: No new line available!");

  return (NULL);
}


//
// 'httpGetState()' - Get the current state of the HTTP request.
//

http_state_t				// O - HTTP state
httpGetState(http_t *http)		// I - HTTP connection
{
  return (http ? http->state : HTTP_STATE_ERROR);
}


//
// 'httpGetStatus()' - Get the status of the last HTTP request.
//
// @since CUPS 1.2@
//

http_status_t				// O - HTTP status
httpGetStatus(http_t *http)		// I - HTTP connection
{
  return (http ? http->status : HTTP_STATUS_ERROR);
}


//
// 'httpGetSubField()' - Get a sub-field value.
//
// @deprecated@ @exclude all@
//

char *					// O - Value or `NULL`
httpGetSubField(http_t       *http,	// I - HTTP connection
                http_field_t field,	// I - Field index
                const char   *name,	// I - Name of sub-field
		char         *value)	// O - Value string
{
  return (httpGetSubField2(http, field, name, value, HTTP_MAX_VALUE));
}


//
// 'httpGetSubField2()' - Get a sub-field value.
//
// @since CUPS 1.2@
//

char *					// O - Value or `NULL`
httpGetSubField2(http_t       *http,	// I - HTTP connection
                 http_field_t field,	// I - Field index
                 const char   *name,	// I - Name of sub-field
		 char         *value,	// O - Value string
		 int          valuelen)	// I - Size of value buffer
{
  const char	*fptr;			// Pointer into field
  char		temp[HTTP_MAX_VALUE],	// Temporary buffer for name
		*ptr,			// Pointer into string buffer
		*end;			// End of value buffer

  DEBUG_printf("2httpGetSubField2(http=%p, field=%d, name=\"%s\", value=%p, valuelen=%d)", (void *)http, field, name, (void *)value, valuelen);

  if (value)
    *value = '\0';

  if (!http || !name || !value || valuelen < 2 || field <= HTTP_FIELD_UNKNOWN || field >= HTTP_FIELD_MAX || !http->fields[field])
    return (NULL);

  end = value + valuelen - 1;

  for (fptr = http->fields[field]; *fptr;)
  {
    // Skip leading whitespace...
    while (_cups_isspace(*fptr))
      fptr ++;

    if (*fptr == ',')
    {
      fptr ++;
      continue;
    }

    // Get the sub-field name...
    for (ptr = temp; *fptr && *fptr != '=' && !_cups_isspace(*fptr) && ptr < (temp + sizeof(temp) - 1); *ptr++ = *fptr++)
      ; // Copy sub-field name

    *ptr = '\0';

    DEBUG_printf("4httpGetSubField2: name=\"%s\"", temp);

    // Skip trailing chars up to the '='...
    while (_cups_isspace(*fptr))
      fptr ++;

    if (!*fptr)
      break;

    if (*fptr != '=')
      continue;

    // Skip = and leading whitespace...
    fptr ++;

    while (_cups_isspace(*fptr))
      fptr ++;

    if (*fptr == '\"')
    {
      // Read quoted string...
      for (ptr = value, fptr ++; *fptr && *fptr != '\"' && ptr < end; *ptr++ = *fptr++)
        ; // Copy quoted string

      *ptr = '\0';

      while (*fptr && *fptr != '\"')
        fptr ++;

      if (*fptr)
        fptr ++;
    }
    else
    {
      // Read unquoted string...
      for (ptr = value; *fptr && !_cups_isspace(*fptr) && *fptr != ',' && ptr < end; *ptr++ = *fptr++)
        ; // Copy unquoted string

      *ptr = '\0';

      while (*fptr && !_cups_isspace(*fptr) && *fptr != ',')
        fptr ++;
    }

    DEBUG_printf("4httpGetSubField2: value=\"%s\"", value);

    // See if this is the one...
    if (!strcmp(name, temp))
    {
      DEBUG_printf("3httpGetSubField2: Returning \"%s\"", value);
      return (value);
    }
  }

  value[0] = '\0';

  DEBUG_puts("3httpGetSubField2: Returning NULL");

  return (NULL);
}


//
// 'httpGetVersion()' - Get the HTTP version at the other end.
//

http_version_t				// O - Version number
httpGetVersion(http_t *http)		// I - HTTP connection
{
  return (http ? http->version : HTTP_VERSION_1_0);
}


//
// 'httpHead()' - Send a HEAD request to the server.
//
// @deprecated@ @exclude all@
//

int					// O - Status of call (0 = success)
httpHead(http_t     *http,		// I - HTTP connection
         const char *uri)		// I - URI for head
{
  DEBUG_printf("httpHead(http=%p, uri=\"%s\")", (void *)http, uri);
  return (http_send(http, HTTP_STATE_HEAD, uri) ? 0 : -1);
}


//
// 'httpInitialize()' - Initialize the HTTP interface library and set the
//                      default HTTP proxy (if any).
//

void
httpInitialize(void)
{
  static int	initialized = 0;	// Have we been called before?
#ifdef _WIN32
  WSADATA	winsockdata;		// WinSock data
#endif // _WIN32


  _cupsGlobalLock();
  if (initialized)
  {
    _cupsGlobalUnlock();
    return;
  }

#ifdef _WIN32
  WSAStartup(MAKEWORD(2,2), &winsockdata);

#elif !defined(SO_NOSIGPIPE)
  // Ignore SIGPIPE signals...
  struct sigaction	action;		// POSIX sigaction data


  memset(&action, 0, sizeof(action));
  action.sa_handler = SIG_IGN;
  sigaction(SIGPIPE, &action, NULL);
#endif // _WIN32

  _httpTLSInitialize();

  initialized = 1;
  _cupsGlobalUnlock();
}


//
// 'httpIsChunked()' - Report whether a message body is chunked.
//
// This function returns non-zero if the message body is composed of
// variable-length chunks.
//
// @since CUPS 2.0/OS 10.10@
//

int					// O - 1 if chunked, 0 if not
httpIsChunked(http_t *http)		// I - HTTP connection
{
  return (http ? http->data_encoding == HTTP_ENCODING_CHUNKED : 0);
}


//
// 'httpIsEncrypted()' - Report whether a connection is encrypted.
//
// This function returns non-zero if the connection is currently encrypted.
//
// @since CUPS 2.0/OS 10.10@
//

int					// O - 1 if encrypted, 0 if not
httpIsEncrypted(http_t *http)		// I - HTTP connection
{
  return (http ? http->tls != NULL : 0);
}


//
// 'httpLoadCredentials()' - Load credentials.
//
// @deprecated@ @exclude all@
//

int					// O - -1 on error
httpLoadCredentials(
    const char   *path,			// I - Path for certs
    cups_array_t **credentials,		// O - Array of credentials
    const char   *common_name)		// I - Common name
{
  (void)path;
  (void)common_name;

  if (credentials)
    *credentials = NULL;

  return (-1);
}


//
// 'httpOptions()' - Send an OPTIONS request to the server.
//
// @deprecated@ @exclude all@
//

int					// O - Status of call (0 = success)
httpOptions(http_t     *http,		// I - HTTP connection
            const char *uri)		// I - URI for options
{
  return (http_send(http, HTTP_STATE_OPTIONS, uri) ? 0 : -1);
}


//
// 'httpPeek()' - Peek at data from a HTTP connection.
//
// This function copies available data from the given HTTP connection, reading
// a buffer as needed.  The data is still available for reading using
// @link httpRead2@.
//
// For non-blocking connections the usual timeouts apply.
//
// @since CUPS 1.7@
//

ssize_t					// O - Number of bytes copied
httpPeek(http_t *http,			// I - HTTP connection
         char   *buffer,		// I - Buffer for data
	 size_t length)			// I - Maximum number of bytes
{
  ssize_t	bytes;			// Bytes read
  char		len[32];		// Length string


  DEBUG_printf("httpPeek(http=%p, buffer=%p, length=" CUPS_LLFMT ")", (void *)http, (void *)buffer, CUPS_LLCAST length);

  if (http == NULL || buffer == NULL)
    return (-1);

  http->activity = time(NULL);
  http->error    = 0;

  if (length <= 0)
    return (0);

  if (http->data_encoding == HTTP_ENCODING_CHUNKED && http->data_remaining <= 0)
  {
    DEBUG_puts("2httpPeek: Getting chunk length...");

    if (httpGets2(http, len, sizeof(len)) == NULL)
    {
      DEBUG_puts("1httpPeek: Could not get length!");
      return (0);
    }

    if (!len[0])
    {
      DEBUG_puts("1httpPeek: Blank chunk length, trying again...");
      if (!httpGets2(http, len, sizeof(len)))
      {
	DEBUG_puts("1httpPeek: Could not get chunk length.");
	return (0);
      }
    }

    http->data_remaining = strtoll(len, NULL, 16);

    if (http->data_remaining < 0)
    {
      DEBUG_puts("1httpPeek: Negative chunk length!");
      return (0);
    }
  }

  DEBUG_printf("2httpPeek: data_remaining=" CUPS_LLFMT, CUPS_LLCAST http->data_remaining);

  if (http->data_remaining <= 0 && http->data_encoding != HTTP_ENCODING_FIELDS)
  {
    // A zero-length chunk ends a transfer; unless we are reading POST
    // data, go idle...
    if (http->coding >= _HTTP_CODING_GUNZIP)
      http_content_coding_finish(http);

    if (http->data_encoding == HTTP_ENCODING_CHUNKED)
      httpGets2(http, len, sizeof(len));

    if (http->state == HTTP_STATE_POST_RECV)
      http->state ++;
    else
      http->state = HTTP_STATE_STATUS;

    DEBUG_printf("1httpPeek: 0-length chunk, set state to %s.", httpStateString(http->state));

    // Prevent future reads for this request...
    http->data_encoding = HTTP_ENCODING_FIELDS;

    return (0);
  }
  else if (length > (size_t)http->data_remaining)
  {
    length = (size_t)http->data_remaining;
  }

  if (http->used == 0 && (http->coding == _HTTP_CODING_IDENTITY || (http->coding >= _HTTP_CODING_GUNZIP && ((z_stream *)http->stream)->avail_in == 0)))
  {
    // Buffer small reads for better performance...
    ssize_t	buflen;			// Length of read for buffer

    if (!http->blocking)
    {
      while (!httpWait(http, http->wait_value))
      {
	if (http->timeout_cb && (*http->timeout_cb)(http, http->timeout_data))
	  continue;

	return (0);
      }
    }

    if ((size_t)http->data_remaining > sizeof(http->buffer))
      buflen = sizeof(http->buffer);
    else
      buflen = (ssize_t)http->data_remaining;

    DEBUG_printf("2httpPeek: Reading %d bytes into buffer.", (int)buflen);
    bytes = http_read(http, http->buffer, (size_t)buflen);

    DEBUG_printf("2httpPeek: Read " CUPS_LLFMT " bytes into buffer.", CUPS_LLCAST bytes);
    if (bytes > 0)
    {
#ifdef DEBUG
      http_debug_hex("httpPeek", http->buffer, (int)bytes);
#endif // DEBUG

      http->used = (int)bytes;
    }
  }

  if (http->coding >= _HTTP_CODING_GUNZIP)
  {
    int		zerr;			// Decompressor error
    z_stream	stream;			// Copy of decompressor stream

    if (http->used > 0 && ((z_stream *)http->stream)->avail_in < HTTP_MAX_BUFFER)
    {
      size_t buflen = HTTP_MAX_BUFFER - ((z_stream *)http->stream)->avail_in;
					// Number of bytes to copy

      if (((z_stream *)http->stream)->avail_in > 0 && ((z_stream *)http->stream)->next_in > http->sbuffer)
        memmove(http->sbuffer, ((z_stream *)http->stream)->next_in, ((z_stream *)http->stream)->avail_in);

      ((z_stream *)http->stream)->next_in = http->sbuffer;

      if (buflen > (size_t)http->data_remaining)
        buflen = (size_t)http->data_remaining;

      if (buflen > (size_t)http->used)
        buflen = (size_t)http->used;

      DEBUG_printf("1httpPeek: Copying %d more bytes of data into decompression buffer.", (int)buflen);

      memcpy(http->sbuffer + ((z_stream *)http->stream)->avail_in, http->buffer, buflen);
      ((z_stream *)http->stream)->avail_in += buflen;
      http->used            -= (int)buflen;
      http->data_remaining  -= (off_t)buflen;

      if (http->used > 0)
        memmove(http->buffer, http->buffer + buflen, (size_t)http->used);
    }

    DEBUG_printf("2httpPeek: length=%d, avail_in=%d", (int)length, (int)((z_stream *)http->stream)->avail_in);

    if (inflateCopy(&stream, (z_stream *)http->stream) != Z_OK)
    {
      DEBUG_puts("2httpPeek: Unable to copy decompressor stream.");
      http->error = ENOMEM;
      inflateEnd(&stream);
      return (-1);
    }

    stream.next_out  = (Bytef *)buffer;
    stream.avail_out = (uInt)length;

    zerr = inflate(&stream, Z_SYNC_FLUSH);
    inflateEnd(&stream);

    if (zerr < Z_OK)
    {
      DEBUG_printf("2httpPeek: zerr=%d", zerr);
#ifdef DEBUG
      http_debug_hex("2httpPeek", (char *)http->sbuffer, (int)((z_stream *)http->stream)->avail_in);
#endif // DEBUG

      http->error = EIO;
      return (-1);
    }

    bytes = (ssize_t)(length - ((z_stream *)http->stream)->avail_out);
  }
  else if (http->used > 0)
  {
    if (length > (size_t)http->used)
      length = (size_t)http->used;

    bytes = (ssize_t)length;

    DEBUG_printf("2httpPeek: grabbing %d bytes from input buffer...", (int)bytes);

    memcpy(buffer, http->buffer, length);
  }
  else
  {
    bytes = 0;
  }

  if (bytes < 0)
  {
#ifdef _WIN32
    if (WSAGetLastError() == WSAEINTR || WSAGetLastError() == WSAEWOULDBLOCK)
      bytes = 0;
    else
      http->error = WSAGetLastError();
#else
    if (errno == EINTR || errno == EAGAIN)
      bytes = 0;
    else
      http->error = errno;
#endif // _WIN32
  }
  else if (bytes == 0)
  {
    http->error = EPIPE;
    return (0);
  }

  return (bytes);
}


//
// 'httpPost()' - Send a POST request to the server.
//
// @deprecated@ @exclude all@
//

int					// O - Status of call (0 = success)
httpPost(http_t     *http,		// I - HTTP connection
         const char *uri)		// I - URI for post
{
  return (http_send(http, HTTP_STATE_POST, uri) ? 0 : -1);
}


//
// 'httpPrintf()' - Print a formatted string to a HTTP connection.
//
// @private@
//

int					// O - Number of bytes written or `-1` on error
httpPrintf(http_t     *http,		// I - HTTP connection
           const char *format,		// I - printf-style format string
	   ...)				// I - Additional args as needed
{
  ssize_t	bytes;			// Number of bytes to write
  char		buf[65536];		// Buffer for formatted string
  va_list	ap;			// Variable argument pointer


  DEBUG_printf("2httpPrintf(http=%p, format=\"%s\", ...)", (void *)http, format);

  va_start(ap, format);
  bytes = vsnprintf(buf, sizeof(buf), format, ap);
  va_end(ap);

  DEBUG_printf("3httpPrintf: (" CUPS_LLFMT " bytes) %s", CUPS_LLCAST bytes, buf);

  if (bytes > (ssize_t)(sizeof(buf) - 1))
  {
    http->error = ENOMEM;
    return (-1);
  }
  else if (http->data_encoding == HTTP_ENCODING_FIELDS)
  {
    return ((int)httpWrite2(http, buf, (size_t)bytes));
  }
  else
  {
    if (http->wused)
    {
      DEBUG_puts("4httpPrintf: flushing existing data...");

      if (httpFlushWrite(http) < 0)
	return (-1);
    }

    return ((int)http_write(http, buf, (size_t)bytes));
  }
}


//
// 'httpPut()' - Send a PUT request to the server.
//
// @deprecated@ @exclude all@
//

int					// O - Status of call (0 = success)
httpPut(http_t     *http,		// I - HTTP connection
        const char *uri)		// I - URI to put
{
  DEBUG_printf("httpPut(http=%p, uri=\"%s\")", (void *)http, uri);
  return (http_send(http, HTTP_STATE_PUT, uri) ? 0 : -1);
}


//
// 'httpRead()' - Read data from a HTTP connection.
//
// This function is deprecated. Use the httpRead2() function which can
// read more than 2GB of data.
//
// @deprecated@ @exclude all@
//

int					// O - Number of bytes read
httpRead(http_t *http,			// I - HTTP connection
         char   *buffer,		// I - Buffer for data
	 int    length)			// I - Maximum number of bytes
{
  return ((int)httpRead2(http, buffer, (size_t)length));
}


//
// 'httpRead2()' - Read data from a HTTP connection.
//
// @since CUPS 1.2@
//

ssize_t					// O - Number of bytes read
httpRead2(http_t *http,			// I - HTTP connection
          char   *buffer,		// I - Buffer for data
	  size_t length)		// I - Maximum number of bytes
{
  ssize_t	bytes;			// Bytes read


  DEBUG_printf("httpRead2(http=%p, buffer=%p, length=" CUPS_LLFMT ") coding=%d data_encoding=%d data_remaining=" CUPS_LLFMT, (void *)http, (void *)buffer, CUPS_LLCAST length, http ? http->coding : 0, http ? http->data_encoding : 0, CUPS_LLCAST (http ? http->data_remaining : -1));

  if (http == NULL || buffer == NULL)
    return (-1);

  http->activity = time(NULL);
  http->error    = 0;

  if (length <= 0)
    return (0);

  if (http->coding >= _HTTP_CODING_GUNZIP)
  {
    do
    {
      if (((z_stream *)http->stream)->avail_in > 0)
      {
	int	zerr;			// Decompressor error

	DEBUG_printf("2httpRead2: avail_in=%d, avail_out=%d", (int)((z_stream *)http->stream)->avail_in, (int)length);

	((z_stream *)http->stream)->next_out  = (Bytef *)buffer;
	((z_stream *)http->stream)->avail_out = (uInt)length;

	if ((zerr = inflate((z_stream *)http->stream, Z_SYNC_FLUSH)) < Z_OK)
	{
	  DEBUG_printf("2httpRead2: zerr=%d", zerr);
#ifdef DEBUG
          http_debug_hex("2httpRead2", (char *)http->sbuffer, (int)((z_stream *)http->stream)->avail_in);
#endif // DEBUG

	  http->error = EIO;
	  return (-1);
	}

	bytes = (ssize_t)(length - ((z_stream *)http->stream)->avail_out);

	DEBUG_printf("2httpRead2: avail_in=%d, avail_out=%d, bytes=%d", ((z_stream *)http->stream)->avail_in, ((z_stream *)http->stream)->avail_out, (int)bytes);
      }
      else
      {
        bytes = 0;
      }

      if (bytes == 0)
      {
        ssize_t buflen = HTTP_MAX_BUFFER - (ssize_t)((z_stream *)http->stream)->avail_in;
					// Additional bytes for buffer

        if (buflen > 0)
        {
          if (((z_stream *)http->stream)->avail_in > 0 && ((z_stream *)http->stream)->next_in > http->sbuffer)
            memmove(http->sbuffer, ((z_stream *)http->stream)->next_in, ((z_stream *)http->stream)->avail_in);

	  ((z_stream *)http->stream)->next_in = http->sbuffer;

          DEBUG_printf("1httpRead2: Reading up to %d more bytes of data into decompression buffer.", (int)buflen);

          if (http->data_remaining > 0)
          {
	    if (buflen > http->data_remaining)
	      buflen = (ssize_t)http->data_remaining;

	    bytes = http_read_buffered(http, (char *)http->sbuffer + ((z_stream *)http->stream)->avail_in, (size_t)buflen);
          }
          else if (http->data_encoding == HTTP_ENCODING_CHUNKED)
          {
            bytes = http_read_chunk(http, (char *)http->sbuffer + ((z_stream *)http->stream)->avail_in, (size_t)buflen);
          }
          else
          {
            bytes = 0;
	  }

          if (bytes < 0)
            return (bytes);
          else if (bytes == 0)
            break;

          DEBUG_printf("1httpRead2: Adding " CUPS_LLFMT " bytes to decompression buffer.", CUPS_LLCAST bytes);

          http->data_remaining  -= bytes;
          ((z_stream *)http->stream)->avail_in += (uInt)bytes;

	  if (http->data_remaining <= 0 && http->data_encoding == HTTP_ENCODING_CHUNKED)
	  {
	    // Read the trailing blank line now...
	    char	len[32];		// Length string

	    httpGets2(http, len, sizeof(len));
	  }

          bytes = 0;
        }
        else
        {
          return (0);
	}
      }
    }
    while (bytes == 0);
  }
  else if (http->data_remaining == 0 && http->data_encoding == HTTP_ENCODING_CHUNKED)
  {
    if ((bytes = http_read_chunk(http, buffer, length)) > 0)
    {
      http->data_remaining -= bytes;

      if (http->data_remaining <= 0)
      {
        // Read the trailing blank line now...
        char	len[32];		// Length string

        httpGets2(http, len, sizeof(len));
      }
    }
  }
  else if (http->data_remaining <= 0)
  {
    // No more data to read...
    return (0);
  }
  else
  {
    DEBUG_printf("1httpRead2: Reading up to %d bytes into buffer.", (int)length);

    if (length > (size_t)http->data_remaining)
      length = (size_t)http->data_remaining;

    if ((bytes = http_read_buffered(http, buffer, length)) > 0)
    {
      http->data_remaining -= bytes;

      if (http->data_remaining <= 0 && http->data_encoding == HTTP_ENCODING_CHUNKED)
      {
        // Read the trailing blank line now...
        char	len[32];		// Length string

        httpGets2(http, len, sizeof(len));
      }
    }
  }

  if ((http->coding == _HTTP_CODING_IDENTITY || (http->coding >= _HTTP_CODING_GUNZIP && ((z_stream *)http->stream)->avail_in == 0)) && ((http->data_remaining <= 0 && http->data_encoding == HTTP_ENCODING_LENGTH) || (http->data_encoding == HTTP_ENCODING_CHUNKED && bytes == 0)))
  {
    if (http->coding >= _HTTP_CODING_GUNZIP)
      http_content_coding_finish(http);

    if (http->state == HTTP_STATE_POST_RECV)
      http->state ++;
    else if (http->state == HTTP_STATE_GET_SEND || http->state == HTTP_STATE_POST_SEND)
      http->state = HTTP_STATE_WAITING;
    else
      http->state = HTTP_STATE_STATUS;

    DEBUG_printf("1httpRead2: End of content, set state to %s.", httpStateString(http->state));
  }

  return (bytes);
}


//
// 'httpReadRequest()' - Read a HTTP request from a connection.
//
// @since CUPS 1.7@
//

http_state_t				// O - New state of connection
httpReadRequest(http_t *http,		// I - HTTP connection
                char   *uri,		// I - URI buffer
		size_t urilen)		// I - Size of URI buffer
{
  char	line[4096],			// HTTP request line
	*req_method,			// HTTP request method
	*req_uri,			// HTTP request URI
	*req_version;			// HTTP request version number string


  // Range check input...
  DEBUG_printf("httpReadRequest(http=%p, uri=%p, urilen=" CUPS_LLFMT ")", (void *)http, (void *)uri, CUPS_LLCAST urilen);

  if (uri)
    *uri = '\0';

  if (!http || !uri || urilen < 1)
  {
    DEBUG_puts("1httpReadRequest: Bad arguments, returning HTTP_STATE_ERROR.");
    return (HTTP_STATE_ERROR);
  }
  else if (http->state != HTTP_STATE_WAITING)
  {
    DEBUG_printf("1httpReadRequest: Bad state %s, returning HTTP_STATE_ERROR.", httpStateString(http->state));
    return (HTTP_STATE_ERROR);
  }

  // Reset state...
  httpClearFields(http);

  http->activity       = time(NULL);
  http->data_encoding  = HTTP_ENCODING_FIELDS;
  http->data_remaining = 0;
  http->keep_alive     = HTTP_KEEPALIVE_OFF;
  http->status         = HTTP_STATUS_OK;
  http->version        = HTTP_VERSION_1_1;

  // Read a line from the socket...
  if (!httpGets2(http, line, sizeof(line)))
  {
    DEBUG_puts("1httpReadRequest: Unable to read, returning HTTP_STATE_ERROR");
    return (HTTP_STATE_ERROR);
  }

  if (!line[0])
  {
    DEBUG_puts("1httpReadRequest: Blank line, returning HTTP_STATE_WAITING");
    return (HTTP_STATE_WAITING);
  }

  DEBUG_printf("1httpReadRequest: %s", line);

  // Parse it...
  req_method = line;
  req_uri    = line;

  while (*req_uri && !isspace(*req_uri & 255))
    req_uri ++;

  if (!*req_uri)
  {
    DEBUG_puts("1httpReadRequest: No request URI.");
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("No request URI."), 1);
    return (HTTP_STATE_ERROR);
  }

  *req_uri++ = '\0';

  while (*req_uri && isspace(*req_uri & 255))
    req_uri ++;

  req_version = req_uri;

  while (*req_version && !isspace(*req_version & 255))
    req_version ++;

  if (!*req_version)
  {
    DEBUG_puts("1httpReadRequest: No request protocol version.");
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("No request protocol version."), 1);
    return (HTTP_STATE_ERROR);
  }

  *req_version++ = '\0';

  while (*req_version && isspace(*req_version & 255))
    req_version ++;

  // Validate...
  if (!strcmp(req_method, "OPTIONS"))
  {
    http->state = HTTP_STATE_OPTIONS;
  }
  else if (!strcmp(req_method, "GET"))
  {
    http->state = HTTP_STATE_GET;
  }
  else if (!strcmp(req_method, "HEAD"))
  {
    http->state = HTTP_STATE_HEAD;
  }
  else if (!strcmp(req_method, "POST"))
  {
    http->state = HTTP_STATE_POST;
  }
  else if (!strcmp(req_method, "PUT"))
  {
    http->state = HTTP_STATE_PUT;
  }
  else if (!strcmp(req_method, "DELETE"))
  {
    http->state = HTTP_STATE_DELETE;
  }
  else if (!strcmp(req_method, "TRACE"))
  {
    http->state = HTTP_STATE_TRACE;
  }
  else if (!strcmp(req_method, "CONNECT"))
  {
    http->state = HTTP_STATE_CONNECT;
  }
  else
  {
    DEBUG_printf("1httpReadRequest: Unknown method \"%s\".", req_method);
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Unknown request method."), 1);
    return (HTTP_STATE_UNKNOWN_METHOD);
  }

  DEBUG_printf("1httpReadRequest: Set state to %s.", httpStateString(http->state));

  if (!strcmp(req_version, "HTTP/1.0"))
  {
    http->version    = HTTP_VERSION_1_0;
    http->keep_alive = HTTP_KEEPALIVE_OFF;
  }
  else if (!strcmp(req_version, "HTTP/1.1"))
  {
    http->version    = HTTP_VERSION_1_1;
    http->keep_alive = HTTP_KEEPALIVE_ON;
  }
  else
  {
    DEBUG_printf("1httpReadRequest: Unknown version \"%s\".", req_version);
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Unknown request version."), 1);
    return (HTTP_STATE_UNKNOWN_VERSION);
  }

  DEBUG_printf("1httpReadRequest: URI is \"%s\".", req_uri);
  cupsCopyString(uri, req_uri, urilen);

  return (http->state);
}


//
// 'httpReconnect()' - Reconnect to a HTTP server.
//
// This function is deprecated. Please use the @link httpReconnect2@ function
// instead.
//
// @deprecated@ @exclude all@
//

int					// O - 0 on success, non-zero on failure
httpReconnect(http_t *http)		// I - HTTP connection
{
  DEBUG_printf("httpReconnect(http=%p)", (void *)http);

  return (httpReconnect2(http, 30000, NULL));
}


//
// 'httpReconnect2()' - Reconnect to a HTTP server with timeout and optional
//                      cancel.
//
// @deprecated@ @exclude all@
//

int					// O - 0 on success, non-zero on failure
httpReconnect2(http_t *http,		// I - HTTP connection
	       int    msec,		// I - Timeout in milliseconds
	       int    *cancel)		// I - Pointer to "cancel" variable
{
  DEBUG_printf("httpReconnect2(http=%p, msec=%d, cancel=%p)", (void *)http, msec, (void *)cancel);

  return (httpConnectAgain(http, msec, cancel) ? 0 : -1);
}


//
// 'httpSaveCredentials()' - Save credentials.
//
// @deprecated@ @exclude all@
//

int					// O - -1 on error
httpSaveCredentials(
    const char   *path,			// I - Path for certs
    cups_array_t *credentials,		// O - Array of credentials
    const char   *common_name)		// I - Common name
{
  (void)path;
  (void)credentials;
  (void)common_name;

  return (-1);
}


//
// 'httpSetAuthString()' - Set the current authorization string.
//
// This function just stores a copy of the current authorization string in
// the HTTP connection object.  You must still call @link httpSetField@ to set
// `HTTP_FIELD_AUTHORIZATION` prior to issuing a HTTP request using
// @link httpWriteRequest@.
//
// @since CUPS 1.3@
//

void
httpSetAuthString(http_t     *http,	// I - HTTP connection
                  const char *scheme,	// I - Auth scheme (NULL to clear it)
		  const char *data)	// I - Auth data (NULL for none)
{
  // Range check input...
  DEBUG_printf("httpSetAuthString(http=%p, scheme=\"%s\", data=\"%s\")", (void *)http, scheme, data);

  if (!http)
    return;

  if (http->authstring && http->authstring != http->_authstring)
    free(http->authstring);

  http->authstring = http->_authstring;

  if (scheme)
  {
    // Set the current authorization string...
    size_t len = strlen(scheme) + (data ? strlen(data) + 1 : 0) + 1;
    char *temp;

    if (len > sizeof(http->_authstring))
    {
      if ((temp = malloc(len)) == NULL)
        len = sizeof(http->_authstring);
      else
        http->authstring = temp;
    }

    if (data)
      snprintf(http->authstring, len, "%s %s", scheme, data);
    else
      cupsCopyString(http->authstring, scheme, len);
  }
  else
  {
    // Clear the current authorization string...
    http->_authstring[0] = '\0';
  }

  DEBUG_printf("1httpSetAuthString: authstring=\"%s\"", http->authstring);
}


//
// 'httpSetBlocking()' - Set blocking/non-blocking behavior on a connection.
//

void
httpSetBlocking(http_t *http,		// I - HTTP connection
                bool   b)		// I - `true` for blocking, `false` for non-blocking
{
  if (http)
  {
    http->blocking = b ? 1 : 0;
    http_set_wait(http);
  }
}


//
// 'httpSetCredentials()' - Set the credentials associated with an encrypted connection.
//
// @deprecated@ @exclude all@
//

int						// O - Status of call (0 = success)
httpSetCredentials(http_t	*http,		// I - HTTP connection
		   cups_array_t *credentials)	// I - Array of credentials
{
  (void)http;
  (void)credentials;

  return (-1);
}


//
// 'httpSetCookie()' - Add Set-Cookie value(s).
//
// This function adds one or more Set-Cookie header values that will be sent to
// the client with the @link httpWriteResponse@ function.  Each value conforms
// to the format defined in RFC 6265.  Multiple values can be passed in the
// "cookie" string separated by a newline character.
//
// Call the @link httpClearCookies@ function to clear all Set-Cookie values.
//
// @since CUPS 1.1.19@
//

void
httpSetCookie(http_t     *http,		// I - HTTP cnnection
              const char *cookie)	// I - Cookie string
{
  // Range check input...
  if (!http || !cookie)
    return;

  // Set or append the Set-Cookie value....
  if (http->cookie)
  {
    // Append with a newline between values...
    size_t	clen,			// Length of cookie string
		ctotal;			// Total length of cookies
    char	*temp;			// Temporary value

    clen   = strlen(http->cookie);
    ctotal = clen + strlen(cookie) + 2;

    if ((temp = realloc(http->cookie, ctotal)) == NULL)
      return;

    http->cookie = temp;
    temp[clen]   = '\n';
    cupsCopyString(temp + clen + 1, cookie, ctotal - clen - 1);
  }
  else
  {
    // Just copy/set this cookie...
    http->cookie = strdup(cookie);
  }
}


//
// 'httpSetDefaultField()' - Set the default value of an HTTP header.
//
// Currently only `HTTP_FIELD_ACCEPT_ENCODING`, `HTTP_FIELD_SERVER`,
// and `HTTP_FIELD_USER_AGENT` can be set.
//
// @since CUPS 1.7@
//

void
httpSetDefaultField(http_t       *http,	// I - HTTP connection
                    http_field_t field,	// I - Field index
	            const char   *value)// I - Value
{
  DEBUG_printf("httpSetDefaultField(http=%p, field=%d(%s), value=\"%s\")", (void *)http, field, http_fields[field], value);

  if (!http || field <= HTTP_FIELD_UNKNOWN || field >= HTTP_FIELD_MAX)
    return;

  if (http->default_fields[field])
    free(http->default_fields[field]);

  http->default_fields[field] = value ? strdup(value) : NULL;
}


//
// 'httpSetEncryption()' - Set the required encryption on the link.
//
// @since CUPS 2.5@
//

bool					// O - `true` on success, `false` on error
httpSetEncryption(
    http_t            *http,		// I - HTTP connection
    http_encryption_t e)		// I - New encryption preference
{
  DEBUG_printf("httpSetEncryption(http=%p, e=%d)", (void *)http, e);

  if (!http)
    return (true);

  if (http->mode == _HTTP_MODE_CLIENT)
  {
    http->encryption = e;

    if ((http->encryption == HTTP_ENCRYPTION_ALWAYS && !http->tls) || (http->encryption == HTTP_ENCRYPTION_NEVER && http->tls))
      return (httpConnectAgain(http, 30000, NULL));
    else if (http->encryption == HTTP_ENCRYPTION_REQUIRED && !http->tls)
      return (http_tls_upgrade(http));
    else
      return (true);
  }
  else
  {
    if (e == HTTP_ENCRYPTION_NEVER && http->tls)
      return (true);

    http->encryption = e;
    if (e != HTTP_ENCRYPTION_IF_REQUESTED && !http->tls)
      return (_httpTLSStart(http));
    else
      return (true);
  }
}


//
// 'httpSetExpect()' - Set the Expect: header in a request.
//
// Currently only `HTTP_STATUS_CONTINUE` is supported for the "expect"
// argument.
//
// @since CUPS 1.2@
//

void
httpSetExpect(http_t        *http,	// I - HTTP connection
              http_status_t expect)	// I - HTTP status to expect (`HTTP_STATUS_CONTINUE`)
{
  DEBUG_printf("httpSetExpect(http=%p, expect=%d)", (void *)http, expect);

  if (http)
    http->expect = expect;
}


//
// 'httpSetField()' - Set the value of an HTTP header.
//

void
httpSetField(http_t       *http,	// I - HTTP connection
             http_field_t field,	// I - Field index
	     const char   *value)	// I - Value
{
  DEBUG_printf("httpSetField(http=%p, field=%d(%s), value=\"%s\")", (void *)http, field, http_fields[field], value);

  if (!http || field <= HTTP_FIELD_UNKNOWN || field >= HTTP_FIELD_MAX || !value)
    return;

  http_add_field(http, field, value, 0);
}


//
// 'httpSetKeepAlive()' - Set the current Keep-Alive state of a connection.
//
// @since CUPS 2.0/OS 10.10@
//

void
httpSetKeepAlive(
    http_t           *http,		// I - HTTP connection
    http_keepalive_t keep_alive)	// I - New Keep-Alive value
{
  if (http)
    http->keep_alive = keep_alive;
}


//
// 'httpSetLength()' - Set the content-length and content-encoding.
//
// @since CUPS 1.2@
//

void
httpSetLength(http_t *http,		// I - HTTP connection
              size_t length)		// I - Length (0 for chunked)
{
  DEBUG_printf("httpSetLength(http=%p, length=" CUPS_LLFMT ")", (void *)http, CUPS_LLCAST length);

  if (!http)
    return;

  if (!length)
  {
    httpSetField(http, HTTP_FIELD_TRANSFER_ENCODING, "chunked");
    httpSetField(http, HTTP_FIELD_CONTENT_LENGTH, "");
  }
  else
  {
    char len[32];			// Length string

    snprintf(len, sizeof(len), CUPS_LLFMT, CUPS_LLCAST length);
    httpSetField(http, HTTP_FIELD_TRANSFER_ENCODING, "");
    httpSetField(http, HTTP_FIELD_CONTENT_LENGTH, len);
  }
}


//
// 'httpSetTimeout()' - Set read/write timeouts and an optional callback.
//
// The optional timeout callback receives both the HTTP connection and a user
// data pointer and must return 1 to continue or 0 to error (time) out.
//
// @since CUPS 1.5@
//

void
httpSetTimeout(
    http_t            *http,		// I - HTTP connection
    double            timeout,		// I - Number of seconds for timeout, must be greater than `0.0`
    http_timeout_cb_t cb,		// I - Callback function or `NULL`
    void              *user_data)	// I - User data pointer
{
  if (!http || timeout <= 0.0)
    return;

  http->timeout_cb    = cb;
  http->timeout_data  = user_data;
  http->timeout_value = timeout;

  if (http->fd >= 0)
    http_set_timeout(http->fd, timeout);

  http_set_wait(http);
}


//
// 'httpShutdown()' - Shutdown one side of an HTTP connection.
//
// @since CUPS 2.0/OS 10.10@
//

void
httpShutdown(http_t *http)		// I - HTTP connection
{
  if (!http || http->fd < 0)
    return;

  if (http->tls)
    _httpTLSStop(http);

#ifdef _WIN32
  shutdown(http->fd, SD_RECEIVE);	// Microsoft-ism...
#else
  shutdown(http->fd, SHUT_RD);
#endif // _WIN32
}


//
// 'httpTrace()' - Send an TRACE request to the server.
//
// @deprecated@ @exclude all@
//

int					// O - Status of call (0 = success)
httpTrace(http_t     *http,		// I - HTTP connection
          const char *uri)		// I - URI for trace
{
  return (http_send(http, HTTP_STATE_TRACE, uri) ? 0 : -1);
}


//
// '_httpUpdate()' - Update the current HTTP status for incoming data.
//
// Note: Unlike httpUpdate(), this function does not flush pending write data
// and only retrieves a single status line from the HTTP connection.
//

int					// O - 1 to continue, 0 to stop
_httpUpdate(http_t        *http,	// I - HTTP connection
            http_status_t *status)	// O - Current HTTP status
{
  char		line[32768],		// Line from connection...
		*value;			// Pointer to value on line
  http_field_t	field;			// Field index
  int		major, minor;		// HTTP version numbers


  DEBUG_printf("_httpUpdate(http=%p, status=%p), state=%s", (void *)http, (void *)status, httpStateString(http->state));

  // Grab a single line from the connection...
  if (!httpGets2(http, line, sizeof(line)))
  {
    *status = HTTP_STATUS_ERROR;
    return (0);
  }

  DEBUG_printf("2_httpUpdate: Got \"%s\"", line);

  if (line[0] == '\0')
  {
    // Blank line means the start of the data section (if any).  Return
    // the result code, too...
    //
    // If we get status 100 (HTTP_STATUS_CONTINUE), then we *don't* change
    // states.  Instead, we just return HTTP_STATUS_CONTINUE to the caller and
    // keep on tryin'...
    if (http->status == HTTP_STATUS_CONTINUE)
    {
      *status = http->status;
      return (0);
    }

    if (http->status < HTTP_STATUS_BAD_REQUEST)
      http->digest_tries = 0;

    if (http->status == HTTP_STATUS_SWITCHING_PROTOCOLS && !http->tls)
    {
      if (!_httpTLSStart(http))
      {
        httpAddrClose(NULL, http->fd);
        http->fd = -1;

	*status = http->status = HTTP_STATUS_ERROR;
	return (0);
      }

      *status = HTTP_STATUS_CONTINUE;
      return (0);
    }

    if (http_set_length(http) < 0)
    {
      DEBUG_puts("1_httpUpdate: Bad Content-Length.");
      http->error  = EINVAL;
      http->status = *status = HTTP_STATUS_ERROR;
      return (0);
    }

    switch (http->state)
    {
      case HTTP_STATE_GET :
      case HTTP_STATE_POST :
      case HTTP_STATE_POST_RECV :
      case HTTP_STATE_PUT :
	  http->state ++;

	  DEBUG_printf("1_httpUpdate: Set state to %s.", httpStateString(http->state));

      case HTTP_STATE_POST_SEND :
      case HTTP_STATE_HEAD :
	  break;

      default :
	  http->state = HTTP_STATE_WAITING;

	  DEBUG_puts("1_httpUpdate: Reset state to HTTP_STATE_WAITING.");
	  break;
    }

    DEBUG_puts("1_httpUpdate: Calling http_content_coding_start.");
    http_content_coding_start(http, httpGetField(http, HTTP_FIELD_CONTENT_ENCODING));

    *status = http->status;
    return (0);
  }
  else if (!strncmp(line, "HTTP/", 5) && http->mode == _HTTP_MODE_CLIENT)
  {
    // Got the beginning of a response...
    int	intstatus;			// Status value as an integer

    if (sscanf(line, "HTTP/%d.%d%d", &major, &minor, &intstatus) != 3)
    {
      *status = http->status = HTTP_STATUS_ERROR;
      return (0);
    }

    httpClearFields(http);

    http->version = (http_version_t)(major * 100 + minor);
    *status       = http->status = (http_status_t)intstatus;
  }
  else if ((value = strchr(line, ':')) != NULL)
  {
    // Got a value...
    *value++ = '\0';
    while (_cups_isspace(*value))
      value ++;

    DEBUG_printf("1_httpUpdate: Header %s: %s", line, value);

    // Be tolerants of servers that send unknown attribute fields...
    if (!_cups_strcasecmp(line, "expect"))
    {
      // "Expect: 100-continue" or similar...
      http->expect = (http_status_t)atoi(value);
    }
    else if (!_cups_strcasecmp(line, "cookie"))
    {
      // "Cookie: name=value[; name=value ...]" - replaces previous cookies...
      httpSetCookie(http, value);
    }
    else if ((field = httpFieldValue(line)) != HTTP_FIELD_UNKNOWN)
    {
      http_add_field(http, field, value, 1);

      if (field == HTTP_FIELD_AUTHENTICATION_INFO)
        httpGetSubField2(http, HTTP_FIELD_AUTHENTICATION_INFO, "nextnonce", http->nextnonce, (int)sizeof(http->nextnonce));
    }
#ifdef DEBUG
    else
    {
      DEBUG_printf("1_httpUpdate: unknown field %s seen!", line);
    }
#endif // DEBUG
  }
  else
  {
    DEBUG_printf("1_httpUpdate: Bad response line \"%s\"!", line);
    http->error  = EINVAL;
    http->status = *status = HTTP_STATUS_ERROR;
    return (0);
  }

  return (1);
}


//
// 'httpUpdate()' - Update the current HTTP state for incoming data.
//

http_status_t				// O - HTTP status
httpUpdate(http_t *http)		// I - HTTP connection
{
  http_status_t	status;			// Request status


  DEBUG_printf("httpUpdate(http=%p), state=%s", (void *)http, httpStateString(http->state));

  // Flush pending data, if any...
  if (http->wused)
  {
    DEBUG_puts("2httpUpdate: flushing buffer...");

    if (httpFlushWrite(http) < 0)
      return (HTTP_STATUS_ERROR);
  }

  // If we haven't issued any commands, then there is nothing to "update"...
  if (http->state == HTTP_STATE_WAITING)
    return (HTTP_STATUS_CONTINUE);

  // Grab all of the lines we can from the connection...
  while (_httpUpdate(http, &status))
    ; // Update as needed...

  // See if there was an error...
  if (http->error == EPIPE && http->status > HTTP_STATUS_CONTINUE)
  {
    DEBUG_printf("1httpUpdate: Returning status %d...", http->status);
    return (http->status);
  }

  if (http->error)
  {
    DEBUG_printf("1httpUpdate: socket error %d - %s", http->error, strerror(http->error));
    http->status = HTTP_STATUS_ERROR;
    return (HTTP_STATUS_ERROR);
  }

  // Return the current status...
  return (status);
}


//
// '_httpWait()' - Wait for data available on a connection (no flush).
//

int					// O - 1 if data is available, 0 otherwise
_httpWait(http_t *http,			// I - HTTP connection
          int    msec,			// I - Milliseconds to wait
	  int    usessl)		// I - Use TLS context?
{
  struct pollfd		pfd;		// Polled file descriptor
  int			nfds;		// Result from select()/poll()


  DEBUG_printf("4_httpWait(http=%p, msec=%d, usessl=%d)", (void *)http, msec, usessl);

  if (http->fd < 0)
  {
    DEBUG_printf("5_httpWait: Returning 0 since fd=%d", http->fd);
    return (0);
  }

  // Check the TLS buffers for data first...
  if (usessl && http->tls && _httpTLSPending(http))
  {
    DEBUG_puts("5_httpWait: Return 1 since there is pending TLS data.");
    return (1);
  }

  // Then try doing a select() or poll() to poll the socket...
  pfd.fd     = http->fd;
  pfd.events = POLLIN;

  do
  {
    nfds = poll(&pfd, 1, msec);
  }
#ifdef _WIN32
  while (nfds < 0 && (WSAGetLastError() == WSAEINTR || WSAGetLastError() == WSAEWOULDBLOCK));
#else
  while (nfds < 0 && (errno == EINTR || errno == EAGAIN));
#endif // _WIN32

  DEBUG_printf("5_httpWait: returning with nfds=%d, errno=%d...", nfds, errno);

  return (nfds > 0);
}


//
// 'httpWait()' - Wait for data available on a connection.
//
// @since CUPS 1.1.19@
//

int					// O - 1 if data is available, 0 otherwise
httpWait(http_t *http,			// I - HTTP connection
         int    msec)			// I - Milliseconds to wait
{
  // First see if there is data in the buffer...
  DEBUG_printf("2httpWait(http=%p, msec=%d)", (void *)http, msec);

  if (http == NULL)
    return (0);

  if (http->used)
  {
    DEBUG_puts("3httpWait: Returning 1 since there is buffered data ready.");
    return (1);
  }

  if (http->coding >= _HTTP_CODING_GUNZIP && ((z_stream *)http->stream)->avail_in > 0)
  {
    DEBUG_puts("3httpWait: Returning 1 since there is buffered data ready.");
    return (1);
  }

  // Flush pending data, if any...
  if (http->wused)
  {
    DEBUG_puts("3httpWait: Flushing write buffer.");

    if (httpFlushWrite(http) < 0)
      return (0);
  }

  // If not, check the TLS buffers and do a select() on the connection...
  return (_httpWait(http, msec, 1));
}


//
// 'httpWrite()' - Write data to a HTTP connection.
//
// This function is deprecated. Use the httpWrite2() function which can
// write more than 2GB of data.
//
// @deprecated@ @exclude all@
//

int					// O - Number of bytes written
httpWrite(http_t     *http,		// I - HTTP connection
          const char *buffer,		// I - Buffer for data
	  int        length)		// I - Number of bytes to write
{
  return ((int)httpWrite2(http, buffer, (size_t)length));
}


//
// 'httpWrite2()' - Write data to a HTTP connection.
//
// @since CUPS 1.2@
//

ssize_t					// O - Number of bytes written
httpWrite2(http_t     *http,		// I - HTTP connection
           const char *buffer,		// I - Buffer for data
	   size_t     length)		// I - Number of bytes to write
{
  ssize_t	bytes;			// Bytes written


  DEBUG_printf("httpWrite2(http=%p, buffer=%p, length=" CUPS_LLFMT ")", (void *)http, (void *)buffer, CUPS_LLCAST length);

  // Range check input...
  if (!http || !buffer)
  {
    DEBUG_puts("1httpWrite2: Returning -1 due to bad input.");
    return (-1);
  }

  // Mark activity on the connection...
  http->activity = time(NULL);

  // Buffer small writes for better performance...
  if (http->coding == _HTTP_CODING_GZIP || http->coding == _HTTP_CODING_DEFLATE)
  {
    DEBUG_printf("1httpWrite2: http->coding=%d", http->coding);

    if (length == 0)
    {
      http_content_coding_finish(http);
      bytes = 0;
    }
    else
    {
      size_t	slen;			// Bytes to write
      ssize_t	sret;			// Bytes written

      ((z_stream *)http->stream)->next_in   = (Bytef *)buffer;
      ((z_stream *)http->stream)->avail_in  = (uInt)length;

      while (deflate((z_stream *)http->stream, Z_NO_FLUSH) == Z_OK)
      {
        DEBUG_printf("1httpWrite2: avail_out=%d", ((z_stream *)http->stream)->avail_out);

        if (((z_stream *)http->stream)->avail_out > 0)
	  continue;

	slen = _HTTP_MAX_SBUFFER - ((z_stream *)http->stream)->avail_out;

        DEBUG_printf("1httpWrite2: Writing intermediate chunk, len=%d", (int)slen);

	if (slen > 0 && http->data_encoding == HTTP_ENCODING_CHUNKED)
	  sret = http_write_chunk(http, (char *)http->sbuffer, slen);
	else if (slen > 0)
	  sret = http_write(http, (char *)http->sbuffer, slen);
	else
	  sret = 0;

        if (sret < 0)
	{
	  DEBUG_puts("1httpWrite2: Unable to write, returning -1.");
	  return (-1);
	}

	((z_stream *)http->stream)->next_out  = (Bytef *)http->sbuffer;
	((z_stream *)http->stream)->avail_out = (uInt)_HTTP_MAX_SBUFFER;
      }

      bytes = (ssize_t)length;
    }
  }
  else if (length > 0)
  {
    if (http->wused && (length + (size_t)http->wused) > sizeof(http->wbuffer))
    {
      DEBUG_printf("2httpWrite2: Flushing buffer (wused=%d, length=" CUPS_LLFMT ")", http->wused, CUPS_LLCAST length);

      httpFlushWrite(http);
    }

    if ((length + (size_t)http->wused) <= sizeof(http->wbuffer) && length < sizeof(http->wbuffer))
    {
      // Write to buffer...
      DEBUG_printf("2httpWrite2: Copying " CUPS_LLFMT " bytes to wbuffer...", CUPS_LLCAST length);

      memcpy(http->wbuffer + http->wused, buffer, length);
      http->wused += (int)length;
      bytes = (ssize_t)length;
    }
    else
    {
      // Otherwise write the data directly...
      DEBUG_printf("2httpWrite2: Writing " CUPS_LLFMT " bytes to socket...", CUPS_LLCAST length);

      if (http->data_encoding == HTTP_ENCODING_CHUNKED)
	bytes = http_write_chunk(http, buffer, length);
      else
	bytes = http_write(http, buffer, length);

      DEBUG_printf("2httpWrite2: Wrote " CUPS_LLFMT " bytes...", CUPS_LLCAST bytes);
    }

    if (http->data_encoding == HTTP_ENCODING_LENGTH)
      http->data_remaining -= bytes;
  }
  else
  {
    bytes = 0;
  }

  // Handle end-of-request processing...
  if ((http->data_encoding == HTTP_ENCODING_CHUNKED && length == 0) || (http->data_encoding == HTTP_ENCODING_LENGTH && http->data_remaining == 0))
  {
    // Finished with the transfer; unless we are sending POST or PUT data, go idle...
    if (http->coding == _HTTP_CODING_GZIP || http->coding == _HTTP_CODING_DEFLATE)
      http_content_coding_finish(http);

    if (http->wused)
    {
      if (httpFlushWrite(http) < 0)
        return (-1);
    }

    if (http->data_encoding == HTTP_ENCODING_CHUNKED)
    {
      // Send a 0-length chunk at the end of the request...
      http_write(http, "0\r\n\r\n", 5);

      // Reset the data state...
      http->data_encoding  = HTTP_ENCODING_FIELDS;
      http->data_remaining = 0;
    }

    if (http->state == HTTP_STATE_POST_RECV)
      http->state ++;
    else if (http->state == HTTP_STATE_POST_SEND || http->state == HTTP_STATE_GET_SEND)
      http->state = HTTP_STATE_WAITING;
    else
      http->state = HTTP_STATE_STATUS;

    DEBUG_printf("2httpWrite2: Changed state to %s.", httpStateString(http->state));
  }

  DEBUG_printf("1httpWrite2: Returning " CUPS_LLFMT ".", CUPS_LLCAST bytes);

  return (bytes);
}


//
// 'httpWriteRequest()' - Send a HTTP request.
//
// @since CUPS 2.5@
//

bool					// O - `true` on success, `false` on error
httpWriteRequest(http_t     *http,	// I - HTTP connection
                 const char *method,	// I - Method string ("GET", "POST", etc.)
                 const char *uri)	// I - URI
{
  if (!strcasecmp(method, "DELETE"))
    return (http_send(http, HTTP_STATE_DELETE, uri));
  else if (!strcasecmp(method, "GET"))
    return (http_send(http, HTTP_STATE_GET, uri));
  else if (!strcasecmp(method, "HEAD"))
    return (http_send(http, HTTP_STATE_HEAD, uri));
  else if (!strcasecmp(method, "OPTIONS"))
    return (http_send(http, HTTP_STATE_OPTIONS, uri));
  else if (!strcasecmp(method, "POST"))
    return (http_send(http, HTTP_STATE_POST, uri));
  else if (!strcasecmp(method, "PUT"))
    return (http_send(http, HTTP_STATE_PUT, uri));
  else if (!strcasecmp(method, "TRACE"))
    return (http_send(http, HTTP_STATE_TRACE, uri));
  else
    return (false);
}


//
// 'httpWriteResponse()' - Write a HTTP response to a client connection.
//
// @since CUPS 1.7@
//

int					// O - 0 on success, -1 on error
httpWriteResponse(http_t        *http,	// I - HTTP connection
		  http_status_t status)	// I - Status code
{
  http_encoding_t	old_encoding;	// Old data_encoding value
  off_t			old_remaining;	// Old data_remaining value
  cups_lang_t		*lang;		// Response language


  // Range check input...
  DEBUG_printf("httpWriteResponse(http=%p, status=%d)", (void *)http, status);

  if (!http || status < HTTP_STATUS_CONTINUE)
  {
    DEBUG_puts("1httpWriteResponse: Bad input.");
    return (-1);
  }

  // Set the various standard fields if they aren't already...
  if (!http->fields[HTTP_FIELD_DATE])
    httpSetField(http, HTTP_FIELD_DATE, httpGetDateString(time(NULL)));

  if (status >= HTTP_STATUS_BAD_REQUEST && http->keep_alive)
  {
    http->keep_alive = HTTP_KEEPALIVE_OFF;
    httpSetField(http, HTTP_FIELD_KEEP_ALIVE, "");
  }

  if (http->version == HTTP_VERSION_1_1)
  {
    if (!http->fields[HTTP_FIELD_CONNECTION])
    {
      if (http->keep_alive)
	httpSetField(http, HTTP_FIELD_CONNECTION, "Keep-Alive");
      else
	httpSetField(http, HTTP_FIELD_CONNECTION, "close");
    }

    if (http->keep_alive && !http->fields[HTTP_FIELD_KEEP_ALIVE])
      httpSetField(http, HTTP_FIELD_KEEP_ALIVE, "timeout=10");
  }

  if (status == HTTP_STATUS_UPGRADE_REQUIRED || status == HTTP_STATUS_SWITCHING_PROTOCOLS)
  {
    if (!http->fields[HTTP_FIELD_CONNECTION])
      httpSetField(http, HTTP_FIELD_CONNECTION, "Upgrade");

    if (!http->fields[HTTP_FIELD_UPGRADE])
      httpSetField(http, HTTP_FIELD_UPGRADE, "TLS/1.2,TLS/1.1,TLS/1.0");

    if (!http->fields[HTTP_FIELD_CONTENT_LENGTH])
      httpSetField(http, HTTP_FIELD_CONTENT_LENGTH, "0");
  }

  if (!http->fields[HTTP_FIELD_SERVER])
    httpSetField(http, HTTP_FIELD_SERVER, http->default_fields[HTTP_FIELD_SERVER] ? http->default_fields[HTTP_FIELD_SERVER] : CUPS_MINIMAL);

  // Set the Accept-Encoding field if it isn't already...
  if (!http->fields[HTTP_FIELD_ACCEPT_ENCODING])
    httpSetField(http, HTTP_FIELD_ACCEPT_ENCODING, http->default_fields[HTTP_FIELD_ACCEPT_ENCODING] ? http->default_fields[HTTP_FIELD_ACCEPT_ENCODING] : "gzip, deflate, identity");

  // Get the response language, if any...
  lang = cupsLangGet(http->fields[HTTP_FIELD_CONTENT_LANGUAGE]);

  // Send the response header...
  old_encoding        = http->data_encoding;
  old_remaining       = http->data_remaining;
  http->data_encoding = HTTP_ENCODING_FIELDS;

  if (httpPrintf(http, "HTTP/%d.%d %d %s\r\n", http->version / 100, http->version % 100, (int)status, _httpStatusString(lang, status)) < 0)
  {
    http->status = HTTP_STATUS_ERROR;
    return (-1);
  }

  if (status != HTTP_STATUS_CONTINUE)
  {
    // 100 Continue doesn't have the rest of the response headers...
    int		i;			// Looping var
    const char	*value;			// Field value

    for (i = 0; i < HTTP_FIELD_MAX; i ++)
    {
      if ((value = httpGetField(http, i)) != NULL && *value)
      {
	if (httpPrintf(http, "%s: %s\r\n", http_fields[i], value) < 1)
	{
	  http->status = HTTP_STATUS_ERROR;
	  return (-1);
	}
      }
    }

    if (http->cookie)
    {
      char	*start,			// Start of cookie
		*ptr;			// Pointer into cookie

      for (start = http->cookie; start; start = ptr)
      {
        if ((ptr = strchr(start, '\n')) != NULL)
          *ptr = '\0';

	if (strchr(start, ';'))
	{
	  if (httpPrintf(http, "Set-Cookie: %s\r\n", start) < 1)
	  {
	    http->status = HTTP_STATUS_ERROR;
	    if (ptr)
	      *ptr = '\n';
	    return (-1);
	  }
	}
	else if (httpPrintf(http, "Set-Cookie: %s; path=/; httponly;%s\r\n", start, http->tls ? " secure;" : "") < 1)
	{
	  http->status = HTTP_STATUS_ERROR;
	  if (ptr)
	    *ptr = '\n';
	  return (-1);
	}

	if (ptr)
	  *ptr++ = '\n';
      }
    }

    // "Click-jacking" defense (STR #4492)...
    if (httpPrintf(http, "X-Frame-Options: DENY\r\nContent-Security-Policy: frame-ancestors 'none'\r\n") < 1)
    {
      http->status = HTTP_STATUS_ERROR;
      return (-1);
    }
  }

  if (httpWrite2(http, "\r\n", 2) < 2)
  {
    http->status = HTTP_STATUS_ERROR;
    return (-1);
  }

  if (httpFlushWrite(http) < 0)
  {
    http->status = HTTP_STATUS_ERROR;
    return (-1);
  }

  if (status == HTTP_STATUS_CONTINUE || status == HTTP_STATUS_SWITCHING_PROTOCOLS)
  {
    // Restore the old data_encoding and data_length values...
    http->data_encoding  = old_encoding;
    http->data_remaining = old_remaining;

    if (old_remaining <= INT_MAX)
      http->_data_remaining = (int)old_remaining;
    else
      http->_data_remaining = INT_MAX;
  }
  else if (http->state == HTTP_STATE_OPTIONS || http->state == HTTP_STATE_HEAD || http->state == HTTP_STATE_PUT || http->state == HTTP_STATE_TRACE || http->state == HTTP_STATE_CONNECT || http->state == HTTP_STATE_STATUS)
  {
    DEBUG_printf("1httpWriteResponse: Resetting state to HTTP_STATE_WAITING, was %s.", httpStateString(http->state));
    http->state = HTTP_STATE_WAITING;
  }
  else
  {
    // Force data_encoding and data_length to be set according to the response  headers...
    http_set_length(http);

    if (http->data_encoding == HTTP_ENCODING_LENGTH && http->data_remaining == 0)
    {
      DEBUG_printf("1httpWriteResponse: Resetting state to HTTP_STATE_WAITING, was %s.", httpStateString(http->state));
      http->state = HTTP_STATE_WAITING;
      return (0);
    }

    if (http->state == HTTP_STATE_POST_RECV || http->state == HTTP_STATE_GET)
      http->state ++;

    // Then start any content encoding...
    DEBUG_puts("1httpWriteResponse: Calling http_content_coding_start.");
    http_content_coding_start(http, httpGetField(http, HTTP_FIELD_CONTENT_ENCODING));
  }

  return (0);
}


//
// 'http_add_field()' - Add a value for a HTTP field, appending if needed.
//

static void
http_add_field(http_t       *http,	// I - HTTP connection
               http_field_t field,	// I - HTTP field
               const char   *value,	// I - Value string
               int          append)	// I - Append value?
{
  char		temp[1024],		// Temporary value string
		combined[HTTP_MAX_VALUE];
					// Combined value string
  size_t	fieldlen,		// Length of existing value
		valuelen,		// Length of value string
		total;			// Total length of string


  if (field == HTTP_FIELD_HOST)
  {
    // Special-case for Host: as we don't want a trailing "." on the hostname
    // and need to bracket IPv6 numeric addresses.
    char *ptr = strchr(value, ':');

    if (value[0] != '[' && ptr && strchr(ptr + 1, ':'))
    {
      // Bracket IPv6 numeric addresses...
      //
      // This is slightly inefficient (basically copying twice), but is an edge
      // case and not worth optimizing...
      snprintf(temp, sizeof(temp), "[%s]", value);
      value = temp;
    }
    else if (*value)
    {
      // Check for a trailing dot on the hostname...
      cupsCopyString(temp, value, sizeof(temp));
      value = temp;
      ptr   = temp + strlen(temp) - 1;

      if (*ptr == '.')
	*ptr = '\0';
    }
  }

  if (append && field != HTTP_FIELD_ACCEPT_ENCODING && field != HTTP_FIELD_ACCEPT_LANGUAGE && field != HTTP_FIELD_ACCEPT_RANGES && field != HTTP_FIELD_ALLOW && field != HTTP_FIELD_LINK && field != HTTP_FIELD_TRANSFER_ENCODING && field != HTTP_FIELD_UPGRADE && field != HTTP_FIELD_WWW_AUTHENTICATE)
    append = 0;

  if (!append && http->fields[field])
  {
    if (field >= HTTP_FIELD_ACCEPT_ENCODING || http->fields[field] != http->_fields[field])
      free(http->fields[field]);

    http->fields[field] = NULL;
  }

  valuelen = strlen(value);

  if (!valuelen)
  {
    if (field < HTTP_FIELD_ACCEPT_ENCODING)
      http->_fields[field][0] = '\0';
    return;
  }

  if (http->fields[field])
  {
    fieldlen = strlen(http->fields[field]);
    total    = fieldlen + 2 + valuelen;
  }
  else
  {
    fieldlen = 0;
    total    = valuelen;
  }

  if (total < HTTP_MAX_VALUE && field < HTTP_FIELD_ACCEPT_ENCODING)
  {
    // Copy short values to legacy char arrays (maintained for binary
    // compatibility with CUPS 1.2.x and earlier applications...)
    if (fieldlen)
    {
      snprintf(combined, sizeof(combined), "%s, %s", http->_fields[field], value);
      value = combined;
    }

    cupsCopyString(http->_fields[field], value, sizeof(http->_fields[field]));
    http->fields[field] = http->_fields[field];
  }
  else if (fieldlen)
  {
    // Expand the field value...
    char *mcombined;			// New value string

    if (field < HTTP_FIELD_ACCEPT_ENCODING && http->fields[field] == http->_fields[field])
    {
      if ((mcombined = malloc(total + 1)) != NULL)
      {
	http->fields[field] = mcombined;
	snprintf(mcombined, total + 1, "%s, %s", http->_fields[field], value);
      }
    }
    else if ((mcombined = realloc(http->fields[field], total + 1)) != NULL)
    {
      http->fields[field] = mcombined;
      cupsConcatString(mcombined, ", ", total + 1);
      cupsConcatString(mcombined, value, total + 1);
    }
  }
  else
  {
    // Allocate the field value...
    http->fields[field] = strdup(value);
  }

  DEBUG_printf("1http_add_field: New value of %s is \"%s\"", http_fields[field], http->fields[field]);

  if (field == HTTP_FIELD_CONTENT_ENCODING && http->data_encoding != HTTP_ENCODING_FIELDS)
  {
    DEBUG_puts("1http_add_field: Calling http_content_coding_start.");
    http_content_coding_start(http, value);
  }
}


//
// 'http_content_coding_finish()' - Finish doing any content encoding.
//

static void
http_content_coding_finish(
    http_t *http)			// I - HTTP connection
{
  int		zerr;			// Compression status
  Byte		dummy[1];		// Dummy read buffer
  size_t	bytes;			// Number of bytes to write


  DEBUG_printf("http_content_coding_finish(http=%p)", (void *)http);
  DEBUG_printf("1http_content_coding_finishing: http->coding=%d", http->coding);

  switch (http->coding)
  {
    case _HTTP_CODING_DEFLATE :
    case _HTTP_CODING_GZIP :
        ((z_stream *)http->stream)->next_in  = dummy;
        ((z_stream *)http->stream)->avail_in = 0;

        do
        {
          zerr  = deflate((z_stream *)http->stream, Z_FINISH);
	  bytes = _HTTP_MAX_SBUFFER - ((z_stream *)http->stream)->avail_out;

          if (bytes > 0)
	  {
	    DEBUG_printf("1http_content_coding_finish: Writing trailing chunk, len=%d", (int)bytes);

	    if (http->data_encoding == HTTP_ENCODING_CHUNKED)
	      http_write_chunk(http, (char *)http->sbuffer, bytes);
	    else
	      http_write(http, (char *)http->sbuffer, bytes);
          }

          ((z_stream *)http->stream)->next_out  = (Bytef *)http->sbuffer;
          ((z_stream *)http->stream)->avail_out = (uInt)_HTTP_MAX_SBUFFER;
	}
        while (zerr == Z_OK);

        deflateEnd((z_stream *)http->stream);

        free(http->sbuffer);
        free(http->stream);

        http->sbuffer = NULL;
        http->stream  = NULL;

        if (http->wused)
          httpFlushWrite(http);
        break;

    case _HTTP_CODING_INFLATE :
    case _HTTP_CODING_GUNZIP :
        inflateEnd((z_stream *)http->stream);

        free(http->sbuffer);
        free(http->stream);

        http->sbuffer = NULL;
        http->stream  = NULL;
        break;

    default :
        break;
  }

  http->coding = _HTTP_CODING_IDENTITY;
}


//
// 'http_content_coding_start()' - Start doing content encoding.
//

static void
http_content_coding_start(
    http_t     *http,			// I - HTTP connection
    const char *value)			// I - Value of Content-Encoding
{
  int			zerr;		// Error/status
  _http_coding_t	coding;		// Content coding value


  DEBUG_printf("http_content_coding_start(http=%p, value=\"%s\")", (void *)http, value);

  if (http->coding != _HTTP_CODING_IDENTITY)
  {
    DEBUG_printf("1http_content_coding_start: http->coding already %d.", http->coding);
    return;
  }
  else if (!strcmp(value, "x-gzip") || !strcmp(value, "gzip"))
  {
    if (http->state == HTTP_STATE_GET_SEND || http->state == HTTP_STATE_POST_SEND)
    {
      coding = http->mode == _HTTP_MODE_SERVER ? _HTTP_CODING_GZIP : _HTTP_CODING_GUNZIP;
    }
    else if (http->state == HTTP_STATE_POST_RECV || http->state == HTTP_STATE_PUT_RECV)
    {
      coding = http->mode == _HTTP_MODE_CLIENT ? _HTTP_CODING_GZIP : _HTTP_CODING_GUNZIP;
    }
    else
    {
      DEBUG_puts("1http_content_coding_start: Not doing content coding.");
      return;
    }
  }
  else if (!strcmp(value, "x-deflate") || !strcmp(value, "deflate"))
  {
    if (http->state == HTTP_STATE_GET_SEND || http->state == HTTP_STATE_POST_SEND)
    {
      coding = http->mode == _HTTP_MODE_SERVER ? _HTTP_CODING_DEFLATE : _HTTP_CODING_INFLATE;
    }
    else if (http->state == HTTP_STATE_POST_RECV || http->state == HTTP_STATE_PUT_RECV)
    {
      coding = http->mode == _HTTP_MODE_CLIENT ? _HTTP_CODING_DEFLATE : _HTTP_CODING_INFLATE;
    }
    else
    {
      DEBUG_puts("1http_content_coding_start: Not doing content coding.");
      return;
    }
  }
  else
  {
    DEBUG_puts("1http_content_coding_start: Not doing content coding.");
    return;
  }

  switch (coding)
  {
    case _HTTP_CODING_DEFLATE :
    case _HTTP_CODING_GZIP :
        if (http->wused)
          httpFlushWrite(http);

        if ((http->sbuffer = malloc(_HTTP_MAX_SBUFFER)) == NULL)
        {
          http->status = HTTP_STATUS_ERROR;
          http->error  = errno;
          return;
        }

        // Window size for compression is 11 bits - optimal based on PWG Raster
        // sample files on pwg.org.  -11 is raw deflate, 27 is gzip, per ZLIB
        // documentation.
	if ((http->stream = calloc(1, sizeof(z_stream))) == NULL)
	{
          free(http->sbuffer);

          http->sbuffer = NULL;
          http->status  = HTTP_STATUS_ERROR;
          http->error   = errno;
          return;
	}

        if ((zerr = deflateInit2((z_stream *)http->stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, coding == _HTTP_CODING_DEFLATE ? -11 : 27, 7, Z_DEFAULT_STRATEGY)) < Z_OK)
        {
          free(http->sbuffer);
          free(http->stream);

          http->sbuffer = NULL;
          http->stream  = NULL;
          http->status  = HTTP_STATUS_ERROR;
          http->error   = zerr == Z_MEM_ERROR ? ENOMEM : EINVAL;
          return;
        }

	((z_stream *)http->stream)->next_out  = (Bytef *)http->sbuffer;
	((z_stream *)http->stream)->avail_out = (uInt)_HTTP_MAX_SBUFFER;
        break;

    case _HTTP_CODING_INFLATE :
    case _HTTP_CODING_GUNZIP :
        if ((http->sbuffer = malloc(_HTTP_MAX_SBUFFER)) == NULL)
        {
          http->status = HTTP_STATUS_ERROR;
          http->error  = errno;
          return;
        }

        // Window size for decompression is up to 15 bits (maximum supported).
        // -15 is raw inflate, 31 is gunzip, per ZLIB documentation.
	if ((http->stream = calloc(1, sizeof(z_stream))) == NULL)
	{
          free(http->sbuffer);

          http->sbuffer = NULL;
          http->status  = HTTP_STATUS_ERROR;
          http->error   = errno;
          return;
	}

        if ((zerr = inflateInit2((z_stream *)http->stream, coding == _HTTP_CODING_INFLATE ? -15 : 31)) < Z_OK)
        {
          free(http->sbuffer);
          free(http->stream);

          http->sbuffer = NULL;
          http->stream  = NULL;
          http->status  = HTTP_STATUS_ERROR;
          http->error   = zerr == Z_MEM_ERROR ? ENOMEM : EINVAL;
          return;
        }

        ((z_stream *)http->stream)->avail_in = 0;
        ((z_stream *)http->stream)->next_in  = http->sbuffer;
        break;

    default :
        break;
  }

  http->coding = coding;

  DEBUG_printf("1http_content_coding_start: http->coding now %d.", http->coding);
}


//
// 'http_create()' - Create an unconnected HTTP connection.
//

static http_t *				// O - HTTP connection
http_create(
    const char        *host,		// I - Hostname
    int               port,		// I - Port number
    http_addrlist_t   *addrlist,	// I - Address list or `NULL`
    int               family,		// I - Address family or AF_UNSPEC
    http_encryption_t encryption,	// I - Encryption to use
    int               blocking,		// I - 1 for blocking mode
    _http_mode_t      mode)		// I - _HTTP_MODE_CLIENT or _SERVER
{
  http_t	*http;			// New HTTP connection
  char		service[255];		// Service name
  http_addrlist_t *myaddrlist = NULL;	// My address list
  _cups_globals_t *cg = _cupsGlobals();	// Thread global data


  DEBUG_printf("4http_create(host=\"%s\", port=%d, addrlist=%p, family=%d, encryption=%d, blocking=%d, mode=%d)", host, port, (void *)addrlist, family, encryption, blocking, mode);

  if (!host && mode == _HTTP_MODE_CLIENT)
    return (NULL);

  httpInitialize();

  // Lookup the host...
  if (addrlist)
  {
    myaddrlist = httpAddrCopyList(addrlist);
  }
  else
  {
    snprintf(service, sizeof(service), "%d", port);

    myaddrlist = httpAddrGetList(host, family, service);
  }

  if (!myaddrlist)
    return (NULL);

  // Allocate memory for the structure...
  if ((http = calloc(1, sizeof(http_t))) == NULL)
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, strerror(errno), 0);
    httpAddrFreeList(myaddrlist);
    return (NULL);
  }

  // Initialize the HTTP data...
  http->mode     = mode;
  http->activity = time(NULL);
  http->addrlist = myaddrlist;
  http->blocking = blocking;
  http->fd       = -1;
#ifdef HAVE_GSSAPI
  http->gssctx   = GSS_C_NO_CONTEXT;
  http->gssname  = GSS_C_NO_NAME;
#endif // HAVE_GSSAPI
  http->status   = HTTP_STATUS_CONTINUE;
  http->version  = HTTP_VERSION_1_1;

  if (host)
  {
    DEBUG_printf("5http_create: host=\"%s\"", host);

    if (!strncmp(host, "fe80::", 6))
    {
      // IPv6 link local address, convert to IPvFuture format...
      char	*zoneid;		// Pointer to zoneid separator

      snprintf(http->hostname, sizeof(http->hostname), "[v1.%s]", host);
      if ((zoneid = strchr(http->hostname, '%')) != NULL)
        *zoneid = '+';
    }
    else if (isxdigit(host[0]) && isxdigit(host[1]) && isxdigit(host[2]) && isxdigit(host[3]) && host[4] == ':')
    {
      // IPv6 address, convert to URI format...
      snprintf(http->hostname, sizeof(http->hostname), "[%s]", host);
    }
    else
    {
      // Not an IPv6 numeric address...
      cupsCopyString(http->hostname, host, sizeof(http->hostname));
    }

    DEBUG_printf("5http_create: http->hostname=\"%s\"", http->hostname);
  }

  if (port == 443)			// Always use encryption for https
    http->encryption = HTTP_ENCRYPTION_ALWAYS;
  else
    http->encryption = encryption;

  http_set_wait(http);

  // Set client credentials...
  http->tls_credentials = _httpUseCredentials(cg->credentials);

  // Return the new structure...
  return (http);
}


#ifdef DEBUG
//
// 'http_debug_hex()' - Do a hex dump of a buffer.
//

static void
http_debug_hex(const char *prefix,	// I - Prefix for line
               const char *buffer,	// I - Buffer to dump
               int        bytes)	// I - Bytes to dump
{
  int	i, j,				// Looping vars
	ch;				// Current character
  char	line[255],			// Line buffer
	*start,				// Start of line after prefix
	*ptr;				// Pointer into line


  if (_cups_debug_fd < 0 || _cups_debug_level < 6)
    return;

  DEBUG_printf("9%s: %d bytes:", prefix, bytes);

  snprintf(line, sizeof(line), "9%s: ", prefix);
  start = line + strlen(line);

  for (i = 0; i < bytes; i += 16)
  {
    for (j = 0, ptr = start; j < 16 && (i + j) < bytes; j ++, ptr += 2)
      snprintf(ptr, 3, "%02X", buffer[i + j] & 255);

    while (j < 16)
    {
      memcpy(ptr, "  ", 3);
      ptr += 2;
      j ++;
    }

    memcpy(ptr, "  ", 3);
    ptr += 2;

    for (j = 0; j < 16 && (i + j) < bytes; j ++)
    {
      ch = buffer[i + j] & 255;

      if (ch < ' ' || ch >= 127)
	ch = '.';

      *ptr++ = (char)ch;
    }

    *ptr = '\0';
    DEBUG_puts(line);
  }
}
#endif // DEBUG


//
// 'http_read()' - Read a buffer from a HTTP connection.
//
// This function does the low-level read from the socket, retrying and timing
// out as needed.
//

static ssize_t				// O - Number of bytes read or -1 on error
http_read(http_t *http,			// I - HTTP connection
          char   *buffer,		// I - Buffer
          size_t length)		// I - Maximum bytes to read
{
  ssize_t	bytes;			// Bytes read


  DEBUG_printf("7http_read(http=%p, buffer=%p, length=" CUPS_LLFMT ")", (void *)http, (void *)buffer, CUPS_LLCAST length);

  if (!http->blocking || http->timeout_value > 0.0)
  {
    while (!httpWait(http, http->wait_value))
    {
      if (http->timeout_cb && (*http->timeout_cb)(http, http->timeout_data))
	continue;

      DEBUG_puts("8http_read: Timeout.");
      return (0);
    }
  }

  DEBUG_printf("8http_read: Reading %d bytes into buffer.", (int)length);

  do
  {
    if (http->tls)
      bytes = _httpTLSRead(http, buffer, (int)length);
    else
      bytes = recv(http->fd, buffer, length, 0);

    if (bytes < 0)
    {
#ifdef _WIN32
      if (WSAGetLastError() != WSAEINTR)
      {
	http->error = WSAGetLastError();
	return (-1);
      }
      else if (WSAGetLastError() == WSAEWOULDBLOCK)
      {
	if (!http->timeout_cb ||
	    !(*http->timeout_cb)(http, http->timeout_data))
	{
	  http->error = WSAEWOULDBLOCK;
	  return (-1);
	}
      }
#else
      DEBUG_printf("8http_read: %s", strerror(errno));

      if (errno == EWOULDBLOCK || errno == EAGAIN)
      {
	if (http->timeout_cb && !(*http->timeout_cb)(http, http->timeout_data))
	{
	  http->error = errno;
	  return (-1);
	}
	else if (!http->timeout_cb && errno != EAGAIN)
	{
	  http->error = errno;
	  return (-1);
	}
      }
      else if (errno != EINTR)
      {
	http->error = errno;
	return (-1);
      }
#endif // _WIN32
    }
  }
  while (bytes < 0);

  DEBUG_printf("8http_read: Read " CUPS_LLFMT " bytes into buffer.", CUPS_LLCAST bytes);
#ifdef DEBUG
  if (bytes > 0)
  {
    http_debug_hex("http_read", buffer, (int)bytes);
  }
  else
#endif // DEBUG
  if (bytes == 0)
  {
    http->error = EPIPE;
    return (0);
  }

  return (bytes);
}


//
// 'http_read_buffered()' - Do a buffered read from a HTTP connection.
//
// This function reads data from the HTTP buffer or from the socket, as needed.
//

static ssize_t				// O - Number of bytes read or -1 on error
http_read_buffered(http_t *http,	// I - HTTP connection
                   char   *buffer,	// I - Buffer
                   size_t length)	// I - Maximum bytes to read
{
  ssize_t	bytes;			// Bytes read


  DEBUG_printf("7http_read_buffered(http=%p, buffer=%p, length=" CUPS_LLFMT ") used=%d", (void *)http, (void *)buffer, CUPS_LLCAST length, http->used);

  if (http->used > 0)
  {
    if (length > (size_t)http->used)
      bytes = (ssize_t)http->used;
    else
      bytes = (ssize_t)length;

    DEBUG_printf("8http_read: Grabbing %d bytes from input buffer.", (int)bytes);

    memcpy(buffer, http->buffer, (size_t)bytes);
    http->used -= (int)bytes;

    if (http->used > 0)
      memmove(http->buffer, http->buffer + bytes, (size_t)http->used);
  }
  else
  {
    bytes = http_read(http, buffer, length);
  }

  return (bytes);
}


//
// 'http_read_chunk()' - Read a chunk from a HTTP connection.
//
// This function reads and validates the chunk length, then does a buffered read
// returning the number of bytes placed in the buffer.
//

static ssize_t				// O - Number of bytes read or -1 on error
http_read_chunk(http_t *http,		// I - HTTP connection
		char   *buffer,		// I - Buffer
		size_t length)		// I - Maximum bytes to read
{
  DEBUG_printf("7http_read_chunk(http=%p, buffer=%p, length=" CUPS_LLFMT ")", (void *)http, (void *)buffer, CUPS_LLCAST length);

  if (http->data_remaining <= 0)
  {
    char	len[32];		// Length string

    if (!httpGets2(http, len, sizeof(len)))
    {
      DEBUG_puts("8http_read_chunk: Could not get chunk length.");
      return (0);
    }

    if (!len[0])
    {
      DEBUG_puts("8http_read_chunk: Blank chunk length, trying again...");
      if (!httpGets2(http, len, sizeof(len)))
      {
	DEBUG_puts("8http_read_chunk: Could not get chunk length.");
	return (0);
      }
    }

    http->data_remaining = strtoll(len, NULL, 16);

    if (http->data_remaining < 0)
    {
      DEBUG_printf("8http_read_chunk: Negative chunk length \"%s\" (" CUPS_LLFMT ")", len, CUPS_LLCAST http->data_remaining);
      return (0);
    }

    DEBUG_printf("8http_read_chunk: Got chunk length \"%s\" (" CUPS_LLFMT ")", len, CUPS_LLCAST http->data_remaining);

    if (http->data_remaining == 0)
    {
      // 0-length chunk, grab trailing blank line...
      httpGets2(http, len, sizeof(len));
    }
  }

  DEBUG_printf("8http_read_chunk: data_remaining=" CUPS_LLFMT, CUPS_LLCAST http->data_remaining);

  if (http->data_remaining <= 0)
    return (0);
  else if (length > (size_t)http->data_remaining)
    length = (size_t)http->data_remaining;

  return (http_read_buffered(http, buffer, length));
}


//
// 'http_send()' - Send a request with all fields and the trailing blank line.
//

static bool				// O - `true` on success, `false` on error
http_send(http_t       *http,		// I - HTTP connection
          http_state_t request,		// I - Request code
	  const char   *uri)		// I - URI
{
  int		i;			// Looping var
  char		buf[1024];		// Encoded URI buffer
  const char	*value;			// Field value
  static const char * const codes[] =	// Request code strings
		{
		  NULL,
		  "OPTIONS",
		  "GET",
		  NULL,
		  "HEAD",
		  "POST",
		  NULL,
		  NULL,
		  "PUT",
		  NULL,
		  "DELETE",
		  "TRACE",
		  "CLOSE",
		  NULL,
		  NULL
		};


  DEBUG_printf("4http_send(http=%p, request=HTTP_%s, uri=\"%s\")", (void *)http, codes[request], uri);

  if (http == NULL || uri == NULL)
    return (false);

  // Set the User-Agent field if it isn't already...
  if (!http->fields[HTTP_FIELD_USER_AGENT])
  {
    if (http->default_fields[HTTP_FIELD_USER_AGENT])
      httpSetField(http, HTTP_FIELD_USER_AGENT, http->default_fields[HTTP_FIELD_USER_AGENT]);
    else
      httpSetField(http, HTTP_FIELD_USER_AGENT, cupsGetUserAgent());
  }

  // Set the Accept-Encoding field if it isn't already...
  if (!http->fields[HTTP_FIELD_ACCEPT_ENCODING] && http->default_fields[HTTP_FIELD_ACCEPT_ENCODING])
    httpSetField(http, HTTP_FIELD_ACCEPT_ENCODING, http->default_fields[HTTP_FIELD_ACCEPT_ENCODING]);

  // Set the Authorization field if it isn't already...
  if (!http->fields[HTTP_FIELD_AUTHORIZATION] && http->authstring)
    httpSetField(http, HTTP_FIELD_AUTHORIZATION, http->authstring);

  // Encode the URI as needed...
  _httpEncodeURI(buf, uri, sizeof(buf));

  // See if we had an error the last time around; if so, reconnect...
  if (http->fd < 0 || http->status == HTTP_STATUS_ERROR || http->status >= HTTP_STATUS_BAD_REQUEST)
  {
    DEBUG_printf("5http_send: Reconnecting, fd=%d, status=%d, tls_upgrade=%d", http->fd, http->status, http->tls_upgrade);

    if (!httpConnectAgain(http, 30000, NULL))
      return (false);
  }

  // Flush any written data that is pending...
  if (http->wused)
  {
    if (httpFlushWrite(http) < 0)
    {
      if (!httpConnectAgain(http, 30000, NULL))
        return (false);
    }
  }

  // Send the request header...
  http->state         = request;
  http->data_encoding = HTTP_ENCODING_FIELDS;

  if (request == HTTP_STATE_POST || request == HTTP_STATE_PUT)
    http->state ++;

  http->status = HTTP_STATUS_CONTINUE;

  if (http->encryption == HTTP_ENCRYPTION_REQUIRED && !http->tls)
  {
    httpSetField(http, HTTP_FIELD_CONNECTION, "Upgrade");
    httpSetField(http, HTTP_FIELD_UPGRADE, "TLS/1.2,TLS/1.1,TLS/1.0");
  }

  if (httpPrintf(http, "%s %s HTTP/1.1\r\n", codes[request], buf) < 1)
  {
    http->status = HTTP_STATUS_ERROR;
    return (false);
  }

  for (i = 0; i < HTTP_FIELD_MAX; i ++)
  {
    if ((value = httpGetField(http, i)) != NULL && *value)
    {
      DEBUG_printf("5http_send: %s: %s", http_fields[i], value);

      if (i == HTTP_FIELD_HOST)
      {
	if (httpPrintf(http, "Host: %s:%d\r\n", value, httpAddrGetPort(http->hostaddr)) < 1)
	{
	  http->status = HTTP_STATUS_ERROR;
	  return (false);
	}
      }
      else if (httpPrintf(http, "%s: %s\r\n", http_fields[i], value) < 1)
      {
	http->status = HTTP_STATUS_ERROR;
	return (false);
      }
    }
  }

  if (http->cookie)
  {
    if (httpPrintf(http, "Cookie: $Version=0; %s\r\n", http->cookie) < 1)
    {
      http->status = HTTP_STATUS_ERROR;
      return (false);
    }
  }

  DEBUG_printf("5http_send: expect=%d, mode=%d, state=%d", http->expect, http->mode, http->state);

  if (http->expect == HTTP_STATUS_CONTINUE && http->mode == _HTTP_MODE_CLIENT && (http->state == HTTP_STATE_POST_RECV || http->state == HTTP_STATE_PUT_RECV))
  {
    if (httpPrintf(http, "Expect: 100-continue\r\n") < 1)
    {
      http->status = HTTP_STATUS_ERROR;
      return (false);
    }
  }

  if (httpPrintf(http, "\r\n") < 1)
  {
    http->status = HTTP_STATUS_ERROR;
    return (false);
  }

  if (httpFlushWrite(http) < 0)
    return (false);

  http_set_length(http);
  httpClearFields(http);

  // The Kerberos and AuthRef authentication strings can only be used once...
  if (http->fields[HTTP_FIELD_AUTHORIZATION] && http->authstring && (!strncmp(http->authstring, "Negotiate", 9) || !strncmp(http->authstring, "AuthRef", 7)))
  {
    http->_authstring[0] = '\0';

    if (http->authstring != http->_authstring)
      free(http->authstring);

    http->authstring = http->_authstring;
  }

  return (true);
}


//
// 'http_set_length()' - Set the data_encoding and data_remaining values.
//

static off_t				// O - Remainder or -1 on error
http_set_length(http_t *http)		// I - Connection
{
  off_t	remaining;			// Remainder


  DEBUG_printf("4http_set_length(http=%p) mode=%d state=%s", (void *)http, http->mode, httpStateString(http->state));

  if ((remaining = httpGetLength2(http)) >= 0)
  {
    if (http->mode == _HTTP_MODE_SERVER && http->state != HTTP_STATE_GET_SEND && http->state != HTTP_STATE_PUT && http->state != HTTP_STATE_POST && http->state != HTTP_STATE_POST_SEND)
    {
      DEBUG_puts("5http_set_length: Not setting data_encoding/remaining.");
      return (remaining);
    }

    if (!_cups_strcasecmp(httpGetField(http, HTTP_FIELD_TRANSFER_ENCODING), "chunked"))
    {
      DEBUG_puts("5http_set_length: Setting data_encoding to HTTP_ENCODING_CHUNKED.");
      http->data_encoding = HTTP_ENCODING_CHUNKED;
    }
    else
    {
      DEBUG_puts("5http_set_length: Setting data_encoding to HTTP_ENCODING_LENGTH.");
      http->data_encoding = HTTP_ENCODING_LENGTH;
    }

    DEBUG_printf("5http_set_length: Setting data_remaining to " CUPS_LLFMT ".", CUPS_LLCAST remaining);
    http->data_remaining = remaining;

    if (remaining <= INT_MAX)
      http->_data_remaining = (int)remaining;
    else
      http->_data_remaining = INT_MAX;
  }

  return (remaining);
}


//
// 'http_set_timeout()' - Set the socket timeout values.
//

static void
http_set_timeout(int    fd,		// I - File descriptor
                 double timeout)	// I - Timeout in seconds
{
#ifdef _WIN32
  DWORD tv = (DWORD)(timeout * 1000);
				      // Timeout in milliseconds

  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, CUPS_SOCAST &tv, sizeof(tv));
  setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, CUPS_SOCAST &tv, sizeof(tv));

#else
  struct timeval tv;			// Timeout in secs and usecs

  tv.tv_sec  = (int)timeout;
  tv.tv_usec = (int)(1000000 * fmod(timeout, 1.0));

  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, CUPS_SOCAST &tv, sizeof(tv));
  setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, CUPS_SOCAST &tv, sizeof(tv));
#endif // _WIN32
}


//
// 'http_set_wait()' - Set the default wait value for reads.
//

static void
http_set_wait(http_t *http)		// I - HTTP connection
{
  if (http->blocking)
  {
    http->wait_value = (int)(http->timeout_value * 1000);

    if (http->wait_value <= 0)
      http->wait_value = 60000;
  }
  else
  {
    http->wait_value = 10000;
  }
}


//
// 'http_tls_upgrade()' - Force upgrade to TLS encryption.
//

static bool				// O - `true` on success, `false` on error
http_tls_upgrade(http_t *http)		// I - HTTP connection
{
  int		ret;			// Return value
  http_t	myhttp;			// Local copy of HTTP data


  DEBUG_printf("4http_tls_upgrade(%p)", (void *)http);

  // Flush the connection to make sure any previous "Upgrade" message
  // has been read.
  httpFlush(http);

  // Copy the HTTP data to a local variable so we can do the OPTIONS
  // request without interfering with the existing request data...
  memcpy(&myhttp, http, sizeof(myhttp));

  // Send an OPTIONS request to the server, requiring TLS
  // encryption on the link...
  http->tls_upgrade = 1;
  memset(http->fields, 0, sizeof(http->fields));
  http->expect = (http_status_t)0;

  if (http->hostname[0] == '/')
    httpSetField(http, HTTP_FIELD_HOST, "localhost");
  else
    httpSetField(http, HTTP_FIELD_HOST, http->hostname);

  httpSetField(http, HTTP_FIELD_CONNECTION, "upgrade");
  httpSetField(http, HTTP_FIELD_UPGRADE, "TLS/1.3,TLS/1.2,TLS/1.1,TLS/1.0");

  if ((ret = httpOptions(http, "*")) == 0)
  {
    // Wait for the secure connection...
    while (httpUpdate(http) == HTTP_STATUS_CONTINUE);
  }

  // Restore the HTTP request data...
  httpClearFields(http);
  memcpy(http->_fields, myhttp._fields, sizeof(http->_fields));
  memcpy(http->fields, myhttp.fields, sizeof(http->fields));

  http->data_encoding   = myhttp.data_encoding;
  http->data_remaining  = myhttp.data_remaining;
  http->_data_remaining = myhttp._data_remaining;
  http->expect          = myhttp.expect;
  http->digest_tries    = myhttp.digest_tries;
  http->tls_upgrade     = 0;

  // See if we actually went secure...
  if (!http->tls)
  {
    // Server does not support HTTP upgrade...
    DEBUG_puts("5http_tls_upgrade: Server does not support HTTP upgrade!");

    _cupsSetError(IPP_STATUS_ERROR_CUPS_PKI, _("Encryption is not supported."), 1);
    httpAddrClose(NULL, http->fd);

    http->fd = -1;

    return (false);
  }
  else
  {
    return (ret == 0);
  }
}


//
// 'http_write()' - Write a buffer to a HTTP connection.
//

static ssize_t				// O - Number of bytes written
http_write(http_t     *http,		// I - HTTP connection
           const char *buffer,		// I - Buffer for data
	   size_t     length)		// I - Number of bytes to write
{
  ssize_t	tbytes,			// Total bytes sent
		bytes;			// Bytes sent


  DEBUG_printf("7http_write(http=%p, buffer=%p, length=" CUPS_LLFMT ")", (void *)http, (void *)buffer, CUPS_LLCAST length);
  http->error = 0;
  tbytes      = 0;

  while (length > 0)
  {
    DEBUG_printf("8http_write: About to write %d bytes.", (int)length);

    if (http->timeout_value > 0.0)
    {
      struct pollfd	pfd;		// Polled file descriptor
      int		nfds;		// Result from select()/poll()

      do
      {
	pfd.fd     = http->fd;
	pfd.events = POLLOUT;

        do
        {
          nfds = poll(&pfd, 1, http->wait_value);
        }
#  ifdef _WIN32
	while (nfds < 0 && (WSAGetLastError() == WSAEINTR || WSAGetLastError() == WSAEWOULDBLOCK));
#  else
	while (nfds < 0 && (errno == EINTR || errno == EAGAIN));
#  endif // _WIN32

        if (nfds < 0)
	{
	  http->error = errno;
	  return (-1);
	}
	else if (nfds == 0 && (!http->timeout_cb || !(*http->timeout_cb)(http, http->timeout_data)))
	{
#ifdef _WIN32
	  http->error = WSAEWOULDBLOCK;
#else
	  http->error = EWOULDBLOCK;
#endif // _WIN32
	  return (-1);
	}
      }
      while (nfds <= 0);
    }

    if (http->tls)
      bytes = _httpTLSWrite(http, buffer, (int)length);
    else
      bytes = send(http->fd, buffer, length, 0);

    DEBUG_printf("8http_write: Write of " CUPS_LLFMT " bytes returned " CUPS_LLFMT ".", CUPS_LLCAST length, CUPS_LLCAST bytes);

    if (bytes < 0)
    {
#ifdef _WIN32
      if (WSAGetLastError() == WSAEINTR)
      {
        continue;
      }
      else if (WSAGetLastError() == WSAEWOULDBLOCK)
      {
        if (http->timeout_cb && (*http->timeout_cb)(http, http->timeout_data))
          continue;

        http->error = WSAGetLastError();
      }
      else if (WSAGetLastError() != http->error)
      {
        http->error = WSAGetLastError();
	continue;
      }

#else
      if (errno == EINTR)
      {
        continue;
      }
      else if (errno == EWOULDBLOCK || errno == EAGAIN)
      {
	if (http->timeout_cb && (*http->timeout_cb)(http, http->timeout_data))
          continue;
        else if (!http->timeout_cb && errno == EAGAIN)
	  continue;

        http->error = errno;
      }
      else if (errno != http->error)
      {
        http->error = errno;
	continue;
      }
#endif // _WIN32

      DEBUG_printf("8http_write: error writing data (%s).", strerror(http->error));

      return (-1);
    }

    buffer += bytes;
    tbytes += bytes;
    length -= (size_t)bytes;
  }

#ifdef DEBUG
  http_debug_hex("http_write", buffer - tbytes, (int)tbytes);
#endif // DEBUG

  DEBUG_printf("8http_write: Returning " CUPS_LLFMT ".", CUPS_LLCAST tbytes);

  return (tbytes);
}


//
// 'http_write_chunk()' - Write a chunked buffer.
//

static ssize_t				// O - Number bytes written
http_write_chunk(http_t     *http,	// I - HTTP connection
                 const char *buffer,	// I - Buffer to write
		 size_t        length)	// I - Length of buffer
{
  char		header[16];		// Chunk header
  ssize_t	bytes;			// Bytes written


  DEBUG_printf("7http_write_chunk(http=%p, buffer=%p, length=" CUPS_LLFMT ")", (void *)http, (void *)buffer, CUPS_LLCAST length);

  // Write the chunk header, data, and trailer.
  snprintf(header, sizeof(header), "%x\r\n", (unsigned)length);
  if (http_write(http, header, strlen(header)) < 0)
  {
    DEBUG_puts("8http_write_chunk: http_write of length failed.");
    return (-1);
  }

  if ((bytes = http_write(http, buffer, length)) < 0)
  {
    DEBUG_puts("8http_write_chunk: http_write of buffer failed.");
    return (-1);
  }

  if (http_write(http, "\r\n", 2) < 0)
  {
    DEBUG_puts("8http_write_chunk: http_write of CR LF failed.");
    return (-1);
  }

  return (bytes);
}
