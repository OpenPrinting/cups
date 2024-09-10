//
// OAuth API implementation for CUPS.
//
// Copyright © 2024 by OpenPrinting.
// Copyright © 2017-2024 by Michael R Sweet
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

#include "cups-private.h"
#include "oauth.h"
#include "form.h"
#include <sys/stat.h>
#ifdef _WIN32
#  include <process.h>
#  define O_NOFOLLOW 0			// Windows doesn't support this...
#else
#  include <poll.h>
#  ifdef __APPLE__
#    include <CoreFoundation/CoreFoundation.h>
#    include <CoreServices/CoreServices.h>
#  else
#    include <spawn.h>
#    include <sys/wait.h>
extern char **environ;			// @private@
#  endif // __APPLE__
#endif // _WIN32


//
// Overview
// ========
//
// The CUPS OAuth implementation follows the IEEE-ISTO Printer Working Group's
// IPP OAuth Extensions v1.0 (OAUTH) specification (pending publication), which
// in turn depends on a boatload of IETF RFCs and the OpenID Connect
// specifications.  In short, the IPP specification handles how to combine IPP
// (which is layered on top of HTTP) with OAuth and works to "consolidate" the
// different requirements of IETF OAuth 2.x and OpenID Connect so that we are as
// widely interoperable as possible.
//
//
// Compatibility
// -------------
//
// The intent is for CUPS to support using common OAuth implementations,
// including (but not limited to):
//
// - Amazon Cognito (<https://aws.amazon.com/cognito/>)
// - Github  (<https://docs.github.com/en/apps/oauth-apps/building-oauth-apps/authorizing-oauth-apps>)
// - Google (<https://developers.google.com/identity/openid-connect/openid-connect>)
// - Microsoft Account/Azure Active Directory/Entra ID (<https://learn.microsoft.com/en-us/entra/identity/>)
// - mOAuth (<https://www.msweet.org/moauth/>)
// - Okta Auth0 (<https://developer.auth0.com>)
//
//
// Security
// --------
//
// Security on the wire is as good as OAuth and TLS provides.
//
// The current OAuth cache implementation uses unencrypted files in your home
// directory with restricted permissions.  Ideally they should be encrypted
// "at rest" but Unix doesn't have a universal solution for this and the
// available options don't generally protect against malicious code running as
// the target user.  The code is setup to facilitate replacement with another
// storage "backend" (like the Keychain API on macOS), and adding conditional
// platform support code for this is planned.  This sort of issue is generally
// mitigated by access tokens having a limited life...
//
//
// Notes
// -----
//
// - Amazon and Microsoft require you to setup an Authorization Server for your
//   domain before you can play/test.  There is no public sandbox service.
// - Github support currently depends on hardcoded metadata
//   (<https://github.com/orgs/community/discussions/127556>) and has a few
//   authorization extensions that might require some special-handling.
// - Google implements OpenID Connect but not RFC 8414
//   (<https://accounts.google.com>) and seems to only allow a redirect URI of
//   "http://localhost" without a specified path.
// - Okta Auth0 provides a sample OpenID Connect Authorization Server
//   (<https://samples.auth0.com>) that also supports Device Connect and a few
//   other extensions that might be handy in the future.
//


//
// Local types...
//

typedef enum _cups_otype_e		// OAuth data type
{
  _CUPS_OTYPE_ACCESS,			// Access token
  _CUPS_OTYPE_CLIENT_ID,		// Client ID
  _CUPS_OTYPE_CLIENT_SECRET,		// Client secret
  _CUPS_OTYPE_CODE_VERIFIER,		// Client code_verifier
  _CUPS_OTYPE_USER_ID,			// (User) ID token
  _CUPS_OTYPE_JWKS,			// Server key store
  _CUPS_OTYPE_METADATA,			// Server metadata
  _CUPS_OTYPE_NONCE,			// Client nonce
  _CUPS_OTYPE_REDIRECT_URI,		// Redirect URI used
  _CUPS_OTYPE_REFRESH			// Refresh token
} _cups_otype_t;


//
// Local constants...
//

#define _CUPS_OAUTH_REDIRECT_FORMAT	"http://127.0.0.1:%d/"
					// Redirect URI with port
#define _CUPS_OAUTH_REDIRECT_PATH	"/?"
					// Redirect URI request path prefix
#define _CUPS_OAUTH_REDIRECT_PATHLEN	2
					// Redirect URI request path length

#ifdef DEBUG
static const char * const cups_otypes[] =
{					// OAuth data types...
  "_CUPS_OTYPE_ACCESS",			// Access token
  "_CUPS_OTYPE_CLIENT_ID",		// Client ID
  "_CUPS_OTYPE_CLIENT_SECRET",		// Client secret
  "_CUPS_OTYPE_CODE_VERIFIER",		// Client code_verifier
  "_CUPS_OTYPE_USER_ID",		// (User) ID token
  "_CUPS_OTYPE_JWKS",			// Server key store
  "_CUPS_OTYPE_METADATA",		// Server metadata
  "_CUPS_OTYPE_NONCE",			// Client nonce
  "_CUPS_OTYPE_REDIRECT_URI",		// Redirect URI used
  "_CUPS_OTYPE_REFRESH"			// Refresh token
};
#endif // DEBUG

static const char *github_metadata =	// Github.com OAuth metadata
"{\
\"issuer\":\"https://github.com\",\
\"authorization_endpoint\":\"https://github.com/login/oauth/authorize\",\
\"token_endpoint\":\"https://github.com/login/oauth/access_token\",\
\"token_endpoint_auth_methods_supported\":[\"client_secret_basic\"],\
\"scopes_supported\":[\"repo\",\"repo:status\",\"repo_deployment\",\"public_repo\",\"repo:invite\",\"security_events\",\"admin:repo_hook\",\"write:repo_hook\",\"read:repo_hook\",\"admin:org\",\"write:org\",\"read:org\",\"admin:public_key\",\"write:public_key\",\"read:public_key\",\"admin:org_hook\",\"gist\",\"notifications\",\"user\",\"read:user\",\"user:email\",\"user:follow\",\"project\",\"read:project\",\"delete_repo\",\"write:packages\",\"read:packages\",\"delete:packages\",\"admin.gpg_key\",\"write:gpg_key\",\"read:gpg_key\",\"codespace\",\"workflow\"],\
\"response_types_supported\":[\"code\"],\
\"grant_types_supported\":[\"authorization_code\",\"refresh_token\",\"\",\"urn:ietf:params:oauth:grant-type:device_code\"],\
\"device_authorization_endpoint\":\"https://github.com/login/device/code\",\
}";


//
// Local functions...
//

static char	*oauth_copy_response(http_t *http);
static cups_json_t *oauth_do_post(const char *ep, const char *content_type, const char *data);
static cups_json_t *oauth_get_jwks(const char *auth_uri, cups_json_t *metadata);
static char	*oauth_load_value(const char *auth_uri, const char *secondary_uri, _cups_otype_t otype);
static char	*oauth_make_path(char *buffer, size_t bufsize, const char *auth_uri, const char *secondary_uri, _cups_otype_t otype);
static char	*oauth_make_software_id(char *buffer, size_t bufsize);
static bool	oauth_metadata_contains(cups_json_t *metadata, const char *parameter, const char *value);
static void	oauth_save_value(const char *auth_uri, const char *secondary_uri, _cups_otype_t otype, const char *value);
static bool	oauth_set_error(cups_json_t *json, size_t num_form, cups_option_t *form);


//
// 'cupsOAuthClearTokens()' - Clear any cached authorization information.
//
// This function clears cached authorization information for the given
// Authorization Server "auth_uri" and Resource "resource_uri" combination.

void
cupsOAuthClearTokens(
    const char *auth_uri,		// I - Authorization server URI
    const char *resource_uri)		// I - Resource server URI
{
  oauth_save_value(auth_uri, resource_uri, _CUPS_OTYPE_ACCESS, /*value*/NULL);
  oauth_save_value(auth_uri, resource_uri, _CUPS_OTYPE_CODE_VERIFIER, NULL);
  oauth_save_value(auth_uri, resource_uri, _CUPS_OTYPE_USER_ID, /*value*/NULL);
  oauth_save_value(auth_uri, resource_uri, _CUPS_OTYPE_NONCE, NULL);
  oauth_save_value(auth_uri, resource_uri, _CUPS_OTYPE_REFRESH, /*value*/NULL);
}


//
// 'cupsOAuthCopyAccessToken()' - Get a cached access token.
//
// This function makes a copy of a cached access token and any
// associated expiration time for the given Authorization Server "auth_uri" and
// Resource "resource_uri" combination.  The returned access token must be freed
// using the `free` function.
//
// `NULL` is returned if no token is cached.
//

