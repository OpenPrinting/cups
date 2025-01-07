/*
 * User, system, and password routines for CUPS.
 *
 * Copyright © 2020-2025 by OpenPrinting.
 * Copyright © 2007-2019 by Apple Inc.
 * Copyright © 1997-2006 by Easy Software Products.
 *
 * Licensed under Apache License v2.0.  See the file "LICENSE" for more
 * information.
 */

/*
 * Include necessary headers...
 */

#include "cups-private.h"
#include "debug-internal.h"
#include <stdlib.h>
#include <sys/stat.h>
#ifdef _WIN32
#  include <windows.h>
#else
#  include <pwd.h>
#  include <termios.h>
#  include <sys/utsname.h>
#endif /* _WIN32 */
#ifdef __APPLE__
#  include <sys/sysctl.h>
#endif /* __APPLE__ */


/*
 * Local constants...
 */

#ifdef __APPLE__
#  if TARGET_OS_OSX
#    define kCUPSPrintingPrefs	CFSTR("org.cups.PrintingPrefs")
#    define kPREFIX		""
#  else
#    define kCUPSPrintingPrefs	CFSTR(".GlobalPreferences")
#    define kPREFIX		"AirPrint"
#  endif /* TARGET_OS_OSX */
#  define kDigestOptionsKey	CFSTR(kPREFIX "DigestOptions")
#  define kUserKey		CFSTR(kPREFIX "User")
#  define kUserAgentTokensKey	CFSTR(kPREFIX "UserAgentTokens")
#  define kAllowAnyRootKey	CFSTR(kPREFIX "AllowAnyRoot")
#  define kAllowExpiredCertsKey	CFSTR(kPREFIX "AllowExpiredCerts")
#  define kEncryptionKey	CFSTR(kPREFIX "Encryption")
#  define kGSSServiceNameKey	CFSTR(kPREFIX "GSSServiceName")
#  define kSSLOptionsKey	CFSTR(kPREFIX "SSLOptions")
#  define kTrustOnFirstUseKey	CFSTR(kPREFIX "TrustOnFirstUse")
#  define kValidateCertsKey	CFSTR(kPREFIX "ValidateCerts")
/* Deprecated */
#  define kAllowRC4		CFSTR(kPREFIX "AllowRC4")
#  define kAllowSSL3		CFSTR(kPREFIX "AllowSSL3")
#  define kAllowDH		CFSTR(kPREFIX "AllowDH")
#endif /* __APPLE__ */

#define _CUPS_PASSCHAR	'*'		/* Character that is echoed for password */


/*
 * Local types...
 */

typedef struct _cups_client_conf_s	/**** client.conf config data ****/
{
  _cups_digestoptions_t	digestoptions;	/* DigestOptions values */
  _cups_uatokens_t	uatokens;	/* UserAgentTokens values */
#ifdef HAVE_TLS
  int			ssl_options,	/* SSLOptions values */
			ssl_min_version,/* Minimum SSL/TLS version */
			ssl_max_version;/* Maximum SSL/TLS version */
#endif /* HAVE_TLS */
  int			trust_first,	/* Trust on first use? */
			any_root,	/* Allow any (e.g., self-signed) root */
			expired_certs,	/* Allow expired certs */
			validate_certs;	/* Validate certificates */
  http_encryption_t	encryption;	/* Encryption setting */
  char			user[65],	/* User name */
			server_name[256];
					/* Server hostname */
#ifdef HAVE_GSSAPI
  char			gss_service_name[32];
  					/* Kerberos service name */
#endif /* HAVE_GSSAPI */
} _cups_client_conf_t;


/*
 * Local functions...
 */

#ifdef __APPLE__
#  ifdef HAVE_TLS
static int	cups_apple_get_boolean(CFStringRef key, int *value);
#  endif /* HAVE_TLS */
static int	cups_apple_get_string(CFStringRef key, char *value, size_t valsize);
#endif /* __APPLE__ */
static int	cups_boolean_value(const char *value);
static void	cups_finalize_client_conf(_cups_client_conf_t *cc);
static void	cups_init_client_conf(_cups_client_conf_t *cc);
static void	cups_read_client_conf(cups_file_t *fp, _cups_client_conf_t *cc);
static void	cups_set_default_ipp_port(_cups_globals_t *cg);
static void	cups_set_digestoptions(_cups_client_conf_t *cc, const char *value);
static void	cups_set_encryption(_cups_client_conf_t *cc, const char *value);
#ifdef HAVE_GSSAPI
static void	cups_set_gss_service_name(_cups_client_conf_t *cc, const char *value);
#endif /* HAVE_GSSAPI */
static void	cups_set_server_name(_cups_client_conf_t *cc, const char *value);
#ifdef HAVE_TLS
static void	cups_set_ssl_options(_cups_client_conf_t *cc, const char *value);
#endif /* HAVE_TLS */
static void	cups_set_uatokens(_cups_client_conf_t *cc, const char *value);
static void	cups_set_user(_cups_client_conf_t *cc, const char *value);


/*
 * 'cupsEncryption()' - Get the current encryption settings.
 *
 * The default encryption setting comes from the CUPS_ENCRYPTION
 * environment variable, then the ~/.cups/client.conf file, and finally the
 * /etc/cups/client.conf file. If not set, the default is
 * @code HTTP_ENCRYPTION_IF_REQUESTED@.
 *
 * Note: The current encryption setting is tracked separately for each thread
 * in a program. Multi-threaded programs that override the setting via the
 * @link cupsSetEncryption@ function need to do so in each thread for the same
 * setting to be used.
 */

http_encryption_t			/* O - Encryption settings */
cupsEncryption(void)
{
  _cups_globals_t *cg = _cupsGlobals();	/* Pointer to library globals */


  if (cg->encryption == (http_encryption_t)-1)
    _cupsSetDefaults();

  return (cg->encryption);
}


/*
 * 'cupsGetPassword()' - Get a password from the user.
 *
 * Uses the current password callback function. Returns @code NULL@ if the
 * user does not provide a password.
 *
 * Note: The current password callback function is tracked separately for each
 * thread in a program. Multi-threaded programs that override the setting via
 * the @link cupsSetPasswordCB@ or @link cupsSetPasswordCB2@ functions need to
 * do so in each thread for the same function to be used.
 *
 * @exclude all@
 */

const char *				/* O - Password */
cupsGetPassword(const char *prompt)	/* I - Prompt string */
{
  _cups_globals_t *cg = _cupsGlobals();	/* Pointer to library globals */


  return ((cg->password_cb)(prompt, NULL, NULL, NULL, cg->password_data));
}


