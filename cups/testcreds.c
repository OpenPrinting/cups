//
// X.509 credentials test program for CUPS.
//
// Copyright © 2020-2024 by OpenPrinting.
// Copyright © 2007-2016 by Apple Inc.
// Copyright © 1997-2006 by Easy Software Products.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//
// Usage: testcreds [OPTIONS] [SUB-COMMAND] [ARGUMENT]
//
// Sub-Commands:
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
//   -C COUNTRY                 Set country.
//   -L LOCALITY                Set locality name.
//   -O ORGANIZATION            Set organization name.
//   -R CSR-FILENAME            Specify certificate signing request filename.
//   -S STATE                   Set state.
//   -U ORGANIZATIONAL-UNIT     Set organizational unit name.
//   -a SUBJECT-ALT-NAME        Add a subjectAltName.
//   -d DAYS                    Set expiration date in days.
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

#include "cups-private.h"
#include "test-internal.h"
#include <sys/stat.h>
#include <poll.h>


//
// Constants...
//

#define TEST_CERT_PATH	".testssl"


//
// Local functions...
//

static int	do_unit_tests(void);
static int	test_ca(const char *common_name, const char *csrfile, const char *root_name, int days);
static int	test_cert(bool ca_cert, cups_credpurpose_t purpose, cups_credtype_t type, cups_credusage_t keyusage, const char *organization, const char *org_unit, const char *locality, const char *state, const char *country, const char *root_name, const char *common_name, size_t num_alt_names, const char **alt_names, int days);
static int	test_client(const char *uri);
static int	test_csr(cups_credpurpose_t purpose, cups_credtype_t type, cups_credusage_t keyusage, const char *organization, const char *org_unit, const char *locality, const char *state, const char *country, const char *common_name, size_t num_alt_names, const char **alt_names);
static int	test_server(const char *host_port);
static int	test_show(const char *common_name);
static int	usage(FILE *fp);


//
// 'main()' - Main entry.
//

