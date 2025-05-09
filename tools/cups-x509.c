//
// X.509 credentials utiltiy for CUPS.
//
// Copyright © 2022-2025 by OpenPrinting.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//
// Usage: cups-x509 [OPTIONS] [COMMAND] [ARGUMENT(S)]
//
// Commands:
//
//   ca COMMON-NAME             Sign a CSR to produce a certificate.
//   cacert COMMON-NAME         Create a CA certificate.
//   cert COMMON-NAME           Create a certificate.
//   client URI                 Connect to URI.
//   csr COMMON-NAME            Create a certificate signing request.
//   server COMMON-NAME[:PORT]  Run a HTTPS server (default port 8NNN.)
//   show COMMON-NAME           Show stored credentials for COMMON-NAME.
//
// Options:
//
//   --help                     Show program help
//   --pin                      Pin the certificate found by the client command
//   --require-ca               Require a CA-signed certificate for the client command
//   --version                  Show program version
//   -C COUNTRY                 Set country
//   -L LOCALITY                Set locality name
//   -O ORGANIZATION            Set organization name
//   -R CSR-FILENAME            Specify certificate signing request filename
//   -S STATE                   Set state
//   -U ORGANIZATIONAL-UNIT     Set organizational unit name
//   -a SUBJECT-ALT-NAME        Add a subjectAltName
//   -d DAYS                    Set expiration date in days
//   -p PURPOSE                 Comma-delimited certificate purpose (serverAuth,
//                              clientAuth, codeSigning, emailProtection,
//                              timeStamping, OCSPSigning)
//   -r ROOT-NAME               Name of root certificate
//   -t TYPE                    Certificate type (rsa-2048, rsa-3072, rsa-4096,
//                              ecdsa-p256, ecdsa-p384, ecdsa-p521)
//   -u USAGE                   Comma-delimited key usage (digitalSignature,
//                              nonRepudiation, keyEncipherment,
//                              dataEncipherment, keyAgreement, keyCertSign,
//                              cRLSign, encipherOnly, decipherOnly, default-ca,
//                              default-tls)
//

#include <cups/cups-private.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <poll.h>


//
// Macro for localized text...
//

#  define _(x) x


//
// Local functions...
//

static int	do_ca(const char *common_name, const char *csrfile, const char *root_name, int days);
static int	do_cert(bool ca_cert, cups_credpurpose_t purpose, cups_credtype_t type, cups_credusage_t keyusage, const char *organization, const char *org_unit, const char *locality, const char *state, const char *country, const char *root_name, const char *common_name, size_t num_alt_names, const char **alt_names, int days);
static int	do_client(const char *uri, bool pin, bool require_ca);
static int	do_csr(cups_credpurpose_t purpose, cups_credtype_t type, cups_credusage_t keyusage, const char *organization, const char *org_unit, const char *locality, const char *state, const char *country, const char *common_name, size_t num_alt_names, const char **alt_names);
static int	do_server(const char *host_port);
static int	do_show(const char *common_name);
static int	usage(FILE *fp);


//
// 'main()' - Main entry.
//