/*
 * 'cupsGetPassword2()' - Get a password from the user using the current
 *                        password callback.
 *
 * Uses the current password callback function. Returns @code NULL@ if the
 * user does not provide a password.
 *
 * Note: The current password callback function is tracked separately for each
 * thread in a program. Multi-threaded programs that override the setting via
 * the @link cupsSetPasswordCB2@ function need to do so in each thread for the
 * same function to be used.
 *
 * @since CUPS 1.4/macOS 10.6@
 */

const char *				/* O - Password */
cupsGetPassword2(const char *prompt,	/* I - Prompt string */
		 http_t     *http,	/* I - Connection to server or @code CUPS_HTTP_DEFAULT@ */
		 const char *method,	/* I - Request method ("GET", "POST", "PUT") */
		 const char *resource)	/* I - Resource path */
{
  _cups_globals_t *cg = _cupsGlobals();	/* Pointer to library globals */


  if (!http)
    http = _cupsConnect();

  return ((cg->password_cb)(prompt, http, method, resource, cg->password_data));
}


/*
 * 'cupsServer()' - Return the hostname/address of the current server.
 *
 * The default server comes from the CUPS_SERVER environment variable, then the
 * ~/.cups/client.conf file, and finally the /etc/cups/client.conf file. If not
 * set, the default is the local system - either "localhost" or a domain socket
 * path.
 *
 * The returned value can be a fully-qualified hostname, a numeric IPv4 or IPv6
 * address, or a domain socket pathname.
 *
 * Note: The current server is tracked separately for each thread in a program.
 * Multi-threaded programs that override the server via the
 * @link cupsSetServer@ function need to do so in each thread for the same
 * server to be used.
 */

const char *				/* O - Server name */
cupsServer(void)
{
  _cups_globals_t *cg = _cupsGlobals();	/* Pointer to library globals */


  if (!cg->server[0])
    _cupsSetDefaults();

  return (cg->server);
}


/*
 * 'cupsSetClientCertCB()' - Set the client certificate callback.
 *
 * Pass @code NULL@ to restore the default callback.
 *
 * Note: The current certificate callback is tracked separately for each thread
 * in a program. Multi-threaded programs that override the callback need to do
 * so in each thread for the same callback to be used.
 *
 * @since CUPS 1.5/macOS 10.7@
 */

void
cupsSetClientCertCB(
    cups_client_cert_cb_t cb,		/* I - Callback function */
    void                  *user_data)	/* I - User data pointer */
{
  _cups_globals_t *cg = _cupsGlobals();	/* Pointer to library globals */


  cg->client_cert_cb	= cb;
  cg->client_cert_data	= user_data;
}


/*
 * 'cupsSetCredentials()' - Set the default credentials to be used for SSL/TLS
 *			    connections.
 *
 * Note: The default credentials are tracked separately for each thread in a
 * program. Multi-threaded programs that override the setting need to do so in
 * each thread for the same setting to be used.
 *
 * @since CUPS 1.5/macOS 10.7@
 */

int					/* O - Status of call (0 = success) */
cupsSetCredentials(
    cups_array_t *credentials)		/* I - Array of credentials */
{
  _cups_globals_t *cg = _cupsGlobals();	/* Pointer to library globals */


  if (cupsArrayCount(credentials) < 1)
    return (-1);

#ifdef HAVE_TLS
  _httpFreeCredentials(cg->tls_credentials);
  cg->tls_credentials = _httpCreateCredentials(credentials);
#endif /* HAVE_TLS */

  return (cg->tls_credentials ? 0 : -1);
}


/*
 * 'cupsSetEncryption()' - Set the encryption preference.
 *
 * The default encryption setting comes from the CUPS_ENCRYPTION
 * environment variable, then the ~/.cups/client.conf file, and finally the
 * /etc/cups/client.conf file. If not set, the default is
 * @code HTTP_ENCRYPTION_IF_REQUESTED@.
 *
 * Note: The current encryption setting is tracked separately for each thread
 * in a program. Multi-threaded programs that override the setting need to do
 * so in each thread for the same setting to be used.
 */

void
cupsSetEncryption(http_encryption_t e)	/* I - New encryption preference */
{
  _cups_globals_t *cg = _cupsGlobals();	/* Pointer to library globals */


  cg->encryption = e;

  if (cg->http)
    httpEncryption(cg->http, e);
}


/*
 * 'cupsSetOAuthCB()' - Set the OAuth 2.0 callback for CUPS.
 *
 * This function sets the OAuth 2.0 callback for the various CUPS APIs that
 * send HTTP requests. Pass @code NULL@ to restore the default (console-based)
 * callback.
 *
 * The OAuth callback receives the HTTP connection, realm name, scope name (if
 * any), resource path, and the "user_data" pointer for each request that
 * requires an OAuth access token. The function then returns either the Bearer
 * token string or `NULL` if no authorization could be obtained.
 *
 * Beyond reusing the Bearer token for subsequent requests on the same HTTP
 * connection, no caching of the token is done by the CUPS library.  The
 * callback can determine whether to refresh a cached token by examining any
 * existing token returned by the @link httpGetAuthString@ function.
 *
 * Note: The current OAuth callback is tracked separately for each thread in a
 * program. Multi-threaded programs that override the callback need to do so in
 * each thread for the same callback to be used.
 *
 * @since CUPS 2.4@
 */

void
cupsSetOAuthCB(
    cups_oauth_cb_t cb,			/* I - Callback function */
    void            *user_data)		/* I - User data pointer */
{
  _cups_globals_t *cg = _cupsGlobals();	/* Pointer to library globals */


  cg->oauth_cb   = cb;
  cg->oauth_data = user_data;
}


/*
 * 'cupsSetPasswordCB()' - Set the password callback for CUPS.
 *
 * Pass @code NULL@ to restore the default (console) password callback, which
 * reads the password from the console. Programs should call either this
 * function or @link cupsSetPasswordCB2@, as only one callback can be registered
 * by a program per thread.
 *
 * Note: The current password callback is tracked separately for each thread
 * in a program. Multi-threaded programs that override the callback need to do
 * so in each thread for the same callback to be used.
 *
 * @exclude all@
 */