char *					// O - Access token
cupsOAuthCopyAccessToken(
    const char *auth_uri,		// I - Authorization Server URI
    const char *resource_uri,		// I - Resource URI
    time_t     *access_expires)		// O - Access expiration time or `NULL` for don't care
{
  char		*token,			// Access token
		*tokptr;		// Pointer into token


  // See if we have a token...
  if (access_expires)
    *access_expires = 0;

  if ((token = oauth_load_value(auth_uri, resource_uri, _CUPS_OTYPE_ACCESS)) != NULL)
  {
    if ((tokptr = strchr(token, '\n')) != NULL)
    {
      *tokptr++ = '\0';

      if (access_expires)
        *access_expires = strtol(tokptr, NULL, 10);
    }
  }

  return (token);
}


//
// 'cupsOAuthCopyClientId()' - Get the cached `client_id` value.
//
// This function makes a copy of the cached `client_id` value for a given
// Authorization Server "auth_uri" and Redirection URI "resource_uri". The
// returned value must be freed using the `free` function.
//
// `NULL` is returned if no `client_id` is cached.
//

char *					// O - `client_id` value
cupsOAuthCopyClientId(
    const char *auth_uri,		// I - Authorization Server URI
    const char *redirect_uri)		// I - Redirection URI
{
  return (oauth_load_value(auth_uri, redirect_uri, _CUPS_OTYPE_CLIENT_ID));
}


//
// 'cupsOAuthCopyRefreshToken()' - Get a cached refresh token.
//
// This function makes a copy of a cached refresh token for the given
// given Authorization Server "auth_uri" and Resource "resource_uri"
// combination.  The returned refresh token must be freed using the `free`
// function.
//
// `NULL` is returned if no refresh token is cached.
//

char *					// O - Refresh token
cupsOAuthCopyRefreshToken(
    const char *auth_uri,		// I - Authorization Server URI
    const char *resource_uri)		// I - Resource URI
{
  return (oauth_load_value(auth_uri, resource_uri, _CUPS_OTYPE_REFRESH));
}


//
// 'cupsOAuthCopyUserId()' - Get cached user identification information.
//
// This function makes a copy of cached user identification information for the
// given Authorization Server "auth_uri" and Resource "resource_uri"
// combination. The returned user information must be freed using the
// @link cupsJWTDelete@ function.
//
// `NULL` is returned if no identification information is cached.
//

cups_jwt_t *				// O - Identification information
cupsOAuthCopyUserId(
    const char *auth_uri,		// I - Authorization Server URI
    const char *resource_uri)		// I - Resource URI
{
  char		*value;			// ID token value
  cups_jwt_t	*jwt;			// JWT value


  value = oauth_load_value(auth_uri, resource_uri, _CUPS_OTYPE_USER_ID);
  jwt   = cupsJWTImportString(value, CUPS_JWS_FORMAT_COMPACT);

  free(value);
  return (jwt);
}


//
// 'cupsOAuthGetAuthorizationCode()' - Authorize access using a web browser.
//
// This function performs a local/"native" OAuth authorization flow to obtain an
// authorization code for use with the @link cupsOAuthGetTokens@ function.
//
// The "auth_uri" parameter specifies the URI for the OAuth Authorization
// Server.  The "metadata" parameter specifies the Authorization Server metadata
// as obtained using @link cupsOAuthCopyMetadata@ and/or
// @link cupsOAuthGetMetadata@.
//
// The "resource_uri" parameter specifies the URI for a resource (printer, web
// file, etc.) that you which to access.
//
// The "scopes" parameter specifies zero or more whitespace-delimited scope
// names to request during authorization.  The list of supported scope names are
// available from the Authorization Server metadata, for example:
//
// The "redirect_uri" parameter specifies a 'http:' URL with a listen address,
// port, and path to use.  If `NULL`, 127.0.0.1 on a random port is used with a
// path of "/".
//
// ```
// cups_json_t *metadata = cupsOAuthGetMetadata(auth_uri);
// cups_json_t *scopes_supported = cupsJSONFind(metadata, "scopes_supported");
// ```
//
// The returned authorization code must be freed using the `free` function.
//

