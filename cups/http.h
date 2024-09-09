//
// Hyper-Text Transport Protocol definitions for CUPS.
//
// Copyright © 2020-2024 by OpenPrinting.
// Copyright © 2007-2018 by Apple Inc.
// Copyright © 1997-2007 by Easy Software Products, all rights reserved.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

#ifndef _CUPS_HTTP_H_
#  define _CUPS_HTTP_H_
#  include "base.h"
#  include "array.h"
#  include <string.h>
#  include <time.h>
#  include <sys/types.h>
#  ifdef _WIN32
#    ifndef __CUPS_SSIZE_T_DEFINED
#      define __CUPS_SSIZE_T_DEFINED
// Windows does not support the ssize_t type, so map it to __int64...
typedef __int64 ssize_t;			// @private@
#    endif // !__CUPS_SSIZE_T_DEFINED
#    include <winsock2.h>
#    include <ws2tcpip.h>
#  else
#    include <unistd.h>
#    include <sys/time.h>
#    include <sys/socket.h>
#    include <netdb.h>
#    include <netinet/in.h>
#    include <arpa/inet.h>
#    include <netinet/in_systm.h>
#    include <netinet/ip.h>
#    if !defined(__APPLE__) || !defined(TCP_NODELAY)
#      include <netinet/tcp.h>
#    endif // !__APPLE__ || !TCP_NODELAY
#    if defined(AF_UNIX) && !defined(AF_LOCAL)
#      define AF_LOCAL AF_UNIX		// Older UNIX's have old names...
#    endif // AF_UNIX && !AF_LOCAL
#    ifdef AF_LOCAL
#      include <sys/un.h>
#    endif // AF_LOCAL
#    if defined(LOCAL_PEERCRED) && !defined(SO_PEERCRED)
#      define SO_PEERCRED LOCAL_PEERCRED
#    endif // LOCAL_PEERCRED && !SO_PEERCRED
#  endif // _WIN32
#  ifdef __cplusplus
extern "C" {
#  endif // __cplusplus


//
// Oh, the wonderful world of IPv6 compatibility.  Apparently some
// implementations expose the (more logical) 32-bit address parts
// to everyone, while others only expose it to kernel code...  To
// make supporting IPv6 even easier, each vendor chose different
// core structure and union names, so the same defines or code
// can't be used on all platforms.
//
// The following will likely need tweaking on new platforms that
// support IPv6 - the "s6_addr32" define maps to the 32-bit integer
// array in the in6_addr union, which is named differently on various
// platforms.
//

#if defined(AF_INET6) && !defined(s6_addr32)
#  if defined(__sun)
#    define s6_addr32	_S6_un._S6_u32
#  elif defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) || defined(__APPLE__)|| defined(__DragonFly__)
#    define s6_addr32	__u6_addr.__u6_addr32
#  elif defined(_WIN32)
//
// Windows only defines byte and 16-bit word members of the union and
// requires special casing of all raw address code...
//
#    define s6_addr32	error_need_win32_specific_code
#  endif // __sun
#endif // AF_INET6 && !s6_addr32


//
// Limits...
//

#  define HTTP_MAX_URI		1024	// Max length of URI string
#  define HTTP_MAX_HOST		256	// Max length of hostname string
#  define HTTP_MAX_BUFFER	2048	// Max length of data buffer
#  define HTTP_MAX_VALUE	256	// Max header field value length


//
// Types and structures...
//

typedef enum http_encoding_e		// HTTP transfer encoding values
{
  HTTP_ENCODING_LENGTH,			// Data is sent with Content-Length
  HTTP_ENCODING_CHUNKED,		// Data is chunked
  HTTP_ENCODING_FIELDS			// Sending HTTP fields
} http_encoding_t;

typedef enum http_encryption_e		// HTTP encryption values
{
  HTTP_ENCRYPTION_IF_REQUESTED,		// Encrypt if requested (TLS upgrade)
  HTTP_ENCRYPTION_NEVER,		// Never encrypt
  HTTP_ENCRYPTION_REQUIRED,		// Encryption is required (TLS upgrade)
  HTTP_ENCRYPTION_ALWAYS		// Always encrypt (HTTPS)
} http_encryption_t;

typedef enum http_field_e		// HTTP field names
{
  HTTP_FIELD_UNKNOWN = -1,		// Unknown field
  HTTP_FIELD_ACCEPT_LANGUAGE,		// Accept-Language field
  HTTP_FIELD_ACCEPT_RANGES,		// Accept-Ranges field
  HTTP_FIELD_AUTHORIZATION,		// Authorization field
  HTTP_FIELD_CONNECTION,		// Connection field
  HTTP_FIELD_CONTENT_ENCODING,		// Content-Encoding field
  HTTP_FIELD_CONTENT_LANGUAGE,		// Content-Language field
  HTTP_FIELD_CONTENT_LENGTH,		// Content-Length field
  HTTP_FIELD_CONTENT_LOCATION,		// Content-Location field
  HTTP_FIELD_CONTENT_MD5,		// Content-MD5 field
  HTTP_FIELD_CONTENT_RANGE,		// Content-Range field
  HTTP_FIELD_CONTENT_TYPE,		// Content-Type field
  HTTP_FIELD_CONTENT_VERSION,		// Content-Version field
  HTTP_FIELD_DATE,			// Date field
  HTTP_FIELD_HOST,			// Host field
  HTTP_FIELD_IF_MODIFIED_SINCE,		// If-Modified-Since field
  HTTP_FIELD_IF_UNMODIFIED_SINCE,	// If-Unmodified-Since field
  HTTP_FIELD_KEEP_ALIVE,		// Keep-Alive field
  HTTP_FIELD_LAST_MODIFIED,		// Last-Modified field
  HTTP_FIELD_LINK,			// Link field
  HTTP_FIELD_LOCATION,			// Location field
  HTTP_FIELD_RANGE,			// Range field
  HTTP_FIELD_REFERER,			// Referer field
  HTTP_FIELD_RETRY_AFTER,		// Retry-After field
  HTTP_FIELD_TRANSFER_ENCODING,		// Transfer-Encoding field
  HTTP_FIELD_UPGRADE,			// Upgrade field
  HTTP_FIELD_USER_AGENT,		// User-Agent field
  HTTP_FIELD_WWW_AUTHENTICATE,		// WWW-Authenticate field
  HTTP_FIELD_ACCEPT_ENCODING,		// Accepting-Encoding field @since CUPS 1.7@
  HTTP_FIELD_ALLOW,			// Allow field @since CUPS 1.7@
  HTTP_FIELD_SERVER,			// Server field @since CUPS 1.7@
  HTTP_FIELD_AUTHENTICATION_INFO,	// Authentication-Info field @since CUPS 2.2.9@
  HTTP_FIELD_ACCESS_CONTROL_ALLOW_CREDENTIALS,
					// CORS/Fetch Access-Control-Allow-Credentials field @since CUPS 2.4@
  HTTP_FIELD_ACCESS_CONTROL_ALLOW_HEADERS,
					// CORS/Fetch Access-Control-Allow-Headers field @since CUPS 2.4@
  HTTP_FIELD_ACCESS_CONTROL_ALLOW_METHODS,
					// CORS/Fetch Access-Control-Allow-Methods field @since CUPS 2.4@
  HTTP_FIELD_ACCESS_CONTROL_ALLOW_ORIGIN,
					// CORS/Fetch Access-Control-Allow-Origin field @since CUPS 2.4@
  HTTP_FIELD_ACCESS_CONTROL_EXPOSE_HEADERS,
					// CORS/Fetch Access-Control-Expose-Headers field @since CUPS 2.4@
  HTTP_FIELD_ACCESS_CONTROL_MAX_AGE,	// CORS/Fetch Access-Control-Max-Age field @since CUPS 2.4@
  HTTP_FIELD_ACCESS_CONTROL_REQUEST_HEADERS,
					// CORS/Fetch Access-Control-Request-Headers field @since CUPS 2.4@
  HTTP_FIELD_ACCESS_CONTROL_REQUEST_METHOD,
					// CORS/Fetch Access-Control-Request-Method field @since CUPS 2.4@
  HTTP_FIELD_OPTIONAL_WWW_AUTHENTICATE,	// RFC 8053 Optional-WWW-Authenticate field @since CUPS 2.4@
  HTTP_FIELD_ORIGIN,			// RFC 6454 Origin field @since CUPS 2.4@
  HTTP_FIELD_OSCORE,			// RFC 8613 OSCORE field @since CUPS 2.4@
  HTTP_FIELD_STRICT_TRANSPORT_SECURITY,	// HSTS Strict-Transport-Security field @since CUPS 2.4@
  HTTP_FIELD_ACCEPT,			// Accept field @since CUPS 2.5@
  HTTP_FIELD_MAX			// Maximum field index
} http_field_t;

typedef enum http_keepalive_e		// HTTP keep-alive values
{
  HTTP_KEEPALIVE_OFF = 0,		// No keep alive support
  HTTP_KEEPALIVE_ON			// Use keep alive
} http_keepalive_t;

enum http_resolve_e			// @link httpResolveURI@ options bit values
{
  HTTP_RESOLVE_DEFAULT = 0,		// Resolve with default options
  HTTP_RESOLVE_FQDN = 1,		// Resolve to a FQDN
  HTTP_RESOLVE_FAXOUT = 2		// Resolve FaxOut service instead of Print
};
typedef unsigned http_resolve_t;	// @link httpResolveURI@ options bitfield

typedef enum http_state_e		// HTTP state values; states are server-oriented...
{
  HTTP_STATE_ERROR = -1,		// Error on socket
  HTTP_STATE_WAITING,			// Waiting for command
  HTTP_STATE_OPTIONS,			// OPTIONS command, waiting for blank line
  HTTP_STATE_GET,			// GET command, waiting for blank line
  HTTP_STATE_GET_SEND,			// GET command, sending data
  HTTP_STATE_HEAD,			// HEAD command, waiting for blank line
  HTTP_STATE_POST,			// POST command, waiting for blank line
  HTTP_STATE_POST_RECV,			// POST command, receiving data
  HTTP_STATE_POST_SEND,			// POST command, sending data
  HTTP_STATE_PUT,			// PUT command, waiting for blank line
  HTTP_STATE_PUT_RECV,			// PUT command, receiving data
  HTTP_STATE_DELETE,			// DELETE command, waiting for blank line
  HTTP_STATE_TRACE,			// TRACE command, waiting for blank line
  HTTP_STATE_CONNECT,			// CONNECT command, waiting for blank line
  HTTP_STATE_STATUS,			// Command complete, sending status
  HTTP_STATE_UNKNOWN_METHOD,		// Unknown request method, waiting for blank line @since CUPS 1.7@
  HTTP_STATE_UNKNOWN_VERSION		// Unknown request method, waiting for blank line @since CUPS 1.7@
} http_state_t;

typedef enum http_status_e		// HTTP status codes
{
  HTTP_STATUS_ERROR = -1,		// An error response from httpXxxx()
  HTTP_STATUS_NONE = 0,			// No Expect value @since CUPS 1.7@

  HTTP_STATUS_CONTINUE = 100,		// Everything OK, keep going...
  HTTP_STATUS_SWITCHING_PROTOCOLS,	// HTTP upgrade to TLS/SSL

  HTTP_STATUS_OK = 200,			// OPTIONS/GET/HEAD/POST/TRACE command was successful
  HTTP_STATUS_CREATED,			// PUT command was successful
  HTTP_STATUS_ACCEPTED,			// DELETE command was successful
  HTTP_STATUS_NOT_AUTHORITATIVE,	// Information isn't authoritative
  HTTP_STATUS_NO_CONTENT,		// Successful command, no new data
  HTTP_STATUS_RESET_CONTENT,		// Content was reset/recreated
  HTTP_STATUS_PARTIAL_CONTENT,		// Only a partial file was received/sent
  HTTP_STATUS_MULTI_STATUS,		// Multiple status codes (WebDAV)
  HTTP_STATUS_ALREADY_REPORTED,		// Already reported (WebDAV)

  HTTP_STATUS_MULTIPLE_CHOICES = 300,	// Multiple files match request
  HTTP_STATUS_MOVED_PERMANENTLY,	// Document has moved permanently
  HTTP_STATUS_FOUND,			// Document was found at a different URI
  HTTP_STATUS_SEE_OTHER,		// See this other link
  HTTP_STATUS_NOT_MODIFIED,		// File not modified
  HTTP_STATUS_USE_PROXY,		// Must use a proxy to access this URI
  HTTP_STATUS_TEMPORARY_REDIRECT = 307,	// Temporary redirection
  HTTP_STATUS_PERMANENT_REDIRECT,	// Permanent redirection

  HTTP_STATUS_BAD_REQUEST = 400,	// Bad request
  HTTP_STATUS_UNAUTHORIZED,		// Unauthorized to access host
  HTTP_STATUS_PAYMENT_REQUIRED,		// Payment required
  HTTP_STATUS_FORBIDDEN,		// Forbidden to access this URI
  HTTP_STATUS_NOT_FOUND,		// URI was not found
  HTTP_STATUS_METHOD_NOT_ALLOWED,	// Method is not allowed
  HTTP_STATUS_NOT_ACCEPTABLE,		// Not Acceptable
  HTTP_STATUS_PROXY_AUTHENTICATION,	// Proxy Authentication is Required
  HTTP_STATUS_REQUEST_TIMEOUT,		// Request timed out
  HTTP_STATUS_CONFLICT,			// Request is self-conflicting
  HTTP_STATUS_GONE,			// Server has gone away
  HTTP_STATUS_LENGTH_REQUIRED,		// A content length or encoding is required
  HTTP_STATUS_PRECONDITION,		// Precondition failed
  HTTP_STATUS_CONTENT_TOO_LARGE,	// Content too large
  HTTP_STATUS_URI_TOO_LONG,		// URI too long
  HTTP_STATUS_UNSUPPORTED_MEDIA_TYPE,	// The requested media type is unsupported
  HTTP_STATUS_RANGE_NOT_SATISFIABLE,	// The requested range is not satisfiable
  HTTP_STATUS_EXPECTATION_FAILED,	// The expectation given in an Expect header field was not met
  HTTP_STATUS_MISDIRECTED_REQUEST = 421,// Misdirected request
  HTTP_STATUS_UNPROCESSABLE_CONTENT,	// Unprocessable content
  HTTP_STATUS_LOCKED,			// Locked (WebDAV)
  HTTP_STATUS_FAILED_DEPENDENCY,	// Failed dependency (WebDAV)
  HTTP_STATUS_TOO_EARLY,		// Too early (WebDAV)
  HTTP_STATUS_UPGRADE_REQUIRED,		// Upgrade to SSL/TLS required
  HTTP_STATUS_PRECONDITION_REQUIRED = 428,
					// Precondition required (WebDAV)
  HTTP_STATUS_TOO_MANY_REQUESTS,	// Too many requests (WebDAV)
  HTTP_STATUS_REQUEST_HEADER_FIELDS_TOO_LARGE = 431,
					// Request Header Fields Too Large (WebDAV)
  HTTP_STATUS_UNAVAILABLE_FOR_LEGAL_REASONS = 451,
					// Unavailable For Legal Reasons (RFC 7725)

  HTTP_STATUS_SERVER_ERROR = 500,	// Internal server error
  HTTP_STATUS_NOT_IMPLEMENTED,		// Feature not implemented
  HTTP_STATUS_BAD_GATEWAY,		// Bad gateway
  HTTP_STATUS_SERVICE_UNAVAILABLE,	// Service is unavailable
  HTTP_STATUS_GATEWAY_TIMEOUT,		// Gateway connection timed out
  HTTP_STATUS_NOT_SUPPORTED,		// HTTP version not supported
  HTTP_STATUS_INSUFFICIENT_STORAGE = 507,
					// Insufficient storage (WebDAV)
  HTTP_STATUS_LOOP_DETECTED,		// Loop detected (WebDAV)
  HTTP_STATUS_NETWORK_AUTHENTICATION_REQUIRED = 511,
					// Network Authentication Required (WebDAV)

  HTTP_STATUS_CUPS_AUTHORIZATION_CANCELED = 1000,
					// User canceled authorization @since CUPS 1.4@
  HTTP_STATUS_CUPS_PKI_ERROR,		// Error negotiating a secure connection @since CUPS 1.5@
  HTTP_STATUS_CUPS_WEBIF_DISABLED	// Web interface is disabled @private@

// Renamed status codes from latest RFCs...
#  define HTTP_STATUS_MOVED_TEMPORARILY HTTP_STATUS_FOUND
#  define HTTP_STATUS_REQUEST_TOO_LARGE HTTP_STATUS_CONTENT_TOO_LARGE
#  define HTTP_STATUS_UNSUPPORTED_MEDIATYPE HTTP_STATUS_UNSUPPORTED_MEDIA_TYPE
#  define HTTP_STATUS_REQUESTED_RANGE HTTP_STATUS_RANGE_NOT_SATISFIABLE
} http_status_t;

typedef enum http_trust_e		// Level of trust for credentials @since CUPS 2.0/OS 10.10@
{
  HTTP_TRUST_OK = 0,			// Credentials are OK/trusted
  HTTP_TRUST_INVALID,			// Credentials are invalid
  HTTP_TRUST_CHANGED,			// Credentials have changed
  HTTP_TRUST_EXPIRED,			// Credentials are expired
  HTTP_TRUST_RENEWED,			// Credentials have been renewed
  HTTP_TRUST_UNKNOWN			// Credentials are unknown/new
} http_trust_t;

typedef enum http_uri_status_e		// URI separation status @since CUPS 1.2@
{
  HTTP_URI_STATUS_OVERFLOW = -8,	// URI buffer for httpAssembleURI is too small
  HTTP_URI_STATUS_BAD_ARGUMENTS = -7,	// Bad arguments to function (error)
  HTTP_URI_STATUS_BAD_RESOURCE = -6,	// Bad resource in URI (error)
  HTTP_URI_STATUS_BAD_PORT = -5,	// Bad port number in URI (error)
  HTTP_URI_STATUS_BAD_HOSTNAME = -4,	// Bad hostname in URI (error)
  HTTP_URI_STATUS_BAD_USERNAME = -3,	// Bad username in URI (error)
  HTTP_URI_STATUS_BAD_SCHEME = -2,	// Bad scheme in URI (error)
  HTTP_URI_STATUS_BAD_URI = -1,		// Bad/empty URI (error)
  HTTP_URI_STATUS_OK = 0,		// URI decoded OK
  HTTP_URI_STATUS_MISSING_SCHEME,	// Missing scheme in URI (warning)
  HTTP_URI_STATUS_UNKNOWN_SCHEME,	// Unknown scheme in URI (warning)
  HTTP_URI_STATUS_MISSING_RESOURCE	// Missing resource in URI (warning)
} http_uri_status_t;

typedef enum http_uri_coding_e		// URI en/decode flags
{
  HTTP_URI_CODING_NONE = 0,		// Don't en/decode anything
  HTTP_URI_CODING_USERNAME = 1,		// En/decode the username portion
  HTTP_URI_CODING_HOSTNAME = 2,		// En/decode the hostname portion
  HTTP_URI_CODING_RESOURCE = 4,		// En/decode the resource portion
  HTTP_URI_CODING_MOST = 7,		// En/decode all but the query
  HTTP_URI_CODING_QUERY = 8,		// En/decode the query portion
  HTTP_URI_CODING_ALL = 15,		// En/decode everything
  HTTP_URI_CODING_RFC6874 = 16		// Use RFC 6874 address format
} http_uri_coding_t;

typedef enum http_version_e		// HTTP version numbers @exclude all@
{
  HTTP_VERSION_0_9 = 9,			// HTTP/0.9
  HTTP_VERSION_1_0 = 100,		// HTTP/1.0
  HTTP_VERSION_1_1 = 101		// HTTP/1.1
} http_version_t;

typedef union _http_addr_u		// Socket address union, which makes using IPv6 and other address types easier and more portable. @since CUPS 1.2@
{
  struct sockaddr	addr;		// Base structure for family value
  struct sockaddr_in	ipv4;		// IPv4 address
#ifdef AF_INET6
  struct sockaddr_in6	ipv6;		// IPv6 address
#endif // AF_INET6
#ifdef AF_LOCAL
  struct sockaddr_un	un;		// Domain socket file
#endif // AF_LOCAL
  char			pad[256];	// Padding to ensure binary compatibility @private@
} http_addr_t;

typedef struct http_addrlist_s		// Socket address list, which is used to enumerate all of the addresses that are associated with a hostname. @since CUPS 1.2@ @exclude all@
{
  struct http_addrlist_s *next;		// Pointer to next address in list
  http_addr_t		addr;		// Address
} http_addrlist_t;

typedef struct _http_s http_t;		// HTTP connection type

typedef struct http_credential_s	// HTTP credential data @deprecated@ @exclude all@
{
  void		*data;			// Pointer to credential data
  size_t	datalen;		// Credential length
} http_credential_t;

typedef bool (*http_resolve_cb_t)(void *data);
					// @link httpResolveURI@ callback @since CUPS 2.5@

typedef int (*http_timeout_cb_t)(http_t *http, void *user_data);
					// HTTP timeout callback @since CUPS 1.5@


//
// Functions...
//

extern http_t		*httpAcceptConnection(int fd, int blocking) _CUPS_PUBLIC;
extern int		httpAddCredential(cups_array_t *credentials, const void *data, size_t datalen) _CUPS_DEPRECATED;
extern int		httpAddrAny(const http_addr_t *addr) _CUPS_DEPRECATED_MSG("Use httpAddrIsAny instead.");
extern int		httpAddrClose(http_addr_t *addr, int fd) _CUPS_PUBLIC;
extern http_addrlist_t	*httpAddrConnect(http_addrlist_t *addrlist, int *sock) _CUPS_DEPRECATED_MSG("Use httpAddrConnect2 instead.");
extern http_addrlist_t	*httpAddrConnect2(http_addrlist_t *addrlist, int *sock, int msec, int *cancel) _CUPS_PUBLIC;
extern http_addrlist_t	*httpAddrCopyList(http_addrlist_t *src) _CUPS_PUBLIC;
extern int		httpAddrEqual(const http_addr_t *addr1, const http_addr_t *addr2) _CUPS_DEPRECATED_MSG("Use httpAddrIsEqual instead.");
extern int		httpAddrFamily(http_addr_t *addr) _CUPS_DEPRECATED_MSG("Use httpAddrGetFamily instead.");
extern void		httpAddrFreeList(http_addrlist_t *addrlist) _CUPS_PUBLIC;
extern int		httpAddrGetFamily(http_addr_t *addr) _CUPS_PUBLIC;
extern size_t		httpAddrGetLength(const http_addr_t *addr) _CUPS_PUBLIC;
extern http_addrlist_t	*httpAddrGetList(const char *hostname, int family, const char *service) _CUPS_PUBLIC;
extern int		httpAddrGetPort(http_addr_t *addr) _CUPS_PUBLIC;
extern char		*httpAddrGetString(const http_addr_t *addr, char *s, size_t slen) _CUPS_PUBLIC;
extern bool		httpAddrIsAny(const http_addr_t *addr) _CUPS_PUBLIC;
extern bool		httpAddrIsEqual(const http_addr_t *addr1, const http_addr_t *addr2) _CUPS_PUBLIC;
extern bool		httpAddrIsLocalhost(const http_addr_t *addr) _CUPS_PUBLIC;
extern int		httpAddrLength(const http_addr_t *addr) _CUPS_DEPRECATED_MSG("Use httpAddrGetLength instead.");
extern int		httpAddrListen(http_addr_t *addr, int port) _CUPS_PUBLIC;
extern int		httpAddrLocalhost(const http_addr_t *addr) _CUPS_DEPRECATED_MSG("Use httpAddrIsLocalhost instead.");
extern char		*httpAddrLookup(const http_addr_t *addr, char *name, int namelen) _CUPS_PUBLIC;
extern void		httpAddrSetPort(http_addr_t *addr, int port) _CUPS_PUBLIC;
extern char		*httpAddrString(const http_addr_t *addr, char *s, int slen) _CUPS_DEPRECATED_MSG("Use httpAddrGetString instead.");
extern int		httpAddrPort(http_addr_t *addr) _CUPS_DEPRECATED_MSG("Use httpAddrGetPort instead.");
extern http_uri_status_t httpAssembleURI(http_uri_coding_t encoding, char *uri, int urilen, const char *scheme, const char *username, const char *host, int port, const char *resource) _CUPS_PUBLIC;
extern http_uri_status_t httpAssembleURIf(http_uri_coding_t encoding, char *uri, int urilen, const char *scheme, const char *username, const char *host, int port, const char *resourcef, ...) _CUPS_FORMAT(8, 9) _CUPS_PUBLIC;
extern char		*httpAssembleUUID(const char *server, int port, const char *name, int number, char *buffer, size_t bufsize) _CUPS_PUBLIC;

extern void		httpBlocking(http_t *http, int b) _CUPS_DEPRECATED_MSG("Use httpSetBlocking instead.");

extern int		httpCheck(http_t *http) _CUPS_DEPRECATED_MSG("Use httpWait instead.");
extern void		httpClearCookie(http_t *http) _CUPS_PUBLIC;
extern void		httpClearFields(http_t *http) _CUPS_PUBLIC;
extern void		httpClose(http_t *http) _CUPS_PUBLIC;
extern int		httpCompareCredentials(cups_array_t *cred1, cups_array_t *cred2) _CUPS_DEPRECATED;
extern http_t		*httpConnect(const char *host, int port) _CUPS_DEPRECATED_MSG("Use httpConnect2 instead.");
extern http_t		*httpConnect2(const char *host, int port, http_addrlist_t *addrlist, int family, http_encryption_t encryption, int blocking, int msec, int *cancel) _CUPS_PUBLIC;
extern bool		httpConnectAgain(http_t *http, int msec, int *cancel) _CUPS_PUBLIC;
extern http_t		*httpConnectEncrypt(const char *host, int port, http_encryption_t encryption) _CUPS_DEPRECATED_MSG("Use httpConnect2 instead.");
extern http_t		*httpConnectURI(const char *uri, char *host, size_t hsize, int *port, char *resource, size_t rsize, bool blocking, int msec, int *cancel, bool require_ca) _CUPS_PUBLIC;
extern int		httpCopyCredentials(http_t *http, cups_array_t **credentials) _CUPS_DEPRECATED_MSG("Use httpCopyPeerCredentials instead.");
extern char		*httpCopyPeerCredentials(http_t *http) _CUPS_PUBLIC;
extern int		httpCredentialsAreValidForName(cups_array_t *credentials, const char *common_name) _CUPS_DEPRECATED_MSG("Use cupsAreCredentialsValidForName instead.");
extern time_t		httpCredentialsGetExpiration(cups_array_t *credentials) _CUPS_DEPRECATED_MSG("Use cupsGetCredentialsExpiration instead.");
extern http_trust_t	httpCredentialsGetTrust(cups_array_t *credentials, const char *common_name) _CUPS_DEPRECATED_MSG("Use cupsGetCredentialsTrust instead.");
extern size_t		httpCredentialsString(cups_array_t *credentials, char *buffer, size_t bufsize) _CUPS_DEPRECATED_MSG("Use cupsGetCredentialsInfo instead.");

extern char		*httpDecode64(char *out, const char *in) _CUPS_DEPRECATED_MSG("Use httpDecode64_3 instead.");
extern char		*httpDecode64_2(char *out, int *outlen, const char *in) _CUPS_DEPRECATED_MSG("Use httpDecode64_3 instead.");
extern char		*httpDecode64_3(char *out, size_t *outlen, const char *in, const char **end) _CUPS_PUBLIC;
extern int		httpDelete(http_t *http, const char *uri) _CUPS_DEPRECATED_MSG("Use httpWriteRequest instead.");

extern char		*httpEncode64(char *out, const char *in) _CUPS_DEPRECATED_MSG("Use httpEncode64_2 instead.");
extern char		*httpEncode64_2(char *out, int outlen, const char *in, int inlen) _CUPS_PUBLIC;
extern char		*httpEncode64_3(char *out, size_t outlen, const char *in, size_t inlen, bool url) _CUPS_PUBLIC;
extern int		httpEncryption(http_t *http, http_encryption_t e) _CUPS_DEPRECATED_MSG("Use httpSetEncryption instead.");
extern int		httpError(http_t *http) _CUPS_DEPRECATED_MSG("Use httpGetError instead.");

extern http_field_t	httpFieldValue(const char *name) _CUPS_PUBLIC;
extern void		httpFlush(http_t *http) _CUPS_PUBLIC;
extern int		httpFlushWrite(http_t *http) _CUPS_PUBLIC;
extern void		httpFreeCredentials(cups_array_t *certs) _CUPS_DEPRECATED_MSG("Use free instead.");

extern int		httpGet(http_t *http, const char *uri) _CUPS_DEPRECATED_MSG("Use httpWriteRequest instead.");
extern time_t		httpGetActivity(http_t *http) _CUPS_PUBLIC;
extern http_addr_t	*httpGetAddress(http_t *http) _CUPS_PUBLIC;
extern char		*httpGetAuthString(http_t *http) _CUPS_PUBLIC;
extern int		httpGetBlocking(http_t *http) _CUPS_PUBLIC;
extern const char	*httpGetContentEncoding(http_t *http) _CUPS_PUBLIC;
extern const char	*httpGetCookie(http_t *http) _CUPS_PUBLIC;
extern const char	*httpGetDateString(time_t t) _CUPS_PUBLIC;
extern const char	*httpGetDateString2(time_t t, char *s, int slen) _CUPS_PUBLIC;
extern time_t		httpGetDateTime(const char *s) _CUPS_PUBLIC;
extern http_encryption_t httpGetEncryption(http_t *http) _CUPS_PUBLIC;
extern int		httpGetError(http_t *http) _CUPS_PUBLIC;
extern http_status_t	httpGetExpect(http_t *http) _CUPS_PUBLIC;
extern int		httpGetFd(http_t *http) _CUPS_PUBLIC;
extern const char	*httpGetField(http_t *http, http_field_t field) _CUPS_PUBLIC;
extern struct hostent	*httpGetHostByName(const char *name) _CUPS_PUBLIC;
extern const char	*httpGetHostname(http_t *http, char *s, int slen) _CUPS_PUBLIC;
extern http_keepalive_t	httpGetKeepAlive(http_t *http) _CUPS_PUBLIC;
extern int		httpGetLength(http_t *http) _CUPS_DEPRECATED_MSG("Use httpGetLength2 instead.");
extern off_t		httpGetLength2(http_t *http) _CUPS_PUBLIC;
extern size_t		httpGetPending(http_t *http) _CUPS_PUBLIC;
extern size_t		httpGetReady(http_t *http) _CUPS_PUBLIC;
extern size_t		httpGetRemaining(http_t *http) _CUPS_PUBLIC;
extern http_state_t	httpGetState(http_t *http) _CUPS_PUBLIC;
extern http_status_t	httpGetStatus(http_t *http) _CUPS_PUBLIC;
extern char		*httpGetSubField(http_t *http, http_field_t field, const char *name, char *value) _CUPS_DEPRECATED_MSG("Use httpGetSubField2 instead.");
extern char		*httpGetSubField2(http_t *http, http_field_t field, const char *name, char *value, int valuelen) _CUPS_PUBLIC;
extern http_version_t	httpGetVersion(http_t *http) _CUPS_PUBLIC;
extern char		*httpGets(char *line, int length, http_t *http) _CUPS_DEPRECATED_MSG("Use httpGets2 instead.");
extern char		*httpGets2(http_t *http, char *line, size_t length) _CUPS_PUBLIC;

extern int		httpHead(http_t *http, const char *uri) _CUPS_DEPRECATED_MSG("Use httpWriteRequest instead.");

extern void		httpInitialize(void) _CUPS_PUBLIC;
extern int		httpIsChunked(http_t *http) _CUPS_PUBLIC;
extern int		httpIsEncrypted(http_t *http) _CUPS_PUBLIC;

extern int		httpLoadCredentials(const char *path, cups_array_t **credentials, const char *common_name) _CUPS_DEPRECATED_MSG("Use cupsCopyCredentials instead.");

extern char		*httpMD5(const char *, const char *, const char *, char [33]) _CUPS_DEPRECATED_MSG("Use cupsDoAuth or cupsHashData instead.");
extern char		*httpMD5Final(const char *, const char *, const char *, char [33]) _CUPS_DEPRECATED_MSG("Use cupsDoAuth or cupsHashData instead.");
extern char		*httpMD5String(const unsigned char *, char [33]) _CUPS_DEPRECATED_MSG("Use cupsHashString instead.");

extern int		httpOptions(http_t *http, const char *uri) _CUPS_DEPRECATED_MSG("Use httpWriteRequest instead.");

extern ssize_t		httpPeek(http_t *http, char *buffer, size_t length) _CUPS_PUBLIC;
extern int		httpPost(http_t *http, const char *uri) _CUPS_DEPRECATED_MSG("Use httpWriteRequest instead.");
extern int		httpPrintf(http_t *http, const char *format, ...) _CUPS_FORMAT(2, 3) _CUPS_PUBLIC;
extern int		httpPut(http_t *http, const char *uri) _CUPS_DEPRECATED_MSG("Use httpWriteRequest instead.");

extern int		httpRead(http_t *http, char *buffer, int length) _CUPS_DEPRECATED_MSG("Use httpRead2 instead.");
extern ssize_t		httpRead2(http_t *http, char *buffer, size_t length) _CUPS_PUBLIC;
extern http_state_t	httpReadRequest(http_t *http, char *resource, size_t resourcelen) _CUPS_PUBLIC;
extern int		httpReconnect(http_t *http) _CUPS_DEPRECATED_MSG("Use httpReconnect2 instead.");
extern int		httpReconnect2(http_t *http, int msec, int *cancel) _CUPS_PUBLIC;
extern const char	*httpResolveHostname(http_t *http, char *buffer, size_t bufsize) _CUPS_PUBLIC;
extern const char	*httpResolveURI(const char *uri, char *resolved_uri, size_t resolved_size, http_resolve_t options, http_resolve_cb_t cb, void *cb_data) _CUPS_PUBLIC;

extern int		httpSaveCredentials(const char *path, cups_array_t *credentials, const char *common_name) _CUPS_DEPRECATED_MSG("Use cupsSaveCredentials instead.");
extern void		httpSeparate(const char *uri, char *method, char *username, char *host, int *port, char *resource) _CUPS_DEPRECATED_MSG("Use httpSeparateURI instead.");
extern void		httpSeparate2(const char *uri, char *method, int methodlen, char *username, int usernamelen, char *host, int hostlen, int *port, char *resource, int resourcelen) _CUPS_DEPRECATED_MSG("Use httpSeparateURI instead.");
extern http_uri_status_t httpSeparateURI(http_uri_coding_t decoding, const char *uri, char *scheme, int schemelen, char *username, int usernamelen, char *host, int hostlen, int *port, char *resource, int resourcelen) _CUPS_PUBLIC;
extern void		httpSetAuthString(http_t *http, const char *scheme, const char *data) _CUPS_PUBLIC;
extern void		httpSetBlocking(http_t *http, bool b) _CUPS_PUBLIC;
extern void		httpSetCookie(http_t *http, const char *cookie) _CUPS_PUBLIC;
extern int		httpSetCredentials(http_t *http, cups_array_t *certs) _CUPS_DEPRECATED_MSG("Use httpSetCredentialsAndKey instead.");
extern bool		httpSetCredentialsAndKey(http_t *http, const char *credentials, const char *key) _CUPS_PUBLIC;
extern void		httpSetDefaultField(http_t *http, http_field_t field, const char *value) _CUPS_PUBLIC;
extern bool		httpSetEncryption(http_t *http, http_encryption_t e) _CUPS_PUBLIC;
extern void		httpSetExpect(http_t *http, http_status_t expect) _CUPS_PUBLIC;
extern void		httpSetField(http_t *http, http_field_t field, const char *value) _CUPS_PUBLIC;
extern void		httpSetKeepAlive(http_t *http, http_keepalive_t keep_alive) _CUPS_PUBLIC;
extern void		httpSetLength(http_t *http, size_t length) _CUPS_PUBLIC;
extern void		httpSetTimeout(http_t *http, double timeout, http_timeout_cb_t cb, void *user_data) _CUPS_PUBLIC;
extern void		httpShutdown(http_t *http) _CUPS_PUBLIC;
extern const char	*httpStateString(http_state_t state) _CUPS_PUBLIC;
extern const char	*httpStatus(http_status_t status) _CUPS_DEPRECATED_MSG("Use httpStatusString instead.");
extern const char	*httpStatusString(http_status_t status) _CUPS_PUBLIC;

extern int		httpTrace(http_t *http, const char *uri) _CUPS_DEPRECATED_MSG("Use httpWriteRequest instead.");

extern http_status_t	httpUpdate(http_t *http) _CUPS_PUBLIC;
extern const char	*httpURIStatusString(http_uri_status_t status) _CUPS_PUBLIC;

extern int		httpWait(http_t *http, int msec) _CUPS_PUBLIC;
extern int		httpWrite(http_t *http, const char *buffer, int length) _CUPS_DEPRECATED_MSG("Use httpWrite2 instead.");
extern ssize_t		httpWrite2(http_t *http, const char *buffer, size_t length) _CUPS_PUBLIC;
extern bool		httpWriteRequest(http_t *http, const char *method, const char *uri);
extern int		httpWriteResponse(http_t *http, http_status_t status) _CUPS_PUBLIC;


#  ifdef __cplusplus
}
#  endif // __cplusplus
#endif // !_CUPS_HTTP_H_