int					// O - Exit status
main(int  argc,				// I - Number of command-line arguments
     char *argv[])			// I - Command-line arguments
{
  int		i;			// Looping var
  const char	*command = NULL,	// Command
		*arg = NULL,		// Argument for command
		*opt,			// Current option character
		*csrfile = NULL,	// Certificste signing request filename
		*root_name = NULL,	// Name of root certificate
		*organization = NULL,	// Organization
		*org_unit = NULL,	// Organizational unit
		*locality = NULL,	// Locality
		*state = NULL,		// State/province
		*country = NULL,	// Country
		*alt_names[100];	// Subject alternate names
  bool		pin = false,		// Pin client cert?
		require_ca = false;	// Require a CA-signed cert?
  size_t	num_alt_names = 0;
  int		days = 365;		// Days until expiration
  cups_credpurpose_t purpose = CUPS_CREDPURPOSE_SERVER_AUTH;
					// Certificate purpose
  cups_credtype_t type = CUPS_CREDTYPE_DEFAULT;
					// Certificate type
  cups_credusage_t keyusage = CUPS_CREDUSAGE_DEFAULT_TLS;
					// Key usage


  // Check command-line...
  for (i = 1; i < argc; i ++)
  {
    if (!strcmp(argv[i], "--help"))
    {
      return (usage(stdout));
    }
    else if (!strcmp(argv[i], "--pin"))
    {
      pin = true;
    }
    else if (!strcmp(argv[i], "--require-ca"))
    {
      require_ca = true;
    }
    else if (!strcmp(argv[i], "--version"))
    {
      puts(CUPS_SVERSION);
      exit(0);
    }
    else if (!strncmp(argv[i], "--", 2))
    {
      _cupsLangPrintf(stderr, _("%s: Unknown option '%s'."), "cups-x509", argv[i]);
      return (usage(stderr));
    }
    else if (argv[i][0] == '-')
    {
      for (opt = argv[i] + 1; *opt; opt ++)
      {
        switch (*opt)
        {
          case 'C' : // -C COUNTRY
              i ++;
              if (i >= argc)
              {
                _cupsLangPuts(stderr, _("cups-x509: Missing country after '-C'."));
                return (usage(stderr));
	      }
	      country = argv[i];
	      break;

          case 'L' : // -L LOCALITY
              i ++;
              if (i >= argc)
              {
                _cupsLangPuts(stderr, _("cups-x509: Missing locality/city/town after '-L'."));
                return (usage(stderr));
	      }
	      locality = argv[i];
	      break;

          case 'O' : // -O ORGANIZATION
              i ++;
              if (i >= argc)
              {
                _cupsLangPuts(stderr, _("cups-x509: Missing organization after '-O'."));
                return (usage(stderr));
	      }
	      organization = argv[i];
	      break;

          case 'R' : // -R CSR-FILENAME
              i ++;
              if (i >= argc)
              {
                _cupsLangPuts(stderr, _("cups-x509: Missing CSR filename after '-R'."));
                return (usage(stderr));
	      }
	      csrfile = argv[i];
	      break;

          case 'S' : // -S STATE
              i ++;
              if (i >= argc)
              {
                _cupsLangPuts(stderr, _("cups-x509: Missing state/province after '-S'."));
                return (usage(stderr));
	      }
	      state = argv[i];
	      break;

          case 'U' : // -U ORGANIZATIONAL-UNIT
              i ++;
              if (i >= argc)
              {
                _cupsLangPuts(stderr, _("cups-x509: Missing organizational unit after '-U'."));
                return (usage(stderr));
	      }
	      org_unit = argv[i];
	      break;

          case 'a' : // -a SUBJECT-ALT-NAME
              i ++;
              if (i >= argc)
              {
                _cupsLangPuts(stderr, _("cups-x509: Missing subjectAltName after '-a'."));
                return (usage(stderr));
	      }
	      if (num_alt_names >= (sizeof(alt_names) / sizeof(alt_names[0])))
	      {
	        _cupsLangPuts(stderr, _("cups-x509: Too many subjectAltName values."));
	        return (1);
	      }
	      alt_names[num_alt_names ++] = argv[i];
	      break;

          case 'd' : // -d DAYS
              i ++;
              if (i >= argc)
              {
                _cupsLangPuts(stderr, _("cups-x509: Missing expiration days after '-d'."));
                return (usage(stderr));
	      }
	      if ((days = atoi(argv[i])) <= 0)
	      {
	        _cupsLangPrintf(stderr, _("cups-x509: Bad DAYS value '%s' after '-d'."), argv[i]);
	        return (1);
	      }
	      break;

          case 'p' : // -p PURPOSE
              i ++;
              if (i >= argc)
              {
                _cupsLangPuts(stderr, _("cups-x509: Missing purpose after '-p'."));
                return (usage(stderr));
	      }
	      purpose = 0;
	      if (strstr(argv[i], "serverAuth"))
	        purpose |= CUPS_CREDPURPOSE_SERVER_AUTH;
	      if (strstr(argv[i], "clientAuth"))
	        purpose |= CUPS_CREDPURPOSE_CLIENT_AUTH;
	      if (strstr(argv[i], "codeSigning"))
	        purpose |= CUPS_CREDPURPOSE_CODE_SIGNING;
	      if (strstr(argv[i], "emailProtection"))
	        purpose |= CUPS_CREDPURPOSE_EMAIL_PROTECTION;
	      if (strstr(argv[i], "timeStamping"))
	        purpose |= CUPS_CREDPURPOSE_TIME_STAMPING;
	      if (strstr(argv[i], "OCSPSigning"))
	        purpose |= CUPS_CREDPURPOSE_OCSP_SIGNING;
              if (purpose == 0)
              {
                _cupsLangPrintf(stderr, _("cups-x509: Bad purpose '%s'."), argv[i]);
                return (usage(stderr));
	      }
	      break;

          case 'r' : // -r ROOT-NAME
              i ++;
              if (i >= argc)
              {
                _cupsLangPuts(stderr, _("cups-x509: Missing root name after '-r'."));
                return (usage(stderr));
	      }
	      root_name = argv[i];
	      break;

          case 't' : // -t TYPE
              i ++;
              if (i >= argc)
              {
                _cupsLangPuts(stderr, _("cups-x509: Missing certificate type after '-t'."));
                return (usage(stderr));
	      }
	      if (!strcmp(argv[i], "default"))
	      {
	        type = CUPS_CREDTYPE_DEFAULT;
	      }
	      else if (!strcmp(argv[i], "rsa-2048"))
	      {
	        type = CUPS_CREDTYPE_RSA_2048_SHA256;
	      }
	      else if (!strcmp(argv[i], "rsa-3072"))
	      {
	        type = CUPS_CREDTYPE_RSA_3072_SHA256;
	      }
	      else if (!strcmp(argv[i], "rsa-4096"))
	      {
	        type = CUPS_CREDTYPE_RSA_4096_SHA256;
	      }
	      else if (!strcmp(argv[i], "ecdsa-p256"))
	      {
	        type = CUPS_CREDTYPE_ECDSA_P256_SHA256;
	      }
	      else if (!strcmp(argv[i], "ecdsa-p384"))
	      {
	        type = CUPS_CREDTYPE_ECDSA_P384_SHA256;
	      }
	      else if (!strcmp(argv[i], "ecdsa-p521"))
	      {
	        type = CUPS_CREDTYPE_ECDSA_P521_SHA256;
	      }
	      else
	      {
	        _cupsLangPrintf(stderr, _("cups-x509: Bad certificate type '%s'."), argv[i]);
	        return (usage(stderr));
	      }
	      break;

          case 'u' : // -u USAGE
              i ++;
              if (i >= argc)
              {
                _cupsLangPuts(stderr, _("cups-x509: Missing key usage after '-u'."));
                return (usage(stderr));
	      }
	      keyusage = 0;
	      if (strstr(argv[i], "default-ca"))
	        keyusage = CUPS_CREDUSAGE_DEFAULT_CA;
	      if (strstr(argv[i], "default-tls"))
	        keyusage = CUPS_CREDUSAGE_DEFAULT_TLS;
	      if (strstr(argv[i], "digitalSignature"))
	        keyusage |= CUPS_CREDUSAGE_DIGITAL_SIGNATURE;
	      if (strstr(argv[i], "nonRepudiation"))
	        keyusage |= CUPS_CREDUSAGE_NON_REPUDIATION;
	      if (strstr(argv[i], "keyEncipherment"))
	        keyusage |= CUPS_CREDUSAGE_KEY_ENCIPHERMENT;
	      if (strstr(argv[i], "dataEncipherment"))
	        keyusage |= CUPS_CREDUSAGE_DATA_ENCIPHERMENT;
	      if (strstr(argv[i], "keyAgreement"))
	        keyusage |= CUPS_CREDUSAGE_KEY_AGREEMENT;
	      if (strstr(argv[i], "keyCertSign"))
	        keyusage |= CUPS_CREDUSAGE_KEY_CERT_SIGN;
	      if (strstr(argv[i], "cRLSign"))
	        keyusage |= CUPS_CREDUSAGE_CRL_SIGN;
	      if (strstr(argv[i], "encipherOnly"))
	        keyusage |= CUPS_CREDUSAGE_ENCIPHER_ONLY;
	      if (strstr(argv[i], "decipherOnly"))
	        keyusage |= CUPS_CREDUSAGE_DECIPHER_ONLY;
	      if (keyusage == 0)
	      {
	        _cupsLangPrintf(stderr, _("cups-x509: Bad key usage '%s'."), argv[i]);
	        return (usage(stderr));
	      }
	      break;

          default :
              _cupsLangPrintf(stderr, _("%s: Unknown option '-%c'."), "cups-x509", *opt);
              return (usage(stderr));
	}
      }
    }
    else if (!command)
    {
      command = argv[i];
    }
    else if (!arg)
    {
      arg = argv[i];
    }
    else
    {
      _cupsLangPrintf(stderr, _("%s: Unknown option '%s'."), "cups-x509", argv[i]);
      return (usage(stderr));
    }
  }

  if (!command || !arg)
  {
    _cupsLangPuts(stderr, _("cups-x509: Missing command argument."));
    return (usage(stderr));
  }

  // Run the corresponding command...
  if (!strcmp(command, "ca"))
  {
    return (do_ca(arg, csrfile, root_name, days));
  }
  else if (!strcmp(command, "cacert"))
  {
    return (do_cert(true, purpose, type, keyusage, organization, org_unit, locality, state, country, root_name, arg, num_alt_names, alt_names, days));
  }
  else if (!strcmp(command, "cert"))
  {
    return (do_cert(false, purpose, type, keyusage, organization, org_unit, locality, state, country, root_name, arg, num_alt_names, alt_names, days));
  }
  else if (!strcmp(command, "client"))
  {
    return (do_client(arg, pin, require_ca));
  }
  else if (!strcmp(command, "csr"))
  {
    return (do_csr(purpose, type, keyusage, organization, org_unit, locality, state, country, arg, num_alt_names, alt_names));
  }
  else if (!strcmp(command, "server"))
  {
    return (do_server(arg));
  }
  else if (!strcmp(command, "show"))
  {
    return (do_show(arg));
  }
  else
  {
    _cupsLangPrintf(stderr, _("cups-x509: Unknown command '%s'."), command);
    return (usage(stderr));
  }
}


