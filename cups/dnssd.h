//
// DNS-SD API definitions for CUPS.
//
// Copyright © 2022-2024 by OpenPrinting.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

#ifndef _CUPS_DNSSD_H_
#  define _CUPS_DNSSD_H_
#  include "cups.h"
#  ifdef __cplusplus
extern "C" {
#  endif // __cplusplus


//
// Types and constants...
//

#  define CUPS_DNSSD_IF_INDEX_ANY	0
#  define CUPS_DNSSD_IF_INDEX_LOCAL	((uint32_t)-1)

typedef struct _cups_dnssd_s cups_dnssd_t;
					// DNS-SD context

enum cups_dnssd_flags_e			// DNS-SD callback flag values
{
  CUPS_DNSSD_FLAGS_NONE = 0,		// No flags
  CUPS_DNSSD_FLAGS_ADD = 1,		// Added (removed if not set)
  CUPS_DNSSD_FLAGS_ERROR = 2,		// Error occurred
  CUPS_DNSSD_FLAGS_COLLISION = 4,	// Collision occurred
  CUPS_DNSSD_FLAGS_HOST_CHANGE = 8,	// Host name changed
  CUPS_DNSSD_FLAGS_NETWORK_CHANGE = 16,	// Network connection changed
  CUPS_DNSSD_FLAGS_MORE = 128		// More coming
};
typedef unsigned cups_dnssd_flags_t;	// DNS-SD callback flag bitmask

typedef enum cups_dnssd_rrtype_e	// DNS record type values
{
  CUPS_DNSSD_RRTYPE_A = 1,		// Host address
  CUPS_DNSSD_RRTYPE_NS,			// Name server
  CUPS_DNSSD_RRTYPE_CNAME = 5,		// Canonical name
  CUPS_DNSSD_RRTYPE_WKS = 11,		// Well known service
  CUPS_DNSSD_RRTYPE_PTR,		// Domain name pointer
  CUPS_DNSSD_RRTYPE_TXT = 16,		// One or more text strings
  CUPS_DNSSD_RRTYPE_RT = 21,		// Router
  CUPS_DNSSD_RRTYPE_SIG = 24,		// Security signature
  CUPS_DNSSD_RRTYPE_KEY,		// Security key
  CUPS_DNSSD_RRTYPE_AAAA = 28,		// IPv6 Address.
  CUPS_DNSSD_RRTYPE_LOC,		// Location Information.
  CUPS_DNSSD_RRTYPE_KX = 36,		// Key Exchange
  CUPS_DNSSD_RRTYPE_CERT,		// Certification record
  CUPS_DNSSD_RRTYPE_RRSIG = 46,		// RRSIG
  CUPS_DNSSD_RRTYPE_DNSKEY = 48,	// DNSKEY
  CUPS_DNSSD_RRTYPE_DHCID,		// DHCP Client Identifier
  CUPS_DNSSD_RRTYPE_HTTPS = 65,		// HTTPS Service Binding
  CUPS_DNSSD_RRTYPE_SPF = 99,		// Sender Policy Framework for E-Mail
  CUPS_DNSSD_RRTYPE_ANY = 255		// Wildcard match
} cups_dnssd_rrtype_t;

typedef struct _cups_dnssd_browse_s cups_dnssd_browse_t;
					// DNS browse request
typedef void (*cups_dnssd_browse_cb_t)(cups_dnssd_browse_t *browse, void *cb_data, cups_dnssd_flags_t flags, uint32_t if_index, const char *name, const char *regtype, const char *domain);
					// DNS-SD browse callback

typedef void (*cups_dnssd_error_cb_t)(void *cb_data, const char *message);
					// DNS-SD error callback

typedef struct _cups_dnssd_query_s cups_dnssd_query_t;
					// DNS query request
typedef void (*cups_dnssd_query_cb_t)(cups_dnssd_query_t *query, void *cb_data, cups_dnssd_flags_t flags, uint32_t if_index, const char *fullname, uint16_t rrtype, const void *qdata, uint16_t qlen);
					// DNS-SD query callback

typedef struct _cups_dnssd_resolve_s cups_dnssd_resolve_t;
					// DNS resolve request
typedef void (*cups_dnssd_resolve_cb_t)(cups_dnssd_resolve_t *res, void *cb_data, cups_dnssd_flags_t flags, uint32_t if_index, const char *fullname, const char *host, uint16_t port, int num_txt, cups_option_t *txt);
					// DNS-SD resolve callback

typedef struct _cups_dnssd_service_s cups_dnssd_service_t;
					// DNS service registration
typedef void (*cups_dnssd_service_cb_t)(cups_dnssd_service_t *service, void *cb_data, cups_dnssd_flags_t flags);
					// DNS-SD service registration callback


//
// Functions...
//

extern char		*cupsDNSSDCopyComputerName(cups_dnssd_t *dnssd, char *buffer, size_t bufsize) _CUPS_PUBLIC;
extern char		*cupsDNSSDCopyHostName(cups_dnssd_t *dnssd, char *buffer, size_t bufsize) _CUPS_PUBLIC;
extern void		cupsDNSSDDelete(cups_dnssd_t *dnssd) _CUPS_PUBLIC;
extern size_t		cupsDNSSDGetConfigChanges(cups_dnssd_t *dnssd) _CUPS_PUBLIC;
extern cups_dnssd_t	*cupsDNSSDNew(cups_dnssd_error_cb_t error_cb, void *cb_data) _CUPS_PUBLIC;

extern void		cupsDNSSDBrowseDelete(cups_dnssd_browse_t *browser) _CUPS_PUBLIC;
extern cups_dnssd_t	*cupsDNSSDBrowseGetContext(cups_dnssd_browse_t *browser) _CUPS_PUBLIC;
extern cups_dnssd_browse_t *cupsDNSSDBrowseNew(cups_dnssd_t *dnssd, uint32_t if_index, const char *types, const char *domain, cups_dnssd_browse_cb_t browse_cb, void *cb_data) _CUPS_PUBLIC;

extern void		cupsDNSSDQueryDelete(cups_dnssd_query_t *query) _CUPS_PUBLIC;
extern cups_dnssd_t	*cupsDNSSDQueryGetContext(cups_dnssd_query_t *query) _CUPS_PUBLIC;
extern cups_dnssd_query_t *cupsDNSSDQueryNew(cups_dnssd_t *dnssd, uint32_t if_index, const char *fullname, uint16_t rrtype, cups_dnssd_query_cb_t query_cb, void *cb_data) _CUPS_PUBLIC;

extern void		cupsDNSSDResolveDelete(cups_dnssd_resolve_t *res) _CUPS_PUBLIC;
extern cups_dnssd_t	*cupsDNSSDResolveGetContext(cups_dnssd_resolve_t *res) _CUPS_PUBLIC;
extern cups_dnssd_resolve_t *cupsDNSSDResolveNew(cups_dnssd_t *dnssd, uint32_t if_index, const char *name, const char *type, const char *domain, cups_dnssd_resolve_cb_t resolve_cb, void *cb_data) _CUPS_PUBLIC;

extern bool		cupsDNSSDServiceAdd(cups_dnssd_service_t *service, const char *types, const char *domain, const char *host, uint16_t port, int num_txt, cups_option_t *txt) _CUPS_PUBLIC;
extern void		cupsDNSSDServiceDelete(cups_dnssd_service_t *service) _CUPS_PUBLIC;
extern cups_dnssd_t	*cupsDNSSDServiceGetContext(cups_dnssd_service_t *service) _CUPS_PUBLIC;
extern const char	*cupsDNSSDServiceGetName(cups_dnssd_service_t *service) _CUPS_PUBLIC;
extern cups_dnssd_service_t *cupsDNSSDServiceNew(cups_dnssd_t *dnssd, uint32_t if_index, const char *name, cups_dnssd_service_cb_t cb, void *cb_data) _CUPS_PUBLIC;
extern bool		cupsDNSSDServicePublish(cups_dnssd_service_t *service) _CUPS_PUBLIC;
extern bool		cupsDNSSDServiceSetLocation(cups_dnssd_service_t *service, const char *geo_uri) _CUPS_PUBLIC;

extern bool		cupsDNSSDAssembleFullName(char *fullname, size_t fullsize, const char *name, const char *type, const char *domain);
extern int		cupsDNSSDDecodeTXT(const unsigned char *txtrec, uint16_t txtlen, cups_option_t **txt) _CUPS_PUBLIC;
extern bool		cupsDNSSDSeparateFullName(const char *fullname, char *name, size_t namesize, char *type, size_t typesize, char *domain, size_t domainsize);


#  ifdef __cplusplus
}
#  endif // __cplusplus
#endif // !_CUPS_DNSSD_H_