int					// O - Exit status
main(int  argc,				// I - Number of command-line arguments
     char *argv[])			// I - Command-line arguments
{
  int		i;			// Looping var
  const char	*subcommand = NULL,	// Sub-command
		*arg = NULL,		// Argument for sub-command
		*opt,			// Current option character
		*csrfile = NULL,	// Certificste signing request filename
		*root_name = NULL,	// Name of root certificate
		*organization = NULL,	// Organization
		*org_unit = NULL,	// Organizational unit
		*locality = NULL,	// Locality
		*state = NULL,		// State/province
		*country = NULL,	// Country
		*alt_names[100];	// Subject alternate names
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
    else if (!strncmp(argv[i], "--", 2))
    {
      fprintf(stderr, "testcreds: Unknown option '%s'.\n", argv[i]);
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
                fputs("testcreds: Missing country after '-C'.\n", stderr);
                return (usage(stderr));
	      }
	      country = argv[i];
	      break;

          case 'L' : // -L LOCALITY
              i ++;
              if (i >= argc)
              {
                fputs("testcreds: Missing locality/city/town after '-L'.\n", stderr);
                return (usage(stderr));
	      }
	      locality = argv[i];
	      break;

          case 'O' : // -O ORGANIZATION
              i ++;
              if (i >= argc)
              {
                fputs("testcreds: Missing organization after '-O'.\n", stderr);
                return (usage(stderr));
	      }
	      organization = argv[i];
	      break;

          case 'R' : // -R CSR-FILENAME
              i ++;
              if (i >= argc)
              {
                fputs("testcreds: Missing CSR filename after '-R'.\n", stderr);
                return (usage(stderr));
	      }
	      csrfile = argv[i];
	      break;

          case 'S' : // -S STATE
              i ++;
              if (i >= argc)
              {
                fputs("testcreds: Missing state/province after '-S'.\n", stderr);
                return (usage(stderr));
	      }
	      state = argv[i];
	      break;

          case 'U' : // -U ORGANIZATIONAL-UNIT
              i ++;
              if (i >= argc)
              {
                fputs("testcreds: Missing organizational unit after '-U'.\n", stderr);
                return (usage(stderr));
	      }
	      org_unit = argv[i];
	      break;

          case 'a' : // -a SUBJECT-ALT-NAME
              i ++;
              if (i >= argc)
              {
                fputs("testcreds: Missing subjectAltName after '-a'.\n", stderr);
                return (usage(stderr));
	      }
	      if (num_alt_names >= (sizeof(alt_names) / sizeof(alt_names[0])))
	      {
	        fputs("testcreds: Too many subjectAltName values.\n", stderr);
	        return (1);
	      }
	      alt_names[num_alt_names ++] = argv[i];
	      break;

          case 'd' : // -d DAYS
              i ++;
              if (i >= argc)
              {
                fputs("testcreds: Missing expiration days after '-d'.\n", stderr);
                return (usage(stderr));
	      }
	      if ((days = atoi(argv[i])) <= 0)
	      {
	        fprintf(stderr, "testcreds: Bad DAYS value '%s' after '-d'.\n", argv[i]);
	        return (1);
	      }
	      break;

          case 'p' : // -p PURPOSE
              i ++;
              if (i >= argc)
              {
                fputs("testcreds: Missing purpose after '-p'.\n", stderr);
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
                fprintf(stderr, "testcreds: Bad purpose '%s'.\n", argv[i]);
                return (usage(stderr));
	      }
	      break;

          case 'r' : // -r ROOT-NAME
              i ++;
              if (i >= argc)
              {
                fputs("testcreds: Missing root name after '-r'.\n", stderr);
                return (usage(stderr));
	      }
	      root_name = argv[i];
	      break;

          case 't' : // -t TYPE
              i ++;
              if (i >= argc)
              {
                fputs("testcreds: Missing certificate type after '-t'.\n", stderr);
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
	        fprintf(stderr, "testcreds: Bad certificate type '%s'.\n", argv[i]);
	        return (usage(stderr));
	      }
	      break;

          case 'u' : // -u USAGE
              i ++;
              if (i >= argc)
              {
                fputs("testcreds: Missing key usage after '-u'.\n", stderr);
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
	        fprintf(stderr, "testcreds: Bad key usage '%s'.\n", argv[i]);
	        return (usage(stderr));
	      }
	      break;

          default :
              fprintf(stderr, "testcreds: Unknown option '-%c'.\n", *opt);
              return (usage(stderr));
	}
      }
    }
    else if (!subcommand)
    {
      subcommand = argv[i];
    }
    else if (!arg)
    {
      arg = argv[i];
    }
    else
    {
      fprintf(stderr, "testcreds: Unknown option '%s'.\n", argv[i]);
      return (usage(stderr));
    }
  }

  // Make certificate directory
  if (access(TEST_CERT_PATH, 0))
    mkdir(TEST_CERT_PATH, 0700);

  // Do unit tests or sub-command...
  if (!subcommand)
  {
    return (do_unit_tests());
  }
  else if (!arg)
  {
    fputs("testcreds: Missing sub-command argument.\n", stderr);
    return (usage(stderr));
  }

  // Run the corresponding sub-command...
  if (!strcmp(subcommand, "ca"))
  {
    return (test_ca(arg, csrfile, root_name, days));
  }
  else if (!strcmp(subcommand, "cacert"))
  {
    return (test_cert(true, purpose, type, keyusage, organization, org_unit, locality, state, country, root_name, arg, num_alt_names, alt_names, days));
  }
  else if (!strcmp(subcommand, "cert"))
  {
    return (test_cert(false, purpose, type, keyusage, organization, org_unit, locality, state, country, root_name, arg, num_alt_names, alt_names, days));
  }
  else if (!strcmp(subcommand, "client"))
  {
    return (test_client(arg));
  }
  else if (!strcmp(subcommand, "csr"))
  {
    return (test_csr(purpose, type, keyusage, organization, org_unit, locality, state, country, arg, num_alt_names, alt_names));
  }
  else if (!strcmp(subcommand, "server"))
  {
    return (test_server(arg));
  }
  else if (!strcmp(subcommand, "show"))
  {
    return (test_show(arg));
  }
  else
  {
    fprintf(stderr, "testcreds: Unknown sub-command '%s'.\n", subcommand);
    return (usage(stderr));
  }
}


//
// 'do_unit_tests()' - Do unit tests.
//