//
// 'do_ca()' - Test generating a certificate from a CSR.
//

static int				// O - Exit status
do_ca(const char *common_name,		// I - Common name
      const char *csrfile,		// I - CSR filename, if any
      const char *root_name,		// I - Root certificate name
      int        days)			// I - Number of days
{
  char	*request,			// Certificate request
	*cert;				// Certificate


  if (csrfile)
  {
    int		csrfd = open(csrfile, O_RDONLY);
					// File descriptor
    struct stat	csrinfo;		// File information

    if (csrfd < 0)
    {
      _cupsLangPrintf(stderr, _("cups-x509: Unable to access '%s': %s"), csrfile, strerror(errno));
      return (1);
    }

    if (fstat(csrfd, &csrinfo))
    {
      _cupsLangPrintf(stderr, _("cups-x509: Unable to stat '%s': %s"), csrfile, strerror(errno));
      close(csrfd);
      return (1);
    }

    if ((request = malloc((size_t)csrinfo.st_size + 1)) == NULL)
    {
      _cupsLangPrintf(stderr, _("cups-x509: Unable to allocate memory for '%s': %s"), csrfile, strerror(errno));
      close(csrfd);
      return (1);
    }

    if (read(csrfd, request, (size_t)csrinfo.st_size) < (ssize_t)csrinfo.st_size)
    {
      _cupsLangPrintf(stderr, _("cups-x509: Unable to read '%s'."), csrfile);
      close(csrfd);
      free(request);
      return (1);
    }

    close(csrfd);
    request[csrinfo.st_size] = '\0';
  }
  else if ((request = cupsCopyCredentialsRequest(/*path*/NULL, common_name)) == NULL)
  {
    _cupsLangPrintf(stderr, _("cups-x509: No request for '%s'."), common_name);
    return (1);
  }

  if (!cupsSignCredentialsRequest(/*path*/NULL, common_name, request, root_name, CUPS_CREDPURPOSE_ALL, CUPS_CREDUSAGE_ALL, /*cb*/NULL, /*cb_data*/NULL, time(NULL) + days * 86400))
  {
    _cupsLangPrintf(stderr, _("cups-x509: Unable to create certificate (%s)"), cupsGetErrorString());
    free(request);
    return (1);
  }

  free(request);

  if ((cert = cupsCopyCredentials(/*path*/NULL, common_name)) != NULL)
  {
    puts(cert);
    free(cert);
  }
  else
  {
    _cupsLangPrintf(stderr, _("cups-x509: Unable to get generated certificate for '%s'."), common_name);
    return (1);
  }

  return (0);
}


