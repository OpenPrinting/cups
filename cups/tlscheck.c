/*
 * TLS check program for CUPS.
 *
 * Copyright © 2020-2024 by OpenPrinting.
 * Copyright © 2007-2017 by Apple Inc.
 * Copyright © 1997-2006 by Easy Software Products.
 *
 * Licensed under Apache License v2.0.  See the file "LICENSE" for more
 * information.
 */

/*
 * Include necessary headers...
 */

#include "cups-private.h"


#ifndef HAVE_TLS
int main(void) { puts("Sorry, no TLS support compiled in."); return (1); }
#else

/*
 * Local functions...
 */

static void	usage(void) _CUPS_NORETURN;


/*
 * 'main()' - Main entry.
 */

int					/* O - Exit status */
main(int  argc,				/* I - Number of command-line arguments */
     char *argv[])			/* I - Command-line arguments */
{
  int		i;			/* Looping var */
  http_t	*http;			/* HTTP connection */
  const char	*server = NULL;		/* Hostname from command-line */
  int		port = 0;		/* Port number */
  cups_array_t	*creds;			/* Server credentials */
  char		creds_str[2048];	/* Credentials string */
  const char	*cipherName = "UNKNOWN";/* Cipher suite name */
  int		dhBits = 0;		/* Diffie-Hellman bits */
  int		tlsVersion = 0;		/* TLS version number */
  char		uri[1024],		/* Printer URI */
		scheme[32],		/* URI scheme */
		host[256],		/* Hostname */
		userpass[256],		/* Username/password */
		resource[256];		/* Resource path */
  int		af = AF_UNSPEC,		/* Address family */
		tls_options = _HTTP_TLS_NONE,
					/* TLS options */
		tls_min_version = _HTTP_TLS_1_0,
		tls_max_version = _HTTP_TLS_MAX,
		verbose = 0;		/* Verbosity */
  ipp_t		*request,		/* IPP Get-Printer-Attributes request */
		*response;		/* IPP Get-Printer-Attributes response */
  ipp_attribute_t *attr;		/* Current attribute */
  const char	*name;			/* Attribute name */
  char		value[1024];		/* Attribute (string) value */
  static const char * const pattrs[] =	/* Requested attributes */
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
      printf("tlscheck: Unknown option '%s'.\n", argv[i]);
      usage();
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
        strlcpy(resource, "/ipp/print", sizeof(resource));
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
      printf("tlscheck: Unexpected argument '%s'.\n", argv[i]);
      usage();
    }
  }

  if (!server)
    usage();

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
    printf("%s: ERROR (%s)\n", server, cupsLastErrorString());
    return (1);
  }

  if (httpCopyCredentials(http, &creds))
  {
    strlcpy(creds_str, "Unable to get server X.509 credentials.", sizeof(creds_str));
  }
  else
  {
    if (!httpCredentialsString(creds, creds_str, sizeof(creds_str)))
      strlcpy(creds_str, "Unable to convert X.509 credential to string.", sizeof(creds_str));
    httpFreeCredentials(creds);
  }

#ifdef HAVE_OPENSSL
  int	cipherBits;			// Encryption key bits
  char	cipherStr[1024];		// Combined cipher name

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

  snprintf(cipherStr, sizeof(cipherStr), "%s_%dbits", SSL_get_cipher_name(http->tls), SSL_get_cipher_bits(http->tls, &cipherBits));

  cipherName = cipherStr;