void
cupsSetPasswordCB(cups_password_cb_t cb)/* I - Callback function */
{
  _cups_globals_t *cg = _cupsGlobals();	/* Pointer to library globals */


  if (cb == (cups_password_cb_t)0)
    cg->password_cb = (cups_password_cb2_t)_cupsGetPassword;
  else
    cg->password_cb = (cups_password_cb2_t)cb;

  cg->password_data = NULL;
}


/*
 * 'cupsSetPasswordCB2()' - Set the advanced password callback for CUPS.
 *
 * Pass @code NULL@ to restore the default (console) password callback, which
 * reads the password from the console. Programs should call either this
 * function or @link cupsSetPasswordCB2@, as only one callback can be registered
 * by a program per thread.
 *
 * Note: The current password callback is tracked separately for each thread
 * in a program. Multi-threaded programs that override the callback need to do
 * so in each thread for the same callback to be used.
 *
 * @since CUPS 1.4/macOS 10.6@
 */

void
cupsSetPasswordCB2(
    cups_password_cb2_t cb,		/* I - Callback function */
    void                *user_data)	/* I - User data pointer */
{
  _cups_globals_t *cg = _cupsGlobals();	/* Pointer to library globals */


  if (cb == (cups_password_cb2_t)0)
    cg->password_cb = (cups_password_cb2_t)_cupsGetPassword;
  else
    cg->password_cb = cb;

  cg->password_data = user_data;
}


/*
 * 'cupsSetServer()' - Set the default server name and port.
 *
 * The "server" string can be a fully-qualified hostname, a numeric
 * IPv4 or IPv6 address, or a domain socket pathname. Hostnames and numeric IP
 * addresses can be optionally followed by a colon and port number to override
 * the default port 631, e.g. "hostname:8631". Pass @code NULL@ to restore the
 * default server name and port.
 *
 * Note: The current server is tracked separately for each thread in a program.
 * Multi-threaded programs that override the server need to do so in each
 * thread for the same server to be used.
 */

void
cupsSetServer(const char *server)	/* I - Server name */
{
  char		*options,		/* Options */
		*port;			/* Pointer to port */
  _cups_globals_t *cg = _cupsGlobals();	/* Pointer to library globals */


  if (server)
  {
    strlcpy(cg->server, server, sizeof(cg->server));

    if (cg->server[0] != '/' && (options = strrchr(cg->server, '/')) != NULL)
    {
      *options++ = '\0';

      if (!strcmp(options, "version=1.0"))
        cg->server_version = 10;
      else if (!strcmp(options, "version=1.1"))
        cg->server_version = 11;
      else if (!strcmp(options, "version=2.0"))
        cg->server_version = 20;
      else if (!strcmp(options, "version=2.1"))
        cg->server_version = 21;
      else if (!strcmp(options, "version=2.2"))
        cg->server_version = 22;
    }
    else
      cg->server_version = 20;

    if (cg->server[0] != '/' && (port = strrchr(cg->server, ':')) != NULL &&
        !strchr(port, ']') && isdigit(port[1] & 255))
    {
      *port++ = '\0';

      cg->ipp_port = atoi(port);
    }

    if (!cg->ipp_port)
      cups_set_default_ipp_port(cg);

    if (cg->server[0] == '/')
      strlcpy(cg->servername, "localhost", sizeof(cg->servername));
    else
      strlcpy(cg->servername, cg->server, sizeof(cg->servername));
  }
  else
  {
    cg->server[0]      = '\0';
    cg->servername[0]  = '\0';
    cg->server_version = 20;
    cg->ipp_port       = 0;
  }

  if (cg->http)
  {
    httpClose(cg->http);
    cg->http = NULL;
  }
}


/*
 * 'cupsSetServerCertCB()' - Set the server certificate callback.
 *
 * Pass @code NULL@ to restore the default callback.
 *
 * Note: The current credentials callback is tracked separately for each thread
 * in a program. Multi-threaded programs that override the callback need to do
 * so in each thread for the same callback to be used.
 *
 * @since CUPS 1.5/macOS 10.7@
 */

void
cupsSetServerCertCB(
    cups_server_cert_cb_t cb,		/* I - Callback function */
    void		  *user_data)	/* I - User data pointer */
{
  _cups_globals_t *cg = _cupsGlobals();	/* Pointer to library globals */


  cg->server_cert_cb	= cb;
  cg->server_cert_data	= user_data;
}


/*
 * 'cupsSetUser()' - Set the default user name.
 *
 * Pass @code NULL@ to restore the default user name.
 *
 * Note: The current user name is tracked separately for each thread in a
 * program. Multi-threaded programs that override the user name need to do so
 * in each thread for the same user name to be used.
 */

void
cupsSetUser(const char *user)		/* I - User name */
{
  _cups_globals_t *cg = _cupsGlobals();	/* Pointer to library globals */


  if (user)
    strlcpy(cg->user, user, sizeof(cg->user));
  else
    cg->user[0] = '\0';
}


/*
 * 'cupsSetUserAgent()' - Set the default HTTP User-Agent string.
 *
 * Setting the string to NULL forces the default value containing the CUPS
 * version, IPP version, and operating system version and architecture.
 *
 * @since CUPS 1.7/macOS 10.9@
 */