//
// 'do_cert()' - Test creating a self-signed certificate.
//

static int				// O - Exit status
do_cert(
    bool               ca_cert,		// I - `true` for a CA certificate, `false` for a regular one
    cups_credpurpose_t purpose,		// I - Certificate purpose
    cups_credtype_t    type,		// I - Certificate type
    cups_credusage_t   keyusage,	// I - Key usage
    const char         *organization,	// I - Organization
    const char         *org_unit,	// I - Organizational unit
    const char         *locality,	// I - Locality (city/town/etc.)
    const char         *state,		// I - State/province
    const char         *country,	// I - Country
    const char         *root_name,	// I - Root certificate name
    const char         *common_name,	// I - Common name
    size_t             num_alt_names,	// I - Number of subjectAltName's
    const char         **alt_names,	// I - subjectAltName's
    int                days)		// I - Number of days until expiration
{
  char	*cert,				// Certificate
	*key;				// Private key


  if (!cupsCreateCredentials(/*path*/NULL, ca_cert, purpose, type, keyusage, organization, org_unit, locality, state, country, common_name, /*email*/NULL, num_alt_names, alt_names, root_name, time(NULL) + days * 86400))
  {
    _cupsLangPrintf(stderr, _("cups-x509: Unable to create certificate (%s)"), cupsGetErrorString());
    return (1);
  }

  if ((cert = cupsCopyCredentials(/*path*/NULL, common_name)) != NULL)
  {
    puts(cert);
    free(cert);
  }
  else
  {
    _cupsLangPrintf(stderr, _("cups-x509: Unable to get generated certificate for '%s'."), common_name);
    return (1);
  }

  if ((key = cupsCopyCredentialsKey(/*path*/NULL, common_name)) != NULL)
  {
    puts(key);
    free(key);
  }
  else
  {
    _cupsLangPrintf(stderr, _("cups-x509: Unable to get generated private key for '%s'."), common_name);
    return (1);
  }

  return (0);
}