#elif defined(HAVE_GNUTLS)
#elif defined(__APPLE__)
  SSLProtocol protocol;
  SSLCipherSuite cipher;
  char unknownCipherName[256];
  int paramsNeeded = 0;
  const void *params;
  size_t paramsLen;
  OSStatus err;

  if ((err = SSLGetNegotiatedProtocolVersion(http->tls, &protocol)) != noErr)
  {
    printf("%s: ERROR (No protocol version - %d)\n", server, (int)err);
    httpClose(http);
    return (1);
  }

  switch (protocol)
  {
    default :
        tlsVersion = 0;
        break;
    case kSSLProtocol3 :
        tlsVersion = 30;
        break;
    case kTLSProtocol1 :
        tlsVersion = 10;
        break;
    case kTLSProtocol11 :
        tlsVersion = 11;
        break;
    case kTLSProtocol12 :
        tlsVersion = 12;
        break;
  }

  if ((err = SSLGetNegotiatedCipher(http->tls, &cipher)) != noErr)
  {
    printf("%s: ERROR (No cipher suite - %d)\n", server, (int)err);
    httpClose(http);
    return (1);
  }

  switch (cipher)
  {
    case TLS_NULL_WITH_NULL_NULL:
	cipherName = "TLS_NULL_WITH_NULL_NULL";
	break;
    case TLS_RSA_WITH_NULL_MD5:
	cipherName = "TLS_RSA_WITH_NULL_MD5";
	break;
    case TLS_RSA_WITH_NULL_SHA:
	cipherName = "TLS_RSA_WITH_NULL_SHA";
	break;
    case TLS_RSA_WITH_RC4_128_MD5:
	cipherName = "TLS_RSA_WITH_RC4_128_MD5";
	break;
    case TLS_RSA_WITH_RC4_128_SHA:
	cipherName = "TLS_RSA_WITH_RC4_128_SHA";
	break;
    case TLS_RSA_WITH_3DES_EDE_CBC_SHA:
	cipherName = "TLS_RSA_WITH_3DES_EDE_CBC_SHA";
	break;
    case TLS_RSA_WITH_NULL_SHA256:
	cipherName = "TLS_RSA_WITH_NULL_SHA256";
	break;
    case TLS_RSA_WITH_AES_128_CBC_SHA256:
	cipherName = "TLS_RSA_WITH_AES_128_CBC_SHA256";
	break;
    case TLS_RSA_WITH_AES_256_CBC_SHA256:
	cipherName = "TLS_RSA_WITH_AES_256_CBC_SHA256";
	break;
    case TLS_DH_DSS_WITH_3DES_EDE_CBC_SHA:
	cipherName = "TLS_DH_DSS_WITH_3DES_EDE_CBC_SHA";
	paramsNeeded = 1;
	break;
    case TLS_DH_RSA_WITH_3DES_EDE_CBC_SHA:
	cipherName = "TLS_DH_RSA_WITH_3DES_EDE_CBC_SHA";
	paramsNeeded = 1;
	break;
    case TLS_DHE_DSS_WITH_3DES_EDE_CBC_SHA:
	cipherName = "TLS_DHE_DSS_WITH_3DES_EDE_CBC_SHA";
	paramsNeeded = 1;
	break;
    case TLS_DHE_RSA_WITH_3DES_EDE_CBC_SHA:
	cipherName = "TLS_DHE_RSA_WITH_3DES_EDE_CBC_SHA";
	paramsNeeded = 1;
	break;
    case TLS_DH_DSS_WITH_AES_128_CBC_SHA256:
	cipherName = "TLS_DH_DSS_WITH_AES_128_CBC_SHA256";
	paramsNeeded = 1;
	break;
    case TLS_DH_RSA_WITH_AES_128_CBC_SHA256:
	cipherName = "TLS_DH_RSA_WITH_AES_128_CBC_SHA256";
	paramsNeeded = 1;
	break;
    case TLS_DHE_DSS_WITH_AES_128_CBC_SHA256:
	cipherName = "TLS_DHE_DSS_WITH_AES_128_CBC_SHA256";
	paramsNeeded = 1;
	break;
    case TLS_DHE_RSA_WITH_AES_128_CBC_SHA256:
	cipherName = "TLS_DHE_RSA_WITH_AES_128_CBC_SHA256";
	paramsNeeded = 1;
	break;
    case TLS_DH_DSS_WITH_AES_256_CBC_SHA256:
	cipherName = "TLS_DH_DSS_WITH_AES_256_CBC_SHA256";
	paramsNeeded = 1;
	break;
    case TLS_DH_RSA_WITH_AES_256_CBC_SHA256:
	cipherName = "TLS_DH_RSA_WITH_AES_256_CBC_SHA256";
	paramsNeeded = 1;
	break;
    case TLS_DHE_DSS_WITH_AES_256_CBC_SHA256:
	cipherName = "TLS_DHE_DSS_WITH_AES_256_CBC_SHA256";
	paramsNeeded = 1;
	break;
    case TLS_DHE_RSA_WITH_AES_256_CBC_SHA256:
	cipherName = "TLS_DHE_RSA_WITH_AES_256_CBC_SHA256";
	paramsNeeded = 1;
	break;
    case TLS_DH_anon_WITH_RC4_128_MD5:
	cipherName = "TLS_DH_anon_WITH_RC4_128_MD5";
	paramsNeeded = 1;
	break;
    case TLS_DH_anon_WITH_3DES_EDE_CBC_SHA:
	cipherName = "TLS_DH_anon_WITH_3DES_EDE_CBC_SHA";
	paramsNeeded = 1;
	break;
    case TLS_DH_anon_WITH_AES_128_CBC_SHA256:
	cipherName = "TLS_DH_anon_WITH_AES_128_CBC_SHA256";
	paramsNeeded = 1;
	break;
    case TLS_DH_anon_WITH_AES_256_CBC_SHA256:
	cipherName = "TLS_DH_anon_WITH_AES_256_CBC_SHA256";
	paramsNeeded = 1;
	break;
    case TLS_PSK_WITH_RC4_128_SHA:
	cipherName = "TLS_PSK_WITH_RC4_128_SHA";
	break;
    case TLS_PSK_WITH_3DES_EDE_CBC_SHA:
	cipherName = "TLS_PSK_WITH_3DES_EDE_CBC_SHA";
	break;
    case TLS_PSK_WITH_AES_128_CBC_SHA:
	cipherName = "TLS_PSK_WITH_AES_128_CBC_SHA";
	break;
    case TLS_PSK_WITH_AES_256_CBC_SHA:
	cipherName = "TLS_PSK_WITH_AES_256_CBC_SHA";
	break;
    case TLS_DHE_PSK_WITH_RC4_128_SHA:
	cipherName = "TLS_DHE_PSK_WITH_RC4_128_SHA";
	paramsNeeded = 1;
	break;
    case TLS_DHE_PSK_WITH_3DES_EDE_CBC_SHA:
	cipherName = "TLS_DHE_PSK_WITH_3DES_EDE_CBC_SHA";
	paramsNeeded = 1;
	break;
    case TLS_DHE_PSK_WITH_AES_128_CBC_SHA:
	cipherName = "TLS_DHE_PSK_WITH_AES_128_CBC_SHA";
	paramsNeeded = 1;
	break;
    case TLS_DHE_PSK_WITH_AES_256_CBC_SHA:
	cipherName = "TLS_DHE_PSK_WITH_AES_256_CBC_SHA";
	paramsNeeded = 1;
	break;
    case TLS_RSA_PSK_WITH_RC4_128_SHA:
	cipherName = "TLS_RSA_PSK_WITH_RC4_128_SHA";
	break;
    case TLS_RSA_PSK_WITH_3DES_EDE_CBC_SHA:
	cipherName = "TLS_RSA_PSK_WITH_3DES_EDE_CBC_SHA";
	break;
    case TLS_RSA_PSK_WITH_AES_128_CBC_SHA:
	cipherName = "TLS_RSA_PSK_WITH_AES_128_CBC_SHA";
	break;
    case TLS_RSA_PSK_WITH_AES_256_CBC_SHA:
	cipherName = "TLS_RSA_PSK_WITH_AES_256_CBC_SHA";
	break;
    case TLS_PSK_WITH_NULL_SHA:
	cipherName = "TLS_PSK_WITH_NULL_SHA";
	break;
    case TLS_DHE_PSK_WITH_NULL_SHA:
	cipherName = "TLS_DHE_PSK_WITH_NULL_SHA";
	paramsNeeded = 1;
	break;
    case TLS_RSA_PSK_WITH_NULL_SHA:
	cipherName = "TLS_RSA_PSK_WITH_NULL_SHA";
	break;
    case TLS_RSA_WITH_AES_128_GCM_SHA256:
	cipherName = "TLS_RSA_WITH_AES_128_GCM_SHA256";
	break;
    case TLS_RSA_WITH_AES_256_GCM_SHA384:
	cipherName = "TLS_RSA_WITH_AES_256_GCM_SHA384";
	break;
    case TLS_DHE_RSA_WITH_AES_128_GCM_SHA256:
	cipherName = "TLS_DHE_RSA_WITH_AES_128_GCM_SHA256";
	paramsNeeded = 1;
	break;
    case TLS_DHE_RSA_WITH_AES_256_GCM_SHA384:
	cipherName = "TLS_DHE_RSA_WITH_AES_256_GCM_SHA384";
	paramsNeeded = 1;
	break;
    case TLS_DH_RSA_WITH_AES_128_GCM_SHA256:
	cipherName = "TLS_DH_RSA_WITH_AES_128_GCM_SHA256";
	paramsNeeded = 1;
	break;
    case TLS_DH_RSA_WITH_AES_256_GCM_SHA384:
	cipherName = "TLS_DH_RSA_WITH_AES_256_GCM_SHA384";
	paramsNeeded = 1;
	break;
    case TLS_DHE_DSS_WITH_AES_128_GCM_SHA256:
	cipherName = "TLS_DHE_DSS_WITH_AES_128_GCM_SHA256";
	paramsNeeded = 1;
	break;
    case TLS_DHE_DSS_WITH_AES_256_GCM_SHA384:
	cipherName = "TLS_DHE_DSS_WITH_AES_256_GCM_SHA384";
	paramsNeeded = 1;
	break;
    case TLS_DH_DSS_WITH_AES_128_GCM_SHA256:
	cipherName = "TLS_DH_DSS_WITH_AES_128_GCM_SHA256";
	paramsNeeded = 1;
	break;
    case TLS_DH_DSS_WITH_AES_256_GCM_SHA384:
	cipherName = "TLS_DH_DSS_WITH_AES_256_GCM_SHA384";
	paramsNeeded = 1;
	break;
    case TLS_DH_anon_WITH_AES_128_GCM_SHA256:
	cipherName = "TLS_DH_anon_WITH_AES_128_GCM_SHA256";
	paramsNeeded = 1;
	break;
    case TLS_DH_anon_WITH_AES_256_GCM_SHA384:
	cipherName = "TLS_DH_anon_WITH_AES_256_GCM_SHA384";
	paramsNeeded = 1;
	break;
    case TLS_PSK_WITH_AES_128_GCM_SHA256:
	cipherName = "TLS_PSK_WITH_AES_128_GCM_SHA256";
	break;
    case TLS_PSK_WITH_AES_256_GCM_SHA384:
	cipherName = "TLS_PSK_WITH_AES_256_GCM_SHA384";
	break;
    case TLS_DHE_PSK_WITH_AES_128_GCM_SHA256:
	cipherName = "TLS_DHE_PSK_WITH_AES_128_GCM_SHA256";
	paramsNeeded = 1;
	break;
    case TLS_DHE_PSK_WITH_AES_256_GCM_SHA384:
	cipherName = "TLS_DHE_PSK_WITH_AES_256_GCM_SHA384";
	paramsNeeded = 1;
	break;
    case TLS_RSA_PSK_WITH_AES_128_GCM_SHA256:
	cipherName = "TLS_RSA_PSK_WITH_AES_128_GCM_SHA256";
	break;
    case TLS_RSA_PSK_WITH_AES_256_GCM_SHA384:
	cipherName = "TLS_RSA_PSK_WITH_AES_256_GCM_SHA384";
	break;
    case TLS_PSK_WITH_AES_128_CBC_SHA256:
	cipherName = "TLS_PSK_WITH_AES_128_CBC_SHA256";
	break;
    case TLS_PSK_WITH_AES_256_CBC_SHA384:
	cipherName = "TLS_PSK_WITH_AES_256_CBC_SHA384";
	break;
    case TLS_PSK_WITH_NULL_SHA256:
	cipherName = "TLS_PSK_WITH_NULL_SHA256";
	break;
    case TLS_PSK_WITH_NULL_SHA384:
	cipherName = "TLS_PSK_WITH_NULL_SHA384";
	break;
    case TLS_DHE_PSK_WITH_AES_128_CBC_SHA256:
	cipherName = "TLS_DHE_PSK_WITH_AES_128_CBC_SHA256";
	paramsNeeded = 1;
	break;
    case TLS_DHE_PSK_WITH_AES_256_CBC_SHA384:
	cipherName = "TLS_DHE_PSK_WITH_AES_256_CBC_SHA384";
	paramsNeeded = 1;
	break;
    case TLS_DHE_PSK_WITH_NULL_SHA256:
	cipherName = "TLS_DHE_PSK_WITH_NULL_SHA256";
	paramsNeeded = 1;
	break;
    case TLS_DHE_PSK_WITH_NULL_SHA384:
	cipherName = "TLS_DHE_PSK_WITH_NULL_SHA384";
	paramsNeeded = 1;
	break;
    case TLS_RSA_PSK_WITH_AES_128_CBC_SHA256:
	cipherName = "TLS_RSA_PSK_WITH_AES_128_CBC_SHA256";
	break;
    case TLS_RSA_PSK_WITH_AES_256_CBC_SHA384:
	cipherName = "TLS_RSA_PSK_WITH_AES_256_CBC_SHA384";
	break;
    case TLS_RSA_PSK_WITH_NULL_SHA256:
	cipherName = "TLS_RSA_PSK_WITH_NULL_SHA256";
	break;
    case TLS_RSA_PSK_WITH_NULL_SHA384:
	cipherName = "TLS_RSA_PSK_WITH_NULL_SHA384";
	break;
    case TLS_ECDHE_ECDSA_WITH_AES_128_CBC_SHA256:
	cipherName = "TLS_ECDHE_ECDSA_WITH_AES_128_CBC_SHA256";
	paramsNeeded = 1;
	break;
    case TLS_ECDHE_ECDSA_WITH_AES_256_CBC_SHA384:
	cipherName = "TLS_ECDHE_ECDSA_WITH_AES_256_CBC_SHA384";
	paramsNeeded = 1;
	break;
    case TLS_ECDH_ECDSA_WITH_AES_128_CBC_SHA256:
	cipherName = "TLS_ECDH_ECDSA_WITH_AES_128_CBC_SHA256";
	paramsNeeded = 1;
	break;
    case TLS_ECDH_ECDSA_WITH_AES_256_CBC_SHA384:
	cipherName = "TLS_ECDH_ECDSA_WITH_AES_256_CBC_SHA384";
	paramsNeeded = 1;
	break;
    case TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA256:
	cipherName = "TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA256";
	paramsNeeded = 1;
	break;
    case TLS_ECDHE_RSA_WITH_AES_256_CBC_SHA384:
	cipherName = "TLS_ECDHE_RSA_WITH_AES_256_CBC_SHA384";
	paramsNeeded = 1;
	break;
    case TLS_ECDH_RSA_WITH_AES_128_CBC_SHA256:
	cipherName = "TLS_ECDH_RSA_WITH_AES_128_CBC_SHA256";
	paramsNeeded = 1;
	break;
    case TLS_ECDH_RSA_WITH_AES_256_CBC_SHA384:
	cipherName = "TLS_ECDH_RSA_WITH_AES_256_CBC_SHA384";
	paramsNeeded = 1;
	break;
    case TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256:
	cipherName = "TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256";
	paramsNeeded = 1;
	break;
    case TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384:
	cipherName = "TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384";
	paramsNeeded = 1;
	break;
    case TLS_ECDH_ECDSA_WITH_AES_128_GCM_SHA256:
	cipherName = "TLS_ECDH_ECDSA_WITH_AES_128_GCM_SHA256";
	paramsNeeded = 1;
	break;
    case TLS_ECDH_ECDSA_WITH_AES_256_GCM_SHA384:
	cipherName = "TLS_ECDH_ECDSA_WITH_AES_256_GCM_SHA384";
	paramsNeeded = 1;
	break;
    case TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256:
	cipherName = "TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256";
	paramsNeeded = 1;
	break;
    case TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384:
	cipherName = "TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384";
	paramsNeeded = 1;
	break;
    case TLS_ECDH_RSA_WITH_AES_128_GCM_SHA256:
	cipherName = "TLS_ECDH_RSA_WITH_AES_128_GCM_SHA256";
	paramsNeeded = 1;
	break;
    case TLS_ECDH_RSA_WITH_AES_256_GCM_SHA384:
	cipherName = "TLS_ECDH_RSA_WITH_AES_256_GCM_SHA384";
	paramsNeeded = 1;
	break;
    case TLS_RSA_WITH_AES_128_CBC_SHA:
	cipherName = "TLS_RSA_WITH_AES_128_CBC_SHA";
	break;
    case TLS_DH_DSS_WITH_AES_128_CBC_SHA:
	cipherName = "TLS_DH_DSS_WITH_AES_128_CBC_SHA";
	paramsNeeded = 1;
	break;
    case TLS_DH_RSA_WITH_AES_128_CBC_SHA:
	cipherName = "TLS_DH_RSA_WITH_AES_128_CBC_SHA";
	paramsNeeded = 1;
	break;
    case TLS_DHE_DSS_WITH_AES_128_CBC_SHA:
	cipherName = "TLS_DHE_DSS_WITH_AES_128_CBC_SHA";
	paramsNeeded = 1;
	break;
    case TLS_DHE_RSA_WITH_AES_128_CBC_SHA:
	cipherName = "TLS_DHE_RSA_WITH_AES_128_CBC_SHA";
	paramsNeeded = 1;
	break;
    case TLS_DH_anon_WITH_AES_128_CBC_SHA:
	cipherName = "TLS_DH_anon_WITH_AES_128_CBC_SHA";
	paramsNeeded = 1;
	break;
    case TLS_RSA_WITH_AES_256_CBC_SHA:
	cipherName = "TLS_RSA_WITH_AES_256_CBC_SHA";
	break;
    case TLS_DH_DSS_WITH_AES_256_CBC_SHA:
	cipherName = "TLS_DH_DSS_WITH_AES_256_CBC_SHA";
	paramsNeeded = 1;
	break;
    case TLS_DH_RSA_WITH_AES_256_CBC_SHA:
	cipherName = "TLS_DH_RSA_WITH_AES_256_CBC_SHA";
	paramsNeeded = 1;
	break;
    case TLS_DHE_DSS_WITH_AES_256_CBC_SHA:
	cipherName = "TLS_DHE_DSS_WITH_AES_256_CBC_SHA";
	paramsNeeded = 1;
	break;
    case TLS_DHE_RSA_WITH_AES_256_CBC_SHA:
	cipherName = "TLS_DHE_RSA_WITH_AES_256_CBC_SHA";
	paramsNeeded = 1;
	break;
    case TLS_DH_anon_WITH_AES_256_CBC_SHA:
	cipherName = "TLS_DH_anon_WITH_AES_256_CBC_SHA";
	paramsNeeded = 1;
	break;
    case TLS_ECDH_ECDSA_WITH_NULL_SHA:
	cipherName = "TLS_ECDH_ECDSA_WITH_NULL_SHA";
	paramsNeeded = 1;
	break;
    case TLS_ECDH_ECDSA_WITH_RC4_128_SHA:
	cipherName = "TLS_ECDH_ECDSA_WITH_RC4_128_SHA";
	paramsNeeded = 1;
	break;
    case TLS_ECDH_ECDSA_WITH_3DES_EDE_CBC_SHA:
	cipherName = "TLS_ECDH_ECDSA_WITH_3DES_EDE_CBC_SHA";
	paramsNeeded = 1;
	break;
    case TLS_ECDH_ECDSA_WITH_AES_128_CBC_SHA:
	cipherName = "TLS_ECDH_ECDSA_WITH_AES_128_CBC_SHA";
	paramsNeeded = 1;
	break;
    case TLS_ECDH_ECDSA_WITH_AES_256_CBC_SHA:
	cipherName = "TLS_ECDH_ECDSA_WITH_AES_256_CBC_SHA";
	paramsNeeded = 1;
	break;
    case TLS_ECDHE_ECDSA_WITH_NULL_SHA:
	cipherName = "TLS_ECDHE_ECDSA_WITH_NULL_SHA";
	paramsNeeded = 1;
	break;
    case TLS_ECDHE_ECDSA_WITH_RC4_128_SHA:
	cipherName = "TLS_ECDHE_ECDSA_WITH_RC4_128_SHA";
	paramsNeeded = 1;
	break;
    case TLS_ECDHE_ECDSA_WITH_3DES_EDE_CBC_SHA:
	cipherName = "TLS_ECDHE_ECDSA_WITH_3DES_EDE_CBC_SHA";
	paramsNeeded = 1;
	break;
    case TLS_ECDHE_ECDSA_WITH_AES_128_CBC_SHA:
	cipherName = "TLS_ECDHE_ECDSA_WITH_AES_128_CBC_SHA";
	paramsNeeded = 1;
	break;
    case TLS_ECDHE_ECDSA_WITH_AES_256_CBC_SHA:
	cipherName = "TLS_ECDHE_ECDSA_WITH_AES_256_CBC_SHA";
	paramsNeeded = 1;
	break;
    case TLS_ECDH_RSA_WITH_NULL_SHA:
	cipherName = "TLS_ECDH_RSA_WITH_NULL_SHA";
	paramsNeeded = 1;
	break;
    case TLS_ECDH_RSA_WITH_RC4_128_SHA:
	cipherName = "TLS_ECDH_RSA_WITH_RC4_128_SHA";
	paramsNeeded = 1;
	break;
    case TLS_ECDH_RSA_WITH_3DES_EDE_CBC_SHA:
	cipherName = "TLS_ECDH_RSA_WITH_3DES_EDE_CBC_SHA";
	paramsNeeded = 1;
	break;
    case TLS_ECDH_RSA_WITH_AES_128_CBC_SHA:
	cipherName = "TLS_ECDH_RSA_WITH_AES_128_CBC_SHA";
	paramsNeeded = 1;
	break;
    case TLS_ECDH_RSA_WITH_AES_256_CBC_SHA:
	cipherName = "TLS_ECDH_RSA_WITH_AES_256_CBC_SHA";
	paramsNeeded = 1;
	break;
    case TLS_ECDHE_RSA_WITH_NULL_SHA:
	cipherName = "TLS_ECDHE_RSA_WITH_NULL_SHA";
	paramsNeeded = 1;
	break;
    case TLS_ECDHE_RSA_WITH_RC4_128_SHA:
	cipherName = "TLS_ECDHE_RSA_WITH_RC4_128_SHA";
	paramsNeeded = 1;
	break;
    case TLS_ECDHE_RSA_WITH_3DES_EDE_CBC_SHA:
	cipherName = "TLS_ECDHE_RSA_WITH_3DES_EDE_CBC_SHA";
	paramsNeeded = 1;
	break;
    case TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA:
	cipherName = "TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA";
	paramsNeeded = 1;
	break;
    case TLS_ECDHE_RSA_WITH_AES_256_CBC_SHA:
	cipherName = "TLS_ECDHE_RSA_WITH_AES_256_CBC_SHA";
	paramsNeeded = 1;
	break;
    case TLS_ECDH_anon_WITH_NULL_SHA:
	cipherName = "TLS_ECDH_anon_WITH_NULL_SHA";
	paramsNeeded = 1;
	break;
    case TLS_ECDH_anon_WITH_RC4_128_SHA:
	cipherName = "TLS_ECDH_anon_WITH_RC4_128_SHA";
	paramsNeeded = 1;
	break;
    case TLS_ECDH_anon_WITH_3DES_EDE_CBC_SHA:
	cipherName = "TLS_ECDH_anon_WITH_3DES_EDE_CBC_SHA";
	paramsNeeded = 1;
	break;
    case TLS_ECDH_anon_WITH_AES_128_CBC_SHA:
	cipherName = "TLS_ECDH_anon_WITH_AES_128_CBC_SHA";
	paramsNeeded = 1;
	break;
    case TLS_ECDH_anon_WITH_AES_256_CBC_SHA:
	cipherName = "TLS_ECDH_anon_WITH_AES_256_CBC_SHA";
	paramsNeeded = 1;
	break;
    default :
        snprintf(unknownCipherName, sizeof(unknownCipherName), "UNKNOWN_%04X", cipher);
        cipherName = unknownCipherName;
        break;
  }

  if (cipher == TLS_RSA_WITH_RC4_128_MD5 ||
      cipher == TLS_RSA_WITH_RC4_128_SHA)
  {
    printf("%s: ERROR (Printers MUST NOT negotiate RC4 cipher suites.)\n", server);
    httpClose(http);
    return (1);
  }

  if ((err = SSLGetDiffieHellmanParams(http->tls, &params, &paramsLen)) != noErr && paramsNeeded)
  {
    printf("%s: ERROR (Unable to get Diffie-Hellman parameters - %d)\n", server, (int)err);
    httpClose(http);
    return (1);
  }

  if (paramsLen < 128 && paramsLen != 0)
  {
    printf("%s: ERROR (Diffie-Hellman parameters MUST be at least 2048 bits, but Printer uses only %d bits/%d bytes)\n", server, (int)paramsLen * 8, (int)paramsLen);
    httpClose(http);
    return (1);
  }

  dhBits = (int)paramsLen * 8;
