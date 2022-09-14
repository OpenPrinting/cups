/*
 * TLS support code for CUPS using OpenSSL/LibreSSL.
 *
 * Copyright © 2020-2022 by OpenPrinting
 * Copyright © 2007-2019 by Apple Inc.
 * Copyright © 1997-2007 by Easy Software Products, all rights reserved.
 *
 * Licensed under Apache License v2.0.  See the file "LICENSE" for more
 * information.
 */

/**** This file is included from tls.c ****/

/*
 * Include necessary headers...
 */

#include <sys/stat.h>
#include <openssl/x509v3.h>


/*
 * Local functions...
 */

static long		http_bio_ctrl(BIO *h, int cmd, long arg1, void *arg2);
static int		http_bio_free(BIO *data);
static int		http_bio_new(BIO *h);
static int		http_bio_puts(BIO *h, const char *str);
static int		http_bio_read(BIO *h, char *buf, int size);
static int		http_bio_write(BIO *h, const char *buf, int num);

static X509		*http_create_credential(http_credential_t *credential);
static const char	*http_default_path(char *buffer, size_t bufsize);
static time_t		http_get_date(X509 *cert, int which);
//static void		http_load_crl(void);
static const char	*http_make_path(char *buffer, size_t bufsize, const char *dirname, const char *filename, const char *ext);
static int		http_x509_add_ext(X509 *cert, int nid, const char *value);
static void		http_x509_add_san(X509 *cert, const char *name);


/*
 * Local globals...
 */

static int		tls_auto_create = 0;
					/* Auto-create self-signed certs? */
static BIO_METHOD	*tls_bio_method = NULL;
					/* OpenSSL BIO method */
static char		*tls_common_name = NULL;
					/* Default common name */
//static X509_CRL		*tls_crl = NULL;/* Certificate revocation list */
static char		*tls_keypath = NULL;
					/* Server cert keychain path */
static _cups_mutex_t	tls_mutex = _CUPS_MUTEX_INITIALIZER;
					/* Mutex for keychain/certs */
static int		tls_options = -1,/* Options for TLS connections */
			tls_min_version = _HTTP_TLS_1_0,
			tls_max_version = _HTTP_TLS_MAX;


/*
 * 'cupsMakeServerCredentials()' - Make a self-signed certificate and private key pair.
 *
 * @since CUPS 2.0/OS 10.10@
 */

int					// O - 1 on success, 0 on failure
cupsMakeServerCredentials(
    const char *path,			// I - Path to keychain/directory
    const char *common_name,		// I - Common name
    int        num_alt_names,		// I - Number of subject alternate names
    const char **alt_names,		// I - Subject Alternate Names
    time_t     expiration_date)		// I - Expiration date
{
  int		result = 0;		// Return value
  EVP_PKEY	*pkey;			// Private key
  RSA		*rsa;			// RSA key pair
  X509		*cert;			// Certificate
  cups_lang_t	*language;		// Default language info
  time_t	curtime;		// Current time
  X509_NAME	*name;			// Subject/issuer name
  ASN1_INTEGER	*serial;		// Serial number
  ASN1_TIME	*notBefore,		// Initial date
		*notAfter;		// Expiration date
  BIO		*bio;			// Output file
  char		temp[1024],		// Temporary directory name
 		crtfile[1024],		// Certificate filename
		keyfile[1024];		// Private key filename
  const char	*common_ptr;		// Pointer into common name


  DEBUG_printf(("cupsMakeServerCredentials(path=\"%s\", common_name=\"%s\", num_alt_names=%d, alt_names=%p, expiration_date=%d)", path, common_name, num_alt_names, alt_names, (int)expiration_date));

  // Filenames...
  if (!path)
    path = http_default_path(temp, sizeof(temp));

  if (!path || !common_name)
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, strerror(EINVAL), 0);
    return (0);
  }

  http_make_path(crtfile, sizeof(crtfile), path, common_name, "crt");
  http_make_path(keyfile, sizeof(keyfile), path, common_name, "key");

  // Create the encryption key...
  DEBUG_puts("1cupsMakeServerCredentials: Creating key pair.");

  if ((rsa = RSA_generate_key(3072, RSA_F4, NULL, NULL)) == NULL)
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Unable to create key pair."), 1);
    return (0);
  }

  if ((pkey = EVP_PKEY_new()) == NULL)
  {
    RSA_free(rsa);
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Unable to create private key."), 1);
    return (0);
  }

  EVP_PKEY_assign_RSA(pkey, rsa);

  DEBUG_puts("1cupsMakeServerCredentials: Key pair created.");

  // Create the X.509 certificate...
  DEBUG_puts("1cupsMakeServerCredentials: Generating self-signed X.509 certificate.");

  if ((cert = X509_new()) == NULL)
  {
    EVP_PKEY_free(pkey);
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Unable to create X.509 certificate."), 1);
    return (0);
  }

  curtime = time(NULL);

  notBefore = ASN1_TIME_new();
  ASN1_TIME_set(notBefore, curtime);
  X509_set_notBefore(cert, notBefore);
  ASN1_TIME_free(notBefore);

  notAfter  = ASN1_TIME_new();
  ASN1_TIME_set(notAfter, expiration_date);
  X509_set_notAfter(cert, notAfter);
  ASN1_TIME_free(notAfter);

  serial = ASN1_INTEGER_new();
  ASN1_INTEGER_set(serial, (int)curtime);
  X509_set_serialNumber(cert, serial);
  ASN1_INTEGER_free(serial);

  X509_set_pubkey(cert, pkey);

  language = cupsLangDefault();
  name     = X509_NAME_new();
  if (strlen(language->language) == 5)
    X509_NAME_add_entry_by_txt(name, SN_countryName, MBSTRING_ASC, (unsigned char *)language->language + 3, -1, -1, 0);
  else
    X509_NAME_add_entry_by_txt(name, SN_countryName, MBSTRING_ASC, (unsigned char *)"US", -1, -1, 0);
  X509_NAME_add_entry_by_txt(name, SN_commonName, MBSTRING_ASC, (unsigned char *)common_name, -1, -1, 0);
  X509_NAME_add_entry_by_txt(name, SN_organizationName, MBSTRING_ASC, (unsigned char *)common_name, -1, -1, 0);
  X509_NAME_add_entry_by_txt(name, SN_organizationalUnitName, MBSTRING_ASC, (unsigned char *)"Unknown", -1, -1, 0);
  X509_NAME_add_entry_by_txt(name, SN_stateOrProvinceName, MBSTRING_ASC, (unsigned char *)"Unknown", -1, -1, 0);
  X509_NAME_add_entry_by_txt(name, SN_localityName, MBSTRING_ASC, (unsigned char *)"Unknown", -1, -1, 0);

  X509_set_issuer_name(cert, name);
  X509_set_subject_name(cert, name);
  X509_NAME_free(name);

  http_x509_add_san(cert, common_name);
  if ((common_ptr = strstr(common_name, ".local")) == NULL)
  {
    // Add common_name.local to the list, too...
    char	localname[256],		// hostname.local
		*localptr;		// Pointer into localname

    strlcpy(localname, common_name, sizeof(localname));
    if ((localptr = strchr(localname, '.')) != NULL)
      *localptr = '\0';
    strlcat(localname, ".local", sizeof(localname));

    http_x509_add_san(cert, localname);
  }

  if (num_alt_names > 0)
  {
    int i;                              // Looping var...

    for (i = 0; i < num_alt_names; i ++)
    {
      if (strcmp(alt_names[i], "localhost"))
        http_x509_add_san(cert, alt_names[i]);
    }
  }

  // Add extensions that are required to make Chrome happy...
  http_x509_add_ext(cert, NID_basic_constraints, "critical,CA:FALSE,pathlen:0");
  http_x509_add_ext(cert, NID_key_usage, "critical,digitalSignature,keyEncipherment");
  http_x509_add_ext(cert, NID_ext_key_usage, "1.3.6.1.5.5.7.3.1");
  http_x509_add_ext(cert, NID_subject_key_identifier, "hash");
  http_x509_add_ext(cert, NID_authority_key_identifier, "keyid,issuer");
  X509_set_version(cert, 2); // v3

  X509_sign(cert, pkey, EVP_sha256());

  // Save them...
  if ((bio = BIO_new_file(keyfile, "wb")) == NULL)
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, strerror(errno), 0);
    goto done;
  }

  if (!PEM_write_bio_PrivateKey(bio, pkey, NULL, NULL, 0, NULL, NULL))
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Unable to write private key."), 1);
    BIO_free(bio);
    goto done;
  }

  BIO_free(bio);

  if ((bio = BIO_new_file(crtfile, "wb")) == NULL)
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, strerror(errno), 0);
    goto done;
  }

  if (!PEM_write_bio_X509(bio, cert))
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Unable to write X.509 certificate."), 1);
    BIO_free(bio);
    goto done;
  }

  BIO_free(bio);

  result = 1;
  DEBUG_puts("1cupsMakeServerCredentials: Successfully created credentials.");

  // Cleanup...
  done:

  X509_free(cert);
  EVP_PKEY_free(pkey);

  return (result);
}


