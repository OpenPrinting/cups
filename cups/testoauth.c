//
// OAuth API unit tests for CUPS.
//
// Copyright © 2024 by OpenPrinting.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//
// Usage: testoauth [-a OAUTH-URI] [-r REDIRECT-URI] [-s SCOPE(S)] [COMMAND [ARGUMENT(S)]]
//
// Commands:
//
//   authorize RESOURCE-URI
//   clear RESOURCE-URI
//   get-access-token RESOURCE-URI
//   get-client-id
//   get-metadata
//   get-refresh-token RESOURCE-URI
//   get-user-id RESOURCE-URI
//   set-client-data CLIENT-ID CLIENT-SECRET
//   test
//

#include "cups.h"
#include "oauth.h"
#include "test-internal.h"


//
// Local constants...
//

#define TEST_OAUTH_URI	"https://samples.auth0.com"


//
// Local functions...
//

static int	authorize(const char *oauth_uri, const char *scopes, const char *resource_uri, const char *redirect_uri);
static int	clear(const char *oauth_uri, const char *resource_uri);
static int	get_access_token(const char *oauth_uri, const char *resource_uri);
static int	get_client_id(const char *oauth_uri, const char *redirect_uri);
static int	get_metadata(const char *oauth_uri);
static int	get_refresh_token(const char *oauth_uri, const char *resource_uri);
static int	get_user_id(const char *oauth_uri, const char *resource_uri);
static int	set_client_data(const char *oauth_uri, const char *redirect_uri, const char *client_id, const char *client_secret);
static int	unit_tests(const char *oauth_uri, const char *redirect_uri);
static int	usage(FILE *out);


//
// 'main()' - Main entry.
//

int					// O - Exit status
main(int  argc,				// I - Number of command-line arguments
     char *argv[])			// I - Command-line arguments
{
  int		i;			// Looping var
  const char	*opt,			// Current option
		*oauth_uri = NULL,	// OAuth authorization server URI
		*command = NULL,	// Command
		*redirect_uri = NULL,	// Redirection URI
		*scopes = NULL;		// Scopes


  // Parse the command-line...
  for (i = 1; i < argc; i ++)
  {
    if (!strcmp(argv[i], "--help"))
    {
      return (usage(stdout));
    }
    else if (argv[i][0] == '-' && argv[i][1] != '-')
    {
      for (opt = argv[i] + 1; *opt; opt ++)
      {
        switch (*opt)
        {
          case 'a' : // -a AUTH-URI
              i ++;
              if (i >= argc)
              {
                fputs("testoauth: Missing Authorization Server URI after '-a'.\n", stderr);
                return (usage(stderr));
              }

              oauth_uri = argv[i];
              break;

          case 'r' : // -r REDIRECT-URI
              i ++;
              if (i >= argc)
              {
                fputs("testoauth: Missing redirect URI after '-r'.\n", stderr);
                return (usage(stderr));
              }

              redirect_uri = argv[i];
              break;

          case 's' : // -s SCOPE(S)
              i ++;
              if (i >= argc)
              {
                fputs("testoauth: Missing scope(s) after '-s'.\n", stderr);
                return (usage(stderr));
              }

              scopes = argv[i];
              break;

          default :
              fprintf(stderr, "testoauth: Unknown option '-%c'.\n", *opt);
              return (usage(stderr));
        }
      }
    }
    else if (strncmp(argv[i], "--", 2) && !command)
    {
      command = argv[i];
      i ++;
      break;
    }
    else
    {
      fprintf(stderr, "testoauth: Unknown option '%s'.\n", argv[i]);
      return (usage(stderr));
    }
  }

  // Apply defaults...
  if (!command)
    command = "test";

  if (!oauth_uri)
    oauth_uri = TEST_OAUTH_URI;

  // Do commands...
  if (!strcmp(command, "authorize"))
  {
    if (i >= argc)
    {
      fputs("testoauth: Missing resource URI.\n", stderr);
      return (usage(stderr));
    }

    return (authorize(oauth_uri, scopes, argv[i], redirect_uri));
  }
  else if (!strcmp(command, "clear"))
  {
    if (i >= argc)
    {
      fputs("testoauth: Missing resource URI.\n", stderr);
      return (usage(stderr));
    }

    return (clear(oauth_uri, argv[i]));
  }
  else if (!strcmp(command, "get-access-token"))
  {
    if (i >= argc)
    {
      fputs("testoauth: Missing resource URI.\n", stderr);
      return (usage(stderr));
    }

    return (get_access_token(oauth_uri, argv[i]));
  }
  else if (!strcmp(command, "get-client-id"))
  {
    return (get_client_id(oauth_uri, redirect_uri));
  }
  else if (!strcmp(command, "get-metadata"))
  {
    return (get_metadata(oauth_uri));
  }
  else if (!strcmp(command, "get-refresh-token"))
  {
    if (i >= argc)
    {
      fputs("testoauth: Missing resource URI.\n", stderr);
      return (usage(stderr));
    }

    return (get_refresh_token(oauth_uri, argv[i]));
  }
  else if (!strcmp(command, "get-user-id"))
  {
    return (get_user_id(oauth_uri, argv[i]));
  }
  else if (!strcmp(command, "set-client-data"))
  {
    if ((i + 1) >= argc)
    {
      fputs("testoauth: Missing client_id and/or client_secret.\n", stderr);
      return (usage(stderr));
    }

    return (set_client_data(oauth_uri, redirect_uri, argv[i], argv[i + 1]));
  }
  else if (!strcmp(command, "test"))
  {
    return (unit_tests(oauth_uri, redirect_uri));
  }
  else
  {
    fprintf(stderr, "testoauth: Unknown command '%s'.\n", command);
    return (usage(stderr));
  }
}