void
cupsSetUserAgent(const char *user_agent)/* I - User-Agent string or @code NULL@ */
{
  _cups_globals_t	*cg = _cupsGlobals();
					/* Thread globals */
#ifdef _WIN32
  SYSTEM_INFO		sysinfo;	/* System information */
  OSVERSIONINFOW	version;	/* OS version info */
  const char		*machine;	/* Hardware/machine name */
#elif defined(__APPLE__)
  struct utsname	name;		/* uname info */
  char			version[256];	/* macOS/iOS version */
  size_t		len;		/* Length of value */
#else
  struct utsname	name;		/* uname info */
#endif /* _WIN32 */


  if (user_agent)
  {
    strlcpy(cg->user_agent, user_agent, sizeof(cg->user_agent));
    return;
  }

  if (cg->uatokens < _CUPS_UATOKENS_OS)
  {
    switch (cg->uatokens)
    {
      default :
      case _CUPS_UATOKENS_NONE :
	  cg->user_agent[0] = '\0';
	  break;
      case _CUPS_UATOKENS_PRODUCT_ONLY :
	  strlcpy(cg->user_agent, "CUPS IPP", sizeof(cg->user_agent));
	  break;
      case _CUPS_UATOKENS_MAJOR :
	  snprintf(cg->user_agent, sizeof(cg->user_agent), "CUPS/%d IPP/2", CUPS_VERSION_MAJOR);
	  break;
      case _CUPS_UATOKENS_MINOR :
	  snprintf(cg->user_agent, sizeof(cg->user_agent), "CUPS/%d.%d IPP/2.1", CUPS_VERSION_MAJOR, CUPS_VERSION_MINOR);
	  break;
      case _CUPS_UATOKENS_MINIMAL :
	  strlcpy(cg->user_agent, CUPS_MINIMAL " IPP/2.1", sizeof(cg->user_agent));
	  break;
    }
    return;
  }

#ifdef _WIN32
 /*
  * Gather Windows version information for the User-Agent string...
  */

  typedef NTSTATUS(WINAPI * RtlGetVersionPtr)(PRTL_OSVERSIONINFOW);

  memset(&version, 0, sizeof(version));
  version.dwOSVersionInfoSize = sizeof(version);

  /* RtlGetVersion gets the current native version of Windows, even when running in compatibility mode */
  RtlGetVersionPtr RtlGetVersionInternal = (RtlGetVersionPtr)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion");
  if (RtlGetVersionInternal)
  {
    RtlGetVersionInternal((PRTL_OSVERSIONINFOW)&version);
  }
  else
  {
    /* Should not happen, but just in case, fallback to deprecated GetVersionExW */
    GetVersionExW(&version);
  }
  GetNativeSystemInfo(&sysinfo);

  switch (sysinfo.wProcessorArchitecture)
  {
    case PROCESSOR_ARCHITECTURE_AMD64 :
        machine = "amd64";
        break;

    case PROCESSOR_ARCHITECTURE_ARM :
        machine = "arm";
        break;

    case PROCESSOR_ARCHITECTURE_IA64 :
        machine = "ia64";
        break;

    case PROCESSOR_ARCHITECTURE_INTEL :
        machine = "intel";
        break;

    default :
        machine = "unknown";
        break;
  }

  if (cg->uatokens == _CUPS_UATOKENS_OS)
    snprintf(cg->user_agent, sizeof(cg->user_agent), CUPS_MINIMAL " (Windows %lu.%lu) IPP/2.0", version.dwMajorVersion, version.dwMinorVersion);
  else
    snprintf(cg->user_agent, sizeof(cg->user_agent), CUPS_MINIMAL " (Windows %lu.%lu; %s) IPP/2.0", version.dwMajorVersion, version.dwMinorVersion, machine);

#elif defined(__APPLE__)
 /*
  * Gather macOS/iOS version information for the User-Agent string...
  */

  uname(&name);

  len = sizeof(version) - 1;
  if (!sysctlbyname("kern.osproductversion", version, &len, NULL, 0))
    version[len] = '\0';
  else
    strlcpy(version, "unknown", sizeof(version));

#  if TARGET_OS_OSX
  if (cg->uatokens == _CUPS_UATOKENS_OS)
    snprintf(cg->user_agent, sizeof(cg->user_agent), CUPS_MINIMAL " (macOS %s) IPP/2.0", version);
  else
    snprintf(cg->user_agent, sizeof(cg->user_agent), CUPS_MINIMAL " (macOS %s; %s) IPP/2.0", version, name.machine);

#  else
  if (cg->uatokens == _CUPS_UATOKENS_OS)
    snprintf(cg->user_agent, sizeof(cg->user_agent), CUPS_MINIMAL " (iOS %s) IPP/2.0", version);
  else
    snprintf(cg->user_agent, sizeof(cg->user_agent), CUPS_MINIMAL " (iOS %s; %s) IPP/2.0", version, name.machine);
#  endif /* TARGET_OS_OSX */

#else
 /*
  * Gather generic UNIX version information for the User-Agent string...
  */

  uname(&name);

  if (cg->uatokens == _CUPS_UATOKENS_OS)
    snprintf(cg->user_agent, sizeof(cg->user_agent), CUPS_MINIMAL " (%s %s) IPP/2.0", name.sysname, name.release);
  else
    snprintf(cg->user_agent, sizeof(cg->user_agent), CUPS_MINIMAL " (%s %s; %s) IPP/2.0", name.sysname, name.release, name.machine);
#endif /* _WIN32 */
}


/*
 * 'cupsUser()' - Return the current user's name.
 *
 * Note: The current user name is tracked separately for each thread in a
 * program. Multi-threaded programs that override the user name with the
 * @link cupsSetUser@ function need to do so in each thread for the same user
 * name to be used.
 */

const char *				/* O - User name */
cupsUser(void)
{
  _cups_globals_t *cg = _cupsGlobals();	/* Pointer to library globals */


  if (!cg->user[0])
    _cupsSetDefaults();

  return (cg->user);
}


/*
 * 'cupsUserAgent()' - Return the default HTTP User-Agent string.
 *
 * @since CUPS 1.7/macOS 10.9@
 */

const char *				/* O - User-Agent string */
cupsUserAgent(void)
{
  _cups_globals_t *cg = _cupsGlobals();	/* Thread globals */


  if (!cg->user_agent[0])
    cupsSetUserAgent(NULL);

  return (cg->user_agent);
}


/*
 * '_cupsGetPassword()' - Get a password from the user.
 */