/*
 * 'cupsSetServerCredentials()' - Set the default server credentials.
 *
 * Note: The server credentials are used by all threads in the running process.
 * This function is threadsafe.
 *
 * @since CUPS 2.0/OS 10.10@
 */

int					// O - 1 on success, 0 on failure
cupsSetServerCredentials(
    const char *path,			// I - Path to keychain/directory
    const char *common_name,		// I - Default common name for server
    int        auto_create)		// I - 1 = automatically create self-signed certificates
{
  char	temp[1024];			// Default path buffer


  DEBUG_printf(("cupsSetServerCredentials(path=\"%s\", common_name=\"%s\", auto_create=%d)", path, common_name, auto_create));

 /*
  * Use defaults as needed...
  */

  if (!path)
    path = http_default_path(temp, sizeof(temp));

 /*
  * Range check input...
  */

  if (!path || !common_name)
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, strerror(EINVAL), 0);
    return (0);
  }

  _cupsMutexLock(&tls_mutex);

 /*
  * Free old values...
  */

  if (tls_keypath)
    _cupsStrFree(tls_keypath);

  if (tls_common_name)
    _cupsStrFree(tls_common_name);

 /*
  * Save the new values...
  */

  tls_keypath     = _cupsStrAlloc(path);
  tls_auto_create = auto_create;
  tls_common_name = _cupsStrAlloc(common_name);

  _cupsMutexUnlock(&tls_mutex);

  return (1);
}


/*
 * 'httpCopyCredentials()' - Copy the credentials associated with the peer in
 *                           an encrypted connection.
 *
 * @since CUPS 1.5/macOS 10.7@
 */

int					// O - Status of call (0 = success)
httpCopyCredentials(
    http_t	 *http,			// I - Connection to server
    cups_array_t **credentials)		// O - Array of credentials
{
  STACK_OF(X509) *chain;		// Certificate chain


  DEBUG_printf(("httpCopyCredentials(http=%p, credentials=%p)", http, credentials));

  if (credentials)
    *credentials = NULL;

  if (!http || !http->tls || !credentials)
    return (-1);

  *credentials = cupsArrayNew(NULL, NULL);
  chain        = SSL_get_peer_cert_chain(http->tls);

  DEBUG_printf(("1httpCopyCredentials: chain=%p", chain));

  if (chain)
  {
    int	i,				// Looping var
	count;				// Number of certs

    for (i = 0, count = sk_X509_num(chain); i < count; i ++)
    {
      X509	*cert = sk_X509_value(chain, i);
					// Current certificate
      BIO	*bio = BIO_new(BIO_s_mem());
					// Memory buffer for cert

      if (bio)
      {
	long	bytes;			// Number of bytes
	char	*buffer;		// Pointer to bytes

	if (PEM_write_bio_X509(bio, cert))
	{
	  bytes = BIO_get_mem_data(bio, &buffer);
	  httpAddCredential(*credentials, buffer, (int)bytes);
	}

	BIO_free(bio);
      }
    }
  }

  return (0);
}


/*
 * '_httpCreateCredentials()' - Create credentials in the internal format.
 */

http_tls_credentials_t			// O - Internal credentials
_httpCreateCredentials(
    cups_array_t *credentials)		// I - Array of credentials
{
  (void)credentials;

  return (NULL);
}


/*
 * '_httpFreeCredentials()' - Free internal credentials.
 */

void
_httpFreeCredentials(
    http_tls_credentials_t credentials)	// I - Internal credentials
{
  X509_free(credentials);
}