char *					// O - Authorization code or `NULL` on error
cupsOAuthGetAuthorizationCode(
    const char  *auth_uri,		// I - Authorization Server URI
    cups_json_t *metadata,		// I - Authorization Server metadata
    const char  *resource_uri,		// I - Resource URI
    const char  *scopes,		// I - Space-delimited scopes
    const char  *redirect_uri)		// I - Redirect URI or `NULL` for default
{
  char		*client_id = NULL,	// `client_id` value
		*code_verifier = NULL,	// Code verifier string
		*nonce = NULL,		// Nonce string
		*state = NULL,		// State string
		*url = NULL,		// URL for authorization page
		*scopes_supported = NULL;
					// Supported scopes
  char		resource[256],		// Resource path
		final_uri[1024];	// Final redirect URI
  size_t	resourcelen;		// Length of resource path
  http_addr_t	addr;			// Loopback listen address
  int		port;			// Port number
  int		fd = -1;		// Listen file descriptor
  fd_set	input;			// Input file descriptors for select()
  struct timeval timeout;		// Timeout for select()
  time_t	endtime;		// End time
  http_t	*http;			// HTTP client
  char		*auth_code = NULL;	// Authorization code


  // Range check input...
  DEBUG_printf("cupsOAuthGetAuthorizationCode(auth_uri=\"%s\", metadata=%p, resource_uri=\"%s\", scopes=\"%s\", redirect_uri=\"%s\")", auth_uri, (void *)metadata, resource_uri, scopes, redirect_uri);

  if (!auth_uri || !metadata || cupsJSONGetString(cupsJSONFind(metadata, "authorization_endpoint")) == NULL)
    return (NULL);

  // Get the client_id value...
  if ((client_id = cupsOAuthCopyClientId(auth_uri, redirect_uri ? redirect_uri : CUPS_OAUTH_REDIRECT_URI)) == NULL)
    client_id = cupsOAuthGetClientId(auth_uri, metadata, redirect_uri ? redirect_uri : CUPS_OAUTH_REDIRECT_URI, /*logo_uri*/NULL, /*tos_uri*/NULL);

  if (!client_id)
    return (NULL);

  // Listen on a local port...
  if (redirect_uri)
  {
    // Use the host/port/resource from the URI
    char	scheme[32],		// URL scheme
		userpass[256],		// Username:password (ignored)
		host[256];		// Hostname

    if (httpSeparateURI(HTTP_URI_CODING_ALL, redirect_uri, scheme, sizeof(scheme), userpass, sizeof(userpass), host, sizeof(host), &port, resource, sizeof(resource)) < HTTP_URI_STATUS_OK || strcmp(scheme, "http"))
    {
      DEBUG_printf("1cupsOAuthGetAuthorizationCode: Bad redirect_uri '%s'.", redirect_uri);
      _cupsSetError(IPP_STATUS_ERROR_INTERNAL, strerror(EINVAL), false);
      goto done;
    }

    memset(&addr, 0, sizeof(addr));
#ifdef __APPLE__
    addr.ipv4.sin_len    = sizeof(struct sockaddr_in);
#endif // __APPLE__
    addr.ipv4.sin_family = AF_INET;

    if (!strcmp(host, "localhost") || !strcmp(host, "127.0.0.1"))
      addr.ipv4.sin_addr.s_addr = htonl(0x7f000001);

    fd = httpAddrListen(&addr, port);

    cupsConcatString(resource, "?", sizeof(resource));
  }
  else
  {
    // Find the next available port on 127.0.0.1...
    memset(&addr, 0, sizeof(addr));
#ifdef __APPLE__
    addr.ipv4.sin_len         = sizeof(struct sockaddr_in);
#endif // __APPLE__
    addr.ipv4.sin_family      = AF_INET;
    addr.ipv4.sin_addr.s_addr = htonl(0x7f000001);

    for (port = 10000; port < 11000; port ++)
    {
      if ((fd = httpAddrListen(&addr, port)) >= 0)
	break;
    }

    // Save the redirect URI and resource...
    cupsCopyString(resource, _CUPS_OAUTH_REDIRECT_PATH, sizeof(resource));
    snprintf(final_uri, sizeof(final_uri), _CUPS_OAUTH_REDIRECT_FORMAT, port);
    redirect_uri = final_uri;
  }

  DEBUG_printf("1cupsOAuthGetAuthorizationCode: Listen socket for port %d is %d (%s)", port, fd, strerror(errno));

  if (fd < 0)
    goto done;

  resourcelen = strlen(resource);

  // Point redirection to the local port...
  oauth_save_value(auth_uri, resource_uri, _CUPS_OTYPE_REDIRECT_URI, redirect_uri);

  // Make state and code verification strings...
  if (oauth_metadata_contains(metadata, "code_challenge_methods_supported", "S256"))
    code_verifier = cupsOAuthMakeBase64Random(128);
  else
    code_verifier = NULL;

  if (oauth_metadata_contains(metadata, "scopes_supported", "openid"))
    nonce = cupsOAuthMakeBase64Random(16);
  else
    nonce = NULL;

  state = cupsOAuthMakeBase64Random(16);

  if (!state)
    goto done;

  if (!scopes)
  {
    cups_json_t	*values;		// Parameter values

    if ((values = cupsJSONFind(metadata, "scopes_supported")) != NULL)
    {
      // Convert scopes_supported to a string...
      size_t		i,		// Looping var
			count,		// Number of values
	 	     	length = 0;	// Length of string
      cups_json_t	*current;	// Current value

      for (i = 0, count = cupsJSONGetCount(values); i < count; i ++)
      {
	current = cupsJSONGetChild(values, i);

	if (cupsJSONGetType(current) == CUPS_JTYPE_STRING)
	  length += strlen(cupsJSONGetString(current)) + 1;
      }

      if (length > 0 && (scopes_supported = malloc(length)) != NULL)
      {
        // Copy the scopes to a string with spaces between them...
        char	*ptr;			// Pointer into value

	for (i = 0, ptr = scopes_supported; i < count; i ++)
	{
	  current = cupsJSONGetChild(values, i);

	  if (cupsJSONGetType(current) == CUPS_JTYPE_STRING)
	  {
	    if (i)
	      *ptr++ = ' ';

	    cupsCopyString(ptr, cupsJSONGetString(current), length - (size_t)(ptr - scopes_supported));
	    ptr += strlen(ptr);
	  }
	}

        // Use the supported scopes in the request...
	scopes = scopes_supported;
      }
    }
  }

  // Get the authorization URL...
  if ((url = cupsOAuthMakeAuthorizationURL(auth_uri, metadata, resource_uri, scopes, client_id, code_verifier, nonce, redirect_uri, state)) == NULL)
    goto done;

  // Open a web browser with the authorization page...
#ifdef __APPLE__
  CFURLRef	cfurl;			// CoreFoundation URL
  int		error = 1;		// Open status

  if ((cfurl = CFURLCreateWithBytes(kCFAllocatorDefault, (const UInt8 *)url, (CFIndex)strlen(url), kCFStringEncodingASCII, NULL)) != NULL)
  {
    error = LSOpenCFURLRef(cfurl, NULL);

    CFRelease(cfurl);
  }

  if (error != noErr)
    goto done;

#elif defined(_WIN32)
  if (_spawnl(_P_WAIT, "start", "", url, NULL))
    goto done;

#else
  pid_t		pid = 0;		// Process ID
  int		estatus;		// Exit status
  const char	*xdg_open_argv[3];	// xdg-open arguments

  xdg_open_argv[0] = "xdg-open";
  xdg_open_argv[1] = url;
  xdg_open_argv[2] = NULL;

  if (posix_spawnp(&pid, "xdg-open", NULL, NULL, (char * const *)xdg_open_argv, environ))
    goto done;				// Couldn't run xdg-open
  else if (waitpid(pid, &estatus, 0))
    goto done;				// Couldn't get exit status
  else if (estatus)
    goto done;				// Non-zero exit status
#endif // __APPLE__

  // Listen for connections up to 60 seconds...
  endtime = time(NULL) + 60;

  while (auth_code == NULL && time(NULL) < endtime)
  {
    timeout.tv_sec  = 1;
    timeout.tv_usec = 0;

    FD_ZERO(&input);
    FD_SET(fd, &input);

    if (select(fd + 1, &input, /*writefds*/NULL, /*errorfds*/NULL, &timeout) > 0 && FD_ISSET(fd, &input))
    {
      // Try accepting a connection...
      if ((http = httpAcceptConnection(fd, true)) != NULL)
      {
        // Respond to HTTP requests...
        while (auth_code == NULL && time(NULL) < endtime && httpWait(http, 1000))
        {
          char		reqres[4096],	// Resource path
			message[2048];	// Response message
	  http_state_t	hstate;		// HTTP request state
	  http_status_t	hstatus;	// HTTP request status
	  const char	*htype = NULL,	// HTTP response content type
			*hbody = NULL;	// HTTP response body

          // Get the request header...
          if ((hstate = httpReadRequest(http, reqres, sizeof(reqres))) == HTTP_STATE_WAITING)
            continue;
	  else if (hstate == HTTP_STATE_ERROR || hstate == HTTP_STATE_UNKNOWN_METHOD || hstate == HTTP_STATE_UNKNOWN_VERSION)
            break;

	  // Read incoming headers until the status changes...
	  do
	  {
	    hstatus = httpUpdate(http);
	  }
	  while (hstatus == HTTP_STATUS_CONTINUE && time(NULL) < endtime);

          // Stop on error...
          if (hstatus != HTTP_STATUS_OK)
            break;

          // Process the request...
          switch (hstate)
          {
            default :
		hstatus = HTTP_STATUS_METHOD_NOT_ALLOWED;
                break;

            case HTTP_STATE_HEAD :
		if (!strncmp(reqres, resource, resourcelen))
		{
		  // Respond that the content will be HTML...
		  htype = "text/html";
		}
		else
		{
		  // Resource doesn't exist...
		  hstatus = HTTP_STATUS_NOT_FOUND;
		}
		break;

	    case HTTP_STATE_GET :
		if (!strncmp(reqres, resource, resourcelen))
		{
		  // Collect form parameters from resource...
		  const char	*code_value,		// Authoziation code value
				*error_code,		// Error code
				*error_desc,		// Error description
				*state_value;		// State value
		  int		num_form;		// Number of form variables
		  cups_option_t	*form = NULL;		// Form variables

		  num_form    = cupsFormDecode(reqres + resourcelen, &form);
                  code_value  = cupsGetOption("code", num_form, form);
                  error_code  = cupsGetOption("error", num_form, form);
                  error_desc  = cupsGetOption("error_description", num_form, form);
                  state_value = cupsGetOption("state", num_form, form);

                  if (code_value && state_value && !strcmp(state, state_value))
                  {
                    // Got a code and the correct state value, copy the code and
                    // save out code_verifier and nonce values...
                    auth_code = strdup(code_value);

                    oauth_save_value(auth_uri, resource_uri, _CUPS_OTYPE_CODE_VERIFIER, code_verifier);
                    oauth_save_value(auth_uri, resource_uri, _CUPS_OTYPE_NONCE, nonce);

		    hbody = "<!DOCTYPE html>\n"
			    "<html>\n"
			    "  <head><title>Authorization Complete</title></head>\n"
			    "  <body>\n"
			    "    <h1>Authorization Complete</h1>\n"
			    "    <p>You may now close this window/tab.</p>\n"
			    "    <script>setTimeout(\"window.close()\", 5000)</script>\n"
			    "  </body>\n"
			    "</html>\n";
                  }
                  else
                  {
                    // Got an error...
		    hbody = message;
                    snprintf(message, sizeof(message),
                             "<!DOCTYPE html>\n"
			     "<html>\n"
			     "  <head><title>Authorization Failed</title></head>\n"
			     "  <body>\n"
			     "    <h1>Authorization Failed</h1>\n"
			     "    <p>%s: %s</p>\n"
			     "  </body>\n"
			     "</html>\n", error_code ? error_code : "bad_response", error_desc ? error_desc : "The authorization server's response was not understood.");
                  }
		  cupsFreeOptions(num_form, form);

                  // Respond accordingly...
		  htype = "text/html";
		}
		else
		{
		  // Resource doesn't exist...
		  hstatus = HTTP_STATUS_NOT_FOUND;
		  htype   = "text/plain";
		  hbody   = "This is not the resource you are looking for.\n";
		}
		break;
          }

	  // Send response...
	  httpClearFields(http);
	  if (hstatus >= HTTP_STATUS_BAD_REQUEST)
	    httpSetField(http, HTTP_FIELD_CONNECTION, "close");
	  if (htype)
	    httpSetField(http, HTTP_FIELD_CONTENT_TYPE, htype);
	  if (hbody)
	    httpSetLength(http, strlen(hbody));
	  httpWriteResponse(http, hstatus);

	  if (hbody)
	    httpWrite(http, hbody, strlen(hbody));

          // Stop on error...
          if (hstatus != HTTP_STATUS_OK)
            break;
        }

	// Close the client connection...
        httpClose(http);
      }
    }
  }

  done:

  // Free strings, close the listen socket, and return...
  if (fd >= 0)
    httpAddrClose(&addr, fd);

  free(client_id);
  free(code_verifier);
  free(nonce);
  free(scopes_supported);
  free(state);
  free(url);

  return (auth_code);
}