const char *				/* O - Password or @code NULL@ if none */
_cupsGetPassword(const char *prompt)	/* I - Prompt string */
{
#ifdef _WIN32
  HANDLE		tty;		/* Console handle */
  DWORD			mode;		/* Console mode */
  char			passch,		/* Current key press */
			*passptr,	/* Pointer into password string */
			*passend;	/* End of password string */
  DWORD			passbytes;	/* Bytes read */
  _cups_globals_t	*cg = _cupsGlobals();
					/* Thread globals */


 /*
  * Disable input echo and set raw input...
  */

  if ((tty = GetStdHandle(STD_INPUT_HANDLE)) == INVALID_HANDLE_VALUE)
    return (NULL);

  if (!GetConsoleMode(tty, &mode))
    return (NULL);

  if (!SetConsoleMode(tty, 0))
    return (NULL);

 /*
  * Display the prompt...
  */

  printf("%s ", prompt);
  fflush(stdout);

 /*
  * Read the password string from /dev/tty until we get interrupted or get a
  * carriage return or newline...
  */

  passptr = cg->password;
  passend = cg->password + sizeof(cg->password) - 1;

  while (ReadFile(tty, &passch, 1, &passbytes, NULL))
  {
    if (passch == 0x0A || passch == 0x0D)
    {
     /*
      * Enter/return...
      */

      break;
    }
    else if (passch == 0x08 || passch == 0x7F)
    {
     /*
      * Backspace/delete (erase character)...
      */

      if (passptr > cg->password)
      {
        passptr --;
        fputs("\010 \010", stdout);
      }
      else
        putchar(0x07);
    }
    else if (passch == 0x15)
    {
     /*
      * CTRL+U (erase line)
      */

      if (passptr > cg->password)
      {
	while (passptr > cg->password)
	{
          passptr --;
          fputs("\010 \010", stdout);
        }
      }
      else
        putchar(0x07);
    }
    else if (passch == 0x03)
    {
     /*
      * CTRL+C...
      */

      passptr = cg->password;
      break;
    }
    else if ((passch & 255) < 0x20 || passptr >= passend)
      putchar(0x07);
    else
    {
      *passptr++ = passch;
      putchar(_CUPS_PASSCHAR);
    }

    fflush(stdout);
  }

  putchar('\n');
  fflush(stdout);

 /*
  * Cleanup...
  */

  SetConsoleMode(tty, mode);

 /*
  * Return the proper value...
  */

  if (passbytes == 1 && passptr > cg->password)
  {
    *passptr = '\0';
    return (cg->password);
  }
  else
  {
    memset(cg->password, 0, sizeof(cg->password));
    return (NULL);
  }

#else
  int			tty;		/* /dev/tty - never read from stdin */
  struct termios	original,	/* Original input mode */
			noecho;		/* No echo input mode */
  char			passch,		/* Current key press */
			*passptr,	/* Pointer into password string */
			*passend;	/* End of password string */
  ssize_t		passbytes;	/* Bytes read */
  _cups_globals_t	*cg = _cupsGlobals();
					/* Thread globals */


 /*
  * Disable input echo and set raw input...
  */

  if ((tty = open("/dev/tty", O_RDONLY)) < 0)
    return (NULL);

  if (tcgetattr(tty, &original))
  {
    close(tty);
    return (NULL);
  }

  noecho = original;
  noecho.c_lflag &= (tcflag_t)~(ICANON | ECHO | ECHOE | ISIG);
  noecho.c_cc[VMIN]  = 1;
  noecho.c_cc[VTIME] = 0;

  if (tcsetattr(tty, TCSAFLUSH, &noecho))
  {
    close(tty);
    return (NULL);
  }

 /*
  * Display the prompt...
  */

  printf("%s ", prompt);
  fflush(stdout);

 /*
  * Read the password string from /dev/tty until we get interrupted or get a
  * carriage return or newline...
  */

  passptr = cg->password;
  passend = cg->password + sizeof(cg->password) - 1;

  while ((passbytes = read(tty, &passch, 1)) == 1)
  {
    if (passch == noecho.c_cc[VEOL] ||
#  ifdef VEOL2
        passch == noecho.c_cc[VEOL2] ||
#  endif /* VEOL2 */
        passch == 0x0A || passch == 0x0D)
    {
     /*
      * Enter/return...
      */

      break;
    }
    else if (passch == noecho.c_cc[VERASE] ||
             passch == 0x08 || passch == 0x7F)
    {
     /*
      * Backspace/delete (erase character)...
      */

      if (passptr > cg->password)
      {
        passptr --;
        fputs("\010 \010", stdout);
      }
      else
        putchar(0x07);
    }
    else if (passch == noecho.c_cc[VKILL])
    {
     /*
      * CTRL+U (erase line)
      */

      if (passptr > cg->password)
      {
	while (passptr > cg->password)
	{
          passptr --;
          fputs("\010 \010", stdout);
        }
      }
      else
        putchar(0x07);
    }
    else if (passch == noecho.c_cc[VINTR] || passch == noecho.c_cc[VQUIT] ||
             passch == noecho.c_cc[VEOF])
    {
     /*
      * CTRL+C, CTRL+D, or CTRL+Z...
      */

      passptr = cg->password;
      break;
    }
    else if ((passch & 255) < 0x20 || passptr >= passend)
      putchar(0x07);
    else
    {
      *passptr++ = passch;
      putchar(_CUPS_PASSCHAR);
    }

    fflush(stdout);
  }

  putchar('\n');
  fflush(stdout);

 /*
  * Cleanup...
  */

  tcsetattr(tty, TCSAFLUSH, &original);
  close(tty);

 /*
  * Return the proper value...
  */

  if (passbytes == 1 && passptr > cg->password)
  {
    *passptr = '\0';
    return (cg->password);
  }
  else
  {
    memset(cg->password, 0, sizeof(cg->password));
    return (NULL);
  }
#endif /* _WIN32 */
}


#ifdef HAVE_GSSAPI
/*
 * '_cupsGSSServiceName()' - Get the GSS (Kerberos) service name.
 */

const char *
_cupsGSSServiceName(void)
{
  _cups_globals_t *cg = _cupsGlobals();	/* Thread globals */


  if (!cg->gss_service_name[0])
    _cupsSetDefaults();

  return (cg->gss_service_name);
}
#endif /* HAVE_GSSAPI */


/*
 * '_cupsSetDefaults()' - Set the default server, port, and encryption.
 */

void
_cupsSetDefaults(void)
{
  cups_file_t	*fp;			/* File */
  char		filename[1024];		/* Filename */
  _cups_client_conf_t cc;		/* client.conf values */
  _cups_globals_t *cg = _cupsGlobals();	/* Pointer to library globals */


  DEBUG_puts("_cupsSetDefaults()");

 /*
  * Load initial client.conf values...
  */

  cups_init_client_conf(&cc);

 /*
  * Read the /etc/cups/client.conf and ~/.cups/client.conf files, if
  * present.
  */

  snprintf(filename, sizeof(filename), "%s/client.conf", cg->cups_serverroot);
  if ((fp = cupsFileOpen(filename, "r")) != NULL)
  {
    cups_read_client_conf(fp, &cc);
    cupsFileClose(fp);
  }

  if (cg->home)
  {
   /*
    * Look for ~/.cups/client.conf...
    */

#if _WIN32
    snprintf(filename, sizeof(filename), "%s/AppData/Local/cups/client.conf", cg->home);
#else
    snprintf(filename, sizeof(filename), "%s/.cups/client.conf", cg->home);
#endif // _WIN32

    if ((fp = cupsFileOpen(filename, "r")) != NULL)
    {
      cups_read_client_conf(fp, &cc);
      cupsFileClose(fp);
    }
  }

 /*
  * Finalize things so every client.conf value is set...
  */

  cups_finalize_client_conf(&cc);

  cg->uatokens = cc.uatokens;

  if (cg->encryption == (http_encryption_t)-1)
    cg->encryption = cc.encryption;

  if (!cg->server[0] || !cg->ipp_port)
    cupsSetServer(cc.server_name);

  if (!cg->ipp_port)
    cups_set_default_ipp_port(cg);

  if (!cg->user[0])
    strlcpy(cg->user, cc.user, sizeof(cg->user));

#ifdef HAVE_GSSAPI
  if (!cg->gss_service_name[0])
    strlcpy(cg->gss_service_name, cc.gss_service_name, sizeof(cg->gss_service_name));
#endif /* HAVE_GSSAPI */

  if (cg->trust_first < 0)
    cg->trust_first = cc.trust_first;

  if (cg->any_root < 0)
    cg->any_root = cc.any_root;

  if (cg->expired_certs < 0)
    cg->expired_certs = cc.expired_certs;

  if (cg->validate_certs < 0)
    cg->validate_certs = cc.validate_certs;

#ifdef HAVE_TLS
  _httpTLSSetOptions(cc.ssl_options | _HTTP_TLS_SET_DEFAULT, cc.ssl_min_version, cc.ssl_max_version);
#endif /* HAVE_TLS */
}