/*
 * 'httpCredentialsAreValidForName()' - Return whether the credentials are valid for the given name.
 *
 * @since CUPS 2.0/OS 10.10@
 */

int					// O - 1 if valid, 0 otherwise
httpCredentialsAreValidForName(
    cups_array_t *credentials,		// I - Credentials
    const char   *common_name)		// I - Name to check
{
  X509	*cert;				// Certificate
  int	result = 0;			// Result


  cert = http_create_credential((http_credential_t *)cupsArrayFirst(credentials));
  if (cert)
  {
    result = X509_check_host(cert, common_name, strlen(common_name), 0, NULL);

    X509_free(cert);
  }

  return (result);
}


/*
 * 'httpCredentialsGetTrust()' - Return the trust of credentials.
 *
 * @since CUPS 2.0/OS 10.10@
 */

http_trust_t				// O - Level of trust
httpCredentialsGetTrust(
    cups_array_t *credentials,		// I - Credentials
    const char   *common_name)		// I - Common name for trust lookup
{
  http_trust_t	trust = HTTP_TRUST_OK;	// Trusted?
  X509		*cert;			// Certificate
  cups_array_t	*tcreds = NULL;		// Trusted credentials
  _cups_globals_t *cg = _cupsGlobals();	// Per-thread globals


  if (!common_name)
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("No common name specified."), 1);
    return (HTTP_TRUST_UNKNOWN);
  }

  if ((cert = http_create_credential((http_credential_t *)cupsArrayFirst(credentials))) == NULL)
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Unable to create credentials from array."), 1);
    return (HTTP_TRUST_UNKNOWN);
  }

  if (cg->any_root < 0)
  {
    _cupsSetDefaults();
//    http_load_crl();
  }

  // Look this common name up in the default keychains...
  httpLoadCredentials(NULL, &tcreds, common_name);

  if (tcreds)
  {
    char	credentials_str[1024],	/* String for incoming credentials */
		tcreds_str[1024];	/* String for saved credentials */

    httpCredentialsString(credentials, credentials_str, sizeof(credentials_str));
    httpCredentialsString(tcreds, tcreds_str, sizeof(tcreds_str));

    if (strcmp(credentials_str, tcreds_str))
    {
      // Credentials don't match, let's look at the expiration date of the new
      // credentials and allow if the new ones have a later expiration...
      if (!cg->trust_first)
      {
        // Do not trust certificates on first use...
        _cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Trust on first use is disabled."), 1);

        trust = HTTP_TRUST_INVALID;
      }
      else if (httpCredentialsGetExpiration(credentials) <= httpCredentialsGetExpiration(tcreds))
      {
        // The new credentials are not newly issued...
        _cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("New credentials are older than stored credentials."), 1);

        trust = HTTP_TRUST_INVALID;
      }
      else if (!httpCredentialsAreValidForName(credentials, common_name))
      {
        // The common name does not match the issued certificate...
        _cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("New credentials are not valid for name."), 1);

        trust = HTTP_TRUST_INVALID;
      }
      else if (httpCredentialsGetExpiration(tcreds) < time(NULL))
      {
        // Save the renewed credentials...
	trust = HTTP_TRUST_RENEWED;

        httpSaveCredentials(NULL, credentials, common_name);
      }
    }

    httpFreeCredentials(tcreds);
  }
  else if (cg->validate_certs && !httpCredentialsAreValidForName(credentials, common_name))
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("No stored credentials, not valid for name."), 1);
    trust = HTTP_TRUST_INVALID;
  }
  else if (!cg->trust_first)
  {
    // See if we have a site CA certificate we can compare...
    if (!httpLoadCredentials(NULL, &tcreds, "site"))
    {
      if (cupsArrayCount(credentials) != (cupsArrayCount(tcreds) + 1))
      {
        // Certificate isn't directly generated from the CA cert...
        trust = HTTP_TRUST_INVALID;
      }
      else
      {
        // Do a tail comparison of the two certificates...
        http_credential_t	*a, *b;		// Certificates

        for (a = (http_credential_t *)cupsArrayFirst(tcreds), b = (http_credential_t *)cupsArrayIndex(credentials, 1); a && b; a = (http_credential_t *)cupsArrayNext(tcreds), b = (http_credential_t *)cupsArrayNext(credentials))
        {
	  if (a->datalen != b->datalen || memcmp(a->data, b->data, a->datalen))
	    break;
	}

        if (a || b)
	  trust = HTTP_TRUST_INVALID;
      }

      if (trust != HTTP_TRUST_OK)
	_cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Credentials do not validate against site CA certificate."), 1);
    }
    else
    {
      _cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Trust on first use is disabled."), 1);
      trust = HTTP_TRUST_INVALID;
    }
  }

  if (trust == HTTP_TRUST_OK && !cg->expired_certs)
  {
    time_t	curtime;		// Current date/time

    time(&curtime);
    if (curtime < http_get_date(cert, 0) || curtime > http_get_date(cert, 1))
    {
      _cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Credentials have expired."), 1);
      trust = HTTP_TRUST_EXPIRED;
    }
  }

  if (trust == HTTP_TRUST_OK && !cg->any_root && cupsArrayCount(credentials) == 1)
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Self-signed credentials are blocked."), 1);
    trust = HTTP_TRUST_INVALID;
  }

  X509_free(cert);

  return (trust);
}


/*
 * 'httpCredentialsGetExpiration()' - Return the expiration date of the credentials.
 *
 * @since CUPS 2.0/OS 10.10@
 */

time_t					// O - Expiration date of credentials
httpCredentialsGetExpiration(
    cups_array_t *credentials)		// I - Credentials
{
  time_t	result = 0;		// Result
  X509		*cert;			// Certificate


  if ((cert = http_create_credential((http_credential_t *)cupsArrayFirst(credentials))) != NULL)
  {
    result = http_get_date(cert, 1);
    X509_free(cert);
  }

  return (result);
}


/*
 * 'httpCredentialsString()' - Return a string representing the credentials.
 *
 * @since CUPS 2.0/OS 10.10@
 */