//
// 'authorize()' - Authorize access.
//

static int				// O - Exit status
authorize(const char *oauth_uri,	// I - Authorization Server URI
          const char *scopes,		// I - Scope(s)
          const char *resource_uri,	// I - Resource URI
          const char *redirect_uri)	// I - Redirect URI
{
  (void)oauth_uri;
  (void)scopes;
  (void)resource_uri;
  (void)redirect_uri;

  return (1);
}


//
// 'clear()' - Clear authorization information.
//

static int				// O - Exit status
clear(const char *oauth_uri,		// I - Authorization Server URI
      const char *resource_uri)		// I - Resource URI
{
  (void)oauth_uri;
  (void)resource_uri;

  return (1);
}


//
// 'get_access_token()' - Get an access token.
//

static int				// O - Exit status
get_access_token(
    const char *oauth_uri,		// I - Authorization Server URI
    const char *resource_uri)		// I - Resource URI
{
  (void)oauth_uri;
  (void)resource_uri;

  return (1);
}


//
// 'get_client_id()' - Get the client ID value.
//

static int				// O - Exit status
get_client_id(const char *oauth_uri,	// I - Authorization Server URI
              const char *redirect_uri)	// I - Redirection URI
{
  (void)oauth_uri;
  (void)redirect_uri;

  return (1);
}


//
// 'get_metadata()' - Get authorization server metadata.
//

static int				// O - Exit status
get_metadata(const char *oauth_uri)	// I - Authorization Server URI
{
  (void)oauth_uri;
  return (1);
}


//
// 'get_refresh_token()' - Get the resource token.
//

static int				// O - Exit status
get_refresh_token(
    const char *oauth_uri,		// I - Authorization Server URI
    const char *resource_uri)		// I - Resource URI
{
  (void)oauth_uri;
  (void)resource_uri;

  return (1);
}


//
// 'get_user_id()' - Get user identification.
//

static int				// O - Exit status
get_user_id(const char *oauth_uri,	// I - Authorization Server URI
            const char *resource_uri)	// I - Resource URI
{
  cups_jwt_t	*user_id;		// User ID information


  if ((user_id = cupsOAuthCopyUserId(oauth_uri, resource_uri)) != NULL)
  {
    const char	*aud = cupsJWTGetClaimString(user_id, CUPS_JWT_AUD);
					// Audience
    const char	*iss = cupsJWTGetClaimString(user_id, CUPS_JWT_ISS);
					// Issuer
    const char	*jti = cupsJWTGetClaimString(user_id, CUPS_JWT_JTI);
					// JWT ID
    const char	*name = cupsJWTGetClaimString(user_id, CUPS_JWT_NAME);
					// Display name
    const char	*sub = cupsJWTGetClaimString(user_id, CUPS_JWT_SUB);
					// Subject (username/ID)
    double	iat = cupsJWTGetClaimNumber(user_id, CUPS_JWT_IAT);
					// Issue time
    double	exp = cupsJWTGetClaimNumber(user_id, CUPS_JWT_EXP);
					// Expiration time
    double	nbf = cupsJWTGetClaimNumber(user_id, CUPS_JWT_NBF);
					// Not before time
    char	date[256];		// Date

    if (iss)
      printf("Issuer: %s\n", iss);
    if (name)
      printf("Display Name: %s\n", name);
    if (sub)
      printf("Subject: %s\n", sub);
    if (aud)
      printf("Audience: %s\n", aud);
    if (jti)
      printf("JWT ID: %s\n", jti);
    if (iat > 0.0)
      printf("Issued On: %s\n", httpGetDateString2((time_t)iat, date, sizeof(date)));
    if (exp > 0.0)
      printf("Expires On: %s\n", httpGetDateString2((time_t)exp, date, sizeof(date)));
    if (nbf > 0.0)
      printf("Not Before: %s\n", httpGetDateString2((time_t)nbf, date, sizeof(date)));

    return (0);
  }
  else
  {
    return (1);
  }
}


//
// 'set_client_data()' - Save client_id and client_secret values.
//

