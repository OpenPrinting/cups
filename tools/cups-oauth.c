//
// OAuth utility for CUPS.
//
// Copyright © 2024-2025 by OpenPrinting.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//
// Usage: cups-oauth [OPTIONS] [COMMAND [ARGUMENT(S)]]
//
// Commands:
//
//   authorize [RESOURCE]
//   clear [RESOURCE]
//   get-access-token [RESOURCE]
//   get-client-id
//   get-metadata [NAME]
//   get-refresh-token [RESOURCE]
//   get-user-id [RESOURCE] [NAME]
//   set-access-token [RESOURCE] TOKEN
//   set-client-data CLIENT-ID CLIENT-SECRET
//
// Options:
//
//   --help
//   --version
//   -a OAUTH-URI
//   -s SCOPE(S)
//

#include <cups/cups-private.h>
#include <cups/oauth.h>


//
// Macro for localized text...
//

#  define _(x) x


//
// Local functions...
//

static int	do_authorize(const char *oauth_uri, const char *scopes, const char *resource_uri);
static int	do_clear(const char *oauth_uri, const char *resource_uri);
static int	do_get_access_token(const char *oauth_uri, const char *resource_uri);
static int	do_get_client_id(const char *oauth_uri);
static int	do_get_metadata(const char *oauth_uri, const char *name);
static int	do_get_user_id(const char *oauth_uri, const char *resource_uri, const char *name);
static int	do_set_access_token(const char *oauth_uri, const char *resource_uri, const char *token);
static int	do_set_client_data(const char *oauth_uri, const char *client_id, const char *client_secret);
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
		*oauth_uri = getenv("CUPS_OAUTH_URI"),
					// OAuth authorization server URI
		*scopes = getenv("CUPS_OAUTH_SCOPES");
					// Scopes


  // Parse the command-line...
  for (i = 1; i < argc; i ++)
  {
    if (!strcmp(argv[i], "--help"))
    {
      return (usage(stdout));
    }
    else if (!strcmp(argv[i], "--version"))
    {
      puts(CUPS_SVERSION);
      exit(0);
    }
    else if (!strncmp(argv[i], "--", 2))
    {
      _cupsLangPrintf(stderr, _("%s: Unknown option '%s'."), "cups-oauth", argv[i]);
      return (usage(stderr));
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
                _cupsLangPuts(stderr, _("cups-oauth: Missing Authorization Server URI after '-a'."));
                return (usage(stderr));
              }

              oauth_uri = argv[i];
              break;

          case 's' : // -s SCOPE(S)
              i ++;
              if (i >= argc)
              {
                _cupsLangPuts(stderr, _("cups-oauth: Missing scope(s) after '-s'."));
                return (usage(stderr));
              }

              scopes = argv[i];
              break;

          default :
              _cupsLangPrintf(stderr, _("%s: Unknown option '-%c'."), "cups-oauth", *opt);
              return (usage(stderr));
        }
      }
    }
    else if (!oauth_uri)
    {
      _cupsLangPuts(stderr, _("cups-oauth: No authorization server specified."));
      return (usage(stderr));
    }
    else if (!strcmp(argv[i], "authorize"))
    {
      // authorize [RESOURCE]
      i ++;
      return (do_authorize(oauth_uri, scopes, argv[i]));
    }
    else if (!strcmp(argv[i], "clear"))
    {
      // clear [RESOURCE]
      i ++;
      return (do_clear(oauth_uri, argv[i]));
    }
    else if (!strcmp(argv[i], "get-access-token"))
    {
      // get-access-token [RESOURCE]
      i ++;
      return (do_get_access_token(oauth_uri, argv[i]));
    }
    else if (!strcmp(argv[i], "get-client-id"))
    {
      // get-client-id
      i ++;
      return (do_get_client_id(oauth_uri));
    }
    else if (!strcmp(argv[i], "get-metadata"))
    {
      // get-metadata [NAME]
      i ++;
      return (do_get_metadata(oauth_uri, argv[i]));
    }
    else if (!strcmp(argv[i], "get-user-id"))
    {
      // get-user-id [RESOURCE] [NAME]
      i ++;
      if (i < argc)
      {
        if (!strncmp(argv[i], "ipp://", 6) || !strncmp(argv[i], "ipps://", 7) || !strncmp(argv[i], "http://", 7) || !strncmp(argv[i], "https://", 8))
	  return (do_get_user_id(oauth_uri, argv[i], argv[i + 1]));
        else
	  return (do_get_user_id(oauth_uri, /*resource_uri*/NULL, argv[i]));
      }
      else
      {
        return (do_get_user_id(oauth_uri, /*resource_uri*/NULL, /*name*/NULL));
      }
    }
    else if (!strcmp(argv[i], "set-access-token"))
    {
      // set-access-token [RESOURCE] TOKEN
      i ++;
      if (i >= argc)
      {
	_cupsLangPuts(stderr, _("cups-oauth: Missing resource URI and/or access token."));
	return (usage(stderr));
      }

      return (do_set_access_token(oauth_uri, argv[i], argv[i + 1]));
    }
    else if (!strcmp(argv[i], "set-client-data"))
    {
      // set-client-data CLIENT-ID CLIENT-DATA
      i ++;
      if ((i + 1) >= argc)
      {
	_cupsLangPuts(stderr, _("cups-oauth: Missing client_id and/or client_secret."));
	return (usage(stderr));
      }

      return (do_set_client_data(oauth_uri, argv[i], argv[i + 1]));
    }
    else
    {
      _cupsLangPrintf(stderr, _("cups-oauth: Unknown command '%s'."), argv[i]);
      return (usage(stderr));
    }
  }

  // If we get this far, show usage...
  return (usage(argc == 1 ? stdout : stderr));
}