size_t					// O - Total size of credentials string
httpCredentialsString(
    cups_array_t *credentials,		// I - Credentials
    char         *buffer,		// I - Buffer
    size_t       bufsize)		// I - Size of buffer
{
  http_credential_t	*first;		// First certificate
  X509			*cert;		// Certificate


  DEBUG_printf(("httpCredentialsString(credentials=%p, buffer=%p, bufsize=" CUPS_LLFMT ")", credentials, buffer, CUPS_LLCAST bufsize));

  if (!buffer)
    return (0);

  if (bufsize > 0)
    *buffer = '\0';

  first = (http_credential_t *)cupsArrayFirst(credentials);
  cert  = http_create_credential(first);

  if (cert)
  {
    char		name[256],	// Common name associated with cert
			issuer[256];	// Issuer associated with cert
    time_t		expiration;	// Expiration date of cert
    const char		*sigalg;	// Signature algorithm
    unsigned char	md5_digest[16];	// MD5 result


    X509_NAME_get_text_by_NID(X509_get_subject_name(cert), NID_commonName, name, sizeof(name));
    X509_NAME_get_text_by_NID(X509_get_issuer_name(cert), NID_commonName, issuer, sizeof(issuer));
    expiration = http_get_date(cert, 1);

    switch (X509_get_signature_nid(cert))
    {
      case NID_ecdsa_with_SHA1 :
          sigalg = "SHA1WithECDSAEncryption";
          break;
      case NID_ecdsa_with_SHA224 :
          sigalg = "SHA224WithECDSAEncryption";
          break;
      case NID_ecdsa_with_SHA256 :
          sigalg = "SHA256WithECDSAEncryption";
          break;
      case NID_ecdsa_with_SHA384 :
          sigalg = "SHA384WithECDSAEncryption";
          break;
      case NID_ecdsa_with_SHA512 :
          sigalg = "SHA512WithECDSAEncryption";
          break;
      case NID_sha1WithRSAEncryption :
          sigalg = "SHA1WithRSAEncryption";
          break;
      case NID_sha224WithRSAEncryption :
          sigalg = "SHA224WithRSAEncryption";
          break;
      case NID_sha256WithRSAEncryption :
          sigalg = "SHA256WithRSAEncryption";
          break;
      case NID_sha384WithRSAEncryption :
          sigalg = "SHA384WithRSAEncryption";
          break;
      case NID_sha512WithRSAEncryption :
          sigalg = "SHA512WithRSAEncryption";
          break;
      default :
          sigalg = "Unknown";
          break;
    }

    cupsHashData("md5", first->data, first->datalen, md5_digest, sizeof(md5_digest));

    snprintf(buffer, bufsize, "%s (issued by %s) / %s / %s / %02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X", name, issuer, httpGetDateString(expiration), sigalg, md5_digest[0], md5_digest[1], md5_digest[2], md5_digest[3], md5_digest[4], md5_digest[5], md5_digest[6], md5_digest[7], md5_digest[8], md5_digest[9], md5_digest[10], md5_digest[11], md5_digest[12], md5_digest[13], md5_digest[14], md5_digest[15]);
    X509_free(cert);
  }

  DEBUG_printf(("1httpCredentialsString: Returning \"%s\".", buffer));

  return (strlen(buffer));
}


/*
 * 'httpLoadCredentials()' - Load X.509 credentials from a keychain file.
 *
 * @since CUPS 2.0/OS 10.10@
 */

int					// O - 0 on success, -1 on error
httpLoadCredentials(
    const char   *path,			// I  - Keychain/PKCS#12 path
    cups_array_t **credentials,		// IO - Credentials
    const char   *common_name)		// I  - Common name for credentials
{
  cups_file_t		*fp;		// Certificate file
  char			filename[1024],	// filename.crt
			temp[1024],	// Temporary string
			line[256];	// Base64-encoded line
  unsigned char		*data = NULL;	// Buffer for cert data
  size_t		alloc_data = 0,	// Bytes allocated
			num_data = 0;	// Bytes used
  int			decoded;	// Bytes decoded
  int			in_certificate = 0;
					// In a certificate?


  if (!credentials || !common_name)
    return (-1);

  if (!path)
    path = http_default_path(temp, sizeof(temp));
  if (!path)
    return (-1);

  http_make_path(filename, sizeof(filename), path, common_name, "crt");

  if ((fp = cupsFileOpen(filename, "r")) == NULL)
    return (-1);

  while (cupsFileGets(fp, line, sizeof(line)))
  {
    if (!strcmp(line, "-----BEGIN CERTIFICATE-----"))
    {
      if (in_certificate)
      {
       /*
	* Missing END CERTIFICATE...
	*/

        httpFreeCredentials(*credentials);
	*credentials = NULL;
        break;
      }

      in_certificate = 1;
    }
    else if (!strcmp(line, "-----END CERTIFICATE-----"))
    {
      if (!in_certificate || !num_data)
      {
       /*
	* Missing data...
	*/

        httpFreeCredentials(*credentials);
	*credentials = NULL;
        break;
      }

      if (!*credentials)
        *credentials = cupsArrayNew(NULL, NULL);

      if (httpAddCredential(*credentials, data, num_data))
      {
        httpFreeCredentials(*credentials);
	*credentials = NULL;
        break;
      }

      num_data       = 0;
      in_certificate = 0;
    }
    else if (in_certificate)
    {
      if (alloc_data == 0)
      {
        data       = malloc(2048);
	alloc_data = 2048;

        if (!data)
	  break;
      }
      else if ((num_data + strlen(line)) >= alloc_data)
      {
        unsigned char *tdata = realloc(data, alloc_data + 1024);
					/* Expanded buffer */

	if (!tdata)
	{
	  httpFreeCredentials(*credentials);
	  *credentials = NULL;
	  break;
	}

	data       = tdata;
        alloc_data += 1024;
      }

      decoded = alloc_data - num_data;
      httpDecode64_2((char *)data + num_data, &decoded, line);
      num_data += (size_t)decoded;
    }
  }

  cupsFileClose(fp);

  if (in_certificate)
  {
   /*
    * Missing END CERTIFICATE...
    */

    httpFreeCredentials(*credentials);
    *credentials = NULL;
  }

  if (data)
    free(data);

  return (*credentials ? 0 : -1);
}


/*
 * 'httpSaveCredentials()' - Save X.509 credentials to a keychain file.
 *
 * @since CUPS 2.0/OS 10.10@
 */