static int				// O - Exit status
do_unit_tests(void)
{
  cups_credtype_t	type;		// Current credential type
  char			*data;		// Cert data
  static const char * const alt_names[] =
  {					// subjectAltName values
    "printer.example.com",
    "localhost"
  };
  static const char * const types[] =
  {					// Credential types
    "default",
    "rsa-2048",
    "rsa-3072",
    "rsa-4096",
    "ecdsa-p256",
    "ecdsa-p384",
    "ecdsa-p521"
  };


  for (type = CUPS_CREDTYPE_DEFAULT; type <= CUPS_CREDTYPE_ECDSA_P521_SHA256; type ++)
  {
    testBegin("cupsCreateCredentials(_site_, %s, CA)", types[type]);
    if (cupsCreateCredentials(TEST_CERT_PATH, true, CUPS_CREDPURPOSE_SERVER_AUTH, type, CUPS_CREDUSAGE_DEFAULT_TLS, "Organization", "Unit", "Locality", "Ontario", "CA", "_site_", /*email*/NULL, 0, NULL, NULL, time(NULL) + 30 * 86400))
    {
      testEnd(true);

      testBegin("cupsCopyCredentials(_site_)");
      data = cupsCopyCredentials(TEST_CERT_PATH, "_site_");
      testEnd(data != NULL);
      free(data);

      testBegin("cupsCopyCredentialsKey(_site_)");
      data = cupsCopyCredentialsKey(TEST_CERT_PATH, "_site_");
      testEnd(data != NULL);
      free(data);
    }
    else
    {
      testEndMessage(false, "%s", cupsGetErrorString());
    }

    testBegin("cupsCreateCredentials(printer w/alt names, %s, signed by CA cert)", types[type]);
    if (cupsCreateCredentials(TEST_CERT_PATH, false, CUPS_CREDPURPOSE_SERVER_AUTH, type, CUPS_CREDUSAGE_DEFAULT_TLS, "Organization", "Unit", "Locality", "Ontario", "CA", "printer", "admin@example.com", sizeof(alt_names) / sizeof(alt_names[0]), alt_names, "_site_", time(NULL) + 30 * 86400))
      testEnd(true);
    else
      testEndMessage(false, "%s", cupsGetErrorString());

    testBegin("cupsCreateCredentialsRequest(altprinter w/alt names, %s)", types[type]);
    if (cupsCreateCredentialsRequest(TEST_CERT_PATH, CUPS_CREDPURPOSE_SERVER_AUTH, type, CUPS_CREDUSAGE_DEFAULT_TLS, "Organization", "Unit", "Locality", "Ontario", "CA", "altprinter", "admin@example.com", sizeof(alt_names) / sizeof(alt_names[0]), alt_names))
    {
      testEnd(true);

      testBegin("cupsCopyCredentialsKey(altprinter w/alt names)");
      data = cupsCopyCredentialsKey(TEST_CERT_PATH, "altprinter");
      testEnd(data != NULL);
      free(data);

      testBegin("cupsCopyCredentialsRequest(altprinter w/alt names)");
      data = cupsCopyCredentialsRequest(TEST_CERT_PATH, "altprinter");
      testEnd(data != NULL);

      if (data)
      {
        testBegin("cupsSignCredentialsRequest(altprinter w/alt names)");
        if (cupsSignCredentialsRequest(TEST_CERT_PATH, "altprinter", data, "_site_", CUPS_CREDPURPOSE_ALL, CUPS_CREDUSAGE_ALL, /*cb*/NULL, /*cb_data*/NULL, time(NULL) + 30 * 86400))
        {
          testEndMessage(false, "Expected a failure");
        }
        else
        {
	  testEndMessage(true, "%s", cupsGetErrorString());
        }

        free(data);
      }
    }
    else
    {
      testEndMessage(false, "%s", cupsGetErrorString());
    }

    testBegin("cupsCreateCredentialsRequest(altprinter w/o alt names, %s)", types[type]);
    if (cupsCreateCredentialsRequest(TEST_CERT_PATH, CUPS_CREDPURPOSE_SERVER_AUTH, type, CUPS_CREDUSAGE_DEFAULT_TLS, "Organization", "Unit", "Locality", "Ontario", "CA", "altprinter", "admin@example.com", 0, NULL))
    {
      testEnd(true);

      testBegin("cupsCopyCredentialsKey(altprinter w/o alt names)");
      data = cupsCopyCredentialsKey(TEST_CERT_PATH, "altprinter");
      testEnd(data != NULL);
      free(data);

      testBegin("cupsCopyCredentialsRequest(altprinter w/o alt names)");
      data = cupsCopyCredentialsRequest(TEST_CERT_PATH, "altprinter");
      testEnd(data != NULL);

      if (data)
      {
        testBegin("cupsSignCredentialsRequest(altprinter w/o alt names)");
        if (cupsSignCredentialsRequest(TEST_CERT_PATH, "altprinter", data, "_site_", CUPS_CREDPURPOSE_ALL, CUPS_CREDUSAGE_ALL, /*cb*/NULL, /*cb_data*/NULL, time(NULL) + 30 * 86400))
        {
          testEnd(true);
	  free(data);

	  testBegin("cupsCopyCredentialsKey(altprinter w/o alt names)");
	  data = cupsCopyCredentialsKey(TEST_CERT_PATH, "altprinter");
	  testEnd(data != NULL);
        }
        else
        {
	  testEndMessage(false, "%s", cupsGetErrorString());
        }

        free(data);
      }
    }
    else
    {
      testEndMessage(false, "%s", cupsGetErrorString());
    }
  }

  return (testsPassed ? 0 : 1);
}