//
// 'do_authorize()' - Authorize access.
//

static int				// O - Exit status
do_authorize(const char *oauth_uri,	// I - Authorization Server URI
             const char *scopes,	// I - Scope(s)
             const char *resource_uri)	// I - Resource URI
{
  int		status = 1;		// Exit status
  cups_json_t	*metadata;		// Server metadata
  char		*auth_code = NULL,	// Authorization code
		*access_token = NULL;	// Access token
  time_t	access_expires;		// Expiration date


  // Get the server metadata...
  if ((metadata = cupsOAuthGetMetadata(oauth_uri)) == NULL)
  {
    _cupsLangPrintf(stderr, _("cups-oauth: Unable to get metadata for '%s': %s"), oauth_uri, cupsGetErrorString());
    return (1);
  }

  // Authorize...
  if ((auth_code = cupsOAuthGetAuthorizationCode(oauth_uri, metadata, resource_uri, scopes, /*redirect_uri*/NULL)) == NULL)
  {
    _cupsLangPrintf(stderr, _("cups-oauth: Unable to get authorization from '%s': %s"), oauth_uri, cupsGetErrorString());
    goto done;
  }

  // Get the access token...
  if ((access_token = cupsOAuthGetTokens(oauth_uri, metadata, resource_uri, auth_code, CUPS_OGRANT_AUTHORIZATION_CODE, CUPS_OAUTH_REDIRECT_URI, &access_expires)) == NULL)
  {
    _cupsLangPrintf(stderr, _("cups-oauth: Unable to get access token from '%s': %s"), oauth_uri, cupsGetErrorString());
    goto done;
  }

  // Show access token
  puts(access_token);

  status = 0;

  // Clean up and return...
  done:

  cupsJSONDelete(metadata);
  free(auth_code);
  free(access_token);

  return (status);
}


//
// 'do_clear()' - Clear authorization information.
//

static int				// O - Exit status
do_clear(const char *oauth_uri,		// I - Authorization Server URI
         const char *resource_uri)	// I - Resource URI
{
  cupsOAuthClearTokens(oauth_uri, resource_uri);

  return (0);
}


//
// 'do_get_access_token()' - Get an access token.
//

static int				// O - Exit status
do_get_access_token(
    const char *oauth_uri,		// I - Authorization Server URI
    const char *resource_uri)		// I - Resource URI
{
  char		*access_token;		// Access token
  time_t	access_expires;		// Expiration date


  if ((access_token = cupsOAuthCopyAccessToken(oauth_uri, resource_uri, &access_expires)) != NULL)
  {
    puts(access_token);
    free(access_token);
    return (0);
  }

  return (1);
}


//
// 'do_get_client_id()' - Get the client ID value.
//

static int				// O - Exit status
do_get_client_id(
    const char *oauth_uri)		// I - Authorization Server URI
{
  char	*client_id;			// Client ID


  if ((client_id = cupsOAuthCopyClientId(oauth_uri, CUPS_OAUTH_REDIRECT_URI)) != NULL)
  {
    puts(client_id);
    free(client_id);
    return (0);
  }

  return (1);
}


//
// 'do_get_metadata()' - Get authorization server metadata.
//

static int				// O - Exit status
do_get_metadata(const char *oauth_uri,	// I - Authorization Server URI
                const char *name)	// I - Field name
{
  cups_json_t	*metadata;		// Metadata
  char		*json;			// JSON string


  // Get the metadata...
  if ((metadata = cupsOAuthGetMetadata(oauth_uri)) == NULL)
  {
    _cupsLangPrintf(stderr, _("cups-oauth: Unable to get metadata for '%s': %s"), oauth_uri, cupsGetErrorString());
    return (1);
  }

  // Show metadata...
  if (name)
  {
    cups_json_t	*value = cupsJSONFind(metadata, name);
					// Metadata value

    if (value)
    {
      switch (cupsJSONGetType(value))
      {
        case CUPS_JTYPE_NULL :
            puts("null");
            break;

        case CUPS_JTYPE_FALSE :
            puts("false");
            break;

        case CUPS_JTYPE_TRUE :
            puts("true");
            break;

        case CUPS_JTYPE_NUMBER :
            printf("%g\n", cupsJSONGetNumber(value));
            break;

        case CUPS_JTYPE_STRING :
            puts(cupsJSONGetString(value));
            break;

        default :
            if ((json = cupsJSONExportString(value)) != NULL)
            {
              puts(json);
              free(json);
            }
            break;
      }

      return (0);
    }
    else
    {
      return (1);
    }
  }
  else if ((json = cupsJSONExportString(metadata)) != NULL)
  {
    puts(json);
    free(json);
  }

  return (0);
}