int					// O - -1 on error, 0 on success
httpSaveCredentials(
    const char   *path,			// I - Keychain/PKCS#12 path
    cups_array_t *credentials,		// I - Credentials
    const char   *common_name)		// I - Common name for credentials
{
  cups_file_t		*fp;		// Certificate file
  char			filename[1024],	// filename.crt
			nfilename[1024],// filename.crt.N
			temp[1024],	// Temporary string
			line[256];	// Base64-encoded line
  const unsigned char	*ptr;		// Pointer into certificate
  ssize_t		remaining;	// Bytes left
  http_credential_t	*cred;		// Current credential


  if (!credentials || !common_name)
    return (-1);

  if (!path)
    path = http_default_path(temp, sizeof(temp));
  if (!path)
    return (-1);

  http_make_path(filename, sizeof(filename), path, common_name, "crt");
  snprintf(nfilename, sizeof(nfilename), "%s.N", filename);

  if ((fp = cupsFileOpen(nfilename, "w")) == NULL)
    return (-1);

#ifndef _WIN32
  fchmod(cupsFileNumber(fp), 0600);
#endif // !_WIN32

  for (cred = (http_credential_t *)cupsArrayFirst(credentials);
       cred;
       cred = (http_credential_t *)cupsArrayNext(credentials))
  {
    cupsFilePuts(fp, "-----BEGIN CERTIFICATE-----\n");
    for (ptr = cred->data, remaining = (ssize_t)cred->datalen; remaining > 0; remaining -= 45, ptr += 45)
    {
      httpEncode64_2(line, sizeof(line), (char *)ptr, remaining > 45 ? 45 : remaining);
      cupsFilePrintf(fp, "%s\n", line);
    }
    cupsFilePuts(fp, "-----END CERTIFICATE-----\n");
  }

  cupsFileClose(fp);

  return (rename(nfilename, filename));
}


/*
 * '_httpTLSInitialize()' - Initialize the TLS stack.
 */

void
_httpTLSInitialize(void)
{
  // OpenSSL no longer requires explicit initialization...
}


/*
 * '_httpTLSPending()' - Return the number of pending TLS-encrypted bytes.
 */

size_t					// O - Bytes available
_httpTLSPending(http_t *http)		// I - HTTP connection
{
  return ((size_t)SSL_pending(http->tls));
}


/*
 * '_httpTLSRead()' - Read from a SSL/TLS connection.
 */

int					// O - Bytes read
_httpTLSRead(http_t *http,		// I - Connection to server
	     char   *buf,		// I - Buffer to store data
	     int    len)		// I - Length of buffer
{
  return (SSL_read((SSL *)(http->tls), buf, len));
}


/*
 * '_httpTLSSetOptions()' - Set TLS protocol and cipher suite options.
 */

void
_httpTLSSetOptions(int options,		// I - Options
                   int min_version,	// I - Minimum TLS version
                   int max_version)	// I - Maximum TLS version
{
  if (!(options & _HTTP_TLS_SET_DEFAULT) || tls_options < 0)
  {
    tls_options     = options;
    tls_min_version = min_version;
    tls_max_version = max_version;
  }
}


/*
 * '_httpTLSStart()' - Set up SSL/TLS support on a connection.
 */