#ifdef __APPLE__
#  ifdef HAVE_TLS
/*
 * 'cups_apple_get_boolean()' - Get a boolean setting from the CUPS preferences.
 */

static int				/* O - 1 if set, 0 otherwise */
cups_apple_get_boolean(
    CFStringRef key,			/* I - Key (name) */
    int         *value)			/* O - Boolean value */
{
  Boolean	bval,			/* Preference value */
		bval_set;		/* Value is set? */


  bval = CFPreferencesGetAppBooleanValue(key, kCUPSPrintingPrefs, &bval_set);

  if (bval_set)
    *value = (int)bval;

  return ((int)bval_set);
}
#  endif /* HAVE_TLS */


/*
 * 'cups_apple_get_string()' - Get a string setting from the CUPS preferences.
 */

static int				/* O - 1 if set, 0 otherwise */
cups_apple_get_string(
    CFStringRef key,			/* I - Key (name) */
    char        *value,			/* O - String value */
    size_t      valsize)		/* I - Size of value buffer */
{
  CFStringRef	sval;			/* String value */


  if ((sval = CFPreferencesCopyAppValue(key, kCUPSPrintingPrefs)) != NULL)
  {
    Boolean result = CFStringGetCString(sval, value, (CFIndex)valsize, kCFStringEncodingUTF8);

    CFRelease(sval);

    if (result)
      return (1);
  }

  return (0);
}
#endif /* __APPLE__ */


/*
 * 'cups_boolean_value()' - Convert a string to a boolean value.
 */

static int				/* O - Boolean value */
cups_boolean_value(const char *value)	/* I - String value */
{
  return (!_cups_strcasecmp(value, "yes") || !_cups_strcasecmp(value, "on") || !_cups_strcasecmp(value, "true"));
}


/*
 * 'cups_finalize_client_conf()' - Finalize client.conf values.
 */

static void
cups_finalize_client_conf(
    _cups_client_conf_t *cc)		/* I - client.conf values */
{
  const char	*value;			/* Environment variable */


  if ((value = getenv("CUPS_TRUSTFIRST")) != NULL)
    cc->trust_first = cups_boolean_value(value);

  if ((value = getenv("CUPS_ANYROOT")) != NULL)
    cc->any_root = cups_boolean_value(value);

  if ((value = getenv("CUPS_ENCRYPTION")) != NULL)
    cups_set_encryption(cc, value);

  if ((value = getenv("CUPS_EXPIREDCERTS")) != NULL)
    cc->expired_certs = cups_boolean_value(value);

#ifdef HAVE_GSSAPI
  if ((value = getenv("CUPS_GSSSERVICENAME")) != NULL)
    cups_set_gss_service_name(cc, value);
#endif /* HAVE_GSSAPI */

  if ((value = getenv("CUPS_SERVER")) != NULL)
    cups_set_server_name(cc, value);

  if ((value = getenv("CUPS_USER")) != NULL)
    cups_set_user(cc, value);

  if ((value = getenv("CUPS_VALIDATECERTS")) != NULL)
    cc->validate_certs = cups_boolean_value(value);

 /*
  * Then apply defaults for those values that haven't been set...
  */

  if (cc->trust_first < 0)
    cc->trust_first = 1;

  if (cc->any_root < 0)
    cc->any_root = 1;

  if (cc->encryption == (http_encryption_t)-1)
    cc->encryption = HTTP_ENCRYPTION_IF_REQUESTED;

  if (cc->expired_certs < 0)
    cc->expired_certs = 0;

#ifdef HAVE_GSSAPI
  if (!cc->gss_service_name[0])
    cups_set_gss_service_name(cc, CUPS_DEFAULT_GSSSERVICENAME);
#endif /* HAVE_GSSAPI */

  if (!cc->server_name[0])
  {
   /*
    * If we are compiled with domain socket support, only use the
    * domain socket if it exists and has the right permissions...
    */

#if defined(__APPLE__) && !TARGET_OS_OSX
    cups_set_server_name(cc, "/private/var/run/printd");

#else
#  ifdef CUPS_DEFAULT_DOMAINSOCKET
    if (!access(CUPS_DEFAULT_DOMAINSOCKET, R_OK))
      cups_set_server_name(cc, CUPS_DEFAULT_DOMAINSOCKET);
    else
#  endif /* CUPS_DEFAULT_DOMAINSOCKET */
    cups_set_server_name(cc, "localhost");
#endif /* __APPLE__ && !TARGET_OS_OSX */
  }

  if (!cc->user[0])
  {
#ifdef _WIN32
   /*
    * Get the current user name from the OS...
    */

    DWORD	size;			/* Size of string */

    size = sizeof(cc->user);
    if (!GetUserNameA(cc->user, &size))
#else
   /*
    * Try the USER environment variable as the default username...
    */

    const char *envuser = getenv("USER");	/* Default username */
    struct passwd pw;				/* Account information */
    struct passwd *result = NULL;		/* Auxiliary pointer */
    _cups_globals_t *cg = _cupsGlobals();	/* Pointer to library globals */

    if (envuser)
    {
     /*
      * Validate USER matches the current UID, otherwise don't allow it to
      * override things...  This makes sure that printing after doing su
      * or sudo records the correct username.
      */
      getpwnam_r(envuser, &pw, cg->pw_buf, PW_BUF_SIZE, &result);
      if (result && pw.pw_uid != getuid())
        result = NULL;
    }

    if (!result)
      getpwuid_r(getuid(), &pw, cg->pw_buf, PW_BUF_SIZE, &result);

    if (result)
      strlcpy(cc->user, pw.pw_name, sizeof(cc->user));
    else
#endif /* _WIN32 */
    {
     /*
      * Use the default "unknown" user name...
      */

      strlcpy(cc->user, "unknown", sizeof(cc->user));
    }
  }

  if (cc->validate_certs < 0)
    cc->validate_certs = 0;
}