//
// 'cupsOAuthGetClientId()' - Register a client application and get its ID.
//
// This function registers a client application with the specified OAuth
// Authorization Server.
//
// The "auth_uri" parameter specifies the URI for the OAuth Authorization
// Server. The "metadata" parameter specifies the Authorization Server metadata
// as obtained using @link cupsOAuthCopyMetadata@ and/or
// @link cupsOAuthGetMetadata@.
//
// The "redirect_uri" argument specifies the URL to use for providing
// authorization results to a WWW application.
//
// The "logo_uri" argument specifies a public URL for the logo of your
// application, while the "tos_uri" specifies a public URL for the terms of
// service for your application.
//
// The returned "client_id" string must be freed using the `free` function.
//
// *Note*: This function should only be used to register WWW applications. The
// @link cupsOAuthGetAuthorizationCode@ function handles registration of
// local/"native" applications for you.
//

char *					// O - `client_id` string or `NULL` on error
cupsOAuthGetClientId(
    const char  *auth_uri,		// I - Authorization Server URI
    cups_json_t *metadata,		// I - Authorization Server metadata
    const char  *redirect_uri,		// I - Redirection URL
    const char  *logo_uri,		// I - Logo URL or `NULL` for none
    const char  *tos_uri)		// I - Terms-of-service URL or `NULL` for none
{
  const char	*registration_ep;	// Registration endpoint
  char		software_id[37];	// `software_id` string
  char		*client_id = NULL;	// `client_id` string
  char		*req_data = NULL;	// JSON request data
  cups_json_t	*request,		// JSON request variables
		*response,		// JSON response
		*jarray;		// JSON array
  const char	*value;			// JSON value


  // Range check input...
  if (!auth_uri || !metadata || (registration_ep = cupsJSONGetString(cupsJSONFind(metadata, "registration_endpoint"))) == NULL || !redirect_uri)
    return (NULL);

  // Prepare JSON data to register the client application...
  request = cupsJSONNew(/*parent*/NULL, /*after*/NULL, CUPS_JTYPE_OBJECT);
  cupsJSONNewString(request, cupsJSONNewKey(request, /*after*/NULL, "client_name"), "CUPS");
  cupsJSONNewString(request, cupsJSONNewKey(request, /*after*/NULL, "client_uri"), "https://openprinting.github.io/cups/");
  if (logo_uri)
    cupsJSONNewString(request, cupsJSONNewKey(request, /*after*/NULL, "logo_uri"), logo_uri);
  cupsJSONNewString(request, cupsJSONNewKey(request, /*after*/NULL, "software_id"), oauth_make_software_id(software_id, sizeof(software_id)));
  cupsJSONNewString(request, cupsJSONNewKey(request, /*after*/NULL, "software_version"), CUPS_SVERSION);
  jarray = cupsJSONNew(request, cupsJSONNewKey(request, /*after*/NULL, "redirect_uris"), CUPS_JTYPE_ARRAY);
  cupsJSONNewString(jarray, /*after*/NULL, redirect_uri);
  if (tos_uri)
    cupsJSONNewString(request, cupsJSONNewKey(request, /*after*/NULL, "tos_uri"), tos_uri);

  req_data = cupsJSONExportString(request);
  cupsJSONDelete(request);

  if (!req_data)
    goto done;

  if ((response = oauth_do_post(registration_ep, "application/json", req_data)) == NULL)
    goto done;

  if ((value = cupsJSONGetString(cupsJSONFind(response, "client_id"))) != NULL)
  {
    if ((client_id = strdup(value)) != NULL)
    {
      // Save client_id and optional client_secret...
      oauth_save_value(auth_uri, redirect_uri, _CUPS_OTYPE_CLIENT_ID, value);
      oauth_save_value(auth_uri, redirect_uri, _CUPS_OTYPE_CLIENT_SECRET, cupsJSONGetString(cupsJSONFind(response, "client_secret")));
    }
  }

  cupsJSONDelete(response);

  // Return whatever we got...
  done:

  free(req_data);

  return (client_id);
}


//
// 'cupsOAuthGetMetadata()' - Get the metadata for an Authorization Server.
//
// This function gets the metadata for the specified Authorization Server URI
// "auth_uri". Metadata is cached per-user for better performance.
//
// The returned metadata must be freed using the @link cupsJSONDelete@ function.
//

cups_json_t *				// O - JSON metadata or `NULL` on error
cupsOAuthGetMetadata(
    const char *auth_uri)		// I - Authorization Server URI
{
  char		filename[1024];		// Local metadata filename
  struct stat	fileinfo;		// Local metadata file info
  char		filedate[256],		// Local metadata modification date
		host[256],		// Hostname
		resource[256];		// Resource path
  int		port;			// Port to use
  http_t	*http;			// Connection to server
  http_status_t	status = HTTP_STATUS_NOT_FOUND;
					// Request status
  size_t	i;			// Looping var
  static const char * const paths[] =	// Metadata paths
  {
    "/.well-known/oauth-authorization-server",
    "/.well-known/openid-configuration"
  };


  DEBUG_printf("cupsOAuthGetMetadata(auth_uri=\"%s\")", auth_uri);

  // Special-cases...
  if (!strcmp(auth_uri, "https://github.com"))
    return (cupsJSONImportString(github_metadata));

  // Get existing metadata...
  if (!oauth_make_path(filename, sizeof(filename), auth_uri, /*secondary_uri*/NULL, _CUPS_OTYPE_METADATA))
    return (NULL);

  if (stat(filename, &fileinfo))
    memset(&fileinfo, 0, sizeof(fileinfo));

  if (fileinfo.st_mtime)
    httpGetDateString2(fileinfo.st_mtime, filedate, sizeof(filedate));
  else
    filedate[0] = '\0';

  // Don't bother connecting if the metadata was updated recently...
  if ((time(NULL) - fileinfo.st_mtime) <= 60)
    goto load_metadata;

  // Try getting the metadata...
  if ((http = httpConnectURI(auth_uri, host, sizeof(host), &port, resource, sizeof(resource), /*blocking*/true, /*msec*/30000, /*cancel*/NULL, /*require_ca*/true)) == NULL)
    return (NULL);

  for (i = 0; i < (sizeof(paths) / sizeof(paths[0])); i ++)
  {
    cupsCopyString(resource, paths[i], sizeof(resource));

    do
    {
      if (!_cups_strcasecmp(httpGetField(http, HTTP_FIELD_CONNECTION), "close"))
      {
        httpClearFields(http);
        if (!httpConnectAgain(http, /*msec*/30000, /*cancel*/NULL))
        {
	  status = HTTP_STATUS_ERROR;
	  break;
        }
      }

      httpClearFields(http);

      httpSetField(http, HTTP_FIELD_IF_MODIFIED_SINCE, filedate);
      if (!httpWriteRequest(http, "GET", resource))
      {
        if (!httpConnectAgain(http, 30000, NULL) || !httpWriteRequest(http, "GET", resource))
        {
          status = HTTP_STATUS_ERROR;
	  break;
        }
      }

      while ((status = httpUpdate(http)) == HTTP_STATUS_CONTINUE)
        ;

      if (status >= HTTP_STATUS_MULTIPLE_CHOICES && status <= HTTP_STATUS_SEE_OTHER)
      {
        // Redirect
	char	lscheme[32],		// Location scheme
		luserpass[256],		// Location user:password (not used)
		lhost[256],		// Location hostname
		lresource[256];		// Location resource path
	int	lport;			// Location port

        if (httpSeparateURI(HTTP_URI_CODING_ALL, httpGetField(http, HTTP_FIELD_LOCATION), lscheme, sizeof(lscheme), luserpass, sizeof(luserpass), lhost, sizeof(lhost), &lport, lresource, sizeof(lresource)) < HTTP_URI_STATUS_OK)
	  break;			// Don't redirect to an invalid URI

        if (_cups_strcasecmp(host, lhost) || port != lport)
	  break;			// Don't redirect off this host

        // Redirect to a local resource...
        cupsCopyString(resource, lresource, sizeof(resource));
      }
    }
    while (status >= HTTP_STATUS_MULTIPLE_CHOICES && status <= HTTP_STATUS_SEE_OTHER);

    if (status == HTTP_STATUS_NOT_MODIFIED)
    {
      // Metadata isn't changed, stop now...
      break;
    }
    else if (status == HTTP_STATUS_OK)
    {
      // Copy the metadata to the file...
      int	fd;			// Local metadata file
      char	buffer[8192];		// Copy buffer
      ssize_t	bytes;			// Bytes read

      if ((fd = open(filename, O_CREAT | O_TRUNC | O_WRONLY | O_NOFOLLOW, 0600)) < 0)
      {
        httpFlush(http);
        break;
      }

      while ((bytes = httpRead(http, buffer, sizeof(buffer))) > 0)
        write(fd, buffer, (size_t)bytes);

      close(fd);
      break;
    }
  }

  if (status != HTTP_STATUS_OK && status != HTTP_STATUS_NOT_MODIFIED)
  {
    // Remove old cached data...
    unlink(filename);
  }

  httpClose(http);

  // Return the cached metadata, if any...
  load_metadata:

  return (cupsJSONImportFile(filename));
}