//
// 'do_get_user_id()' - Get user identification.
//

static int				// O - Exit status
do_get_user_id(
    const char *oauth_uri,		// I - Authorization Server URI
    const char *resource_uri,		// I - Resource URI
    const char *name)			// I - Claim name
{
  cups_jwt_t	*user_id;		// User ID information
  cups_json_t	*claims;		// Claims
  char		*json,			// JSON string
		date[256];		// Date


  // Get the user_id...
  if ((user_id = cupsOAuthCopyUserId(oauth_uri, resource_uri)) == NULL)
  {
    _cupsLangPrintf(stderr, _("cups-oauth: Unable to get user ID for '%s': %s"), oauth_uri, cupsGetErrorString());
    return (1);
  }

  claims = cupsJWTGetClaims(user_id);

  // Show user information...
  if (name)
  {
    cups_json_t	*value = cupsJSONFind(claims, name);
					// Claim value

    if (value)
    {
      switch (cupsJSONGetType(value))
      {
        case CUPS_JTYPE_NULL :
            puts("null");
            break;

        case CUPS_JTYPE_FALSE :
            puts("false");
            break;

        case CUPS_JTYPE_TRUE :
            puts("true");
            break;

        case CUPS_JTYPE_NUMBER :
            if (!strcmp(name, "exp") || !strcmp(name, "iat") || !strcmp(name, "nbf"))
              puts(httpGetDateString2((time_t)cupsJSONGetNumber(value), date, sizeof(date)));
            else
	      printf("%g\n", cupsJSONGetNumber(value));
            break;

        case CUPS_JTYPE_STRING :
            puts(cupsJSONGetString(value));
            break;

        default :
            if ((json = cupsJSONExportString(value)) != NULL)
            {
              puts(json);
              free(json);
            }
            break;
      }

      return (0);
    }
    else
    {
      return (1);
    }
  }
  else if ((json = cupsJSONExportString(claims)) != NULL)
  {
    puts(json);
    free(json);
  }

  return (0);
}


//
// 'do_set_access_token()' - Set the access token.
//

static int				// O - Exit status
do_set_access_token(
    const char *oauth_uri,		// I - Authorization Server URI
    const char *resource_uri,		// I - Resource URI
    const char *token)			// I - Access token
{
  cupsOAuthSaveTokens(oauth_uri, resource_uri, token, /*access_expires*/time(NULL) + 365 * 86400, /*user_id*/NULL, /*refresh_token*/NULL);

  return (0);
}


//
// 'do_set_client_data()' - Save client_id and client_secret values.
//

static int				// O - Exit status
do_set_client_data(
    const char *oauth_uri,		// I - Authorization Server URI
    const char *client_id,		// I - Client ID
    const char *client_secret)		// I - Client secret
{
  cupsOAuthSaveClientData(oauth_uri, CUPS_OAUTH_REDIRECT_URI, client_id, client_secret);

  return (0);
}


//
// 'usage()' - Show usage.
//

static int				// O - Exit status
usage(FILE *out)			// I - Output file
{
  _cupsLangPuts(out, _("Usage: cups-oauth [OPTIONS] [COMMAND [ARGUMENT(S)]]"));
  _cupsLangPuts(out, "");
  _cupsLangPuts(out, _("Commands:"));
  _cupsLangPuts(out, "");
  _cupsLangPuts(out, _("authorize [RESOURCE]           Authorize access to a resource"));
  _cupsLangPuts(out, _("clear [RESOURCE]               Clear the authorization for a resource"));
  _cupsLangPuts(out, _("get-access-token [RESOURCE]    Get the current access token"));
  _cupsLangPuts(out, _("get-client-id                  Get the client ID for the authorization server"));
  _cupsLangPuts(out, _("get-metadata [NAME]            Get metadata from the authorization server"));
  _cupsLangPuts(out, _("get-user-id [RESOURCE] [NAME]  Get the authorized user ID"));
  _cupsLangPuts(out, _("set-access-token [RESOURCE] TOKEN\n"
                      "                               Set the current access token"));
  _cupsLangPuts(out, _("set-client-data CLIENT-ID CLIENT-SECRET\n"
                      "                               Set the client ID and secret for the authorization server."));
  _cupsLangPuts(out, "");
  _cupsLangPuts(out, _("Options:"));
  _cupsLangPuts(out, "");
  _cupsLangPuts(out, _("--help                         Show this help"));
  _cupsLangPuts(out, _("--version                      Show the program version"));
  _cupsLangPuts(out, _("-a OAUTH-URI                   Specify the OAuth authorization server URL"));
  _cupsLangPuts(out, _("-s SCOPE(S)                    Specify the scope(s) to authorize"));

  return (out == stdout ? 0 : 1);
}