/*
 * 'cups_init_client_conf()' - Initialize client.conf values.
 */

static void
cups_init_client_conf(
    _cups_client_conf_t *cc)		/* I - client.conf values */
{
 /*
  * Clear all values to "not set"...
  */

  memset(cc, 0, sizeof(_cups_client_conf_t));

  cc->uatokens = _CUPS_UATOKENS_MINIMAL;

#if defined(__APPLE__) && !TARGET_OS_OSX
  cups_set_user(cc, "mobile");
#endif /* __APPLE__ && !TARGET_OS_OSX */

#ifdef HAVE_TLS
  cc->ssl_min_version = _HTTP_TLS_1_0;
  cc->ssl_max_version = _HTTP_TLS_MAX;
#endif /* HAVE_TLS */
  cc->encryption      = (http_encryption_t)-1;
  cc->trust_first     = -1;
  cc->any_root        = -1;
  cc->expired_certs   = -1;
  cc->validate_certs  = -1;

 /*
  * Load settings from the org.cups.PrintingPrefs plist (which trump
  * everything...)
  */

#if defined(__APPLE__)
  char	sval[1024];			/* String value */
#  ifdef HAVE_TLS
  int	bval;				/* Boolean value */

  if (cups_apple_get_boolean(kAllowAnyRootKey, &bval))
    cc->any_root = bval;

  if (cups_apple_get_boolean(kAllowExpiredCertsKey, &bval))
    cc->expired_certs = bval;

  if (cups_apple_get_string(kEncryptionKey, sval, sizeof(sval)))
    cups_set_encryption(cc, sval);

  if (cups_apple_get_string(kSSLOptionsKey, sval, sizeof(sval)))
  {
    cups_set_ssl_options(cc, sval);
  }
  else
  {
    sval[0] = '\0';

    if (cups_apple_get_boolean(kAllowRC4, &bval) && bval)
      strlcat(sval, " AllowRC4", sizeof(sval));
    if (cups_apple_get_boolean(kAllowSSL3, &bval) && bval)
      strlcat(sval, " AllowSSL3", sizeof(sval));
    if (cups_apple_get_boolean(kAllowDH, &bval) && bval)
      strlcat(sval, " AllowDH", sizeof(sval));

    if (sval[0])
      cups_set_ssl_options(cc, sval);
  }

  if (cups_apple_get_boolean(kTrustOnFirstUseKey, &bval))
    cc->trust_first = bval;

  if (cups_apple_get_boolean(kValidateCertsKey, &bval))
    cc->validate_certs = bval;
#  endif /* HAVE_TLS */

  if (cups_apple_get_string(kDigestOptionsKey, sval, sizeof(sval)))
    cups_set_digestoptions(cc, sval);

  if (cups_apple_get_string(kUserKey, sval, sizeof(sval)))
    strlcpy(cc->user, sval, sizeof(cc->user));

  if (cups_apple_get_string(kUserAgentTokensKey, sval, sizeof(sval)))
    cups_set_uatokens(cc, sval);
#endif /* __APPLE__ */
}


/*
 * 'cups_read_client_conf()' - Read a client.conf file.
 */

static void
cups_read_client_conf(
    cups_file_t         *fp,		/* I - File to read */
    _cups_client_conf_t *cc)		/* I - client.conf values */
{
  int	linenum;			/* Current line number */
  char	line[1024],			/* Line from file */
        *value;				/* Pointer into line */


 /*
  * Read from the file...
  */

  linenum = 0;
  while (cupsFileGetConf(fp, line, sizeof(line), &value, &linenum))
  {
    if (!_cups_strcasecmp(line, "DigestOptions") && value)
      cups_set_digestoptions(cc, value);
    else if (!_cups_strcasecmp(line, "Encryption") && value)
      cups_set_encryption(cc, value);
#ifndef __APPLE__
   /*
    * The ServerName directive is not supported on macOS due to app
    * sandboxing restrictions, i.e. not all apps request network access.
    */
    else if (!_cups_strcasecmp(line, "ServerName") && value)
      cups_set_server_name(cc, value);
#endif /* !__APPLE__ */
    else if (!_cups_strcasecmp(line, "User") && value)
      cups_set_user(cc, value);
    else if (!_cups_strcasecmp(line, "UserAgentTokens") && value)
      cups_set_uatokens(cc, value);
    else if (!_cups_strcasecmp(line, "TrustOnFirstUse") && value)
      cc->trust_first = cups_boolean_value(value);
    else if (!_cups_strcasecmp(line, "AllowAnyRoot") && value)
      cc->any_root = cups_boolean_value(value);
    else if (!_cups_strcasecmp(line, "AllowExpiredCerts") &&
             value)
      cc->expired_certs = cups_boolean_value(value);
    else if (!_cups_strcasecmp(line, "ValidateCerts") && value)
      cc->validate_certs = cups_boolean_value(value);
#ifdef HAVE_GSSAPI
    else if (!_cups_strcasecmp(line, "GSSServiceName") && value)
      cups_set_gss_service_name(cc, value);
#endif /* HAVE_GSSAPI */
#ifdef HAVE_TLS
    else if (!_cups_strcasecmp(line, "SSLOptions") && value)
      cups_set_ssl_options(cc, value);
#endif /* HAVE_TLS */
  }
}


/*
 * 'cups_set_default_ipp_port()' - Set the default IPP port value.
 */

static void
cups_set_default_ipp_port(
    _cups_globals_t *cg)		/* I - Global data */
{
  const char	*ipp_port;		/* IPP_PORT environment variable */


  if ((ipp_port = getenv("IPP_PORT")) != NULL)
  {
    if ((cg->ipp_port = atoi(ipp_port)) <= 0)
      cg->ipp_port = CUPS_DEFAULT_IPP_PORT;
  }
  else
    cg->ipp_port = CUPS_DEFAULT_IPP_PORT;
}