//
// 'do_client()' - Test connecting to a HTTPS server.
//

static int				// O - Exit status
do_client(const char *uri,		// I - URI
          bool       pin,		// I - Pin the cert?
          bool       require_ca)	// I - Require a CA-signed cert?
{
  http_t	*http;			// HTTP connection
  char		hostname[HTTP_MAX_URI],	// Hostname from URI
		resource[HTTP_MAX_URI];	// Resource from URI
  int		port;			// Port number from URI
  http_trust_t	trust;			// Trust evaluation for connection
  char		*hcreds;		// Credentials from connection
  char		hinfo[1024],		// String for connection credentials
		datestr[256];		// Date string
  static const char *trusts[] =		// Trust strings
  { "OK", "Invalid", "Changed", "Expired", "Renewed", "Unknown" };


  // Connect to the host and validate credentials...
  if ((http = httpConnectURI(uri, hostname, sizeof(hostname), &port, resource, sizeof(resource), /*blocking*/true, /*msec*/30000, /*cancel*/NULL, require_ca)) == NULL)
  {
    _cupsLangPrintf(stderr, _("cups-x509: Unable to connect to '%s': %s"), uri, cupsGetErrorString());
    return (1);
  }

  puts("TLS Server Credentials:");
  if ((hcreds = httpCopyPeerCredentials(http)) != NULL)
  {
    trust = cupsGetCredentialsTrust(/*path*/NULL, hostname, hcreds, /*require_ca*/false);

    cupsGetCredentialsInfo(hcreds, hinfo, sizeof(hinfo));

    if (trust == HTTP_TRUST_OK)
      puts("    Trust: OK");
    else
      printf("    Trust: %s (%s)\n", trusts[trust], cupsGetErrorString());
    printf("    Expiration: %s\n", httpGetDateString2(cupsGetCredentialsExpiration(hcreds), datestr, sizeof(datestr)));
    printf("     ValidName: %s\n", cupsAreCredentialsValidForName(hostname, hcreds) ? "true" : "false");
    printf("          Info: \"%s\"\n", hinfo);

    if (pin)
      cupsSaveCredentials(/*path*/NULL, hostname, hcreds, /*key*/NULL);

    free(hcreds);
  }
  else
  {
    puts("    Not present (error).");
  }

  puts("");

  return (do_show(hostname));
}