#endif /* HAVE_OPENSSL */

  if (dhBits > 0)
    printf("%s: OK (TLS: %d.%d, %s, %d DH bits)\n", server, tlsVersion / 10, tlsVersion % 10, cipherName, dhBits);
  else
    printf("%s: OK (TLS: %d.%d, %s)\n", server, tlsVersion / 10, tlsVersion % 10, cipherName);

  printf("    %s\n", creds_str);

  if (verbose)
  {
    httpAssembleURI(HTTP_URI_CODING_ALL, uri, sizeof(uri), "ipps", NULL, server, port, resource);
    request = ippNewRequest(IPP_OP_GET_PRINTER_ATTRIBUTES);
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri", NULL, uri);
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME, "requesting-user-name", NULL, cupsUser());
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


/*
 * 'usage()' - Show program usage.
 */

static void
usage(void)
{
  puts("Usage: ./tlscheck [options] server [port]");
  puts("       ./tlscheck [options] ipps://server[:port]/path");
  puts("");
  puts("Options:");
  puts("  --dh        Allow DH/DHE key exchange");
  puts("  --no-cbc    Disable CBC cipher suites");
  puts("  --no-tls10  Disable TLS/1.0");
  puts("  --rc4       Allow RC4 encryption");
  puts("  --tls10     Only use TLS/1.0");
  puts("  --tls11     Only use TLS/1.1");
  puts("  --tls12     Only use TLS/1.2");
  puts("  --tls13     Only use TLS/1.3");
  puts("  --verbose   Be verbose");
  puts("  -4          Connect using IPv4 addresses only");
  puts("  -6          Connect using IPv6 addresses only");
  puts("  -v          Be verbose");
  puts("");
  puts("The default port is 631.");

  exit(1);
}
#endif /* !HAVE_TLS */