int					// O - 0 on success, -1 on failure
_httpTLSStart(http_t *http)		// I - Connection to server
{
  BIO		*bio;			// Basic input/output context
  SSL_CTX	*context;		// Encryption context
  char		hostname[256],		// Hostname
		cipherlist[256];	// List of cipher suites
  unsigned long	error;			// Error code, if any
  static const int versions[] =		// SSL/TLS versions
  {
    TLS1_VERSION,			// No more SSL support in OpenSSL
    TLS1_VERSION,			// TLS/1.0
    TLS1_1_VERSION,			// TLS/1.1
    TLS1_2_VERSION,			// TLS/1.2
#ifdef TLS1_3_VERSION
    TLS1_3_VERSION,			// TLS/1.3
    TLS1_3_VERSION			// TLS/1.3 (max)
#else
    TLS1_2_VERSION,			// TLS/1.2
    TLS1_2_VERSION			// TLS/1.2 (max)
#endif // TLS1_3_VERSION
  };


  DEBUG_printf(("3_httpTLSStart(http=%p)", http));

  if (tls_options < 0)
  {
    DEBUG_puts("4_httpTLSStart: Setting defaults.");
    _cupsSetDefaults();
    DEBUG_printf(("4_httpTLSStart: tls_options=%x", tls_options));
  }

  if (http->mode == _HTTP_MODE_SERVER && !tls_keypath)
  {
    DEBUG_puts("4_httpTLSStart: cupsSetServerCredentials not called.");
    http->error  = errno = EINVAL;
    http->status = HTTP_STATUS_ERROR;
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Server credentials not set."), 1);

    return (-1);
  }

  if (http->mode == _HTTP_MODE_CLIENT)
  {
    // Negotiate a TLS connection as a client...
    context = SSL_CTX_new(TLS_client_method());
  }
  else
  {
    // Negotiate a TLS connection as a server
    char	crtfile[1024],		// Certificate file
		keyfile[1024];		// Private key file
    const char	*cn,			// Common name to lookup
		*cnptr;			// Pointer into common name
    int		have_creds = 0;		// Have credentials?

    context = SSL_CTX_new(TLS_server_method());

    // Find the TLS certificate...
    if (http->fields[HTTP_FIELD_HOST])
    {
      // Use hostname for TLS upgrade...
      strlcpy(hostname, http->fields[HTTP_FIELD_HOST], sizeof(hostname));
    }
    else
    {
      // Resolve hostname from connection address...
      http_addr_t	addr;		// Connection address
      socklen_t		addrlen;	// Length of address

      addrlen = sizeof(addr);
      if (getsockname(http->fd, (struct sockaddr *)&addr, &addrlen))
      {
        // Unable to get local socket address so use default...
	DEBUG_printf(("4_httpTLSStart: Unable to get socket address: %s", strerror(errno)));
	hostname[0] = '\0';
      }
      else if (httpAddrLocalhost(&addr))
      {
        // Local access top use default...
	hostname[0] = '\0';
      }
      else
      {
        // Lookup the socket address...
	httpAddrLookup(&addr, hostname, sizeof(hostname));
        DEBUG_printf(("4_httpTLSStart: Resolved socket address to \"%s\".", hostname));
      }
    }

    if (isdigit(hostname[0] & 255) || hostname[0] == '[')
      hostname[0] = '\0';		// Don't allow numeric addresses

    if (hostname[0])
      cn = hostname;
    else
      cn = tls_common_name;

    _cupsMutexLock(&tls_mutex);

    if (cn)
    {
      // First look in the CUPS keystore...
      http_make_path(crtfile, sizeof(crtfile), tls_keypath, cn, "crt");
      http_make_path(keyfile, sizeof(keyfile), tls_keypath, cn, "key");

      if (access(crtfile, R_OK) || access(keyfile, R_OK))
      {
        // No CUPS-managed certs, look for CA certs...
        char cacrtfile[1024], cakeyfile[1024];	// CA cert files

        snprintf(cacrtfile, sizeof(cacrtfile), "/etc/letsencrypt/live/%s/fullchain.pem", cn);
        snprintf(cakeyfile, sizeof(cakeyfile), "/etc/letsencrypt/live/%s/privkey.pem", cn);

        if ((access(cacrtfile, R_OK) || access(cakeyfile, R_OK)) && (cnptr = strchr(cn, '.')) != NULL)
        {
          // Try just domain name...
          cnptr ++;
          if (strchr(cnptr, '.'))
          {
            snprintf(cacrtfile, sizeof(cacrtfile), "/etc/letsencrypt/live/%s/fullchain.pem", cnptr);
            snprintf(cakeyfile, sizeof(cakeyfile), "/etc/letsencrypt/live/%s/privkey.pem", cnptr);
          }
        }

        if (!access(cacrtfile, R_OK) && !access(cakeyfile, R_OK))
        {
          // Use the CA certs...
          strlcpy(crtfile, cacrtfile, sizeof(crtfile));
          strlcpy(keyfile, cakeyfile, sizeof(keyfile));
        }
      }

      have_creds = !access(crtfile, R_OK) && !access(keyfile, R_OK);
    }

    if (!have_creds && tls_auto_create && cn)
    {
      DEBUG_printf(("4_httpTLSStart: Auto-create credentials for \"%s\".", cn));

      if (!cupsMakeServerCredentials(tls_keypath, cn, 0, NULL, time(NULL) + 3650 * 86400))
      {
	DEBUG_puts("4_httpTLSStart: cupsMakeServerCredentials failed.");
	http->error  = errno = EINVAL;
	http->status = HTTP_STATUS_ERROR;
	_cupsSetError(IPP_STATUS_ERROR_INTERNAL, _("Unable to create server credentials."), 1);
	SSL_CTX_free(context);
        _cupsMutexUnlock(&tls_mutex);

	return (-1);
      }
    }

    _cupsMutexUnlock(&tls_mutex);

    SSL_CTX_use_PrivateKey_file(context, keyfile, SSL_FILETYPE_PEM);
    SSL_CTX_use_certificate_chain_file(context, crtfile);
  }

  // Set TLS options...
  strlcpy(cipherlist, "HIGH:!DH:+DHE", sizeof(cipherlist));
  if ((tls_options & _HTTP_TLS_ALLOW_RC4) && http->mode == _HTTP_MODE_CLIENT)
    strlcat(cipherlist, ":+RC4", sizeof(cipherlist));
  else
    strlcat(cipherlist, ":!RC4", sizeof(cipherlist));
  if (tls_options & _HTTP_TLS_DENY_CBC)
    strlcat(cipherlist, ":!SHA1:!SHA256:!SHA384", sizeof(cipherlist));
  strlcat(cipherlist, ":@STRENGTH", sizeof(cipherlist));

  SSL_CTX_set_min_proto_version(context, versions[tls_min_version]);
  SSL_CTX_set_max_proto_version(context, versions[tls_max_version]);
  SSL_CTX_set_cipher_list(context, cipherlist);

  // Setup a TLS session
  _cupsMutexLock(&tls_mutex);
  if (!tls_bio_method)
  {
    tls_bio_method = BIO_meth_new(BIO_get_new_index(), "http");
    BIO_meth_set_ctrl(tls_bio_method, http_bio_ctrl);
    BIO_meth_set_create(tls_bio_method, http_bio_new);
    BIO_meth_set_destroy(tls_bio_method, http_bio_free);
    BIO_meth_set_read(tls_bio_method, http_bio_read);
    BIO_meth_set_puts(tls_bio_method, http_bio_puts);
    BIO_meth_set_write(tls_bio_method, http_bio_write);
  }
  _cupsMutexUnlock(&tls_mutex);

  bio = BIO_new(tls_bio_method);
  BIO_ctrl(bio, BIO_C_SET_FILE_PTR, 0, (char *)http);

  http->tls = SSL_new(context);
  SSL_set_bio(http->tls, bio, bio);

  if (http->mode == _HTTP_MODE_CLIENT)
  {
    // Negotiate as a server...
    if (SSL_connect(http->tls) < 1)
    {
      // Failed
      if ((error = ERR_get_error()) != 0)
        _cupsSetError(IPP_STATUS_ERROR_CUPS_PKI, ERR_error_string(error, NULL), 0);

      http->status = HTTP_STATUS_ERROR;
      http->error  = EPIPE;

      SSL_CTX_free(context);

      SSL_free(http->tls);
      http->tls = NULL;

      return (-1);
    }
  }
  else
  {
    // Negotiate as a server...
    if (SSL_accept(http->tls) < 1)
    {
      // Failed
      if ((error = ERR_get_error()) != 0)
        _cupsSetError(IPP_STATUS_ERROR_CUPS_PKI, ERR_error_string(error, NULL), 0);

      http->status = HTTP_STATUS_ERROR;
      http->error  = EPIPE;

      SSL_CTX_free(context);

      SSL_free(http->tls);
      http->tls = NULL;

      return (-1);
    }
  }

  return (0);
}


/*
 * '_httpTLSStop()' - Shut down SSL/TLS on a connection.
 */

void
_httpTLSStop(http_t *http)		// I - Connection to server
{
  SSL_CTX	*context;		// Context for encryption


  context = SSL_get_SSL_CTX(http->tls);

  SSL_shutdown(http->tls);
  SSL_CTX_free(context);
  SSL_free(http->tls);

  http->tls = NULL;
}


/*
 * '_httpTLSWrite()' - Write to a SSL/TLS connection.
 */