static int				// O - Exit status
set_client_data(
    const char *oauth_uri,		// I - Authorization Server URI
    const char *redirect_uri,		// I - Redirect URI
    const char *client_id,		// I - Client ID
    const char *client_secret)		// I - Client secret
{
  cupsOAuthSaveClientData(oauth_uri, redirect_uri ? redirect_uri : CUPS_OAUTH_REDIRECT_URI, client_id, client_secret);

  return (0);
}


//
// 'unit_tests()' - Run unit tests.
//

static int				// O - Exit status
unit_tests(const char *oauth_uri,	// I - Authorization Server URI
           const char *redirect_uri)	// I - Redirection URI
{
  cups_json_t	*metadata;		// Server metadata
  char		*auth_code = NULL,	// Authorization code
 		*access_token = NULL,	// Access token
 		*refresh_token = NULL;	// Refresh token
  cups_jwt_t	*user_id = NULL;	// User identification
  time_t	access_expires;		// Expiration data of access token


  // Get metadata...
  testBegin("cupsOAuthGetMetadata(%s)", oauth_uri);
  if ((metadata = cupsOAuthGetMetadata(oauth_uri)) != NULL)
  {
    testEnd(true);
  }
  else
  {
    testEndMessage(false, "%s", cupsGetErrorString());
    goto done;
  }

  // Authorize...
  testBegin("cupsOAuthGetAuthorizationCode(%s)", oauth_uri);
  if ((auth_code = cupsOAuthGetAuthorizationCode(oauth_uri, metadata, /*resource_uri*/NULL, "openid email profile", redirect_uri)) != NULL)
  {
    testEndMessage(true, "%s", auth_code);
  }
  else
  {
    testEndMessage(false, "%s", cupsGetErrorString());
    goto done;
  }

  // Get the access token...
  testBegin("cupsOAuthGetTokens(%s)", oauth_uri);
  if ((access_token = cupsOAuthGetTokens(oauth_uri, metadata, /*resource_uri*/NULL, auth_code, CUPS_OGRANT_AUTHORIZATION_CODE, CUPS_OAUTH_REDIRECT_URI, &access_expires)) != NULL)
  {
    testEndMessage(true, "%s, expires in %ld seconds", access_token, (long)(access_expires - time(NULL)));
  }
  else
  {
    testEndMessage(false, "%s", cupsGetErrorString());
    goto done;
  }

  // Get the refresh token...
  testBegin("cupsOAuthCopyRefreshToken(%s)", oauth_uri);
  if ((refresh_token = cupsOAuthCopyRefreshToken(oauth_uri, /*resource_uri*/NULL)) != NULL)
  {
    testEndMessage(true, "%s", refresh_token);
  }
  else
  {
    testEndMessage(false, "%s", cupsGetErrorString());
    goto done;
  }

  // Get the user identifications...
  testBegin("cupsOAuthCopyUserId(%s)", oauth_uri);
  if ((user_id = cupsOAuthCopyUserId(oauth_uri, /*resource_uri*/NULL)) != NULL)
  {
    const char	*iss = cupsJWTGetClaimString(user_id, CUPS_JWT_ISS);
					// Issuer
    const char	*name = cupsJWTGetClaimString(user_id, CUPS_JWT_NAME);
					// Display name
    const char	*sub = cupsJWTGetClaimString(user_id, CUPS_JWT_SUB);
					// Subject (username/ID)
    double	exp = cupsJWTGetClaimNumber(user_id, CUPS_JWT_EXP);
					// Expiration time
    char	expdate[256];		// Expiration date

    testEndMessage(true, "iss=\"%s\", name=\"%s\", sub=\"%s\", exp=%s", iss, name, sub, httpGetDateString2((time_t)exp, expdate, sizeof(expdate)));
  }
  else
  {
    testEndMessage(false, "%s", cupsGetErrorString());
    goto done;
  }

  // Free memory and return...
  done:

  cupsJSONDelete(metadata);
  free(auth_code);
  free(access_token);
  free(refresh_token);
  cupsJWTDelete(user_id);

  return (testsPassed ? 0 : 1);
}


//
// 'usage()' - Show usage.
//

static int				// O - Exit status
usage(FILE *out)			// I - Output file
{
  fputs("Usage: testoauth [-a OAUTH-URI] [-r REDIRECT-URI] [-s SCOPE(S)] [COMMAND [ARGUMENT(S)]]\n", out);
  fputs("Commands:\n", out);
  fputs("  authorize RESOURCE-URI\n", out);
  fputs("  clear RESOURCE-URI\n", out);
  fputs("  get-access-token RESOURCE-URI\n", out);
  fputs("  get-client-id\n", out);
  fputs("  get-metadata\n", out);
  fputs("  get-refresh-token RESOURCE-URI\n", out);
  fputs("  get-user-id RESOURCE-URI\n", out);
  fputs("  set-client-data CLIENT-ID CLIENT-SECRET\n", out);
  fputs("  test\n", out);

  return (out == stdout ? 0 : 1);
}
