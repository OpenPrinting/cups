/*
 * Home page CGI for CUPS.
 *
 * Copyright © 2025 by OpenPrinting.
 *
 * Licensed under Apache License v2.0.  See the file "LICENSE" for more
 * information.
 */

#include "cgi-private.h"
#include <cups/oauth.h>
#include <errno.h>


/*
 * Local functions...
 */

static void	do_dashboard(void);
static void	do_login(void);
static void	do_logout(void);
static void	do_redirect(const char *url);
static void	do_search(char *query);
static void	finish_login(void);
static void	show_error(const char *title, const char *message, const char *error);


/*
 * 'main()' - Main entry for CGI.
 */

int					/* O - Exit status */
main(void)
{
  char	*query;				/* Query string, if any */


 /*
  * Get any form variables...
  */

  cgiInitialize();

 /*
  * Set the web interface section...
  */

  cgiSetVariable("SECTION", "home");
  cgiSetVariable("REFRESH_PAGE", "");

 /*
  * Show the home page...
  */

  if ((query = cgiGetVariable("QUERY")) != NULL)
    do_search(query);
  else if (cgiGetSize("LOGIN"))
    do_login();
  else if (cgiGetSize("LOGOUT"))
    do_logout();
  else if (!cgiIsPOST() && (cgiGetSize("code") || cgiGetSize("error")))
    finish_login();
  else
    do_dashboard();

 /*
  * Return with no errors...
  */

  return (0);
}


/*
 * 'do_dashboard()' - Show the home page dashboard...
 */

static void
do_dashboard(void)
{
  // TOOD: Gather alerts

  // Show the home page (dashboard) content...
  cgiStartHTML(cgiText(_("Home")));
  cgiCopyTemplateLang("home.tmpl");
  cgiEndHTML();
}


/*
 * 'do_login()' - Redirect to the OAuth server's authorization endpoint.
 */

static void
do_login(void)
{
  const char	*oauth_uri = getenv("CUPS_OAUTH_SERVER"),
					// OAuth authorization server URL
		*server_name = getenv("SERVER_NAME"),
					// SERVER_NAME value
		*server_port = getenv("SERVER_PORT");
					// SERVER_PORT value
  char		*client_id = NULL,	// Client ID value
		*code_verifier = NULL,	// Code verifier string
		*nonce = NULL,		// Nonce string
		redirect_uri[1024],	// redirect_uri value
		*state = NULL,		// State string
		*url = NULL;		// Authorization URL
  cups_json_t	*metadata = NULL;	// OAuth metadata


  fputs("DEBUG2: do_login()\n", stderr);

  // Get the metadata...
  oauth_uri = getenv("CUPS_OAUTH_SERVER");
  if ((metadata = cupsOAuthGetMetadata(oauth_uri)) == NULL)
  {
    show_error(cgiText(_("OAuth Login")), cgiText(_("Unable to get authorization server information")), cupsGetErrorString());
    goto done;
  }

  // Get the redirect URL...
  if (!strcmp(server_name, "localhost"))
    snprintf(redirect_uri, sizeof(redirect_uri), "http://127.0.0.1:%s/", server_port);
  else
    snprintf(redirect_uri, sizeof(redirect_uri), "%s://%s:%s/", getenv("HTTPS") ? "https" : "http", server_name, server_port);

  fprintf(stderr, "DEBUG2: do_login: redirect_uri=\"%s\"\n", redirect_uri);

  // Get the client ID...
  if ((client_id = cupsOAuthCopyClientId(oauth_uri, redirect_uri)) == NULL)
  {
    // Nothing saved, try to dynamically register one...
    if ((client_id = cupsOAuthGetClientId(oauth_uri, metadata, redirect_uri, /*logo_uri*/NULL, /*tos_uri*/NULL)) == NULL)
    {
      // Nope, show an error...
      show_error(cgiText(_("OAuth Login")), cgiText(_("Unable to get authorization URL")), cgiText(_("No client ID configured for this server.")));
      goto done;
    }
  }

  fprintf(stderr, "DEBUG2: do_login: client_id=\"%s\"\n", client_id);

  // Make state and code verification strings...
  code_verifier = cupsOAuthMakeBase64Random(128);
  nonce         = cupsOAuthMakeBase64Random(16);
  state         = cupsOAuthMakeBase64Random(16);

  // Get the authorization URL
  if ((url = cupsOAuthMakeAuthorizationURL(oauth_uri, metadata, /*resource_uri*/NULL, getenv("CUPS_OAUTH_SCOPES"), client_id, code_verifier, nonce, redirect_uri, state)) == NULL)
  {
    show_error(cgiText(_("OAuth Login")), cgiText(_("Unable to get authorization URL")), cupsGetErrorString());
    goto done;
  }

  // Redirect...
  cgiSetCookie("CUPS_OAUTH_STATE", state, /*path*/NULL, /*domain*/NULL, time(NULL) + 300, /*secure*/0);
  cgiSetCookie("CUPS_REFERRER", getenv("HTTP_REFERER"), /*path*/NULL, /*domain*/NULL, time(NULL) + 300, /*secure*/0);

  do_redirect(url);

  done:

  // Free memory...
  free(client_id);
  free(code_verifier);
  cupsJSONDelete(metadata);
  free(nonce);
  free(state);
  free(url);
}