int					// O - Bytes written
_httpTLSWrite(http_t     *http,		// I - Connection to server
	      const char *buf,		// I - Buffer holding data
	      int        len)		// I - Length of buffer
{
  return (SSL_write(http->tls, buf, len));
}


/*
 * 'http_bio_ctrl()' - Control the HTTP connection.
 */

static long				// O - Result/data
http_bio_ctrl(BIO  *h,			// I - BIO data
              int  cmd,			// I - Control command
	      long arg1,		// I - First argument
	      void *arg2)		// I - Second argument
{
  switch (cmd)
  {
    default :
        return (0);

    case BIO_CTRL_RESET :
        BIO_set_data(h, NULL);
	return (0);

    case BIO_C_SET_FILE_PTR :
        BIO_set_data(h, arg2);
        BIO_set_init(h, 1);
	return (1);

    case BIO_C_GET_FILE_PTR :
        if (arg2)
	{
	  *((void **)arg2) = BIO_get_data(h);
	  return (1);
	}
	else
	  return (0);

    case BIO_CTRL_DUP :
    case BIO_CTRL_FLUSH :
        return (1);
  }
}


/*
 * 'http_bio_free()' - Free OpenSSL data.
 */

static int				// O - 1 on success, 0 on failure
http_bio_free(BIO *h)			// I - BIO data
{
  if (!h)
    return (0);

  if (BIO_get_shutdown(h))
    BIO_set_init(h, 0);

  return (1);
}


/*
 * 'http_bio_new()' - Initialize an OpenSSL BIO structure.
 */

static int				// O - 1 on success, 0 on failure
http_bio_new(BIO *h)			// I - BIO data
{
  if (!h)
    return (0);

  BIO_set_init(h, 0);
  BIO_set_data(h, NULL);

  return (1);
}


/*
 * 'http_bio_puts()' - Send a string for OpenSSL.
 */

static int				// O - Bytes written
http_bio_puts(BIO        *h,		// I - BIO data
              const char *str)		// I - String to write
{
#ifdef WIN32
  return (send(((http_t *)BIO_get_data(h))->fd, str, (int)strlen(str), 0));
#else
  return ((int)send(((http_t *)BIO_get_data(h))->fd, str, strlen(str), 0));
#endif // WIN32
}


/*
 * 'http_bio_read()' - Read data for OpenSSL.
 */

static int				// O - Bytes read
http_bio_read(BIO  *h,			// I - BIO data
              char *buf,		// I - Buffer
	      int  size)		// I - Number of bytes to read
{
  http_t	*http;			// HTTP connection


  http = (http_t *)BIO_get_data(h);

  if (!http->blocking)
  {
   /*
    * Make sure we have data before we read...
    */

    if (!_httpWait(http, 10000, 0))
    {
#ifdef WIN32
      http->error = WSAETIMEDOUT;
#else
      http->error = ETIMEDOUT;
#endif // WIN32

      return (-1);
    }
  }

  return ((int)recv(http->fd, buf, (size_t)size, 0));
}


/*
 * 'http_bio_write()' - Write data for OpenSSL.
 */

static int				// O - Bytes written
http_bio_write(BIO        *h,		// I - BIO data
               const char *buf,		// I - Buffer to write
	       int        num)		// I - Number of bytes to write
{
  return (send(((http_t *)BIO_get_data(h))->fd, buf, num, 0));
}


/*
 * 'http_create_credential()' - Create a single credential in the internal format.
 */

static X509 *				// O - Certificate
http_create_credential(
    http_credential_t *credential)	// I - Credential
{
  X509	*cert = NULL;			// Certificate
  BIO	*bio;				// Basic I/O for string


  if (!credential)
    return (NULL);

  if ((bio = BIO_new_mem_buf(credential->data, credential->datalen)) == NULL)
    return (NULL);

  PEM_read_bio_X509(bio, &cert, NULL, (void *)"");

  BIO_free(bio);

  return (cert);
}


/*
 * 'http_default_path()' - Get the default credential store path.
 */

static const char *			// O - Path or NULL on error
http_default_path(
    char   *buffer,			// I - Path buffer
    size_t bufsize)			// I - Size of path buffer
{
  _cups_globals_t	*cg = _cupsGlobals();
					// Pointer to library globals


#ifdef _WIN32
  if (cg->home)
#else
  if (cg->home && getuid())
#endif // _WIN32
  {
    snprintf(buffer, bufsize, "%s/.cups", cg->home);
    if (access(buffer, 0))
    {
      DEBUG_printf(("1http_default_path: Making directory \"%s\".", buffer));
      if (mkdir(buffer, 0700))
      {
        DEBUG_printf(("1http_default_path: Failed to make directory: %s", strerror(errno)));
        return (NULL);
      }
    }

    snprintf(buffer, bufsize, "%s/.cups/ssl", cg->home);
    if (access(buffer, 0))
    {
      DEBUG_printf(("1http_default_path: Making directory \"%s\".", buffer));
      if (mkdir(buffer, 0700))
      {
        DEBUG_printf(("1http_default_path: Failed to make directory: %s", strerror(errno)));
        return (NULL);
      }
    }
  }
  else
    strlcpy(buffer, CUPS_SERVERROOT "/ssl", bufsize);

  DEBUG_printf(("1http_default_path: Using default path \"%s\".", buffer));

  return (buffer);
}


//
// 'http_get_date()' - Get the notBefore or notAfter date of a certificate.
//

