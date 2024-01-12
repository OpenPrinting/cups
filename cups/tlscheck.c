//
// TLS check program for CUPS.
//
// Copyright © 2020-2024 by OpenPrinting.
// Copyright © 2007-2017 by Apple Inc.
// Copyright © 1997-2006 by Easy Software Products.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

#include "cups-private.h"


//
// Local functions...
//

static void	usage(FILE *fp) _CUPS_NORETURN;


//
// 'main()' - Main entry.
//

int					// O - Exit status
main(int  argc,				// I - Number of command-line arguments
     char *argv[])			// I - Command-line arguments
{
  int		i;			// Looping var
  http_t	*http = NULL;		// HTTP connection
  const char	*server = NULL;		// Hostname from command-line
  int		port = 0;		// Port number
  char		*creds;			// Server credentials
  char		creds_str[2048];	// Credentials string
  const char	*cipherName;		// Cipher suite name
  int		tlsVersion = 0;		// TLS version number
  char		uri[1024],		// Printer URI
		scheme[32],		// URI scheme
		host[256],		// Hostname
		userpass[256],		// Username/password
		resource[256];		// Resource path
  int		af = AF_UNSPEC,		// Address family
		tls_options = _HTTP_TLS_NONE,
					// TLS options
		tls_min_version = _HTTP_TLS_1_0,
		tls_max_version = _HTTP_TLS_MAX,
		verbose = 0;		// Verbosity
  ipp_t		*request,		// IPP Get-Printer-Attributes request
		*response;		// IPP Get-Printer-Attributes response
  ipp_attribute_t *attr;		// Current attribute
  const char	*name;			// Attribute name
  char		value[1024];		// Attribute (string) value
  static const char * const pattrs[] =	// Requested attributes
  {
    "color-supported",
    "compression-supported",
    "document-format-supported",
    "pages-per-minute",
    "printer-location",
    "printer-make-and-model",
    "printer-state",
    "printer-state-reasons",
    "sides-supported",
    "uri-authentication-supported",
    "uri-security-supported"
  };


  for (i = 1; i < argc; i ++)
  {
    if (!strcmp(argv[i], "--dh"))
    {
      tls_options |= _HTTP_TLS_ALLOW_DH;
    }
    else if (!strcmp(argv[i], "--help"))
    {
      usage(stdout);
    }
    else if (!strcmp(argv[i], "--no-cbc"))
    {
      tls_options |= _HTTP_TLS_DENY_CBC;
    }
    else if (!strcmp(argv[i], "--no-tls10"))
    {
      tls_min_version = _HTTP_TLS_1_1;
    }
    else if (!strcmp(argv[i], "--tls10"))
    {
      tls_min_version = _HTTP_TLS_1_0;
      tls_max_version = _HTTP_TLS_1_0;
    }
    else if (!strcmp(argv[i], "--tls11"))
    {
      tls_min_version = _HTTP_TLS_1_1;
      tls_max_version = _HTTP_TLS_1_1;
    }
    else if (!strcmp(argv[i], "--tls12"))
    {
      tls_min_version = _HTTP_TLS_1_2;
      tls_max_version = _HTTP_TLS_1_2;
    }
    else if (!strcmp(argv[i], "--tls13"))
    {
      tls_min_version = _HTTP_TLS_1_3;
      tls_max_version = _HTTP_TLS_1_3;
    }
    else if (!strcmp(argv[i], "--rc4"))
    {
      tls_options |= _HTTP_TLS_ALLOW_RC4;
    }
    else if (!strcmp(argv[i], "--verbose") || !strcmp(argv[i], "-v"))
    {
      verbose = 1;
    }
    else if (!strcmp(argv[i], "-4"))
    {
      af = AF_INET;
    }
    else if (!strcmp(argv[i], "-6"))
    {
      af = AF_INET6;
    }
    else if (argv[i][0] == '-')
    {
      fprintf(stderr, "tlscheck: Unknown option '%s'.\n", argv[i]);
      usage(stderr);
    }
    else if (!server)
    {
      if (!strncmp(argv[i], "ipps://", 7))
      {
        httpSeparateURI(HTTP_URI_CODING_ALL, argv[i], scheme, sizeof(scheme), userpass, sizeof(userpass), host, sizeof(host), &port, resource, sizeof(resource));
        server = host;
      }
      else
      {
        server = argv[i];
        cupsCopyString(resource, "/ipp/print", sizeof(resource));
      }
    }
    else if (!port && (argv[i][0] == '=' || isdigit(argv[i][0] & 255)))
    {
      if (argv[i][0] == '=')
	port = atoi(argv[i] + 1);
      else
	port = atoi(argv[i]);
    }
    else
    {
      fprintf(stderr, "tlscheck: Unexpected argument '%s'.\n", argv[i]);
      usage(stderr);
    }
  }

  if (!server)
    usage(stderr);

  if (!port)
    port = 631;

  _httpTLSSetOptions(tls_options, tls_min_version, tls_max_version);

  for (i = 0; i < 10; i ++)
  {
    if ((http = httpConnect2(server, port, NULL, af, HTTP_ENCRYPTION_ALWAYS, 1, 30000, NULL)) != NULL)
      break;
  }

  if (!http)
  {
    fprintf(stderr, "tlscheck: Unable to connect to '%s:%d': %s\n", server, port, cupsGetErrorString());
    return (1);
  }

  if ((creds = httpCopyPeerCredentials(http)) == NULL)
  {
    cupsCopyString(creds_str, "Unable to get server X.509 credentials.", sizeof(creds_str));
  }
  else
  {
    if (!cupsGetCredentialsInfo(creds, creds_str, sizeof(creds_str)))
      cupsCopyString(creds_str, "Unable to convert X.509 credential to string.", sizeof(creds_str));
    free(creds);
  }

#ifdef HAVE_OPENSSL
  switch (SSL_version(http->tls))
  {
    default :
        tlsVersion = 0;
        break;

    case TLS1_VERSION :
        tlsVersion = 10;
        break;

    case TLS1_1_VERSION :
        tlsVersion = 11;
        break;

    case TLS1_2_VERSION :
        tlsVersion = 12;
        break;

#  ifdef TLS1_3_VERSION
    case TLS1_3_VERSION :
        tlsVersion = 13;
        break;
#  endif // TLS1_3_VERSION
  }

  cipherName = SSL_get_cipher_name(http->tls);

#else // HAVE_GNUTLS
  switch (gnutls_protocol_get_version(http->tls))
  {
    default :
        tlsVersion = 0;
        break;
    case GNUTLS_TLS1_0 :
        tlsVersion = 10;
        break;
    case GNUTLS_TLS1_1 :
        tlsVersion = 11;
        break;
    case GNUTLS_TLS1_2 :
        tlsVersion = 12;
        break;
    case GNUTLS_TLS1_3 :
        tlsVersion = 13;
        break;
  }
  cipherName = gnutls_session_get_desc(http->tls);
#endif // HAVE_OPENSSL

  printf("%s: OK (TLS: %d.%d, %s)\n", server, tlsVersion / 10, tlsVersion % 10, cipherName);
  printf("    %s\n", creds_str);

  if (verbose)
  {
    httpAssembleURI(HTTP_URI_CODING_ALL, uri, sizeof(uri), "ipps", NULL, server, port, resource);
    request = ippNewRequest(IPP_OP_GET_PRINTER_ATTRIBUTES);
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri", NULL, uri);
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME, "requesting-user-name", NULL, cupsGetUser());
    ippAddStrings(request, IPP_TAG_OPERATION, IPP_TAG_KEYWORD, "requested-attributes", (int)(sizeof(pattrs) / sizeof(pattrs[0])), NULL, pattrs);

    response = cupsDoRequest(http, request, resource);

    for (attr = ippFirstAttribute(response); attr; attr = ippNextAttribute(response))
    {
      if (ippGetGroupTag(attr) != IPP_TAG_PRINTER)
        continue;

      if ((name = ippGetName(attr)) == NULL)
        continue;

      ippAttributeString(attr, value, sizeof(value));
      printf("    %s=%s\n", name, value);
    }

    ippDelete(response);
    puts("");
  }

  httpClose(http);

  return (0);
}