//
// 'do_csr()' - Test creating a certificate signing request.
//

static int				// O - Exit status
do_csr(
    cups_credpurpose_t purpose,		// I - Certificate purpose
    cups_credtype_t    type,		// I - Certificate type
    cups_credusage_t   keyusage,	// I - Key usage
    const char         *organization,	// I - Organization
    const char         *org_unit,	// I - Organizational unit
    const char         *locality,	// I - Locality (city/town/etc.)
    const char         *state,		// I - State/province
    const char         *country,	// I - Country
    const char         *common_name,	// I - Common name
    size_t             num_alt_names,	// I - Number of subjectAltName's
    const char         **alt_names)	// I - subjectAltName's
{
  char	*csr;				// Certificate request


  if (!cupsCreateCredentialsRequest(/*path*/NULL, purpose, type, keyusage, organization, org_unit, locality, state, country, common_name, /*email*/NULL, num_alt_names, alt_names))
  {
    _cupsLangPrintf(stderr, _("cups-x509: Unable to create certificate request (%s)"), cupsGetErrorString());
    return (1);
  }

  if ((csr = cupsCopyCredentialsRequest(/*path*/NULL, common_name)) != NULL)
  {
    puts(csr);
    free(csr);
  }
  else
  {
    _cupsLangPrintf(stderr, _("cups-x509: Unable to get generated certificate request for '%s'."), common_name);
    return (1);
  }

  return (0);
}


//
// 'do_server()' - Test running a server.
//