static time_t				// O - UNIX time in seconds
http_get_date(X509 *cert,		// I - Certificate
              int  which)		// I - 0 for notBefore, 1 for notAfter
{
  unsigned char	*expiration;		// Expiration date of cert
  struct tm	exptm;			// Expiration date components


  if (which)
    ASN1_STRING_to_UTF8(&expiration, X509_get0_notAfter(cert));
  else
    ASN1_STRING_to_UTF8(&expiration, X509_get0_notBefore(cert));

  memset(&exptm, 0, sizeof(exptm));
  if (strlen((char *)expiration) > 13)
  {
    // 4-digit year
    exptm.tm_year = (expiration[0] - '0') * 1000 + (expiration[1] - '0') * 100 + (expiration[2] - '0') * 10 + expiration[3] - '0' - 1900;
    exptm.tm_mon  = (expiration[4] - '0') * 10 + expiration[5] - '0' - 1;
    exptm.tm_mday = (expiration[6] - '0') * 10 + expiration[7] - '0';
    exptm.tm_hour = (expiration[8] - '0') * 10 + expiration[9] - '0';
    exptm.tm_min  = (expiration[10] - '0') * 10 + expiration[11] - '0';
    exptm.tm_sec  = (expiration[12] - '0') * 10 + expiration[13] - '0';
  }
  else
  {
    // 2-digit year
    exptm.tm_year = 100 + (expiration[0] - '0') * 10 + expiration[1] - '0';
    exptm.tm_mon  = (expiration[2] - '0') * 10 + expiration[3] - '0' - 1;
    exptm.tm_mday = (expiration[4] - '0') * 10 + expiration[5] - '0';
    exptm.tm_hour = (expiration[6] - '0') * 10 + expiration[7] - '0';
    exptm.tm_min  = (expiration[8] - '0') * 10 + expiration[9] - '0';
    exptm.tm_sec  = (expiration[10] - '0') * 10 + expiration[11] - '0';
  }

  OPENSSL_free(expiration);

  return (mktime(&exptm));
}


#if 0
/*
 * 'http_load_crl()' - Load the certificate revocation list, if any.
 */

static void
http_load_crl(void)
{
  _cupsMutexLock(&tls_mutex);

  if (!openssl_x509_crl_init(&tls_crl))
  {
    cups_file_t		*fp;		// CRL file
    char		filename[1024],	// site.crl
			line[256];	// Base64-encoded line
    unsigned char	*data = NULL;	// Buffer for cert data
    size_t		alloc_data = 0,	// Bytes allocated
			num_data = 0;	// Bytes used
    int			decoded;	// Bytes decoded
    openssl_datum_t	datum;		// Data record


    http_make_path(filename, sizeof(filename), CUPS_SERVERROOT, "site", "crl");

    if ((fp = cupsFileOpen(filename, "r")) != NULL)
    {
      while (cupsFileGets(fp, line, sizeof(line)))
      {
	if (!strcmp(line, "-----BEGIN X509 CRL-----"))
	{
	  if (num_data)
	  {
	   /*
	    * Missing END X509 CRL...
	    */

	    break;
	  }
	}
	else if (!strcmp(line, "-----END X509 CRL-----"))
	{
	  if (!num_data)
	  {
	   /*
	    * Missing data...
	    */

	    break;
	  }

          datum.data = data;
	  datum.size = num_data;

	  openssl_x509_crl_import(tls_crl, &datum, GNUTLS_X509_FMT_PEM);

	  num_data = 0;
	}
	else
	{
	  if (alloc_data == 0)
	  {
	    data       = malloc(2048);
	    alloc_data = 2048;

	    if (!data)
	      break;
	  }
	  else if ((num_data + strlen(line)) >= alloc_data)
	  {
	    unsigned char *tdata = realloc(data, alloc_data + 1024);
					    // Expanded buffer

	    if (!tdata)
	      break;

	    data       = tdata;
	    alloc_data += 1024;
	  }

	  decoded = alloc_data - num_data;
	  httpDecode64_2((char *)data + num_data, &decoded, line);
	  num_data += (size_t)decoded;
	}
      }

      cupsFileClose(fp);

      if (data)
	free(data);
    }
  }

  _cupsMutexUnlock(&tls_mutex);
}
#endif // 0


/*
 * 'http_make_path()' - Format a filename for a certificate or key file.
 */

static const char *			// O - Filename
http_make_path(
    char       *buffer,			// I - Filename buffer
    size_t     bufsize,			// I - Size of buffer
    const char *dirname,		// I - Directory
    const char *filename,		// I - Filename (usually hostname)
    const char *ext)			// I - Extension
{
  char	*bufptr,			// Pointer into buffer
	*bufend = buffer + bufsize - 1;	// End of buffer


  snprintf(buffer, bufsize, "%s/", dirname);
  bufptr = buffer + strlen(buffer);

  while (*filename && bufptr < bufend)
  {
    if (_cups_isalnum(*filename) || *filename == '-' || *filename == '.')
      *bufptr++ = *filename;
    else
      *bufptr++ = '_';

    filename ++;
  }

  if (bufptr < bufend && filename[-1] != '.')
    *bufptr++ = '.';

  strlcpy(bufptr, ext, (size_t)(bufend - bufptr + 1));

  return (buffer);
}


//
// 'http_x509_add_ext()' - Add an extension to a certificate.
//

static int				// O - 1 on success, 0 on failure
http_x509_add_ext(X509       *cert,	// I - Certificate
                  int        nid,	// I - Extension ID
                  const char *value)	// I - Value
{
  int			ret;		// Return value
  X509_EXTENSION	*ex = NULL;	// Extension
  X509V3_CTX		ctx;		// Certificate context


  DEBUG_printf(("3http_x509_add_ext(cert=%p, nid=%d, value=\"%s\")", (void *)cert, nid, value));

  // Don't use a configuration database...
  X509V3_set_ctx_nodb(&ctx);

  // Self-signed certificates use the same issuer and subject...
  X509V3_set_ctx(&ctx, /*issuer*/cert, /*subject*/cert, /*req*/NULL, /*crl*/NULL, /*flags*/0);

  // Create and add the extension...
  if ((ex = X509V3_EXT_conf_nid(/*conf*/NULL, &ctx, nid, value)) == NULL)
  {
    DEBUG_puts("4http_x509_add_ext: Unable to create extension, returning false.");
    return (0);
  }

  ret = X509_add_ext(cert, ex, -1) != 0;

  DEBUG_printf(("4http_x509_add_ext: X509_add_ext returned %s.", ret ? "true" : "false"));

  // Free the extension and return...
  X509_EXTENSION_free(ex);

  return (ret);
}


//
// 'http_x509_add_san()' - Add a subjectAltName extension to an X.509 certificate.
//

static void
http_x509_add_san(X509       *cert,	// I - Certificate
                  const char *name)	// I - Hostname
{
  char		dns_name[1024];		// DNS: prefixed hostname


  // The subjectAltName value for DNS names starts with a DNS: prefix...
  snprintf(dns_name, sizeof(dns_name), "DNS:%s", name);
  http_x509_add_ext(cert, NID_subject_alt_name, dns_name);
}
