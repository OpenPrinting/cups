//
// OAuth API definitions for CUPS.
//
// Copyright © 2024-2025 by OpenPrinting.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

#ifndef _CUPS_OAUTH_H_
#  define _CUPS_OAUTH_H_
#  include "jwt.h"
#  ifdef __cplusplus
extern "C" {
#  endif /* __cplusplus */


//
// Constants...
//

#  define CUPS_OAUTH_REDIRECT_URI	"http://127.0.0.1/"
					// Redirect URI for local authorization

#  define CUPS_ODEVGRANT_DEVICE_CODE	"device_code"
					// The device code string
#  define CUPS_ODEVGRANT_EXPIRES_IN	"expires_in"
					// The expiration date/time of the device code
#  define CUPS_ODEVGRANT_INTERVAL	"interval"
					// The requested number of seconds between token calls
#  define CUPS_ODEVGRANT_USER_CODE	"user_code"
					// The user code string for authorization
#  define CUPS_ODEVGRANT_VERIFICATION_URI "verification_uri"
					// The URL for the verification web page
#  define CUPS_ODEVGRANT_VERIFICATION_URI_COMPLETE "verification_uri_complete"
					// The URL for the verifiction web page with the user code filled in


//
// Types...
//

typedef enum cups_ogrant_e		// OAuth Grant Types
{
  CUPS_OGRANT_AUTHORIZATION_CODE,	// Authorization code
  CUPS_OGRANT_DEVICE_CODE,		// Device code
  CUPS_OGRANT_REFRESH_TOKEN		// Refresh token
} cups_ogrant_t;


//
// Functions...
//

extern void		cupsOAuthClearTokens(const char *auth_uri, const char *resource_uri) _CUPS_PUBLIC;
extern char		*cupsOAuthCopyAccessToken(const char *auth_uri, const char *resource_uri, time_t *access_expires) _CUPS_PUBLIC;
extern char		*cupsOAuthCopyClientId(const char *auth_uri, const char *redirect_uri) _CUPS_PUBLIC;
extern char		*cupsOAuthCopyRefreshToken(const char *auth_uri, const char *resource_uri) _CUPS_PUBLIC;
extern cups_jwt_t	*cupsOAuthCopyUserId(const char *auth_uri, const char *resource_uri) _CUPS_PUBLIC;

extern char		*cupsOAuthGetAuthorizationCode(const char *auth_uri, cups_json_t *metadata, const char *resource_uri, const char *scopes, const char *redirect_uri) _CUPS_PUBLIC;
extern char		*cupsOAuthGetClientId(const char *auth_uri, cups_json_t *metadata, const char *redirect_uri, const char *logo_uri, const char *tos_uri) _CUPS_PUBLIC;
extern cups_json_t	*cupsOAuthGetDeviceGrant(const char *auth_uri, cups_json_t *metadata, const char *resource_uri, const char *scopes) _CUPS_PUBLIC;
extern cups_json_t	*cupsOAuthGetJWKS(const char *auth_uri, cups_json_t *metadata) _CUPS_PUBLIC;
extern cups_json_t	*cupsOAuthGetMetadata(const char *auth_uri) _CUPS_PUBLIC;
extern char		*cupsOAuthGetTokens(const char *auth_uri, cups_json_t *metadata, const char *resource_uri, const char *grant_code, cups_ogrant_t grant_type, const char *redirect_uri, time_t *access_expires) _CUPS_PUBLIC;
extern cups_jwt_t	*cupsOAuthGetUserId(const char *auth_uri, cups_json_t *metadata, const char *access_token) _CUPS_PUBLIC;

extern char		*cupsOAuthMakeAuthorizationURL(const char *auth_uri, cups_json_t *metadata, const char *resource_uri, const char *scopes, const char *client_id, const char *code_verifier, const char *nonce, const char *redirect_uri, const char *state) _CUPS_PUBLIC;
extern char		*cupsOAuthMakeBase64Random(size_t len) _CUPS_PUBLIC;

extern void		cupsOAuthSaveClientData(const char *auth_uri, const char *redirect_uri, const char *client_id, const char *client_secret) _CUPS_PUBLIC;
extern void		cupsOAuthSaveTokens(const char *auth_uri, const char *resource_uri, const char *access_token, time_t access_expires, const char *user_id, const char *refresh_token) _CUPS_PUBLIC;


#  ifdef __cplusplus
}
#  endif /* __cplusplus */
#endif // !_CUPS_OAUTH_H_