static int				// O - Exit status
do_server(const char *host_port)	// I - Hostname/port
{
  char		host[256],		// Hostname
		*hostptr;		// Pointer into hostname
  int		port;			// Port number
  nfds_t	i,			// Looping var
		num_listeners = 0;	// Number of listeners
  struct pollfd	listeners[2];		// Listeners
  http_addr_t	addr;			// Listen address
  http_t	*http;			// Client


  // Get the host and port...
  cupsCopyString(host, host_port, sizeof(host));
  if ((hostptr = strrchr(host, ':')) != NULL)
  {
    // Extract the port number from the argument...
    *hostptr++ = '\0';
    port       = atoi(hostptr);
  }
  else
  {
    // Use the default port 8NNN where NNN is the bottom 3 digits of the UID...
    port = 8000 + (int)getuid() % 1000;
  }

  // Setup listeners for IPv4 and IPv6...
  memset(&addr, 0, sizeof(addr));
  addr.ipv4.sin_family = AF_INET;

  if ((listeners[num_listeners].fd = httpAddrListen(&addr, port)) > 0)
  {
    listeners[num_listeners].events = POLLIN | POLLERR;
    num_listeners ++;
  }

  addr.ipv6.sin6_family = AF_INET6;

  if ((listeners[num_listeners].fd = httpAddrListen(&addr, port)) > 0)
  {
    listeners[num_listeners].events = POLLIN | POLLERR;
    num_listeners ++;
  }

  if (num_listeners == 0)
  {
    _cupsLangPrintf(stderr, _("cups-x509: Unable to listen on port %d: %s"), port, cupsGetErrorString());
    return (1);
  }

  printf("Listening for connections on port %d...\n", port);

  // Set certificate info...
  cupsSetServerCredentials(/*path*/NULL, host, true);

  // Wait for connections...
  for (;;)
  {
    http_state_t	state;		// HTTP request state
    char		resource[1024];	// Resource path

    // Look for new connections...
    if (poll(listeners, num_listeners, 1000) < 0)
    {
      if (errno == EINTR || errno == EAGAIN)
        continue;

      perror("cups-x509: Unable to poll");
      break;
    }

    // Try accepting a connection...
    for (i = 0, http = NULL; i < num_listeners; i ++)
    {
      if (listeners[i].revents & POLLIN)
      {
        if ((http = httpAcceptConnection(listeners[i].fd, true)) != NULL)
          break;

        _cupsLangPrintf(stderr, _("cups-x509: Unable to accept connection: %s"), cupsGetErrorString());
      }
    }

    if (!http)
      continue;

    // Negotiate a secure connection...
    if (!httpSetEncryption(http, HTTP_ENCRYPTION_ALWAYS))
    {
      _cupsLangPrintf(stderr, _("cups-x509: Unable to encrypt connection: %s"), cupsGetErrorString());
      httpClose(http);
      continue;
    }

    // Process a single request and then close it out...
    while ((state = httpReadRequest(http, resource, sizeof(resource))) == HTTP_STATE_WAITING)
      usleep(1000);

    if (state == HTTP_STATE_ERROR)
    {
      if (httpGetError(http) == EPIPE)
	_cupsLangPuts(stderr, _("cups-x509: Client closed connection."));
      else
	_cupsLangPrintf(stderr, _("cups-x509: Bad request line (%s)."), strerror(httpGetError(http)));
    }
    else if (state == HTTP_STATE_UNKNOWN_METHOD)
    {
      _cupsLangPuts(stderr, _("cups-x509: Bad/unknown operation."));
    }
    else if (state == HTTP_STATE_UNKNOWN_VERSION)
    {
      _cupsLangPuts(stderr, _("cups-x509: Bad HTTP version."));
    }
    else
    {
      printf("%s %s\n", httpStateString(state), resource);

      if (state == HTTP_STATE_GET || state == HTTP_STATE_HEAD)
      {
        httpClearFields(http);
        httpSetField(http, HTTP_FIELD_CONTENT_TYPE, "text/plain");
        httpSetField(http, HTTP_FIELD_CONNECTION, "close");
	httpSetLength(http, strlen(resource) + 1);
	httpWriteResponse(http, HTTP_STATUS_OK);

        if (state == HTTP_STATE_GET)
        {
          // Echo back the resource path...
          httpWrite(http, resource, strlen(resource));
          httpWrite(http, "\n", 1);
          httpFlushWrite(http);
        }
      }
      else
      {
        httpWriteResponse(http, HTTP_STATUS_BAD_REQUEST);
      }
    }

    httpClose(http);
  }

  // Close listeners and return...
  for (i = 0; i < num_listeners; i ++)
    httpAddrClose(&addr, listeners[i].fd);

  return (0);
}


//
// 'do_show()' - Test showing stored certificates.
//