//
// 'usage()' - Show program usage.
//

static void
usage(FILE *fp)				// I - Output file
{
  fputs("Usage: ./tlscheck [OPTIONS] SERVER [PORT]\n", fp);
  fputs("       ./tlscheck [OPTIONS] ipps://SERVER[:PORT]/PATH\n", fp);
  fputs("\n", fp);
  fputs("Options:\n", fp);
  fputs("  --dh        Allow DH/DHE key exchange\n", fp);
  fputs("  --help      Show help\n", fp);
  fputs("  --no-cbc    Disable CBC cipher suites\n", fp);
  fputs("  --no-tls10  Disable TLS/1.0\n", fp);
  fputs("  --rc4       Allow RC4 encryption\n", fp);
  fputs("  --tls10     Only use TLS/1.0\n", fp);
  fputs("  --tls11     Only use TLS/1.1\n", fp);
  fputs("  --tls12     Only use TLS/1.2\n", fp);
  fputs("  --tls13     Only use TLS/1.3\n", fp);
  fputs("  --verbose   Be verbose\n", fp);
  fputs("  -4          Connect using IPv4 addresses only\n", fp);
  fputs("  -6          Connect using IPv6 addresses only\n", fp);
  fputs("  -v          Be verbose\n", fp);
  fputs("\n", fp);
  fputs("The default port is 631.\n", fp);

  exit(fp == stderr);
}