//
// 'cupsOAuthGetTokens()' - Obtain access and refresh tokens.
//
// This function obtains a access and refresh tokens from an OAuth Authorization
// Server. OpenID Authorization Servers also provide user identification
// information.
//
// The "auth_uri" parameter specifies the URI for the OAuth Authorization
// Server.  The "metadata" parameter specifies the Authorization Server metadata
// as obtained using @link cupsOAuthCopyMetadata@ and/or
// @link cupsOAuthGetMetadata@.
//
// The "resource_uri" parameter specifies the URI for a resource (printer, web
// file, etc.) that you which to access.
//
// The "grant_code" parameter specifies the code or token to use while the
// "grant_type" parameter specifies the type of code:
//
// - `CUPS_OGRANT_AUTHORIZATION_CODE`: A user authorization grant code.
// - `CUPS_OGRANT_DEVICE_CODE`: A device authorization grant code.
// - `CUPS_OGRANT_REFRESH_TOKEN`: A refresh token.
//
// The "redirect_uri" specifies the redirection URI used to obtain the code. The
// constant `CUPS_OAUTH_REDIRECT_URI` should be used for codes obtained using
// the @link cupsOAuthGetAuthorizationCode@ function.
//
// When successful, the access token and expiration time are returned. The
// access token must be freed using the `free` function. The new refresh token
// and any user ID information can be obtained using the
// @link cupsOAuthCopyRefreshToken@ and @link cupsOAuthCopyUserId@ functions
// respectively.
//

char *					// O - Access token or `NULL` on error
cupsOAuthGetTokens(
    const char    *auth_uri,		// I - Authorization Server URI
    cups_json_t   *metadata,		// I - Authorization Server metadata
    const char    *resource_uri,	// I - Resource URI
    const char    *grant_code,		// I - Authorization code or refresh token
    cups_ogrant_t grant_type,		// I - Grant code type
    const char    *redirect_uri,	// I - Redirect URI
    time_t        *access_expires)	// O - Expiration time for access token
{
  const char	*token_ep;		// Token endpoint
  char		*value,			// Prior value
		*nonce = NULL;		// Prior nonce value
  int		num_form = 0;		// Number of form variables
  cups_option_t	*form = NULL;		// Form variables
  char		*request = NULL;	// Form request data
  cups_json_t	*response = NULL;	// JSON response variables
  const char	*access_value = NULL,	// access_token
		*id_value = NULL,	// id_token
		*refresh_value = NULL;	// refresh_token
  double	expires_in;		// expires_in value
  time_t	access_expvalue;	// Expiration time for access_token
  cups_jwt_t	*jwt = NULL;		// JWT of the id_token
  const char	*jnonce;		// Nonce value from the JWT
  char		*access_token = NULL;	// Access token
  static const char * const grant_types[] =
  {					// Grant type strings
    "authorization_code",
    "urn:ietf:params:oauth:grant-type:device_code",
    "refresh_token"
  };


  DEBUG_printf("cupsOAuthGetTokens(auth_uri=\"%s\", metadata=%p, resource_uri=\"%s\", grant_code=\"%s\", grant_type=%d, redirect_uri=\"%s\", access_expires=%p)", auth_uri, (void *)metadata, resource_uri, grant_code, grant_type, redirect_uri, (void *)access_expires);

  // Range check input...
  if (access_expires)
    *access_expires = 0;

  if (!auth_uri || !metadata || (token_ep = cupsJSONGetString(cupsJSONFind(metadata, "token_endpoint"))) == NULL || !grant_code || !redirect_uri)
    return (NULL);

  // Prepare form data to get an access token...
  num_form = cupsAddOption("grant_type", grant_types[grant_type], num_form, &form);
  num_form = cupsAddOption("code", grant_code, num_form, &form);

  if (!strcmp(redirect_uri, CUPS_OAUTH_REDIRECT_URI) && (value = oauth_load_value(auth_uri, resource_uri, _CUPS_OTYPE_REDIRECT_URI)) != NULL)
  {
    DEBUG_printf("1cupsOAuthGetTokens: redirect_uri=\"%s\"", value);
    num_form = cupsAddOption("redirect_uri", value, num_form, &form);
    free(value);
  }
  else
  {
    num_form = cupsAddOption("redirect_uri", redirect_uri, num_form, &form);
  }

  if ((value = oauth_load_value(auth_uri, redirect_uri, _CUPS_OTYPE_CLIENT_ID)) != NULL)
  {
    DEBUG_printf("1cupsOAuthGetTokens: client_id=\"%s\"", value);
    num_form = cupsAddOption("client_id", value, num_form, &form);
    free(value);
  }

  if ((value = oauth_load_value(auth_uri, redirect_uri, _CUPS_OTYPE_CLIENT_SECRET)) != NULL)
  {
    DEBUG_printf("1cupsOAuthGetTokens: client_secret=\"%s\"", value);
    num_form = cupsAddOption("client_secret", value, num_form, &form);
    free(value);
  }

  if ((value = oauth_load_value(auth_uri, resource_uri, _CUPS_OTYPE_CODE_VERIFIER)) != NULL)
  {
    DEBUG_printf("1cupsOAuthGetTokens: code_verifier=\"%s\"", value);
    num_form = cupsAddOption("code_verifier", value, num_form, &form);
    free(value);
  }

  request = cupsFormEncode(/*url*/NULL, num_form, form);
  cupsFreeOptions(num_form, form);

  if (!request)
    goto done;

  if ((response = oauth_do_post(token_ep, "application/x-www-form-urlencoded", request)) == NULL)
    goto done;

  access_value  = cupsJSONGetString(cupsJSONFind(response, "access_token"));
  expires_in    = cupsJSONGetNumber(cupsJSONFind(response, "expires_in"));
  id_value      = cupsJSONGetString(cupsJSONFind(response, "id_token"));
  refresh_value = cupsJSONGetString(cupsJSONFind(response, "refresh_token"));

  if (id_value)
  {
    // Validate the JWT
    cups_json_t	*jwks;			// JWT key set
    bool	valid;			// Valid id_token?
    const char	*at_hash;		// at_hash claim value

    jwt    = cupsJWTImportString(id_value, CUPS_JWS_FORMAT_COMPACT);
    jnonce = cupsJWTGetClaimString(jwt, "nonce");
    nonce  = oauth_load_value(auth_uri, resource_uri, _CUPS_OTYPE_NONCE);

    // Check nonce
    if (!jwt || (jnonce && nonce && strcmp(jnonce, nonce)))
      goto done;

    // Validate id_token against the Authorization Server's JWKS
    if ((jwks = oauth_get_jwks(auth_uri, metadata)) == NULL)
      goto done;

    valid = cupsJWTHasValidSignature(jwt, jwks);
    DEBUG_printf("1cupsOAuthGetTokens: valid=%s", valid ? "true" : "false");
    cupsJSONDelete(jwks);
    if (!valid)
      goto done;

    // Validate the at_hash claim string against access_token value
    if (access_value && (at_hash = cupsJWTGetClaimString(jwt, "at_hash")) != NULL)
    {
      unsigned char sha256[32],		// Hash of the access_token value
		at_hash_buffer[32];	// at_hash bytes
      size_t	at_hash_bytes = sizeof(at_hash_buffer);
		      			// Number of at_hash bytes

      cupsHashData("sha2-256", access_value, strlen(access_value), sha256, sizeof(sha256));
      httpDecode64_3((char *)at_hash_buffer, &at_hash_bytes, at_hash, /*end*/NULL);
      if (at_hash_bytes != 16 || memcmp(sha256, at_hash_buffer, 16))
      {
        DEBUG_puts("1cupsOAuthGetTokens: at_hash doesn't match SHA-256 of access_token.");
        goto done;
      }
    }
  }

  if (expires_in > 0.0)
    access_expvalue = time(NULL) + (long)expires_in;
  else
    access_expvalue = 0;

  cupsOAuthSaveTokens(auth_uri, resource_uri, access_value, access_expvalue, id_value, refresh_value);

  if (access_value)
    access_token = strdup(access_value);

  if (access_expires)
    *access_expires = access_expvalue;

  // Return whatever we got...
  done:

  cupsJSONDelete(response);
  cupsJWTDelete(jwt);
  free(nonce);
  free(request);

  return (access_token);
}