//
// 'test_ca()' - Test generating a certificate from a CSR.
//

static int				// O - Exit status
test_ca(const char *common_name,	// I - Common name
        const char *csrfile,		// I - CSR filename, if any
        const char *root_name,		// I - Root certificate name
        int        days)		// I - Number of days
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
      fprintf(stderr, "testcreds: Unable to access '%s': %s\n", csrfile, strerror(errno));
      return (1);
    }

    if (fstat(csrfd, &csrinfo))
    {
      fprintf(stderr, "testcreds: Unable to stat '%s': %s\n", csrfile, strerror(errno));
      close(csrfd);
      return (1);
    }

    if ((request = malloc((size_t)csrinfo.st_size + 1)) == NULL)
    {
      fprintf(stderr, "testcreds: Unable to allocate memory for '%s': %s\n", csrfile, strerror(errno));
      close(csrfd);
      return (1);
    }

    if (read(csrfd, request, (size_t)csrinfo.st_size) < (ssize_t)csrinfo.st_size)
    {
      fprintf(stderr, "testcreds: Unable to read '%s'.\n", csrfile);
      close(csrfd);
      return (1);
    }

    close(csrfd);
    request[csrinfo.st_size] = '\0';
  }
  else if ((request = cupsCopyCredentialsRequest(TEST_CERT_PATH, common_name)) == NULL)
  {
    fprintf(stderr, "testcreds: No request for '%s'.\n", common_name);
    return (1);
  }

  if (!cupsSignCredentialsRequest(TEST_CERT_PATH, common_name, request, root_name, CUPS_CREDPURPOSE_ALL, CUPS_CREDUSAGE_ALL, /*cb*/NULL, /*cb_data*/NULL, time(NULL) + days * 86400))
  {
    fprintf(stderr, "testcreds: Unable to create certificate (%s)\n", cupsGetErrorString());
    free(request);
    return (1);
  }

  free(request);

  if ((cert = cupsCopyCredentials(TEST_CERT_PATH, common_name)) != NULL)
  {
    puts(cert);
    free(cert);
  }
  else
  {
    fprintf(stderr, "testcreds: Unable to get generated certificate for '%s'.\n", common_name);
    return (1);
  }

  return (0);
}


//
// 'test_cert()' - Test creating a self-signed certificate.
//

static int				// O - Exit status
test_cert(
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


  if (!cupsCreateCredentials(TEST_CERT_PATH, ca_cert, purpose, type, keyusage, organization, org_unit, locality, state, country, common_name, /*email*/NULL, num_alt_names, alt_names, root_name, time(NULL) + days * 86400))
  {
    fprintf(stderr, "testcreds: Unable to create certificate (%s)\n", cupsGetErrorString());
    return (1);
  }

  if ((cert = cupsCopyCredentials(TEST_CERT_PATH, common_name)) != NULL)
  {
    puts(cert);
    free(cert);
  }
  else
  {
    fprintf(stderr, "testcreds: Unable to get generated certificate for '%s'.\n", common_name);
    return (1);
  }

  if ((key = cupsCopyCredentialsKey(TEST_CERT_PATH, common_name)) != NULL)
  {
    puts(key);
    free(key);
  }
  else
  {
    fprintf(stderr, "testcreds: Unable to get generated private key for '%s'.\n", common_name);
    return (1);
  }

  return (0);
}


