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
 * 'do_login()' - Start or continue the OAuth device authorization flow.
 */

static void
do_login(void)
{
  const char    *oauth_uri = getenv("CUPS_OAUTH_SERVER");
                                        // OAuth authorization server URL
  cups_json_t   *metadata = NULL;      // OAuth metadata
  const char    *devgrant_cookie;      // CUPS_DEVGRANT cookie value, if any
  cups_json_t   *devgrant = NULL;      // Device grant JSON, if any


  fputs("DEBUG2: do_login()\n", stderr);

  // Get the metadata...
  if ((metadata = cupsOAuthGetMetadata(oauth_uri)) == NULL)
  {
    show_error(cgiText(_("OAuth Login")), cgiText(_("Unable to get authorization server information")), cupsGetErrorString());
    goto done;
  }

  // See if we have a pending device grant...
  if ((devgrant_cookie = cgiGetCookie("CUPS_DEVGRANT")) != NULL && devgrant_cookie[0])
    devgrant = cupsJSONImportString(devgrant_cookie);

  if (devgrant)
  {
    const char  *device_code;          // Device code

    if ((device_code = cupsJSONGetString(cupsJSONFind(devgrant, CUPS_ODEVGRANT_DEVICE_CODE))) != NULL)
    {
      char      *bearer;                // Access token
      time_t    access_expires;        // Expiration date

      bearer = cupsOAuthGetTokens(oauth_uri, metadata, NULL, device_code, CUPS_OGRANT_DEVICE_CODE, NULL, &access_expires);

      if (bearer)
      {
        // Got a token - save it, clear the grant cookie, and go home...
        cgiSetCookie("CUPS_DEVGRANT", "", NULL, NULL, time(NULL) - 1, 0);
        cgiSetCookie("CUPS_BEARER", bearer, NULL, NULL, access_expires, getenv("HTTPS") ? 1 : 0);

        free(bearer);

        do_redirect("/");
        goto done;
      }
      else if (access_expires == 0)
      {
        // Hard failure - discard this grant, fall through to request a new one...
        cupsJSONDelete(devgrant);
        devgrant = NULL;
      }
      // else: still pending ("authorization_pending"/"slow_down") - fall
      // through to show the same grant again...
    }
    else
    {
      // Malformed grant JSON - discard and fall through...
      cupsJSONDelete(devgrant);
      devgrant = NULL;
    }
  }

  if (!devgrant)
  {
    char        *temp;                  // JSON string

    if (!cgiIsPOST())
    {
      // No pending grant, and this wasn't a POST (e.g. a stray GET to
      // "/?LOGIN=...") - don't silently start a new authorization request
      // against the OAuth server, just show the dashboard...
      do_dashboard();
      goto done;
    }

    // Request a new device grant...
    if ((devgrant = cupsOAuthGetDeviceGrant(oauth_uri, metadata, NULL, getenv("CUPS_OAUTH_SCOPES"))) == NULL)
    {
      show_error(cgiText(_("OAuth Login")), cgiText(_("Unable to get authorization URL")), cupsGetErrorString());
      goto done;
    }

    if ((temp = cupsJSONExportString(devgrant)) != NULL)
    {
      cgiSetCookie("CUPS_DEVGRANT", temp, NULL, NULL, time(NULL) + (time_t)cupsJSONGetNumber(cupsJSONFind(devgrant, CUPS_ODEVGRANT_EXPIRES_IN)), 0);
      free(temp);
    }
  }

  // Show the authorization page...
  {
    const char  *user_code = cupsJSONGetString(cupsJSONFind(devgrant, CUPS_ODEVGRANT_USER_CODE));
    const char  *verification_uri = cupsJSONGetString(cupsJSONFind(devgrant, CUPS_ODEVGRANT_VERIFICATION_URI));
    double      interval = cupsJSONGetNumber(cupsJSONFind(devgrant, CUPS_ODEVGRANT_INTERVAL));
    char        refresh[32];            // Refresh value

    snprintf(refresh, sizeof(refresh), "%d;URL=/?LOGIN=Login", interval > 0.0 ? (int)interval : 5);
    cgiSetVariable("REFRESH_PAGE", refresh);

    cgiSetVariable("USER_CODE", user_code ? user_code : "");

    if (verification_uri && (!strncmp(verification_uri, "http://", 7) || !strncmp(verification_uri, "https://", 8)))
      cgiSetVariable("VERIFICATION_URI", verification_uri);
    else
      cgiSetVariable("VERIFICATION_URI", "");

    cgiStartHTML(cgiText(_("Authorize Access")));
    cgiCopyTemplateLang("oauth-login.tmpl");
    cgiEndHTML();
  }

  done:

  cupsJSONDelete(devgrant);
  cupsJSONDelete(metadata);
}


/*
 * 'do_logout()' - Clear the OAuth bearer token cookie.
 */

static void
do_logout(void)
{
  const char    *oauth_uri = getenv("CUPS_OAUTH_SERVER");
                                        // OAuth authorization server URL

  // Clear the CUPS_BEARER cookie...
  cgiSetCookie("CUPS_BEARER", "", NULL, NULL, time(NULL) - 1, 0);

  // Clear any pending device grant cookie...
  cgiSetCookie("CUPS_DEVGRANT", "", NULL, NULL, time(NULL) - 1, 0);

  // Clear the stored OAuth tokens server-side as well...
  cupsOAuthClearTokens(oauth_uri, NULL);

  // Redirect back to the dashboard...
  do_redirect("/");
}


/*
 * 'do_redirect()' - Redirect to another web page...
 */

static void
do_redirect(const char *url)		// URL or NULL for home page
{
  fprintf(stderr, "DEBUG2: do_redirect(url=\"%s\")\n", url);

  if (url && (!strncmp(url, "http://", 7) || !strncmp(url, "https://", 8)))
    printf("Location: %s\n", url);
  else
    printf("Location: %s://%s:%s%s\n", getenv("HTTPS") ? "https" : "http", getenv("SERVER_NAME"), getenv("SERVER_PORT"), url ? url : "/");

  puts("Content-Type: text/plain\n");
  puts("Redirecting...");
  fflush(stdout);


  if (url && (!strncmp(url, "http://", 7) || !strncmp(url, "https://", 8)))
    fprintf(stderr, "DEBUG2: do_redirect: Location: %s\n", url);
  else
    fprintf(stderr, "DEBUG2: do_redirect: Location: %s://%s:%s%s\n", getenv("HTTPS") ? "https" : "http", getenv("SERVER_NAME"), getenv("SERVER_PORT"), url ? url : "/");

  fputs("DEBUG2: do_redirect: Content-Type: text/plain\n", stderr);
  fputs("DEBUG2: do_redirect:\n", stderr);
  fputs("DEBUG2: do_redirect: Redirecting...", stderr);
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