//
// 'cupsOAuthMakeAuthorizationURL()' - Make an authorization URL.
//
// This function makes an authorization URL for the specified authorization
// server and resource.
//
// The "auth_uri" parameter specifies the URI for the OAuth Authorization
// Server.  The "metadata" parameter specifies the Authorization Server metadata
// as obtained using @link cupsOAuthCopyMetadata@ and/or
// @link cupsOAuthGetMetadata@.
//
// The "resource_uri" parameter specifies the URI for a resource (printer, web
// file, etc.) that you which to access.
//
// The "scopes" parameter specifies zero or more whitespace-delimited scope
// names to request during authorization.  The list of supported scope names are
// available from the Authorization Server metadata, for example:
//
// ```
// cups_json_t *metadata = cupsOAuthGetMetadata(auth_uri);
// cups_json_t *scopes_supported = cupsJSONFind(metadata, "scopes_supported");
// ```
//
// The "client_id" parameter specifies the client identifier obtained using
// @link cupsOAuthCopyClientId@ and/or @link cupsOAuthGetClientId@.
//
// The "client_id" parameter is the string returned by
// @link cupsOAuthCopyClientId@ or @link cupsOAuthGetClientId@.
//
// The "code_verifier" parameter specifies a random Base64URL-encoded string
// that is used by the Proof Key for Code Exchange [RFC7636] extension to help
// secure the authorization flow.  The @link cupsOAuthMakeBase64Random@ function
// can be used to generate this string.
//
// The "nonce" parameter specifies a random Base64URL-encoded string that is
// used by OpenID to validate the ID token. The @link cupsOAuthMakeBase64Random@
// function can be used to generate this string.
//
// The "redirect_uri" parameter specifies the URI that will receive the
// authorization grant code.
//
// The "state" parameter is a unique (random) identifier for the authorization
// request.  It is provided to the redirection URI as a form parameter.
//

char *					// O - Authorization URL
cupsOAuthMakeAuthorizationURL(
    const char  *auth_uri,		// I - Authorization Server URI
    cups_json_t *metadata,		// I - Authorization Server metadata
    const char  *resource_uri,		// I - Resource URI
    const char  *scopes,		// I - Space-delimited scope(s)
    const char  *client_id,		// I - Client ID
    const char  *code_verifier,		// I - Code verifier string
    const char  *nonce,			// I - Nonce
    const char  *redirect_uri,		// I - Redirection URI
    const char  *state)			// I - State
{
  const char	*authorization_ep;	// Authorization endpoint
  unsigned char	sha256[32];		// SHA-256 hash of code verifier
  char		code_challenge[64];	// Hashed code verifier string
  int		num_vars = 0;		// Number of form variables
  cups_option_t	*vars = NULL;		// Form variables
  char		*url;			// URL for authorization page


  // Range check input...
  if (!auth_uri || !metadata || (authorization_ep = cupsJSONGetString(cupsJSONFind(metadata, "authorization_endpoint"))) == NULL || !redirect_uri || !client_id)
    return (NULL);

  // Make the authorization URL using the information supplied...
  if (oauth_metadata_contains(metadata, "response_type_supported", "code id_token"))
    num_vars = cupsAddOption("response_type", "code id_token", num_vars, &vars);
  else
    num_vars = cupsAddOption("response_type", "code", num_vars, &vars);

  num_vars = cupsAddOption("client_id", client_id, num_vars, &vars);
  num_vars = cupsAddOption("redirect_uri", redirect_uri, num_vars, &vars);

  if (code_verifier && oauth_metadata_contains(metadata, "code_challenge_methods_supported", "S256"))
  {
    oauth_save_value(auth_uri, resource_uri, _CUPS_OTYPE_CODE_VERIFIER, /*value*/NULL);

    cupsHashData("sha2-256", code_verifier, strlen(code_verifier), sha256, sizeof(sha256));
    httpEncode64_3(code_challenge, sizeof(code_challenge), (char *)sha256, sizeof(sha256), true);
    num_vars = cupsAddOption("code_challenge", code_challenge, num_vars, &vars);
    num_vars = cupsAddOption("code_challenge_method", "S256", num_vars, &vars);
  }

  if (nonce && oauth_metadata_contains(metadata, "scopes_supported", "openid"))
  {
    oauth_save_value(auth_uri, resource_uri, _CUPS_OTYPE_NONCE, /*value*/NULL);

    num_vars = cupsAddOption("nonce", nonce, num_vars, &vars);
  }

  if (resource_uri)
    num_vars = cupsAddOption("resource", resource_uri, num_vars, &vars);

  if (scopes)
    num_vars = cupsAddOption("scope", scopes, num_vars, &vars);

  if (state)
    num_vars = cupsAddOption("state", state, num_vars, &vars);

  url = cupsFormEncode(authorization_ep, num_vars, vars);

  cupsFreeOptions(num_vars, vars);

  return (url);
}


//
// 'cupsOAuthMakeBase64Random()' - Make a random data string.
//
// This function creates a string containing random data that has been Base64URL
// encoded. "len" specifies the number of random bytes to include in the string.
// The returned string must be freed using the `free` function.
//

char *					// O - Random string
cupsOAuthMakeBase64Random(size_t len)	// I - Number of bytes
{
  size_t	i;			// Looping var
  char		bytes[768],		// Random bytes
		base64url[1025];	// Random string


  // Range check input...
  len = len * 3 / 4;

  if (len < 1)
    len = 1;
  else if (len > sizeof(bytes))
    len = sizeof(bytes);

  // Fill out random bytes and convert it to Base64URL...
  for (i = 0; i < len; i ++)
    bytes[i] = (char)cupsGetRand();

  httpEncode64_3(base64url, sizeof(base64url), bytes, len, /*url*/true);

  // Copy and return the random string...
  return (strdup(base64url));
}


//
// 'cupsOAuthSaveClientData()' - Save client_id and client_secret values.
//
// This function saves the "client_id" and "client_secret" values for the given
// Authorization Server "auth_uri" and redirection URI "redirect_uri". If the
// "client_id" is `NULL` then any saved values are deleted from the per-user
// store.
//

void
cupsOAuthSaveClientData(
    const char *auth_uri,		// I - Authorization Server URI
    const char *redirect_uri,		// I - Redirection URI
    const char *client_id,		// I - client_id or `NULL` to delete
    const char *client_secret)		// I - client_secret value or `NULL` for none
{
  oauth_save_value(auth_uri, redirect_uri, _CUPS_OTYPE_CLIENT_ID, client_id);
  oauth_save_value(auth_uri, redirect_uri, _CUPS_OTYPE_CLIENT_SECRET, client_secret);
}


//
// 'cupsOAuthSaveTokens()' - Save authorization and refresh tokens.
//
// This function saves the access token "access_token", user ID "user_id", and
// refresh token "refresh_token" values for the given Authorization Server
// "auth_uri" and resource "resource_uri". Specifying `NULL` for any of the
// values will delete the corresponding saved values from the per-user store.
//

void
cupsOAuthSaveTokens(
    const char *auth_uri,		// I - Authorization Server URI
    const char *resource_uri,		// I - Resource URI
    const char *access_token,		// I - Access token or `NULL` to delete
    time_t     access_expires,		// I - Access expiration time
    const char *user_id,		// I - User ID or `NULL` to delete
    const char *refresh_token)		// I - Refresh token or `NULL` to delete
{
  char		temp[16384];		// Temporary string


  // Access token...
  if (access_token)
  {
    // Save access token...
    snprintf(temp, sizeof(temp), "%s\n%ld\n", access_token, (long)access_expires);
    oauth_save_value(auth_uri, resource_uri, _CUPS_OTYPE_ACCESS, temp);
  }
  else
  {
    // Remove access token
    oauth_save_value(auth_uri, resource_uri, _CUPS_OTYPE_ACCESS, NULL);
  }

  // User ID...
  oauth_save_value(auth_uri, resource_uri, _CUPS_OTYPE_USER_ID, user_id);

  // Refresh token...
  oauth_save_value(auth_uri, resource_uri, _CUPS_OTYPE_REFRESH, refresh_token);
}