//
// 'test_client()' - Test connecting to a HTTPS server.
//

static int				// O - Exit status
test_client(const char *uri)		// I - URI
{
  http_t	*http;			// HTTP connection
  char		scheme[HTTP_MAX_URI],	// Scheme from URI
		hostname[HTTP_MAX_URI],	// Hostname from URI
		username[HTTP_MAX_URI],	// Username:password from URI
		resource[HTTP_MAX_URI];	// Resource from URI
  int		port;			// Port number from URI
  http_trust_t	trust;			// Trust evaluation for connection
  char		*hcreds;		// Credentials from connection
  char		hinfo[1024],		// String for connection credentials
		datestr[256];		// Date string
  static const char *trusts[] =		// Trust strings
  { "OK", "Invalid", "Changed", "Expired", "Renewed", "Unknown" };


  // Connect to the host and validate credentials...
  if (httpSeparateURI(HTTP_URI_CODING_MOST, uri, scheme, sizeof(scheme), username, sizeof(username), hostname, sizeof(hostname), &port, resource, sizeof(resource)) < HTTP_URI_STATUS_OK)
  {
    fprintf(stderr, "testcreds: Bad URI '%s'.\n", uri);
    return (1);
  }

  if ((http = httpConnect2(hostname, port, NULL, AF_UNSPEC, HTTP_ENCRYPTION_ALWAYS, 1, 30000, NULL)) == NULL)
  {
    fprintf(stderr, "testcreds: Unable to connect to '%s' on port %d: %s\n", hostname, port, cupsGetErrorString());
    return (1);
  }

  puts("TLS Server Credentials:");
  if ((hcreds = httpCopyPeerCredentials(http)) != NULL)
  {
    trust = cupsGetCredentialsTrust(TEST_CERT_PATH, hostname, hcreds, /*require_ca*/false);

    cupsGetCredentialsInfo(hcreds, hinfo, sizeof(hinfo));

//    printf("    Certificate Count: %u\n", (unsigned)cupsArrayGetCount(hcreds));
    if (trust == HTTP_TRUST_OK)
      puts("    Trust: OK");
    else
      printf("    Trust: %s (%s)\n", trusts[trust], cupsGetErrorString());
    printf("    Expiration: %s\n", httpGetDateString2(cupsGetCredentialsExpiration(hcreds), datestr, sizeof(datestr)));
    printf("     ValidName: %s\n", cupsAreCredentialsValidForName(hostname, hcreds) ? "true" : "false");
    printf("          Info: \"%s\"\n", hinfo);

    free(hcreds);
  }
  else
  {
    puts("    Not present (error).");
  }

  puts("");

  return (test_show(hostname));
}


//
// 'test_csr()' - Test creating a certificate signing request.
//

static int				// O - Exit status
test_csr(
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


  if (!cupsCreateCredentialsRequest(TEST_CERT_PATH, purpose, type, keyusage, organization, org_unit, locality, state, country, common_name, /*email*/NULL, num_alt_names, alt_names))
  {
    fprintf(stderr, "testcreds: Unable to create certificate request (%s)\n", cupsGetErrorString());
    return (1);
  }

  if ((csr = cupsCopyCredentialsRequest(TEST_CERT_PATH, common_name)) != NULL)
  {
    puts(csr);
    free(csr);
  }
  else
  {
    fprintf(stderr, "testcreds: Unable to get generated certificate request for '%s'.\n", common_name);
    return (1);
  }

  return (0);
}


//
// 'test_server()' - Test running a server.
//