/*
 * 'cups_set_digestoptions()' - Set the DigestOptions value.
 */

static void
cups_set_digestoptions(
    _cups_client_conf_t *cc,		/* I - client.conf values */
    const char          *value)		/* I - Value */
{
  if (!_cups_strcasecmp(value, "DenyMD5"))
    cc->digestoptions = _CUPS_DIGESTOPTIONS_DENYMD5;
  else if (!_cups_strcasecmp(value, "None"))
    cc->digestoptions = _CUPS_DIGESTOPTIONS_NONE;
}


/*
 * 'cups_set_encryption()' - Set the Encryption value.
 */

static void
cups_set_encryption(
    _cups_client_conf_t *cc,		/* I - client.conf values */
    const char          *value)		/* I - Value */
{
  if (!_cups_strcasecmp(value, "never"))
    cc->encryption = HTTP_ENCRYPTION_NEVER;
  else if (!_cups_strcasecmp(value, "always"))
    cc->encryption = HTTP_ENCRYPTION_ALWAYS;
  else if (!_cups_strcasecmp(value, "required"))
    cc->encryption = HTTP_ENCRYPTION_REQUIRED;
  else
    cc->encryption = HTTP_ENCRYPTION_IF_REQUESTED;
}


/*
 * 'cups_set_gss_service_name()' - Set the GSSServiceName value.
 */

#ifdef HAVE_GSSAPI
static void
cups_set_gss_service_name(
    _cups_client_conf_t *cc,		/* I - client.conf values */
    const char          *value)		/* I - Value */
{
  strlcpy(cc->gss_service_name, value, sizeof(cc->gss_service_name));
}
#endif /* HAVE_GSSAPI */


/*
 * 'cups_set_server_name()' - Set the ServerName value.
 */

static void
cups_set_server_name(
    _cups_client_conf_t *cc,		/* I - client.conf values */
    const char          *value)		/* I - Value */
{
  strlcpy(cc->server_name, value, sizeof(cc->server_name));
}


/*
 * 'cups_set_ssl_options()' - Set the SSLOptions value.
 */

#ifdef HAVE_TLS
static void
cups_set_ssl_options(
    _cups_client_conf_t *cc,		/* I - client.conf values */
    const char          *value)		/* I - Value */
{
 /*
  * SSLOptions [AllowRC4] [AllowSSL3] [AllowDH] [DenyTLS1.0] [None]
  */

  int	options = _HTTP_TLS_NONE,	/* SSL/TLS options */
	min_version = _HTTP_TLS_1_0,	/* Minimum SSL/TLS version */
	max_version = _HTTP_TLS_MAX;	/* Maximum SSL/TLS version */
  char	temp[256],			/* Copy of value */
	*start,				/* Start of option */
	*end;				/* End of option */


  strlcpy(temp, value, sizeof(temp));

  for (start = temp; *start; start = end)
  {
   /*
    * Find end of keyword...
    */

    end = start;
    while (*end && !_cups_isspace(*end))
      end ++;

    if (*end)
      *end++ = '\0';

   /*
    * Compare...
    */

    if (!_cups_strcasecmp(start, "AllowRC4"))
      options |= _HTTP_TLS_ALLOW_RC4;
    else if (!_cups_strcasecmp(start, "AllowSSL3"))
      min_version = _HTTP_TLS_SSL3;
    else if (!_cups_strcasecmp(start, "AllowDH"))
      options |= _HTTP_TLS_ALLOW_DH;
    else if (!_cups_strcasecmp(start, "DenyCBC"))
      options |= _HTTP_TLS_DENY_CBC;
    else if (!_cups_strcasecmp(start, "DenyTLS1.0"))
      min_version = _HTTP_TLS_1_1;
    else if (!_cups_strcasecmp(start, "MaxTLS1.0"))
      max_version = _HTTP_TLS_1_0;
    else if (!_cups_strcasecmp(start, "MaxTLS1.1"))
      max_version = _HTTP_TLS_1_1;
    else if (!_cups_strcasecmp(start, "MaxTLS1.2"))
      max_version = _HTTP_TLS_1_2;
    else if (!_cups_strcasecmp(start, "MaxTLS1.3"))
      max_version = _HTTP_TLS_1_3;
    else if (!_cups_strcasecmp(start, "MinTLS1.0"))
      min_version = _HTTP_TLS_1_0;
    else if (!_cups_strcasecmp(start, "MinTLS1.1"))
      min_version = _HTTP_TLS_1_1;
    else if (!_cups_strcasecmp(start, "MinTLS1.2"))
      min_version = _HTTP_TLS_1_2;
    else if (!_cups_strcasecmp(start, "MinTLS1.3"))
      min_version = _HTTP_TLS_1_3;
    else if (!_cups_strcasecmp(start, "None"))
      options = _HTTP_TLS_NONE;
    else if (!_cups_strcasecmp(start, "NoSystem"))
      options |= _HTTP_TLS_NO_SYSTEM;
  }

  cc->ssl_options     = options;
  cc->ssl_max_version = max_version;
  cc->ssl_min_version = min_version;

  DEBUG_printf(("4cups_set_ssl_options(cc=%p, value=\"%s\") options=%x, min_version=%d, max_version=%d", (void *)cc, value, options, min_version, max_version));
}
#endif /* HAVE_TLS */


/*
 * 'cups_set_uatokens()' - Set the UserAgentTokens value.
 */

static void
cups_set_uatokens(
    _cups_client_conf_t *cc,		/* I - client.conf values */
    const char          *value)		/* I - Value */
{
  int	i;				/* Looping var */
  static const char * const uatokens[] =/* UserAgentTokens values */
  {
    "NONE",
    "PRODUCTONLY",
    "MAJOR",
    "MINOR",
    "MINIMAL",
    "OS",
    "FULL"
  };

  for (i = 0; i < (int)(sizeof(uatokens) / sizeof(uatokens[0])); i ++)
  {
    if (!_cups_strcasecmp(value, uatokens[i]))
    {
      cc->uatokens = (_cups_uatokens_t)i;
      return;
    }
  }
}


/*
 * 'cups_set_user()' - Set the User value.
 */

static void
cups_set_user(
    _cups_client_conf_t *cc,		/* I - client.conf values */
    const char          *value)		/* I - Value */
{
  strlcpy(cc->user, value, sizeof(cc->user));
}