/*
 * 'do_logout()' - Clear the OAuth bearer token cookie.
 */

static void
do_logout(void)
{
  // Clear the CUPS_BEARER cookie...
  cgiSetCookie("CUPS_BEARER", "", /*path*/NULL, /*domain*/NULL, time(NULL) - 1, /*secure*/0);

  // Redirect back to the referrer...
  do_redirect(getenv("HTTP_REFERER"));
}


/*
 * 'do_redirect()' - Redirect to another web page...
 */

static void
do_redirect(const char *url)		// URL or NULL for home page
{
  fprintf(stderr, "DEBUG2: do_redirect(url=\"%s\")\n", url);

  if (url && (!strncmp(url, "http://", 7) || !strncmp(url, "https://", 8)))
    printf("Location: %s\n\n", url);
  else
    printf("Location: %s://%s:%s%s\n\n", getenv("HTTPS") ? "https" : "http", getenv("SERVER_NAME"), getenv("SERVER_PORT"), url ? url : "/");

  puts("Content-Type: text/plain\n");
  puts("Redirecting...");
}


/*
 * 'do_search()' - Search classes, printers, jobs, and online help.
 */

static void
do_search(char *query)			/* I - Search string */
{
  (void)query;
}


//
// 'finish_login()' - Finish OAuth login and then redirect back to the original page.
//

static void
finish_login(void)
{
  const char	*oauth_uri = getenv("CUPS_OAUTH_SERVER"),
					// OAuth authorization server URL
		*server_name = getenv("SERVER_NAME"),
					// SERVER_NAME value
		*server_port = getenv("SERVER_PORT");
					// SERVER_PORT value
  char		*bearer = NULL,		// Bearer token
		*client_id = NULL,	// Client ID value
		*error,			// Error string
		redirect_uri[1024];	// redirect_uri value
  const char	*code,			// Authorization code
		*state_cookie,		// State cookie
		*state_var;		// State variable
  cups_json_t	*metadata = NULL;	// OAuth metadata
  time_t	access_expires;		// When the bearer token expires


  // Show any error from authorization...
  if ((error = cgiGetVariable("error_description")) == NULL)
    error = cgiGetVariable("error");

  if (error)
  {
    show_error(cgiText(_("OAuth Login")), cgiText(_("Unable to authorize access")), error);
    return;
  }

  // Get the metadata...
  oauth_uri = getenv("CUPS_OAUTH_SERVER");
  if ((metadata = cupsOAuthGetMetadata(oauth_uri)) == NULL)
  {
    show_error(cgiText(_("OAuth Login")), cgiText(_("Unable to get authorization server information")), cupsGetErrorString());
    goto done;
  }

  // Get the redirect URL...
  if (!strcmp(server_name, "localhost"))
    snprintf(redirect_uri, sizeof(redirect_uri), "http://127.0.0.1:%s/", server_port);
  else
    snprintf(redirect_uri, sizeof(redirect_uri), "%s://%s:%s/", getenv("HTTPS") ? "https" : "http", server_name, server_port);

  fprintf(stderr, "DEBUG2: finish_login: redirect_uri=\"%s\"\n", redirect_uri);

  // Get the client ID...
  if ((client_id = cupsOAuthCopyClientId(oauth_uri, redirect_uri)) == NULL)
  {
    // Nope, show an error...
    show_error(cgiText(_("OAuth Login")), cgiText(_("Unable to authorize access")), cgiText(_("No client ID configured for this server.")));
    goto done;
  }

  fprintf(stderr, "DEBUG2: finish_login: client_id=\"%s\"\n", client_id);

  // Get the state and code strings...
  code         = cgiGetVariable("code");
  state_cookie = cgiGetCookie("CUPS_OAUTH_STATE");
  state_var    = cgiGetVariable("state");

  if (!state_cookie || !state_var || strcmp(state_cookie, state_var))
  {
    show_error(cgiText(_("OAuth Login")), cgiText(_("Unable to authorize access")), cgiText(_("Bad client state value in response.")));
    goto done;
  }

  // Get the access token...
  if ((bearer = cupsOAuthGetTokens(oauth_uri, metadata, /*resource_uri*/NULL, code, CUPS_OGRANT_AUTHORIZATION_CODE, redirect_uri, &access_expires)) == NULL)
  {
    show_error(cgiText(_("OAuth Login")), cgiText(_("Unable to authorize access")), cupsGetErrorString());
    goto done;
  }

  // Save it as a cookie...
  cgiSetCookie("CUPS_BEARER", bearer, /*path*/NULL, /*domain*/NULL, access_expires, /*secure*/0);

  // Redirect...
  do_redirect(cgiGetCookie("CUPS_REFERRER"));

  done:

  // Free memory...
  free(bearer);
  free(client_id);
  cupsJSONDelete(metadata);
}


//
// 'show_error()' - Show an error message.
//

static void
show_error(const char *title,		// I - Page title
           const char *message,		// I - Initial message
           const char *error)		// I - Error message
{
  cgiStartHTML(title);

  cgiSetVariable("title", title);
  cgiSetVariable("message", message);
  cgiSetVariable("error", error);
  cgiCopyTemplateLang("error.tmpl");

  cgiEndHTML();
}