static int				// O - Exit status
do_show(const char *common_name)	// I - Common name
{
  char	*tcreds,			// Credentials from trust store
	tinfo[1024],			// String for trust store credentials
	datestr[256];			// Date string


  printf("Trust Store for \"%s\":\n", common_name);

  if ((tcreds = cupsCopyCredentials(/*path*/NULL, common_name)) != NULL)
  {
    cupsGetCredentialsInfo(tcreds, tinfo, sizeof(tinfo));

//    printf("    Certificate Count: %u\n", (unsigned)cupsArrayGetCount(tcreds));
    printf("    Expiration: %s\n", httpGetDateString2(cupsGetCredentialsExpiration(tcreds), datestr, sizeof(datestr)));
    printf("     ValidName: %s\n", cupsAreCredentialsValidForName(common_name, tcreds) ? "true" : "false");
    printf("          Info: \"%s\"\n", tinfo);

    free(tcreds);
  }
  else
  {
    puts("    Not present.");
  }

  return (0);
}


//
// 'usage()' - Show program usage...
//

static int				// O - Exit code
usage(FILE *out)			// I - Output file (stdout or stderr)
{
  _cupsLangPuts(out, _("Usage: cups-x509 [OPTIONS] [SUB-COMMAND] [ARGUMENT]"));
  _cupsLangPuts(out, "");
  _cupsLangPuts(out, _("Sub-Commands:"));
  _cupsLangPuts(out, "");
  _cupsLangPuts(out, _("ca COMMON-NAME             Sign a CSR to produce a certificate."));
  _cupsLangPuts(out, _("cacert COMMON-NAME         Create a CA certificate."));
  _cupsLangPuts(out, _("cert COMMON-NAME           Create a certificate."));
  _cupsLangPuts(out, _("client URI                 Connect to URI."));
  _cupsLangPuts(out, _("csr COMMON-NAME            Create a certificate signing request."));
  _cupsLangPuts(out, _("server COMMON-NAME[:PORT]  Run a HTTPS server (default port 8NNN.)"));
  _cupsLangPuts(out, _("show COMMON-NAME           Show stored credentials for COMMON-NAME."));
  _cupsLangPuts(out, "");
  _cupsLangPuts(out, _("Options:"));
  _cupsLangPuts(out, _(""));
  _cupsLangPuts(out, _("--help                         Show this help"));
  _cupsLangPuts(out, _("--pin                          Pin certificate found by client command"));
  _cupsLangPuts(out, _("--require-ca                   Require CA-signed certificate for client command"));
  _cupsLangPuts(out, _("--version                      Show the program version"));
  _cupsLangPuts(out, _("-C COUNTRY                     Set country."));
  _cupsLangPuts(out, _("-L LOCALITY                    Set locality name."));
  _cupsLangPuts(out, _("-O ORGANIZATION                Set organization name."));
  _cupsLangPuts(out, _("-R CSR-FILENAME                Specify certificate signing request file."));
  _cupsLangPuts(out, _("-S STATE                       Set state."));
  _cupsLangPuts(out, _("-U ORGANIZATIONAL-UNIT         Set organizational unit name."));
  _cupsLangPuts(out, _("-a SUBJECT-ALT-NAME            Add a subjectAltName."));
  _cupsLangPuts(out, _("-d DAYS                        Set expiration date in days."));
  _cupsLangPuts(out, _("-p PURPOSE                     Comma-delimited certificate purpose\n"
                      "                               (serverAuth, clientAuth, codeSigning, emailProtection, timeStamping, OCSPSigning)"));
  _cupsLangPuts(out, _("-r ROOT-NAME                   Name of root certificate"));
  _cupsLangPuts(out, _("-t TYPE                        Certificate type\n"
                      "                               (rsa-2048, rsa-3072, rsa-4096, ecdsa-p256, ecdsa-p384, ecdsa-p521)"));
  _cupsLangPuts(out, _("-u USAGE                       Comma-delimited key usage\n"
                      "                               (digitalSignature, nonRepudiation, keyEncipherment, dataEncipherment, keyAgreement, keyCertSign, cRLSign, encipherOnly, decipherOnly, default-ca, default-tls)"));

  return (out == stderr);
}