//
// 'oauth_copy_response()' - Copy the response from a HTTP response.
//

static char *				// O - Response as a string
oauth_copy_response(http_t *http)	// I - HTTP connection
{
  char		*body,			// Message body data string
		*end,			// End of data
		*ptr;			// Pointer into string
  size_t	bodylen;		// Allocated length of string
  ssize_t	bytes;			// Bytes read
  http_state_t	initial_state;		// Initial HTTP state


  // Allocate memory for string...
  initial_state = httpGetState(http);

  if ((bodylen = (size_t)httpGetLength(http)) == 0 || bodylen > 65536)
    bodylen = 65536;			// Accept up to 64k for GETs/POSTs

  if ((body = calloc(1, bodylen + 1)) != NULL)
  {
    for (ptr = body, end = body + bodylen; ptr < end; ptr += bytes)
    {
      if ((bytes = httpRead(http, ptr, (size_t)(end - ptr))) <= 0)
        break;
    }
  }

  if (httpGetState(http) == initial_state)
    httpFlush(http);

  return (body);
}


//
// 'oauth_do_post()' - Send a POST request with the specified data and do error
//                     handling, returning JSON when available.
//

static cups_json_t *			// O - JSON response
oauth_do_post(const char *ep,		// I - Endpoint URI
              const char *content_type,	// I - Content type
              const char *request)	// I - Request message body
{
  http_t	*http;			// Connection to endpoint
  char		host[256],		// Endpoint hostname
		resource[1024];		// Endpoint resource
  int		port;			// Endpoint port
  size_t	req_length;		// Length of request message body
  http_status_t	status;			// POST status
  char		*response;		// Response message body
  cups_json_t	*resp_json = NULL;	// Response JSON
  bool		resp_error;		// Is the response an error?


  DEBUG_printf("3oauth_do_post(ep=\"%s\", content_type=\"%s\", request=\"%s\")", ep, content_type, request);

  // Connect to the endpoint...
  if ((http = httpConnectURI(ep, host, sizeof(host), &port, resource, sizeof(resource), /*blocking*/true, /*msec*/30000, /*cancel*/NULL, /*require_ca*/true)) == NULL)
    return (NULL);

  // Send a POST request with the request data...
  req_length = strlen(request);

  httpClearFields(http);
  httpSetField(http, HTTP_FIELD_ACCEPT, "application/json,text/json");
  httpSetField(http, HTTP_FIELD_CONTENT_TYPE, content_type);
  httpSetLength(http, req_length);

  if (!httpWriteRequest(http, "POST", resource))
  {
    if (!httpConnectAgain(http, 30000, NULL))
      goto done;

    if (!httpWriteRequest(http, "POST", resource))
      goto done;
  }

  if (httpWrite(http, request, req_length) < (ssize_t)req_length)
    goto done;

  // Get the response...
  while ((status = httpUpdate(http)) == HTTP_STATUS_CONTINUE);

  response  = oauth_copy_response(http);
  resp_json = cupsJSONImportString(response);

  free(response);

  // Check for errors...
  resp_error = oauth_set_error(resp_json, /*num_form*/0, /*form*/NULL);
  if (!resp_error && status != HTTP_STATUS_OK)
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, httpStatusString(status), false);
    resp_error = true;
  }

  if (resp_error)
  {
    cupsJSONDelete(resp_json);
    resp_json = NULL;
  }

  // Close the HTTP connection and return any JSON we have...
  done:

  httpClose(http);

  return (resp_json);
}


//
// 'oauth_get_jwks()' - Get the JWT key set for an Authorization Server.
//

static cups_json_t *			// O - JWKS or `NULL` on error
oauth_get_jwks(const char  *auth_uri,	// I - Authorization server URI
               cups_json_t *metadata)	// I - Server metadata
{
  const char	*jwks_uri;		// URI of key set
  cups_json_t	*jwks;			// JWT key set
  char		filename[1024];		// Local metadata filename
  struct stat	fileinfo;		// Local metadata file info


  DEBUG_printf("oauth_get_jwks(auth_uri=\"%s\", metadata=%p)", auth_uri, (void *)metadata);

  // Get existing key set...
  if (!oauth_make_path(filename, sizeof(filename), auth_uri, /*secondary_uri*/NULL, _CUPS_OTYPE_JWKS))
    return (NULL);

  if (stat(filename, &fileinfo))
    memset(&fileinfo, 0, sizeof(fileinfo));

  // Don't bother connecting if the key set was updated recently...
  if ((time(NULL) - fileinfo.st_mtime) <= 60)
    return (cupsJSONImportFile(filename));

  // Try getting the key set...
  if ((jwks_uri = cupsJSONGetString(cupsJSONFind(metadata, "jwks_uri"))) == NULL)
    return (NULL);

  if ((jwks = cupsJSONImportURL(jwks_uri, &fileinfo.st_mtime)) != NULL)
  {
    // Save the key set...
    char *s = cupsJSONExportString(jwks);
					// JSON string

    oauth_save_value(auth_uri, /*secondary_uri*/NULL, _CUPS_OTYPE_JWKS, s);
    free(s);
  }

  // Return what we got...
  return (jwks);
}


//
// 'oauth_load_value()' - Load the contents of the specified value file.
//

static char *
oauth_load_value(
    const char    *auth_uri,		// I - Authorization Server URI
    const char    *secondary_uri,	// I - Resource or redirect URI
    _cups_otype_t otype)		// I - Type (_CUPS_OTYPE_xxx)
{
  char		filename[1024];		// Filename
  struct stat	fileinfo;		// File information
  int		fd;			// File descriptor
  char		*value = NULL;		// Value


  DEBUG_printf("3oauth_load_value(auth_uri=\"%s\", secondary_uri=\"%s\", otype=%s)", auth_uri, secondary_uri, cups_otypes[otype]);

  // Try to make the corresponding file path...
  if (!oauth_make_path(filename, sizeof(filename), auth_uri, secondary_uri, otype))
    return (NULL);

  // Open the file...
  if ((fd = open(filename, O_RDONLY)) >= 0)
  {
    // Opened, read up to 64k of data...
    if (!fstat(fd, &fileinfo) && fileinfo.st_size <= 65536 && (value = calloc(1, (size_t)fileinfo.st_size + 1)) != NULL)
      read(fd, value, (size_t)fileinfo.st_size);
    else
      _cupsSetError(IPP_STATUS_ERROR_INTERNAL, strerror(errno), false);

    close(fd);
  }
  else
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, strerror(errno), false);
  }

  // Return whatever we have...
  DEBUG_printf("4oauth_load_value: Returning \"%s\".", value);

  return (value);
}


//
// 'oauth_make_path()' - Make an OAuth store filename.
//