static int				// O - Exit status
test_server(const char *host_port)	// I - Hostname/port
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
    fprintf(stderr, "testcreds: Unable to listen on port %d: %s\n", port, cupsGetErrorString());
    return (1);
  }

  printf("Listening for connections on port %d...\n", port);

  // Set certificate info...
  cupsSetServerCredentials(TEST_CERT_PATH, host, true);

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

      perror("testcreds: Unable to poll");
      break;
    }

    // Try accepting a connection...
    for (i = 0, http = NULL; i < num_listeners; i ++)
    {
      if (listeners[i].revents & POLLIN)
      {
        if ((http = httpAcceptConnection(listeners[i].fd, true)) != NULL)
          break;

        fprintf(stderr, "testcreds: Unable to accept connection: %s\n", cupsGetErrorString());
      }
    }

    if (!http)
      continue;

    // Negotiate a secure connection...
    if (!httpSetEncryption(http, HTTP_ENCRYPTION_ALWAYS))
    {
      fprintf(stderr, "testcreds: Unable to encrypt connection: %s\n", cupsGetErrorString());
      httpClose(http);
      continue;
    }

    // Process a single request and then close it out...
    while ((state = httpReadRequest(http, resource, sizeof(resource))) == HTTP_STATE_WAITING)
      usleep(1000);

    if (state == HTTP_STATE_ERROR)
    {
      if (httpGetError(http) == EPIPE)
	fputs("testcreds: Client closed connection.\n", stderr);
      else
	fprintf(stderr, "testcreds: Bad request line (%s).\n", strerror(httpGetError(http)));
    }
    else if (state == HTTP_STATE_UNKNOWN_METHOD)
    {
      fputs("testcreds: Bad/unknown operation.\n", stderr);
    }
    else if (state == HTTP_STATE_UNKNOWN_VERSION)
    {
      fputs("testcreds: Bad HTTP version.\n", stderr);
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
// 'test_show()' - Test showing stored certificates.
//

static int				// O - Exit status
test_show(const char *common_name)	// I - Common name
{
  char	*tcreds,			// Credentials from trust store
	tinfo[1024],			// String for trust store credentials
	datestr[256];			// Date string


  printf("Trust Store for \"%s\":\n", common_name);

  if ((tcreds = cupsCopyCredentials(TEST_CERT_PATH, common_name)) != NULL)
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
usage(FILE *fp)				// I - Output file (stdout or stderr)
{
  fputs("Usage: testcreds [OPTIONS] [SUB-COMMAND] [ARGUMENT]\n", fp);
  fputs("\n", fp);
  fputs("Sub-Commands:\n", fp);
  fputs("\n", fp);
  fputs("  ca COMMON-NAME             Sign a CSR to produce a certificate.\n", fp);
  fputs("  cacert COMMON-NAME         Create a CA certificate.\n", fp);
  fputs("  cert COMMON-NAME           Create a certificate.\n", fp);
  fputs("  client URI                 Connect to URI.\n", fp);
  fputs("  csr COMMON-NAME            Create a certificate signing request.\n", fp);
  fputs("  server COMMON-NAME[:PORT]  Run a HTTPS server (default port 8NNN.)\n", fp);
  fputs("  show COMMON-NAME           Show stored credentials for COMMON-NAME.\n", fp);
  fputs("\n", fp);
  fputs("Options:\n", fp);
  fputs("\n", fp);
  fputs("  -C COUNTRY                 Set country.\n", fp);
  fputs("  -L LOCALITY                Set locality name.\n", fp);
  fputs("  -O ORGANIZATION            Set organization name.\n", fp);
  fputs("  -R CSR-FILENAME            Specify certificate signing request file.\n", fp);
  fputs("  -S STATE                   Set state.\n", fp);
  fputs("  -U ORGANIZATIONAL-UNIT     Set organizational unit name.\n", fp);
  fputs("  -a SUBJECT-ALT-NAME        Add a subjectAltName.\n", fp);
  fputs("  -d DAYS                    Set expiration date in days.\n", fp);
  fputs("  -p PURPOSE                 Comma-delimited certificate purpose (serverAuth, clientAuth, codeSigning, emailProtection, timeStamping, OCSPSigning)\n", fp);
  fputs("  -r ROOT-NAME               Name of root certificate\n", fp);
  fputs("  -t TYPE                    Certificate type (rsa-2048, rsa-3072, rsa-4096, ecdsa-p256, ecdsa-p384, ecdsa-p521)\n", fp);
  fputs("  -u USAGE                   Comma-delimited key usage (digitalSignature, nonRepudiation, keyEncipherment, dataEncipherment, keyAgreement, keyCertSign, cRLSign, encipherOnly, decipherOnly, default-ca, default-tls)\n", fp);

  return (fp == stderr);
}