static char *				// O - Filename
oauth_make_path(
    char          *buffer,		// I - Filename buffer
    size_t        bufsize,		// I - Size of filename buffer
    const char    *auth_uri,		// I - Authorization server URI
    const char    *secondary_uri,	// I - Resource/redirect URI
    _cups_otype_t otype)		// I - Type (_CUPS_OTYPE_xxx)
{
  char		auth_temp[1024],	// Temporary copy of auth_uri
		secondary_temp[1024],	// Temporary copy of secondary_uri
		*ptr;			// Pointer into temporary strings
  unsigned char	auth_hash[32],		// SHA-256 hash of base auth_uri
		secondary_hash[32];	// SHA-256 hash of base secondary_uri
  _cups_globals_t *cg = _cupsGlobals();	// Global data
  static const char * const otypes[] =	// Filename extensions for each type
  {
    "accs",				// Access token
    "clid",				// Client ID
    "csec",				// Client secret
    "cver",				// Code verifier
    "idtk",				// ID token
    "jwks",				// Key store
    "meta",				// Metadata
    "nonc",				// Nonce
    "ruri",				// Redirect URI
    "rfsh"				// Refresh token
  };


  DEBUG_printf("3oauth_make_path(buffer=%p, bufsize=%lu, auth_uri=\"%s\", secondary_uri=\"%s\", otype=%s)", (void *)buffer, (unsigned long)bufsize, auth_uri, secondary_uri, cups_otypes[otype]);

  // Range check input...
  if (!auth_uri || strncmp(auth_uri, "https://", 8) || auth_uri[8] == '[' || isdigit(auth_uri[8] & 255) || (secondary_uri && strncmp(secondary_uri, "http://", 7) && strncmp(secondary_uri, "https://", 8) && strncmp(secondary_uri, "ipps://", 7)))
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, strerror(EINVAL), false);
    *buffer = '\0';
    return (NULL);
  }

  // First make sure the "oauth" directory exists...
  snprintf(buffer, bufsize, "%s/oauth", cg->userconfig);
  if (!_cupsDirCreate(buffer, 0700))
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, strerror(errno), false);
    *buffer = '\0';
    return (NULL);
  }

  // Build the hashed versions of the auth and resource URIs...
  cupsCopyString(auth_temp, auth_uri + 8, sizeof(auth_temp));
  if ((ptr = strchr(auth_temp, '/')) != NULL)
    *ptr = '\0';			// Strip resource path
  if (!strchr(auth_temp, ':'))		// Add :443 if no port is present
    cupsConcatString(auth_temp, ":443", sizeof(auth_temp));

  cupsHashData("sha2-256", auth_temp, strlen(auth_temp), auth_hash, sizeof(auth_hash));
  cupsHashString(auth_hash, sizeof(auth_hash), auth_temp, sizeof(auth_temp));

  if (secondary_uri)
  {
    if (!strncmp(secondary_uri, "http://", 7))
    {
      // HTTP URI
      cupsCopyString(secondary_temp, secondary_uri + 7, sizeof(secondary_temp));
      if ((ptr = strchr(secondary_temp, '/')) != NULL)
        *ptr = '\0';			// Strip resource path
      if (!strchr(secondary_temp, ':'))	// Add :80 if no port is present
        cupsConcatString(secondary_temp, ":80", sizeof(secondary_temp));
    }
    else if (!strncmp(secondary_uri, "https://", 8))
    {
      // HTTPS URI
      cupsCopyString(secondary_temp, secondary_uri + 8, sizeof(secondary_temp));
      if ((ptr = strchr(secondary_temp, '/')) != NULL)
        *ptr = '\0';			// Strip resource path
      if (!strchr(secondary_temp, ':'))	// Add :443 if no port is present
        cupsConcatString(secondary_temp, ":443", sizeof(secondary_temp));
    }
    else
    {
      // IPPS URI
      cupsCopyString(secondary_temp, secondary_uri + 7, sizeof(secondary_temp));
      if ((ptr = strchr(secondary_temp, '/')) != NULL)
        *ptr = '\0';			// Strip resource path
      if (!strchr(secondary_temp, ':'))	// Add :631 if no port is present
        cupsConcatString(secondary_temp, ":631", sizeof(secondary_temp));
    }

    cupsHashData("sha2-256", secondary_temp, strlen(secondary_temp), secondary_hash, sizeof(secondary_hash));
    cupsHashString(secondary_hash, sizeof(secondary_hash), secondary_temp, sizeof(secondary_temp));
  }
  else
  {
    // Leave an empty string for the resource portion
    secondary_temp[0] = '\0';
  }

  // Build the filename for the corresponding data...
  if (secondary_temp[0])
    snprintf(buffer, bufsize, "%s/oauth/%s+%s.%s", cg->userconfig, auth_temp, secondary_temp, otypes[otype]);
  else
    snprintf(buffer, bufsize, "%s/oauth/%s.%s", cg->userconfig, auth_temp, otypes[otype]);

  DEBUG_printf("4oauth_make_path: Returning \"%s\".", buffer);

  return (buffer);
}


//
// 'oauth_make_software_id()' - Make the software_id UUID.
//
// The CUPS OAuth software_id is a format 8 (custom) UUID as defined in RFC 9562
// (replaces RFC 4122).  A certain amount of the UUID is "vanity" (RFC 8010 and
// RFC 8011 define the core IPP standard) with "CUPS" and "OAuth" in the UUID
// bytes as well, but this will be as unique as a regular random UUID will be.
//
// (Has the advantage of being easily identified, too...)
//
// For CUPS 3.0.x:
//
//   43555053-0300-8010-8011-4F4175746820
//

static char *				// O - UUID string
oauth_make_software_id(char   *buffer,	// I - UUID buffer
                       size_t bufsize)	// I - Size of UUID buffer
{
  unsigned char	uuid[16];		// UUID bytes


  uuid[ 0] = 'C';			// "CUPS"
  uuid[ 1] = 'U';
  uuid[ 2] = 'P';
  uuid[ 3] = 'S';
  uuid[ 4] = CUPS_VERSION_MAJOR;	// CUPS major.minor
  uuid[ 5] = CUPS_VERSION_MINOR;
  uuid[ 6] = 0x80;			// Custom UUID format 8
  uuid[ 7] = 0x10;			// "8010" for RFC 8010
  uuid[ 8] = 0x80;			// Variant 8
  uuid[ 9] = 0x11;			// "8011" for RFC 8011
  uuid[10] = 'O';			// "OAuth"
  uuid[11] = 'A';
  uuid[12] = 'u';
  uuid[13] = 't';
  uuid[14] = 'h';
  uuid[15] = 0x20;			// 2.0

  snprintf(buffer, bufsize, "%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X", uuid[0], uuid[1], uuid[2], uuid[3], uuid[4], uuid[5], uuid[6], uuid[7], uuid[8], uuid[9], uuid[10], uuid[11], uuid[12], uuid[13], uuid[14], uuid[15]);

  return (buffer);
}


//
// 'oauth_metadata_contains()' - Determine whether a metadata parameter contains the specified value.
//

static bool				// O - `true` if present, `false` otherwise
oauth_metadata_contains(
    cups_json_t *metadata,		// I - Authorization server metadata
    const char  *parameter,		// I - Metadata parameter
    const char  *value)			// I - Parameter value
{
  size_t	i,			// Looping var
		count;			// Number of values
  cups_json_t	*values,		// Parameter values
		*current;		// Current value


  DEBUG_printf("3oauth_metadata_contains(metadata=%p, parameter=\"%s\", value=\"%s\")", (void *)metadata, parameter, value);

  if ((values = cupsJSONFind(metadata, parameter)) == NULL)
  {
    DEBUG_puts("4oauth_metadata: Returning false.");
    return (false);
  }

  for (i = 0, count = cupsJSONGetCount(values); i < count; i ++)
  {
    current = cupsJSONGetChild(values, i);

    if (cupsJSONGetType(current) == CUPS_JTYPE_STRING && !strcmp(value, cupsJSONGetString(current)))
      return (true);
  }

  return (false);
}


//
// 'oauth_save_value()' - Save a value string to the OAuth store.
//

static void
oauth_save_value(
    const char    *auth_uri,		// I - Authorization Server URI
    const char    *secondary_uri,	// I - Resource or redirect URI
    _cups_otype_t otype,		// I - Type (_CUPS_OTYPE_xxx)
    const char    *value)		// I - Value string or `NULL` to remove
{
  char	filename[1024];			// Filename
  int	fd;				// File descriptor


  DEBUG_printf("3oauth_save_value(auth_uri=\"%s\", secondary_uri=\"%s\", otype=%s, value=\"%s\")", auth_uri, secondary_uri, cups_otypes[otype], value);

  // Try making the filename...
  if (!oauth_make_path(filename, sizeof(filename), auth_uri, secondary_uri, otype))
    return;

  if (value)
  {
    // Create the file...
    if ((fd = open(filename, O_CREAT | O_WRONLY | O_TRUNC | O_NOFOLLOW, 0600)) >= 0)
    {
      // Write the value and close...
      write(fd, value, strlen(value));
      close(fd);
    }
  }
  else
  {
    // Remove the file...
    unlink(filename);
  }
}


//
// 'oauth_set_error()' - Set the OAuth error message from a JSON response.
//

static bool				// O - `true` if there was an error, `false` otherwise
oauth_set_error(cups_json_t   *json,	// I - JSON response
                size_t        num_form,	// I - Number of form variables
                cups_option_t *form)	// I - Form variables
{
  const char	*error,			// error value
		*error_desc;		// error_description value


  if (json)
  {
    error      = cupsJSONGetString(cupsJSONFind(json, "error"));
    error_desc = cupsJSONGetString(cupsJSONFind(json, "error_description"));
  }
  else
  {
    error      = cupsGetOption("error", num_form, form);
    error_desc = cupsGetOption("error_description", num_form, form);
  }

  if (error)
  {
    if (error_desc)
    {
      char	message[1024];		// Message string

      snprintf(message, sizeof(message), "%s: %s", error, error_desc);
      _cupsSetError(IPP_STATUS_ERROR_CUPS_OAUTH, message, false);
    }
    else
    {
      _cupsSetError(IPP_STATUS_ERROR_CUPS_OAUTH, error, false);
    }

    return (true);
  }

  return (false);
}
